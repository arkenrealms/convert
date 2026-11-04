#include "bytes.hpp"
#include <bit>
#include <new>

// AIFF (FORM/AIFF) and uncompressed AIFF-C (FORM/AIFC). All chunk sizes and
// sample words are big-endian except 'sowt' sample data. Sample points are
// left-justified in a container of ceil(sampleSize / 8) bytes and the bits
// below sampleSize must be zero, exactly like WAV's valid-bits padding.
namespace arken::audio::v1 {
namespace {
using namespace detail;

enum class Order { big, little, offsetBinary };

struct Common {
    std::uint32_t channels = 0, frames = 0, sampleSize = 0, width = 0, sampleRate = 0;
    Order order = Order::big;
};

constexpr std::uint32_t extendedBias = 16383;

// The COMM sampleRate is an 80-bit IEEE 754 extended value: sign, 15-bit
// biased exponent, 64-bit significand with an explicit integer bit, i.e.
// value = significand * 2^(exponent - 16383 - 63). Only an exact positive
// integer is a sample rate; nothing is rounded.
Error ExtendedRate(Bytes b, std::size_t p, const Limits& limits, std::uint32_t& rate) noexcept {
    const std::uint32_t head = B16(b, p);
    const std::uint64_t significand = (std::uint64_t(B32(b, p + 2)) << 32) | B32(b, p + 6);
    const std::uint32_t exponent = head & 0x7FFFu;
    if (exponent == 0x7FFFu) return Error::unsupported_sample_rate; // infinity or NaN
    if (significand == 0) return Error::sample_rate_limit;          // +0 or -0
    if (head & 0x8000u) return Error::unsupported_sample_rate;      // negative
    const std::int64_t shift = std::int64_t(exponent) - std::int64_t(extendedBias) - 63;
    std::uint64_t value = 0;
    if (shift >= 0) {
        if (shift > 31 || significand > (0xFFFFFFFFull >> shift)) return Error::sample_rate_limit;
        value = significand << shift;
    } else {
        if (shift <= -64) return Error::unsupported_sample_rate; // 0 < value < 1
        if (significand & ((std::uint64_t(1) << -shift) - 1)) return Error::unsupported_sample_rate;
        value = significand >> -shift;
        if (value > 0xFFFFFFFFull) return Error::sample_rate_limit;
    }
    if (value < limits.minimumSampleRate || value > limits.maximumSampleRate) return Error::sample_rate_limit;
    rate = std::uint32_t(value);
    return Error::none;
}

DecodeResult ParseCommon(Bytes b, std::size_t body, std::uint32_t size, bool aifc, const Limits& limits,
    Common& out) {
    if (size < (aifc ? 22u : 18u)) return Failure(Error::invalid_header, body);
    Order order = Order::big;
    if (aifc) {
        // compressionName is a Pascal string; when present it must fit the chunk.
        if (size > 22 && 1u + U8(b, body + 22) > size - 22) return Failure(Error::invalid_header, body + 22);
        const auto type = body + 18;
        if (Is(b, type, "NONE") || Is(b, type, "twos")) order = Order::big;
        else if (Is(b, type, "sowt")) order = Order::little;
        else if (Is(b, type, "raw ")) order = Order::offsetBinary;
        else if (Is(b, type, "fl32") || Is(b, type, "FL32") || Is(b, type, "fl64") || Is(b, type, "FL64"))
            return Failure(Error::unsupported_float, type);
        else return Failure(Error::unsupported_encoding, type);
    }
    const auto channels = std::int16_t(B16(b, body));
    const auto frames = B32(b, body + 2);
    const auto bits = std::int16_t(B16(b, body + 6));
    // AIFF admits 1..32; the canonical model starts at 4 significant bits.
    if (bits < 4 || bits > 32) return Failure(Error::unsupported_bit_depth, body + 6);
    const std::uint32_t width = (std::uint32_t(bits) + 7) / 8;
    if (order == Order::offsetBinary && width != 1) return Failure(Error::unsupported_bit_depth, body + 6);
    if (channels <= 0 || std::uint32_t(channels) > limits.maximumChannels) return Failure(Error::channel_limit, body);
    std::uint32_t rate = 0;
    if (const auto e = ExtendedRate(b, body + 8, limits, rate); e != Error::none) return Failure(e, body + 8);
    out = {std::uint32_t(channels), frames, std::uint32_t(bits), width, rate, order};
    return {};
}
} // namespace

DecodeResult DecodeAiff(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        if (bytes.size() < 12) return Failure(Error::truncated);
        if (!Is(bytes, 0, "FORM")) return Failure(Error::wrong_magic);
        bool aifc = false;
        if (Is(bytes, 8, "AIFC")) aifc = true;
        else if (!Is(bytes, 8, "AIFF")) return Failure(Error::wrong_magic, 8);
        const auto extent = std::uint64_t(B32(bytes, 4)) + 8;
        if (extent > bytes.size()) return Failure(Error::truncated, 4);
        if (extent < bytes.size()) return Failure(Error::invalid_layout, extent);
        Common common;
        bool haveCommon = false, haveSound = false;
        std::size_t commonBody = 0, soundBody = 0;
        std::uint32_t soundSize = 0, chunks = 0;
        DecodeReport report;
        // COMM and SSND may appear in either order (AIFF 1.3 section 4).
        for (std::size_t p = 12; p < extent;) {
            if (++chunks > limits.maximumMetadataBlocks) return Failure(Error::metadata_limit, p);
            if (!Range(bytes, p, 8)) return Failure(Error::truncated, p);
            const auto size = B32(bytes, p + 4);
            const auto body = p + 8;
            if (!Range(bytes, body, std::uint64_t(size) + (size & 1u))) return Failure(Error::truncated, p);
            if (Is(bytes, p, "SSND")) {
                if (haveSound) return Failure(Error::invalid_layout, p);
                haveSound = true;
                soundBody = body;
                soundSize = size;
            } else {
                if (size > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit, p);
                if (Is(bytes, p, "COMM")) {
                    if (haveCommon) return Failure(Error::invalid_layout, p);
                    auto parsed = ParseCommon(bytes, body, size, aifc, limits, common);
                    if (!parsed) return parsed;
                    haveCommon = true;
                    commonBody = body;
                } else {
                    ++report.skippedChunks; // FVER, MARK, INST, COMT, NAME, AUTH, (c) , ANNO, APPL, ID3 , ...
                }
            }
            p = body + size + (size & 1u);
        }
        // SSND is optional only when COMM declares zero frames.
        if (!haveCommon || (!haveSound && common.frames != 0)) return Failure(Error::invalid_layout);
        if (common.frames > limits.maximumTotalSamples / common.channels)
            return Failure(Error::sample_limit, commonBody + 2);
        const std::uint64_t blockAlign = std::uint64_t(common.channels) * common.width;
        const std::uint64_t dataSize = common.frames * blockAlign;
        std::size_t dataOffset = 0;
        if (haveSound) {
            if (soundSize < 8) return Failure(Error::invalid_header, soundBody - 8);
            // offset skips leading bytes before the first sample frame; blockSize
            // only describes the writer's alignment and does not move samples.
            const auto offset = B32(bytes, soundBody);
            if (offset > soundSize - 8) return Failure(Error::invalid_header, soundBody);
            if (std::uint64_t(soundSize) - 8 - offset != dataSize) return Failure(Error::invalid_header, soundBody - 8);
            dataOffset = soundBody + 8 + offset;
        }
        DecodeResult result;
        result.report = report;
        auto& audio = result.audio;
        audio.sampleRate = common.sampleRate;
        audio.channels = common.channels;
        audio.bitsPerSample = common.sampleSize;
        audio.channelMask = std::nullopt; // AIFF declares no speaker mask
        audio.frames = common.frames;
        audio.samples.resize(std::size_t(std::uint64_t(common.frames) * common.channels));
        const std::uint32_t width = common.width, containerBits = width * 8;
        const std::uint32_t padBits = containerBits - common.sampleSize;
        const std::uint32_t padMask = padBits ? (1u << padBits) - 1u : 0u;
        const std::uint32_t lift = 32 - containerBits, drop = 32 - common.sampleSize;
        std::size_t p = dataOffset;
        for (auto& sample : audio.samples) {
            std::uint32_t raw = 0;
            if (common.order == Order::little)
                for (std::uint32_t i = width; i-- > 0;) raw = (raw << 8) | U8(bytes, p + i);
            else
                for (std::uint32_t i = 0; i < width; ++i) raw = (raw << 8) | U8(bytes, p + i);
            if (common.order == Order::offsetBinary) raw ^= 0x80u;
            if (raw & padMask) return Failure(Error::invalid_sample_padding, p);
            sample = std::int32_t(raw << lift) >> drop;
            p += width;
        }
        return result;
    } catch (const std::bad_alloc&) {
        return Failure(Error::allocation_failed);
    } catch (...) {
        return Failure(Error::invalid_audio);
    }
}

