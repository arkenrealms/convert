#include "bytes.hpp"
#include <FLAC/format.h>
#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

static_assert(std::is_same_v<FLAC__int32, std::int32_t>, "canonical samples feed libFLAC directly");

namespace arken::audio::v1 {
namespace {
using namespace detail;
constexpr std::string_view maskField = "WAVEFORMATEXTENSIBLE_CHANNEL_MASK";
constexpr std::uint32_t metadataStreamInfo = 0, metadataPadding = 1, metadataVorbisComment = 4;
constexpr std::uint64_t maximumFlacFrames = (std::uint64_t(1) << 36) - 1;
constexpr unsigned encodeChunkFrames = 4096;

struct StreamInfo {
    std::uint32_t sampleRate = 0, channels = 0, bitsPerSample = 0;
    std::uint64_t totalFrames = 0; // 0: unknown, as RFC 9639 permits
};

struct Scan {
    Issue issue;
    StreamInfo info;
    DecodeReport report;
};

// Bounded pre-scan of the metadata chain before libFLAC allocates anything:
// enforces block size/count limits and admits stream parameters early.
Scan ScanMetadata(Bytes b, const Limits& limits) {
    Scan scan;
    const auto fail = [&](Error e, std::uint64_t offset) { scan.issue = {e, offset}; return scan; };
    std::size_t p = 0;
    if (Is(b, 0, "OggS")) return fail(Error::unsupported_container, 0);
    if (b.size() >= 3 && U8(b, 0) == 'I' && U8(b, 1) == 'D' && U8(b, 2) == '3') {
        if (!Range(b, 0, 10)) return fail(Error::truncated, 0);
        std::uint32_t size = 0;
        for (std::size_t i = 6; i < 10; ++i) {
            if (U8(b, i) & 0x80) return fail(Error::invalid_metadata, i);
            size = (size << 7) | U8(b, i);
        }
        const std::uint64_t whole = 10ull + size + ((U8(b, 5) & 0x10) ? 10 : 0);
        if (whole > limits.maximumMetadataBlockBytes) return fail(Error::metadata_limit, 0);
        if (!Range(b, 0, whole)) return fail(Error::truncated, 0);
        p = std::size_t(whole);
        ++scan.report.ignoredMetadataBlocks;
    }
    if (!Range(b, p, 4)) return fail(Error::truncated, p);
    if (!Is(b, p, "fLaC")) return fail(Error::wrong_magic, p);
    p += 4;
    std::uint32_t blocks = 0;
    for (bool last = false; !last;) {
        if (!Range(b, p, 4)) return fail(Error::truncated, p);
        if (++blocks > limits.maximumMetadataBlocks) return fail(Error::metadata_limit, p);
        last = (U8(b, p) & 0x80) != 0;
        const std::uint32_t type = U8(b, p) & 0x7F, length = B24(b, p + 1);
        if (type == 127) return fail(Error::invalid_metadata, p);
        if ((blocks == 1) != (type == metadataStreamInfo)) return fail(Error::invalid_layout, p);
        if (length > limits.maximumMetadataBlockBytes) return fail(Error::metadata_limit, p);
        if (!Range(b, p + 4, length)) return fail(Error::truncated, p);
        const auto body = p + 4;
        if (type == metadataStreamInfo) {
            if (length != 34) return fail(Error::invalid_metadata, p);
            const std::uint64_t packed = (std::uint64_t(B24(b, body + 10)) << 40)
                | (std::uint64_t(B24(b, body + 13)) << 16) | (std::uint64_t(U8(b, body + 16)) << 8)
                | U8(b, body + 17);
            scan.info.sampleRate = std::uint32_t(packed >> 44);
            scan.info.channels = std::uint32_t((packed >> 41) & 7) + 1;
            scan.info.bitsPerSample = std::uint32_t((packed >> 36) & 31) + 1;
            scan.info.totalFrames = packed & maximumFlacFrames;
        } else if (type != metadataPadding && type != metadataVorbisComment) {
            ++scan.report.ignoredMetadataBlocks;
        }
        p = body + length;
    }
    const auto& info = scan.info;
    if (info.channels > limits.maximumChannels) return fail(Error::channel_limit, 8);
    if (info.bitsPerSample < 4) return fail(Error::unsupported_bit_depth, 8);
    if (info.sampleRate == 0 || info.sampleRate < limits.minimumSampleRate || info.sampleRate > limits.maximumSampleRate)
        return fail(Error::sample_rate_limit, 8);
    if (info.totalFrames > limits.maximumTotalSamples / info.channels) return fail(Error::sample_limit, 8);
    return scan;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = char(x - 32);
        if (y >= 'a' && y <= 'z') y = char(y - 32);
        if (x != y) return false;
    }
    return true;
}

// "0x" followed by 1..8 hexadecimal digits, as written by the reference flac tool.
std::optional<std::uint32_t> ParseMask(std::string_view value) {
    if (value.size() < 3 || value.size() > 10 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
        return std::nullopt;
    std::uint32_t mask = 0;
    for (const auto c : value.substr(2)) {
        std::uint32_t digit;
        if (c >= '0' && c <= '9') digit = std::uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') digit = std::uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = std::uint32_t(c - 'A' + 10);
        else return std::nullopt;
        mask = (mask << 4) | digit;
    }
    return mask;
}

struct DecoderDeleter { void operator()(FLAC__StreamDecoder* d) const noexcept { FLAC__stream_decoder_delete(d); } };
struct EncoderDeleter { void operator()(FLAC__StreamEncoder* e) const noexcept { FLAC__stream_encoder_delete(e); } };
struct MetadataDeleter { void operator()(FLAC__StreamMetadata* m) const noexcept { FLAC__metadata_object_delete(m); } };

struct DecodeState {
    Bytes source;
    std::size_t position = 0;
    StreamInfo info;
    std::uint64_t frameLimit = 0;
    Error error = Error::none;
    bool sawMask = false;
    DecodeResult* result = nullptr;
    void Fail(Error e) { if (error == Error::none) error = e; }
};

FLAC__StreamDecoderReadStatus Read(const FLAC__StreamDecoder*, FLAC__byte buffer[], std::size_t* bytes, void* client) noexcept {
    auto& state = *static_cast<DecodeState*>(client);
    const auto count = std::min(*bytes, state.source.size() - state.position);
    if (count) std::memcpy(buffer, state.source.data() + state.position, count);
    state.position += count;
    *bytes = count;
    return count ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

FLAC__bool AtEnd(const FLAC__StreamDecoder*, void* client) noexcept {
    const auto& state = *static_cast<DecodeState*>(client);
    return state.position >= state.source.size();
}

FLAC__StreamDecoderWriteStatus WriteFrame(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
    const FLAC__int32* const buffer[], void* client) noexcept {
    auto& state = *static_cast<DecodeState*>(client);
    if (state.error != Error::none) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    const auto& header = frame->header;
    if (header.channels != state.info.channels || header.bits_per_sample != state.info.bitsPerSample
        || header.sample_rate != state.info.sampleRate) {
        state.Fail(Error::corrupt_stream);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    auto& audio = state.result->audio;
    if (header.blocksize > state.frameLimit - audio.frames) {
        state.Fail(state.info.totalFrames ? Error::length_mismatch : Error::sample_limit);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    const std::int64_t high = (std::int64_t(1) << (state.info.bitsPerSample - 1)) - 1, low = -high - 1;
    try {
        const auto start = audio.samples.size();
        audio.samples.resize(start + std::size_t(header.blocksize) * header.channels);
        auto* out = audio.samples.data() + start;
        for (std::uint32_t i = 0; i < header.blocksize; ++i)
            for (std::uint32_t c = 0; c < header.channels; ++c) {
                const auto sample = buffer[c][i];
                if (sample < low || sample > high) {
                    state.Fail(Error::corrupt_stream);
                    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
                *out++ = sample;
            }
        audio.frames += header.blocksize;
    } catch (...) {
        state.Fail(Error::allocation_failed);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void OnMetadata(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* metadata, void* client) noexcept {
    auto& state = *static_cast<DecodeState*>(client);
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        const auto& s = metadata->data.stream_info;
        if (s.sample_rate != state.info.sampleRate || s.channels != state.info.channels
            || s.bits_per_sample != state.info.bitsPerSample || s.total_samples != state.info.totalFrames)
            state.Fail(Error::invalid_metadata);
        return;
    }
    if (metadata->type != FLAC__METADATA_TYPE_VORBIS_COMMENT) return;
    const auto& comments = metadata->data.vorbis_comment;
    for (FLAC__uint32 i = 0; i < comments.num_comments; ++i) {
        const std::string_view entry(reinterpret_cast<const char*>(comments.comments[i].entry), comments.comments[i].length);
        const auto equals = entry.find('=');
        if (equals == std::string_view::npos || !EqualsIgnoreCase(entry.substr(0, equals), maskField)) {
            state.result->report.ignoredComments = true;
            continue;
        }
        const auto mask = ParseMask(entry.substr(equals + 1));
        if (!mask || state.sawMask) { state.Fail(Error::invalid_metadata); return; }
        state.sawMask = true;
        state.result->audio.channelMask = *mask;
    }
}

void OnError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* client) noexcept {
    static_cast<DecodeState*>(client)->Fail(Error::corrupt_stream);
}

struct EncodeState {
    std::vector<std::byte> bytes;
    std::uint64_t position = 0, limit = 0;
    Error error = Error::none;
};

FLAC__StreamEncoderWriteStatus WriteBytes(const FLAC__StreamEncoder*, const FLAC__byte buffer[], std::size_t size,
    std::uint32_t, std::uint32_t, void* client) noexcept {
    auto& state = *static_cast<EncodeState*>(client);
    if (state.error != Error::none) return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    if (state.position > state.limit || size > state.limit - state.position) {
        state.error = Error::output_limit;
        return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    }
    try {
        const auto end = std::size_t(state.position) + size;
        if (end > state.bytes.size()) state.bytes.resize(end);
        if (size) std::memcpy(state.bytes.data() + state.position, buffer, size);
        state.position = end;
    } catch (...) {
        state.error = Error::allocation_failed;
        return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    }
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderSeekStatus SeekBytes(const FLAC__StreamEncoder*, FLAC__uint64 offset, void* client) noexcept {
    auto& state = *static_cast<EncodeState*>(client);
    if (offset > state.bytes.size()) return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    state.position = offset;
    return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus TellBytes(const FLAC__StreamEncoder*, FLAC__uint64* offset, void* client) noexcept {
    *offset = static_cast<EncodeState*>(client)->position;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

bool FlacSubset(const Audio& audio) noexcept {
    const auto bits = audio.bitsPerSample;
    return (bits == 8 || bits == 12 || bits == 16 || bits == 20 || bits == 24 || bits == 32)
        && FLAC__format_sample_rate_is_subset(audio.sampleRate);
}
} // namespace

DecodeResult DecodeFlac(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        auto scan = ScanMetadata(bytes, limits);
        if (scan.issue.code != Error::none) return Failure(scan.issue.code, scan.issue.byteOffset);
        DecodeResult result;
        result.report = scan.report;
        auto& audio = result.audio;
        audio.sampleRate = scan.info.sampleRate;
        audio.channels = scan.info.channels;
        audio.bitsPerSample = scan.info.bitsPerSample;
        DecodeState state;
        state.source = bytes;
        state.info = scan.info;
        state.result = &result;
        state.frameLimit = scan.info.totalFrames ? scan.info.totalFrames : limits.maximumTotalSamples / scan.info.channels;
        if (scan.info.totalFrames) audio.samples.reserve(std::size_t(scan.info.totalFrames * scan.info.channels));
        std::unique_ptr<FLAC__StreamDecoder, DecoderDeleter> decoder(FLAC__stream_decoder_new());
        if (!decoder) return Failure(Error::allocation_failed);
        auto* d = decoder.get();
        if (!FLAC__stream_decoder_set_md5_checking(d, true) || !FLAC__stream_decoder_set_metadata_ignore_all(d)
            || !FLAC__stream_decoder_set_metadata_respond(d, FLAC__METADATA_TYPE_STREAMINFO)
            || !FLAC__stream_decoder_set_metadata_respond(d, FLAC__METADATA_TYPE_VORBIS_COMMENT))
            return Failure(Error::codec_failed);
        if (FLAC__stream_decoder_init_stream(d, Read, nullptr, nullptr, nullptr, AtEnd, WriteFrame, OnMetadata,
                OnError, &state) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
            return Failure(Error::codec_failed);
        const bool processed = FLAC__stream_decoder_process_until_end_of_stream(d);
        const auto decoderState = FLAC__stream_decoder_get_state(d);
        if (state.error != Error::none) return Failure(state.error);
        if (!processed || decoderState != FLAC__STREAM_DECODER_END_OF_STREAM) {
            return Failure(decoderState == FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR
                ? Error::allocation_failed : Error::corrupt_stream);
        }
        // finish() is where libFLAC compares the running MD5 with STREAMINFO.
        if (!FLAC__stream_decoder_finish(d)) return Failure(Error::checksum_mismatch);
        if (state.error != Error::none) return Failure(state.error);
        if (scan.info.totalFrames && audio.frames != scan.info.totalFrames) return Failure(Error::length_mismatch);
        return result;
    } catch (const std::bad_alloc&) {
        return Failure(Error::allocation_failed);
    } catch (...) {
        return Failure(Error::codec_failed);
    }
}

EncodeResult EncodeFlac(const Audio& audio, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        if (audio.channels > FLAC__MAX_CHANNELS) return {Error::channel_limit, {}};
        if (audio.sampleRate > FLAC__MAX_SAMPLE_RATE) return {Error::sample_rate_limit, {}};
        if (audio.frames > maximumFlacFrames) return {Error::sample_limit, {}};
        std::unique_ptr<FLAC__StreamEncoder, EncoderDeleter> encoder(FLAC__stream_encoder_new());
        if (!encoder) return {Error::allocation_failed, {}};
        auto* e = encoder.get();
        std::unique_ptr<FLAC__StreamMetadata, MetadataDeleter> comments;
        FLAC__StreamMetadata* metadata[1] = {nullptr};
        if (audio.channelMask) {
            comments.reset(FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT));
            if (!comments) return {Error::allocation_failed, {}};
            char value[11];
            static constexpr char hex[] = "0123456789ABCDEF";
            // Matches the reference tool's "0x%04X" spelling without stdio.
            auto mask = *audio.channelMask;
            unsigned digits = 4;
            while (digits < 8 && (mask >> (4 * digits))) ++digits;
            value[0] = '0'; value[1] = 'x';
            for (unsigned i = 0; i < digits; ++i) value[2 + i] = hex[(mask >> (4 * (digits - 1 - i))) & 15];
            value[2 + digits] = '\0';
            FLAC__StreamMetadata_VorbisComment_Entry entry{};
            if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, maskField.data(), value))
                return {Error::allocation_failed, {}};
            if (!FLAC__metadata_object_vorbiscomment_append_comment(comments.get(), entry, false)) {
                std::free(entry.entry);
                return {Error::allocation_failed, {}};
            }
            metadata[0] = comments.get();
        }
        // Fixed configuration: every setting that shapes the bitstream is explicit.
        if (!FLAC__stream_encoder_set_verify(e, true) || !FLAC__stream_encoder_set_streamable_subset(e, FlacSubset(audio))
            || !FLAC__stream_encoder_set_channels(e, audio.channels)
            || !FLAC__stream_encoder_set_bits_per_sample(e, audio.bitsPerSample)
            || !FLAC__stream_encoder_set_sample_rate(e, audio.sampleRate)
            || !FLAC__stream_encoder_set_compression_level(e, 5)
            // MD5 is libFLAC's default (do_md5 = true); its setter is not public API.
            ||!FLAC__stream_encoder_set_total_samples_estimate(e, audio.frames)
            || !FLAC__stream_encoder_set_metadata(e, metadata[0] ? metadata : nullptr, metadata[0] ? 1 : 0))
            return {Error::codec_failed, {}};
        EncodeState state;
        state.limit = limits.maximumOutputBytes;
        if (FLAC__stream_encoder_init_stream(e, WriteBytes, SeekBytes, TellBytes, nullptr, &state)
            != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
            return {state.error != Error::none ? state.error : Error::codec_failed, {}};
        const auto failure = [&]() -> Error {
            if (state.error != Error::none) return state.error;
            switch (FLAC__stream_encoder_get_state(e)) {
            case FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR:
            case FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA: return Error::verify_failed;
            case FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR: return Error::allocation_failed;
            default: return Error::codec_failed;
            }
        };
        for (std::uint64_t frame = 0; frame < audio.frames;) {
            const auto count = unsigned(std::min<std::uint64_t>(encodeChunkFrames, audio.frames - frame));
            if (!FLAC__stream_encoder_process_interleaved(e, audio.samples.data() + frame * audio.channels, count)) {
                const auto error = failure();
                return {error, {}};
            }
            frame += count;
        }
        if (!FLAC__stream_encoder_finish(e)) return {failure(), {}};
        if (state.error != Error::none) return {state.error, {}};
        return {Error::none, std::move(state.bytes)};
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::codec_failed, {}};
    }
}

std::string_view FlacVendorString() noexcept { return FLAC__VENDOR_STRING; }
} // namespace arken::audio::v1
