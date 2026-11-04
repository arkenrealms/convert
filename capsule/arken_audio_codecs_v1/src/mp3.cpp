#include "lossy.hpp"
#include <lame.h>
#include <minimp3.h>
#include <algorithm>
#include <memory>
#include <new>

namespace arken::audio::v1 {
namespace {
using namespace detail;
constexpr std::uint32_t decoderDelay = 529; // Layer III synthesis delay, the LAME-tag convention (528 + 1)
constexpr unsigned encodeChunkFrames = 4096;

constexpr std::uint16_t mpeg1Kbps[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
constexpr std::uint16_t mpeg2Kbps[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
constexpr std::uint32_t rates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

struct Header {
    std::uint32_t version = 0; // 0: MPEG-1, 1: MPEG-2, 2: MPEG-2.5
    std::uint32_t sampleRate = 0, channels = 0, frameBytes = 0, frameSamples = 0, sideInfo = 0;
    bool crc = false;
};

enum class HeaderStatus { ok, not_a_frame, unsupported, invalid };

HeaderStatus ParseHeader(Bytes b, std::size_t p, Header& h) noexcept {
    const auto b1 = U8(b, p + 1), b2 = U8(b, p + 2), b3 = U8(b, p + 3);
    if (U8(b, p) != 0xFF || (b1 & 0xE0) != 0xE0) return HeaderStatus::not_a_frame;
    const auto versionBits = (b1 >> 3) & 3, layerBits = (b1 >> 1) & 3;
    if (versionBits == 1 || layerBits == 0) return HeaderStatus::invalid; // reserved
    if (layerBits != 1) return HeaderStatus::unsupported;                // Layer I or II
    const auto bitrateIndex = b2 >> 4, rateIndex = (b2 >> 2) & 3;
    if (bitrateIndex == 15 || rateIndex == 3 || (b3 & 3) == 2) return HeaderStatus::invalid;
    if (bitrateIndex == 0) return HeaderStatus::unsupported;             // free format
    h.version = versionBits == 3 ? 0 : versionBits == 2 ? 1 : 2;
    h.sampleRate = rates[h.version][rateIndex];
    h.channels = (b3 >> 6) == 3 ? 1 : 2;
    h.crc = (b1 & 1) == 0;
    const std::uint32_t kbps = h.version == 0 ? mpeg1Kbps[bitrateIndex] : mpeg2Kbps[bitrateIndex];
    const std::uint32_t padding = (b2 >> 1) & 1;
    h.frameSamples = h.version == 0 ? 1152 : 576;
    h.frameBytes = (h.version == 0 ? 144000 : 72000) * kbps / h.sampleRate + padding;
    h.sideInfo = h.version == 0 ? (h.channels == 1 ? 17 : 32) : (h.channels == 1 ? 9 : 17);
    return HeaderStatus::ok;
}

// CRC-16 (polynomial 0x8005, initial 0xFFFF, MSB first) over header bytes 2-3
// and the side information, as ISO/IEC 11172-3 defines for protected frames.
bool FrameCrcMatches(Bytes b, std::size_t p, const Header& h) noexcept {
    std::uint32_t crc = 0xFFFF;
    const auto feed = [&](std::uint8_t byte) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool top = ((crc >> 15) & 1) != (((byte >> bit) & 1) != 0);
            crc = (crc << 1) & 0xFFFF;
            if (top) crc ^= 0x8005;
        }
    };
    feed(U8(b, p + 2));
    feed(U8(b, p + 3));
    for (std::uint32_t i = 0; i < h.sideInfo; ++i) feed(U8(b, p + 6 + i));
    return crc == B16(b, p + 4);
}

// CRC-16/ARC (reflected 0x8005, initial 0) as used by the LAME tag, over
// [p, p + length); the two bytes at absolute offset `zeroAt`, and bytes at or
// beyond `end`, count as zero.
constexpr std::size_t noZero = SIZE_MAX;
std::uint16_t TagCrc(Bytes b, std::size_t p, std::size_t length, std::size_t zeroAt, std::size_t end) noexcept {
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const auto at = p + i;
        crc ^= (at >= end || (zeroAt != noZero && (at == zeroAt || at == zeroAt + 1))) ? 0u : U8(b, at);
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return std::uint16_t(crc);
}

struct Frame { std::size_t offset; std::uint32_t bytes; };

struct InfoTag {
    bool present = false;          // Xing/Info frame (not audio)
    bool hasFrames = false;
    std::uint32_t frames = 0;
    bool gapless = false;          // LAME/Lavf/Lavc extension with a valid tag CRC
    std::uint32_t delay = 0, padding = 0;
};

InfoTag ReadInfoTag(Bytes b, const Frame& f, const Header& h) noexcept {
    InfoTag tag;
    const std::size_t at = f.offset + 4 + (h.crc ? 2 : 0) + h.sideInfo;
    const std::size_t end = f.offset + f.bytes;
    if (at + 8 > end || !(Is(b, at, "Xing") || Is(b, at, "Info"))) return tag;
    tag.present = true;
    const std::uint32_t flags = B32(b, at + 4);
    std::size_t p = at + 8;
    if (flags & 1) {
        if (p + 4 > end) return tag;
        tag.hasFrames = true;
        tag.frames = B32(b, p);
        p += 4;
    }
    if (flags & 2) p += 4;
    if (flags & 4) p += 100;
    if (flags & 8) p += 4;
    if (p + 36 > end) return tag;
    if (!(Is(b, p, "LAME") || Is(b, p, "Lavf") || Is(b, p, "Lavc"))) return tag;
    const std::uint16_t stored = B16(b, p + 34);
    // LAME: CRC of every frame byte before the CRC field (190 bytes for MPEG-1
    // stereo). FFmpeg (Lavf/Lavc): CRC of the first 190 frame bytes with the CRC
    // field zeroed and a frame shorter than 190 bytes zero-extended, which differs
    // when the field lies inside them (mono, MPEG-2, low-bitrate tag frames).
    const auto crcLength = p + 34 - f.offset;
    const bool crcOk = TagCrc(b, f.offset, crcLength, noZero, end) == stored
        || TagCrc(b, f.offset, 190, p + 34, end) == stored;
    if (!crcOk) return tag; // a damaged tag is not trusted for trimming
    tag.gapless = true;
    tag.delay = (std::uint32_t(U8(b, p + 21)) << 4) | (U8(b, p + 22) >> 4);
    tag.padding = ((std::uint32_t(U8(b, p + 22)) & 15) << 8) | U8(b, p + 23);
    return tag;
}

struct LameDeleter { void operator()(lame_global_flags* g) const noexcept { lame_close(g); } };
void Quiet(const char*, va_list) {}
} // namespace

DecodeResult DecodeMp3(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        const Bytes b = bytes;
        DecodeResult result;
        result.report.lossy = true;
        std::size_t p = 0, end = b.size();
        std::uint32_t tags = 0;
        // ID3v2 tags (one or more) at the start.
        while (end - p >= 3 && U8(b, p) == 'I' && U8(b, p + 1) == 'D' && U8(b, p + 2) == '3') {
            if (!Range(b, p, 10)) return Failure(Error::truncated, p);
            std::uint32_t size = 0;
            for (std::size_t i = 6; i < 10; ++i) {
                if (U8(b, p + i) & 0x80) return Failure(Error::invalid_metadata, p + i);
                size = (size << 7) | U8(b, p + i);
            }
            const std::uint64_t whole = 10ull + size + ((U8(b, p + 5) & 0x10) ? 10 : 0);
            if (whole > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit, p);
            if (!Range(b, p, whole)) return Failure(Error::truncated, p);
            if (++tags > limits.maximumMetadataBlocks) return Failure(Error::metadata_limit, p);
            p += std::size_t(whole);
        }
        // ID3v1 and APEv2 at the end.
        if (end - p >= 128 && U8(b, end - 128) == 'T' && U8(b, end - 127) == 'A' && U8(b, end - 126) == 'G') {
            end -= 128;
            ++tags;
        }
        if (end - p >= 32 && Is(b, end - 32, "APET") && Is(b, end - 28, "AGEX")) {
            const std::uint64_t size = U32(b, end - 20) + ((U32(b, end - 12) & 0x80000000u) ? 32ull : 0ull);
            if (size < 32 || size > end - p) return Failure(Error::invalid_metadata, end - 32);
            if (size > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit, end - 32);
            end -= std::size_t(size);
            ++tags;
        }
        result.report.skippedTags = tags;

        // Frame walk: every remaining byte is part of a consecutive Layer III frame.
        Header first;
        std::vector<Frame> frames;
        std::uint64_t frameLimit = 0;
        for (std::size_t q = p; q < end;) {
            if (end - q < 4) return Failure(frames.empty() ? Error::wrong_magic : Error::truncated, q);
            Header h;
            switch (ParseHeader(b, q, h)) {
            case HeaderStatus::ok: break;
            case HeaderStatus::unsupported: return Failure(Error::unsupported_encoding, q);
            case HeaderStatus::not_a_frame:
            case HeaderStatus::invalid: return Failure(frames.empty() ? Error::wrong_magic : Error::corrupt_stream, q);
            }
            if (frames.empty()) {
                first = h;
                if (h.channels > limits.maximumChannels) return Failure(Error::channel_limit, q);
                if (h.sampleRate < limits.minimumSampleRate || h.sampleRate > limits.maximumSampleRate)
                    return Failure(Error::sample_rate_limit, q);
                frameLimit = limits.maximumTotalSamples / h.channels;
            } else if (h.version != first.version || h.sampleRate != first.sampleRate || h.channels != first.channels) {
                return Failure(Error::corrupt_stream, q); // parameter changes cannot be represented
            }
            if (h.frameBytes > end - q) return Failure(Error::truncated, q);
            if (h.frameBytes < 4 + (h.crc ? 2u : 0u) + h.sideInfo) return Failure(Error::corrupt_stream, q);
            if (h.crc && !FrameCrcMatches(b, q, h)) return Failure(Error::corrupt_stream, q);
            if (std::uint64_t(frames.size() + 1) * h.frameSamples > frameLimit + h.frameSamples)
                return Failure(Error::sample_limit, q);
            frames.push_back({q, h.frameBytes});
            q += h.frameBytes;
        }
        if (frames.empty()) return Failure(Error::truncated, p);
        const auto info = ReadInfoTag(b, frames.front(), first);
        const std::size_t audioStart = info.present ? 1 : 0;
        const std::uint64_t audioFrames = frames.size() - audioStart;
        if (info.hasFrames && info.frames != audioFrames) return Failure(Error::length_mismatch);
        const std::uint64_t decodedFrames = audioFrames * first.frameSamples;

        Trim trim{0, decodedFrames, 0};
        if (info.gapless) {
            const std::int64_t start = std::int64_t(info.delay) + decoderDelay;
            const std::int64_t endPosition = std::int64_t(decodedFrames) - std::int64_t(info.padding) + decoderDelay;
            if (!PlanTrim(0, start, endPosition, decodedFrames, decodedFrames, trim)) return Failure(Error::length_mismatch);
            result.report.encoderDelayTrimmed = trim.leading || trim.trailing;
        } else {
            result.report.encoderDelayUnknown = true;
        }
        if (trim.kept > frameLimit) return Failure(Error::sample_limit);

        auto& audio = result.audio;
        audio.sampleRate = first.sampleRate;
        audio.channels = first.channels;
        audio.bitsPerSample = lossyBits;
        audio.frames = trim.kept;
        audio.samples.reserve(std::size_t(trim.kept) * first.channels);
        auto decoder = std::make_unique<mp3dec_t>();
        mp3dec_init(decoder.get());
        std::vector<mp3d_sample_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
        std::uint64_t position = 0; // decoded frames so far
        bool decodedAny = false;
        for (std::size_t i = audioStart; i < frames.size(); ++i) {
            mp3dec_frame_info_t frameInfo{};
            const auto* data = reinterpret_cast<const std::uint8_t*>(b.data() + frames[i].offset);
            int n = mp3dec_decode_frame(decoder.get(), data, int(frames[i].bytes), pcm.data(), &frameInfo);
            if (frameInfo.frame_bytes != int(frames[i].bytes) || (n && frameInfo.channels != int(first.channels)))
                return Failure(Error::corrupt_stream, frames[i].offset);
            if (n == 0) {
                // The bit reservoir refers to data before the stream (a cut file):
                // only acceptable before the first decodable frame; emitted as silence.
                if (decodedAny) return Failure(Error::corrupt_stream, frames[i].offset);
                std::fill(pcm.begin(), pcm.begin() + std::ptrdiff_t(first.frameSamples * first.channels), mp3d_sample_t(0));
                n = int(first.frameSamples);
                ++result.report.silencedFrames;
            } else {
                decodedAny = true;
            }
            if (std::uint32_t(n) != first.frameSamples) return Failure(Error::corrupt_stream, frames[i].offset);
            for (std::uint32_t k = 0; k < first.frameSamples; ++k, ++position) {
                if (position < trim.leading || position >= trim.leading + trim.kept) continue;
                for (std::uint32_t c = 0; c < first.channels; ++c)
                    audio.samples.push_back(std::int32_t(pcm[std::size_t(k) * first.channels + c]));
            }
        }
        result.report.trimmedLeadingFrames = trim.leading;
        result.report.trimmedTrailingFrames = trim.trailing;
        return result;
    } catch (const std::bad_alloc&) {
        return Failure(Error::allocation_failed);
    } catch (...) {
        return Failure(Error::codec_failed);
    }
}

