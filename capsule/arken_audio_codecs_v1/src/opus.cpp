#include "lossy.hpp"
#include <ogg/ogg.h>
#include <opus.h>
#include <opus_multistream.h>
#include <algorithm>
#include <array>
#include <memory>
#include <new>

namespace arken::audio::v1 {
namespace {
using namespace detail;
constexpr int opusSerial = 0x41524B4F;          // "ARKO", fixed
constexpr std::uint32_t opusRate = 48000;       // RFC 7845: granule positions and decoding at 48 kHz
constexpr int maximumPacketFrames = 5760;       // 120 ms at 48 kHz
constexpr std::size_t maximumPacketBytes = 1275 * 3 + 7; // RFC 6716 upper bound for one 120 ms packet

struct Head {
    std::uint32_t channels = 0, preSkip = 0, inputRate = 0, family = 0, streams = 0, coupled = 0;
    std::int32_t gain = 0;
    std::array<unsigned char, 255> mapping{};
};

std::uint16_t Le16(const unsigned char* p) noexcept { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t Le32(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

Error ParseHead(const unsigned char* p, long size, const Limits& limits, Head& head) noexcept {
    if (size < 19 || std::memcmp(p, "OpusHead", 8) != 0) return Error::invalid_header;
    if ((p[8] & 0xF0) != 0) return Error::invalid_header; // incompatible major version
    head.channels = p[9];
    head.preSkip = Le16(p + 10);
    head.inputRate = Le32(p + 12);
    head.gain = std::int16_t(Le16(p + 16));
    head.family = p[18];
    if (head.channels == 0) return Error::invalid_header;
    if (head.family == 0) {
        if (head.channels > 2) return Error::invalid_header;
        head.streams = 1;
        head.coupled = head.channels - 1;
        head.mapping[0] = 0;
        head.mapping[1] = 1;
    } else if (head.family == 1) {
        if (head.channels > 8) return Error::invalid_header; // family 1 is defined for 1..8 channels
        if (size < 21 + long(head.channels)) return Error::invalid_header;
        head.streams = p[19];
        head.coupled = p[20];
        if (head.streams == 0 || head.coupled > head.streams || head.streams + head.coupled > 255)
            return Error::unsupported_channel_mapping;
        for (std::uint32_t c = 0; c < head.channels; ++c) {
            head.mapping[c] = p[21 + c];
            if (head.mapping[c] != 255 && head.mapping[c] >= head.streams + head.coupled)
                return Error::unsupported_channel_mapping;
        }
    } else {
        return Error::unsupported_channel_mapping; // 2/3 (ambisonics), 255 (undefined order), reserved
    }
    if (head.channels > limits.maximumChannels) return Error::channel_limit;
    return Error::none;
}

// OpusTags: magic, vendor string, comment list; every length must fit the packet.
Error ParseTags(const unsigned char* p, long size, std::uint32_t& comments) noexcept {
    if (size < 16 || std::memcmp(p, "OpusTags", 8) != 0) return Error::invalid_metadata;
    const auto end = std::uint64_t(size);
    std::uint64_t at = 8;
    const std::uint64_t vendor = Le32(p + at);
    at += 4;
    if (vendor > end - at || end - at - vendor < 4) return Error::invalid_metadata;
    at += vendor;
    comments = Le32(p + at);
    at += 4;
    for (std::uint32_t i = 0; i < comments; ++i) {
        if (end - at < 4) return Error::invalid_metadata;
        const std::uint64_t length = Le32(p + at);
        at += 4;
        if (length > end - at) return Error::invalid_metadata;
        at += length;
    }
    return Error::none; // bytes after the list (e.g. binary padding) are permitted by RFC 7845
}

struct MsDecoderDeleter { void operator()(OpusMSDecoder* d) const noexcept { opus_multistream_decoder_destroy(d); } };
struct EncoderDeleter { void operator()(OpusEncoder* e) const noexcept { opus_encoder_destroy(e); } };
struct OggStream {
    ogg_stream_state state;
    bool ready = false;
    explicit OggStream(int serial) noexcept { ready = ogg_stream_init(&state, serial) == 0; }
    ~OggStream() { if (ready) ogg_stream_clear(&state); }
    OggStream(const OggStream&) = delete;
    OggStream& operator=(const OggStream&) = delete;
};

void Put16(std::vector<unsigned char>& out, std::uint32_t v) { out.push_back((unsigned char)(v & 0xFF)); out.push_back((unsigned char)(v >> 8)); }
void Put32(std::vector<unsigned char>& out, std::uint32_t v) { Put16(out, v & 0xFFFF); Put16(out, v >> 16); }
} // namespace

DecodeResult DecodeOpus(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        const auto scan = ScanOgg(bytes, OggCodec::opus, limits);
        if (scan.issue.code != Error::none) return Failure(scan.issue.code, scan.issue.byteOffset);
        if (opusRate < limits.minimumSampleRate || opusRate > limits.maximumSampleRate) return Failure(Error::sample_rate_limit);
        DecodeResult result;
        result.report.lossy = true;
        result.report.skippedStreams = scan.skippedStreams;
        auto& audio = result.audio;
        OggPackets packets(bytes, scan);
        if (!packets.Valid()) return Failure(Error::allocation_failed);
        Head head;
        std::unique_ptr<OpusMSDecoder, MsDecoderDeleter> decoder;
        std::uint32_t headers = 0;
        std::uint64_t frameLimit = 0, decoded = 0;
        std::int64_t base = 0;
        bool baseKnown = false;
        std::vector<std::int32_t> raw;
        std::vector<float> pcm;
        const ChannelLayout* layout = nullptr;
        for (Error pageError = Error::none; packets.NextPage(pageError);) {
            bool audioOnPage = false;
            const unsigned char* data = nullptr;
            long size = 0;
            for (int r; (r = packets.NextPacket(data, size)) != 0;) {
                if (r < 0) return Failure(Error::corrupt_stream);
                if (headers == 0) {
                    if (const auto e = ParseHead(data, size, limits, head); e != Error::none) return Failure(e);
                    ++headers;
                    frameLimit = limits.maximumTotalSamples / head.channels;
                    if (scan.lastGranule < std::int64_t(head.preSkip)) return Failure(Error::corrupt_stream);
                    if (std::uint64_t(scan.lastGranule) - head.preSkip > frameLimit) return Failure(Error::sample_limit);
                    int error = OPUS_OK;
                    decoder.reset(opus_multistream_decoder_create(opus_int32(opusRate), int(head.channels), int(head.streams),
                        int(head.coupled), head.mapping.data(), &error));
                    if (!decoder || error != OPUS_OK) return Failure(error == OPUS_ALLOC_FAIL ? Error::allocation_failed : Error::invalid_header);
                    // Output gain (Q7.8 dB) is applied by libopus as RFC 7845 requires.
                    if (opus_multistream_decoder_ctl(decoder.get(), OPUS_SET_GAIN(int(head.gain))) != OPUS_OK)
                        return Failure(Error::invalid_header);
                    layout = &VorbisLayout(head.channels); // family 1 uses Vorbis order; family 0 is 1-2 ch
                    audio.sampleRate = opusRate;
                    audio.channels = head.channels;
                    audio.bitsPerSample = lossyBits;
                    if (head.channels > 2) audio.channelMask = layout->mask;
                    result.report.originalSampleRate = head.inputRate;
                    pcm.resize(std::size_t(maximumPacketFrames) * head.channels);
                    raw.reserve(std::size_t(std::uint64_t(scan.lastGranule) - head.preSkip) * head.channels);
                    continue;
                }
                if (headers == 1) {
                    if (std::uint64_t(size) > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit);
                    if (const auto e = ParseTags(data, size, result.report.droppedComments); e != Error::none) return Failure(e);
                    ++headers;
                    continue;
                }
                audioOnPage = true;
                if (size <= 0) return Failure(Error::corrupt_stream); // RFC 7845: audio packets are never empty
                const int n = opus_multistream_decode_float(decoder.get(), data, opus_int32(size), pcm.data(),
                    maximumPacketFrames, 0);
                if (n < 0) return Failure(n == OPUS_ALLOC_FAIL ? Error::allocation_failed : Error::corrupt_stream);
                if (decoded + std::uint64_t(n) > frameLimit + std::uint64_t(head.preSkip) + maximumPacketFrames)
                    return Failure(Error::sample_limit);
                const auto start = raw.size();
                raw.resize(start + std::size_t(n) * head.channels);
                auto* out = raw.data() + start;
                for (int i = 0; i < n; ++i)
                    for (std::uint32_t c = 0; c < head.channels; ++c)
                        *out++ = FloatToPcm16(pcm[std::size_t(i) * head.channels + layout->order[c]]);
                decoded += std::uint64_t(n);
            }
            if (audioOnPage && !baseKnown && packets.PageGranule() != -1) {
                base = packets.Base(decoded);
                baseKnown = true;
            }
            pageError = Error::none;
        }
        if (headers < 2) return Failure(headers == 0 ? Error::invalid_header : Error::truncated);
        if (packets.Incomplete()) return Failure(Error::truncated);
        if (!baseKnown) base = 0;
        Trim trim;
        if (!PlanTrim(base, std::int64_t(head.preSkip), scan.lastGranule, decoded, maximumPacketFrames, trim))
            return Failure(Error::length_mismatch);
        if (trim.kept > frameLimit) return Failure(Error::sample_limit);
        audio.frames = trim.kept;
        // Trim in place: the decoded buffer becomes the result without a second copy.
        raw.resize(std::size_t((trim.leading + trim.kept) * audio.channels));
        raw.erase(raw.begin(), raw.begin() + std::ptrdiff_t(trim.leading * audio.channels));
        audio.samples = std::move(raw);
        result.report.trimmedLeadingFrames = trim.leading;
        result.report.trimmedTrailingFrames = trim.trailing;
        result.report.encoderDelayTrimmed = trim.leading || trim.trailing;
        return result;
    } catch (const std::bad_alloc&) {
        return Failure(Error::allocation_failed);
    } catch (...) {
        return Failure(Error::codec_failed);
    }
}

EncodeResult EncodeOpus(const Audio& audio, const LossyOptions& options, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        if (audio.channels > 2) return {Error::channel_limit, {}};
        const auto rate = audio.sampleRate;
        if (rate != 8000 && rate != 12000 && rate != 16000 && rate != 24000 && rate != 48000)
            return {Error::resampling_required, {}};
        if (options.opusBitrateKbps < 6 || options.opusBitrateKbps > 510) return {Error::invalid_option, {}};
        int error = OPUS_OK;
        std::unique_ptr<OpusEncoder, EncoderDeleter> encoder(
            opus_encoder_create(opus_int32(rate), int(audio.channels), OPUS_APPLICATION_AUDIO, &error));
        if (!encoder || error != OPUS_OK) return {error == OPUS_ALLOC_FAIL ? Error::allocation_failed : Error::codec_failed, {}};
        auto* e = encoder.get();
        // Every setting that shapes the bitstream is explicit.
        if (opus_encoder_ctl(e, OPUS_SET_BITRATE(opus_int32(options.opusBitrateKbps * 1000))) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_VBR(1)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_VBR_CONSTRAINT(0)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(10)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_SIGNAL(OPUS_AUTO)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_BANDWIDTH(OPUS_AUTO)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_FORCE_CHANNELS(OPUS_AUTO)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(0)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_PACKET_LOSS_PERC(0)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_DTX(0)) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_LSB_DEPTH(opus_int32(std::clamp<std::uint32_t>(audio.bitsPerSample, 8, 24)))) != OPUS_OK
            || opus_encoder_ctl(e, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS)) != OPUS_OK)
            return {Error::codec_failed, {}};
        opus_int32 lookahead = 0;
        if (opus_encoder_ctl(e, OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK || lookahead < 0) return {Error::codec_failed, {}};
        const std::uint32_t scale = opusRate / rate;               // exact for the admitted rates
        const std::uint32_t frameSize = rate / 50;                 // 20 ms
        const std::uint64_t preSkip = std::uint64_t(lookahead) * scale;
        const std::uint64_t total = audio.frames * scale;
        if (preSkip > 0xFFFF) return {Error::codec_failed, {}};
        // Enough 20 ms packets that the last input sample has left the encoder's lookahead.
        const std::uint64_t packets = std::max<std::uint64_t>(1, (audio.frames + std::uint64_t(lookahead) + frameSize - 1) / frameSize);

        OggStream stream(opusSerial);
        if (!stream.ready) return {Error::allocation_failed, {}};
        OggSink sink;
        sink.limit = limits.maximumOutputBytes;
        ogg_page page;
        const auto flush = [&]() {
            while (ogg_stream_flush(&stream.state, &page))
                if (!sink.Append(page.header, page.header_len) || !sink.Append(page.body, page.body_len)) return false;
            return true;
        };
        std::vector<unsigned char> headBytes, tagsBytes;
        headBytes.insert(headBytes.end(), {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, (unsigned char)audio.channels});
        Put16(headBytes, std::uint32_t(preSkip));
        Put32(headBytes, rate);
        Put16(headBytes, 0); // output gain
        headBytes.push_back(0); // mapping family 0
        const std::string_view vendor = opus_get_version_string();
        tagsBytes.insert(tagsBytes.end(), {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'});
        Put32(tagsBytes, std::uint32_t(vendor.size()));
        tagsBytes.insert(tagsBytes.end(), vendor.begin(), vendor.end());
        Put32(tagsBytes, 0); // no user comments
        ogg_int64_t packetNumber = 0;
        ogg_packet op{};
        op.packet = headBytes.data();
        op.bytes = long(headBytes.size());
        op.b_o_s = 1;
        op.granulepos = 0;
        op.packetno = packetNumber++;
        ogg_stream_packetin(&stream.state, &op);
        if (!flush()) return {sink.error, {}};
        op = {};
        op.packet = tagsBytes.data();
        op.bytes = long(tagsBytes.size());
        op.granulepos = 0;
        op.packetno = packetNumber++;
        ogg_stream_packetin(&stream.state, &op);
        if (!flush()) return {sink.error, {}}; // audio starts on a fresh page (RFC 7845 section 3)

        std::vector<float> input(std::size_t(frameSize) * audio.channels);
        std::vector<unsigned char> packet(maximumPacketBytes);
        for (std::uint64_t k = 0; k < packets; ++k) {
            const std::uint64_t first = k * frameSize;
            for (std::uint32_t i = 0; i < frameSize; ++i)
                for (std::uint32_t c = 0; c < audio.channels; ++c) {
                    const auto f = first + i;
                    input[std::size_t(i) * audio.channels + c] =
                        f < audio.frames ? PcmToFloat(audio.samples[f * audio.channels + c], audio.bitsPerSample) : 0.0f;
                }
            const opus_int32 length = opus_encode_float(e, input.data(), int(frameSize), packet.data(), opus_int32(packet.size()));
            if (length < 0) return {Error::codec_failed, {}};
            const bool last = k + 1 == packets;
            op = {};
            op.packet = packet.data();
            op.bytes = long(length);
            op.e_o_s = last ? 1 : 0;
            // Granule = 48 kHz samples decoded through this packet (pre-skip included);
            // the final one is exact, so decoders trim the end to the input length.
            op.granulepos = ogg_int64_t(last ? preSkip + total : (k + 1) * frameSize * scale);
            op.packetno = packetNumber++;
            ogg_stream_packetin(&stream.state, &op);
            while (ogg_stream_pageout(&stream.state, &page))
                if (!sink.Append(page.header, page.header_len) || !sink.Append(page.body, page.body_len)) return {sink.error, {}};
        }
        if (!flush()) return {sink.error, {}};
        return {Error::none, std::move(sink.bytes)};
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::codec_failed, {}};
    }
}

std::string_view OpusVersionString() noexcept { return opus_get_version_string(); }
} // namespace arken::audio::v1
