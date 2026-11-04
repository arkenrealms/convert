#include "lossy.hpp"
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <algorithm>
#include <new>

namespace arken::audio::v1 {
namespace {
using namespace detail;
// Fixed serial number of every Ogg stream this capsule writes ("ARKV").
constexpr int vorbisSerial = 0x41524B56;
constexpr unsigned encodeChunkFrames = 1024;

struct VorbisState {
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    bool dspReady = false, blockReady = false;
    VorbisState() noexcept {
        vorbis_info_init(&info);
        vorbis_comment_init(&comment);
    }
    ~VorbisState() {
        if (blockReady) vorbis_block_clear(&block);
        if (dspReady) vorbis_dsp_clear(&dsp);
        vorbis_comment_clear(&comment);
        vorbis_info_clear(&info);
    }
    VorbisState(const VorbisState&) = delete;
    VorbisState& operator=(const VorbisState&) = delete;
};

struct OggStream {
    ogg_stream_state state;
    bool ready = false;
    explicit OggStream(int serial) noexcept { ready = ogg_stream_init(&state, serial) == 0; }
    ~OggStream() { if (ready) ogg_stream_clear(&state); }
    OggStream(const OggStream&) = delete;
    OggStream& operator=(const OggStream&) = delete;
};
} // namespace

DecodeResult DecodeVorbis(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        const auto scan = ScanOgg(bytes, OggCodec::vorbis, limits);
        if (scan.issue.code != Error::none) return Failure(scan.issue.code, scan.issue.byteOffset);
        DecodeResult result;
        result.report.lossy = true;
        result.report.skippedStreams = scan.skippedStreams;
        auto& audio = result.audio;
        VorbisState v;
        OggPackets packets(bytes, scan);
        if (!packets.Valid()) return Failure(Error::allocation_failed);
        std::uint32_t headers = 0;
        std::uint64_t frameLimit = 0, decoded = 0;
        std::int64_t base = 0;
        bool baseKnown = false;
        std::vector<std::int32_t> raw;
        const ChannelLayout* layout = nullptr;
        for (Error pageError = Error::none; packets.NextPage(pageError);) {
            bool audioOnPage = false;
            const unsigned char* data = nullptr;
            long size = 0;
            for (int r; (r = packets.NextPacket(data, size)) != 0;) {
                if (r < 0) return Failure(Error::corrupt_stream);
                ogg_packet op{};
                op.packet = const_cast<unsigned char*>(data);
                op.bytes = size;
                op.b_o_s = headers == 0;
                op.granulepos = -1; // trimming is ours (PlanTrim); libvorbis must not trim on its own
                if (headers < 3) {
                    if (std::uint64_t(size) > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit);
                    if (vorbis_synthesis_headerin(&v.info, &v.comment, &op) != 0)
                        return Failure(headers == 0 ? Error::invalid_header : Error::invalid_metadata);
                    if (++headers == 1) {
                        if (v.info.channels < 1 || std::uint32_t(v.info.channels) > limits.maximumChannels
                            || v.info.channels > 8) return Failure(Error::channel_limit);
                        if (v.info.rate < 1 || std::uint64_t(v.info.rate) < limits.minimumSampleRate
                            || std::uint64_t(v.info.rate) > limits.maximumSampleRate) return Failure(Error::sample_rate_limit);
                        const auto channels = std::uint32_t(v.info.channels);
                        frameLimit = limits.maximumTotalSamples / channels;
                        // Output never exceeds the final granule position.
                        if (scan.lastGranule > 0 && std::uint64_t(scan.lastGranule) > frameLimit) return Failure(Error::sample_limit);
                        layout = &VorbisLayout(channels);
                        audio.sampleRate = std::uint32_t(v.info.rate);
                        audio.channels = channels;
                        audio.bitsPerSample = lossyBits;
                        if (channels > 2) audio.channelMask = layout->mask;
                    } else if (headers == 3) {
                        result.report.droppedComments = std::uint32_t(v.comment.comments);
                        if (vorbis_synthesis_init(&v.dsp, &v.info) != 0) return Failure(Error::invalid_metadata);
                        v.dspReady = true;
                        if (vorbis_block_init(&v.dsp, &v.block) != 0) return Failure(Error::allocation_failed);
                        v.blockReady = true;
                        if (scan.lastGranule > 0) raw.reserve(std::size_t(scan.lastGranule) * audio.channels);
                    }
                    continue;
                }
                audioOnPage = true;
                if (vorbis_synthesis(&v.block, &op) != 0 || vorbis_synthesis_blockin(&v.dsp, &v.block) != 0)
                    return Failure(Error::corrupt_stream);
                float** pcm = nullptr;
                for (int n; (n = vorbis_synthesis_pcmout(&v.dsp, &pcm)) > 0;) {
                    // Bound memory before growing: a stream may claim little and decode much.
                    if (decoded + std::uint64_t(n) > frameLimit + 8192) return Failure(Error::sample_limit);
                    const auto start = raw.size();
                    raw.resize(start + std::size_t(n) * audio.channels);
                    auto* out = raw.data() + start;
                    for (int i = 0; i < n; ++i)
                        for (std::uint32_t c = 0; c < audio.channels; ++c)
                            *out++ = FloatToPcm16(pcm[layout->order[c]][i]);
                    decoded += std::uint64_t(n);
                    vorbis_synthesis_read(&v.dsp, n);
                }
            }
            // First audio page with a granule position anchors the timeline:
            // a granule below the frames decoded so far means a trimmed start.
            if (audioOnPage && !baseKnown && packets.PageGranule() != -1) {
                base = packets.Base(decoded);
                baseKnown = true;
            }
            pageError = Error::none;
        }
        if (headers < 3) return Failure(headers == 0 ? Error::invalid_header : Error::truncated);
        if (packets.Incomplete()) return Failure(Error::truncated);
        if (!baseKnown) base = 0;
        Trim trim;
        const auto endTrim = std::uint64_t(vorbis_info_blocksize(&v.info, 1));
        if (!PlanTrim(base, 0, std::max<std::int64_t>(scan.lastGranule, 0), decoded, endTrim, trim))
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

EncodeResult EncodeVorbis(const Audio& audio, const LossyOptions& options, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        if (audio.channels > 2) return {Error::channel_limit, {}};
        if (options.vorbisQuality > 100) return {Error::invalid_option, {}};
        const float quality = float(-0.1 + 1.1 * double(options.vorbisQuality) / 100.0);
        VorbisState v;
        if (vorbis_encode_init_vbr(&v.info, long(audio.channels), long(audio.sampleRate), quality) != 0)
            return {Error::sample_rate_limit, {}}; // no libvorbis mode for this rate
        if (vorbis_analysis_init(&v.dsp, &v.info) != 0) return {Error::codec_failed, {}};
        v.dspReady = true;
        if (vorbis_block_init(&v.dsp, &v.block) != 0) return {Error::allocation_failed, {}};
        v.blockReady = true;
        OggStream stream(vorbisSerial);
        if (!stream.ready) return {Error::allocation_failed, {}};
        OggSink sink;
        sink.limit = limits.maximumOutputBytes;
        ogg_page page;
        {
            ogg_packet id, comments, setup;
            if (vorbis_analysis_headerout(&v.dsp, &v.comment, &id, &comments, &setup) != 0) return {Error::codec_failed, {}};
            ogg_stream_packetin(&stream.state, &id);
            ogg_stream_packetin(&stream.state, &comments);
            ogg_stream_packetin(&stream.state, &setup);
            // The first audio packet starts on a fresh page (Vorbis I section A.2).
            while (ogg_stream_flush(&stream.state, &page))
                if (!sink.Append(page.header, page.header_len) || !sink.Append(page.body, page.body_len)) return {sink.error, {}};
        }
        const auto drain = [&]() -> bool {
            while (vorbis_analysis_blockout(&v.dsp, &v.block) == 1) {
                if (vorbis_analysis(&v.block, nullptr) != 0 || vorbis_bitrate_addblock(&v.block) != 0) {
                    sink.error = Error::codec_failed;
                    return false;
                }
                ogg_packet packet;
                while (vorbis_bitrate_flushpacket(&v.dsp, &packet) == 1) {
                    ogg_stream_packetin(&stream.state, &packet);
                    while (ogg_stream_pageout(&stream.state, &page))
                        if (!sink.Append(page.header, page.header_len) || !sink.Append(page.body, page.body_len)) return false;
                }
            }
            return true;
        };
        for (std::uint64_t frame = 0; frame < audio.frames;) {
            const auto count = unsigned(std::min<std::uint64_t>(encodeChunkFrames, audio.frames - frame));
            float** buffer = vorbis_analysis_buffer(&v.dsp, int(count));
            if (!buffer) return {Error::allocation_failed, {}};
            const auto* in = audio.samples.data() + frame * audio.channels;
            for (unsigned i = 0; i < count; ++i)
                for (std::uint32_t c = 0; c < audio.channels; ++c)
                    buffer[c][i] = PcmToFloat(*in++, audio.bitsPerSample);
            if (vorbis_analysis_wrote(&v.dsp, int(count)) != 0) return {Error::codec_failed, {}};
            if (!drain()) return {sink.error, {}};
            frame += count;
        }
        if (vorbis_analysis_wrote(&v.dsp, 0) != 0 || !drain()) return {sink.error != Error::none ? sink.error : Error::codec_failed, {}};
        while (ogg_stream_flush(&stream.state, &page))
            if (!sink.Append(page.header, page.header_len) || !sink.Append(page.body, page.body_len)) return {sink.error, {}};
        return {Error::none, std::move(sink.bytes)};
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::codec_failed, {}};
    }
}

std::string_view VorbisVendorString() noexcept { return vorbis_version_string(); }
} // namespace arken::audio::v1