EncodeResult EncodeMp3(const Audio& audio, const LossyOptions& options, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        if (audio.channels > 2) return {Error::channel_limit, {}};
        int version = -1;
        for (int v = 0; v < 3; ++v)
            for (auto rate : rates[v])
                if (rate == audio.sampleRate) version = v;
        if (version < 0) return {Error::resampling_required, {}};
        if (options.mp3RateControl == Mp3RateControl::cbr) {
            const auto* table = version == 0 ? mpeg1Kbps : mpeg2Kbps;
            if (options.mp3BitrateKbps == 0 || std::find(table + 1, table + 15, options.mp3BitrateKbps) == table + 15)
                return {Error::invalid_option, {}};
        } else if (options.mp3RateControl == Mp3RateControl::vbr) {
            if (options.mp3VbrQuality > 9) return {Error::invalid_option, {}};
        } else {
            return {Error::invalid_option, {}};
        }
        std::unique_ptr<lame_global_flags, LameDeleter> lame(lame_init());
        if (!lame) return {Error::allocation_failed, {}};
        auto* g = lame.get();
        // No console output from the library (the defaults print to stderr).
        lame_set_errorf(g, Quiet);
        lame_set_debugf(g, Quiet);
        lame_set_msgf(g, Quiet);
        const bool cbr = options.mp3RateControl == Mp3RateControl::cbr;
        if (lame_set_num_channels(g, int(audio.channels)) < 0 || lame_set_in_samplerate(g, int(audio.sampleRate)) < 0
            || lame_set_out_samplerate(g, int(audio.sampleRate)) < 0
            || lame_set_mode(g, audio.channels == 1 ? MONO : JOINT_STEREO) < 0
            || lame_set_quality(g, 2) < 0
            || lame_set_bWriteVbrTag(g, 1) < 0
            || lame_set_findReplayGain(g, 0) < 0
            || lame_set_error_protection(g, 0) < 0
            || lame_set_VBR(g, cbr ? vbr_off : vbr_mtrh) < 0
            || (cbr ? lame_set_brate(g, int(options.mp3BitrateKbps)) : lame_set_VBR_quality(g, float(options.mp3VbrQuality))) < 0)
            return {Error::codec_failed, {}};
        lame_set_write_id3tag_automatic(g, 0);
        if (lame_init_params(g) < 0) return {Error::invalid_option, {}};
        if (lame_get_out_samplerate(g) != int(audio.sampleRate)) return {Error::resampling_required, {}};

        std::vector<std::byte> out;
        const auto limit = limits.maximumOutputBytes;
        std::vector<unsigned char> buffer(encodeChunkFrames * 5 / 4 + 7200);
        std::vector<float> chunk(std::size_t(encodeChunkFrames) * audio.channels);
        const auto append = [&](int written) -> Error {
            if (written < 0) return written == -1 ? Error::allocation_failed : Error::codec_failed;
            if (out.size() > limit || std::uint64_t(written) > limit - out.size()) return Error::output_limit;
            const auto start = out.size();
            out.resize(start + std::size_t(written));
            if (written) std::memcpy(out.data() + start, buffer.data(), std::size_t(written));
            return Error::none;
        };
        for (std::uint64_t frame = 0; frame < audio.frames;) {
            const auto count = unsigned(std::min<std::uint64_t>(encodeChunkFrames, audio.frames - frame));
            const auto* in = audio.samples.data() + frame * audio.channels;
            for (std::size_t i = 0; i < std::size_t(count) * audio.channels; ++i) chunk[i] = PcmToFloat(in[i], audio.bitsPerSample);
            const int written = audio.channels == 2
                ? lame_encode_buffer_interleaved_ieee_float(g, chunk.data(), int(count), buffer.data(), int(buffer.size()))
                : lame_encode_buffer_ieee_float(g, chunk.data(), chunk.data(), int(count), buffer.data(), int(buffer.size()));
            if (const auto e = append(written); e != Error::none) return {e, {}};
            frame += count;
        }
        if (const auto e = append(lame_encode_flush(g, buffer.data(), int(buffer.size()))); e != Error::none) return {e, {}};
        // The stream begins with a placeholder frame of exactly the tag's size; the
        // final LAME tag (frame count, delay, padding, CRCs) replaces it.
        const std::size_t tagBytes = lame_get_lametag_frame(g, buffer.data(), buffer.size());
        if (tagBytes == 0 || tagBytes > buffer.size() || tagBytes > out.size()) return {Error::codec_failed, {}};
        std::memcpy(out.data(), buffer.data(), tagBytes);
        return {Error::none, std::move(out)};
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::codec_failed, {}};
    }
}

std::string_view LameVersionString() noexcept { return get_lame_version(); }
} // namespace arken::audio::v1