EncodeResult EncodeAiff(const Audio& audio, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        if (audio.channels > 0x7FFF) return {Error::invalid_audio, {}};
        if (audio.frames > 0xFFFFFFFFull) return {Error::output_limit, {}};
        const std::uint32_t containerBits = (audio.bitsPerSample + 7) / 8 * 8;
        const std::uint32_t width = containerBits / 8, shift = containerBits - audio.bitsPerSample;
        const std::uint64_t dataSize = audio.frames * audio.channels * width;
        const std::uint64_t total = 12 + (8 + 18) + (8 + 8 + dataSize) + (dataSize & 1u);
        // Chunk sizes are signed 32-bit in AIFF; stay within the positive range.
        if (total - 8 > 0x7FFFFFFFu || total > limits.maximumOutputBytes) return {Error::output_limit, {}};
        EncodeResult result;
        auto& out = result.bytes;
        out.resize(std::size_t(total));
        std::size_t p = 0;
        const auto put = [&](std::uint64_t value, unsigned count) {
            for (unsigned i = count; i-- > 0;) out[p++] = std::byte((value >> (8 * i)) & 0xFF);
        };
        const auto tag = [&](const char* id) {
            for (int i = 0; i < 4; ++i) out[p++] = std::byte(std::uint8_t(id[i]));
        };
        tag("FORM"); put(total - 8, 4); tag("AIFF");
        tag("COMM"); put(18, 4);
        put(audio.channels, 2); put(audio.frames, 4); put(audio.bitsPerSample, 2);
        // Exact extended encoding of a positive integer: the integer bit is the
        // rate's top bit, so the significand is the rate shifted to bit 63.
        const unsigned top = unsigned(std::bit_width(audio.sampleRate)) - 1;
        put(extendedBias + top, 2);
        put(std::uint64_t(audio.sampleRate) << (63 - top), 8);
        tag("SSND"); put(8 + dataSize, 4); put(0, 4); put(0, 4);
        for (const auto sample : audio.samples) put(std::uint32_t(sample) << shift, width);
        // A trailing pad byte (odd sound data size) is already zero from resize().
        return result;
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::invalid_audio, {}};
    }
}
} // namespace arken::audio::v1
