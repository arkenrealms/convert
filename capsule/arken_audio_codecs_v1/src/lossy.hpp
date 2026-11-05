#pragma once
// Shared internals of the lossy codecs: the bounded Ogg demuxer/muxer, the
// float <-> canonical PCM policy, Vorbis/Opus channel order and trimming.
#include "bytes.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace arken::audio::v1::detail {

// Lossy decoders always produce 16-bit canonical PCM.
inline constexpr std::uint32_t lossyBits = 16;

// Float sample (nominal full scale +-1.0) to 16-bit canonical PCM:
// round half up of x * 32768 in double precision, then clip to [-32768, 32767].
// No dither, no noise shaping, no soft clipping: a pure function of x.
inline std::int32_t FloatToPcm16(float x) noexcept {
    const double scaled = std::floor(double(x) * 32768.0 + 0.5);
    if (!(scaled >= -32768.0)) return -32768; // also maps NaN to the negative rail
    if (scaled > 32767.0) return 32767;
    return std::int32_t(scaled);
}

// Canonical sample of `bits` significant bits to float in [-1, 1).
inline float PcmToFloat(std::int32_t sample, std::uint32_t bits) noexcept {
    return float(double(sample) / double(std::int64_t(1) << (bits - 1)));
}

// Vorbis I channel order (also Opus mapping family 1) -> WAVE (WAVEFORMATEXTENSIBLE)
// order, and the standard WAVE mask of that layout. Index: channels (1..8).
// wave[i] = codec[order[i]].
struct ChannelLayout {
    std::uint8_t order[8];
    std::uint32_t mask;
};
inline const ChannelLayout& VorbisLayout(std::uint32_t channels) noexcept {
    static const ChannelLayout layouts[9] = {
        {{0}, 0},
        {{0}, 0},                               // mono: no mask (as WAVE_FORMAT_PCM)
        {{0, 1}, 0},                            // stereo: no mask
        {{0, 2, 1}, 0x7},                       // L C R -> FL FR FC
        {{0, 1, 2, 3}, 0x33},                   // FL FR RL RR -> FL FR BL BR
        {{0, 2, 1, 3, 4}, 0x37},                // FL C FR RL RR -> FL FR FC BL BR
        {{0, 2, 1, 5, 3, 4}, 0x3F},             // FL C FR RL RR LFE -> FL FR FC LFE BL BR
        {{0, 2, 1, 6, 5, 3, 4}, 0x70F},         // FL C FR SL SR RC LFE -> FL FR FC LFE BC SL SR
        {{0, 2, 1, 7, 5, 6, 3, 4}, 0x63F},      // FL C FR SL SR RL RR LFE -> FL FR FC LFE BL BR SL SR
    };
    return layouts[channels <= 8 ? channels : 0];
}

// Applies the container-declared trimming to raw decoded frames.
//  base:  granule position (in output frames) of the first decoded frame
//  start: first granule position to keep (pre-skip for Opus, 0 for Vorbis)
//  end:   final granule position (exclusive end of the kept range)
// Returns false when the declared range is not covered by the decoded frames
// or when the end trim exceeds maximumEndTrim.
struct Trim {
    std::uint64_t leading = 0, kept = 0, trailing = 0;
};
inline bool PlanTrim(std::int64_t base, std::int64_t start, std::int64_t end, std::uint64_t decoded,
    std::uint64_t maximumEndTrim, Trim& trim) noexcept {
    const std::int64_t first = base > start ? base : start;
    if (end < first) return false;
    const std::uint64_t leading = std::uint64_t(first - base);
    const std::uint64_t kept = std::uint64_t(end - first);
    if (leading > decoded || kept > decoded - leading) return false;
    const std::uint64_t trailing = decoded - leading - kept;
    if (trailing > maximumEndTrim) return false;
    trim = {leading, kept, trailing};
    return true;
}

// ---- Ogg (libogg) ----
enum class OggCodec { vorbis, opus };

struct OggPageRef {
    std::size_t offset = 0, headerLength = 0, bodyLength = 0;
    std::int64_t granule = -1;
};

// Result of the bounded page-level pass over a complete Ogg source.
struct OggScan {
    Issue issue;
    std::uint32_t serial = 0;
    std::vector<OggPageRef> pages; // pages of the selected logical stream, in order
    std::int64_t lastGranule = -1; // last non-(-1) granule position of the selected stream
    std::uint32_t skippedStreams = 0;
};
// Page sync and CRC (libogg), BOS grouping, one selected logical stream (the first
// of `codec`), chained links rejected, the selected stream must end with EOS and
// every byte must belong to a page.
OggScan ScanOgg(Bytes source, OggCodec codec, const Limits& limits);

// Feeds the selected pages to a libogg stream and hands out packets.
class OggPackets {
public:
    OggPackets(Bytes source, const OggScan& scan);
    ~OggPackets();
    OggPackets(const OggPackets&) = delete;
    OggPackets& operator=(const OggPackets&) = delete;
    bool Valid() const noexcept { return valid_; }
    // Loads the next page; false when every page has been consumed.
    // Sets error on a libogg page rejection.
    bool NextPage(Error& error) noexcept;
    // 1: packet available, 0: page drained, -1: hole/corruption.
    int NextPacket(const unsigned char*& data, long& bytes) noexcept;
    std::int64_t PageGranule() const noexcept;
    bool LastPage() const noexcept { return next_ == scan_.pages.size(); }
    // Timeline anchor from the first audio page with a granule position: the
    // granule of the first decoded frame. A granule below the frames decoded so
    // far trims the start, except on the final page, where (Vorbis I A.2,
    // RFC 7845 4.5) it trims the end instead.
    std::int64_t Base(std::uint64_t decoded) const noexcept {
        const std::int64_t base = PageGranule() - std::int64_t(decoded);
        return base < 0 && LastPage() ? 0 : base;
    }
    bool Incomplete() const noexcept; // a partial packet is left after the last page
private:
    Bytes source_;
    const OggScan& scan_;
    std::size_t next_ = 0;
    bool valid_ = false;
    void* state_ = nullptr; // ogg_stream_state
};

// Collects Ogg pages within an output byte limit.
struct OggSink {
    std::vector<std::byte> bytes;
    std::uint64_t limit = 0;
    Error error = Error::none;
    bool Append(const unsigned char* data, long size);
};
} // namespace arken::audio::v1::detail
