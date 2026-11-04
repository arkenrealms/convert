#include "bytes.hpp"
#include <new>

namespace arken::audio::v1 {
namespace {
using namespace detail;
constexpr std::uint16_t formatPcm = 0x0001, formatFloat = 0x0003, formatExtensible = 0xFFFE;
// KSDATAFORMAT_SUBTYPE_* GUIDs share this tail after their 16-bit format code:
// {0000xxxx-0000-0010-8000-00AA00389B71}, stored little-endian.
constexpr std::uint8_t subtypeTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                          0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

struct Format {
    std::uint32_t channels = 0, sampleRate = 0, containerBits = 0, validBits = 0, blockAlign = 0;
    std::optional<std::uint32_t> channelMask;
};

DecodeResult ParseFormat(Bytes b, std::size_t body, std::uint32_t size, const Limits& limits,
    Format& out) {
    if (size < 16) return Failure(Error::invalid_header, body);
    const auto tag = U16(b, body);
    const std::uint32_t channels = U16(b, body + 2), rate = U32(b, body + 4), average = U32(b, body + 8);
    const std::uint32_t align = U16(b, body + 12), bits = U16(b, body + 14);
    std::uint32_t valid = bits;
    std::optional<std::uint32_t> mask;
    if (tag == formatExtensible) {
        if (size < 40) return Failure(Error::invalid_header, body);
        const std::uint32_t extra = U16(b, body + 16);
        if (extra < 22 || extra > size - 18) return Failure(Error::invalid_header, body + 16);
        if (std::memcmp(b.data() + body + 26, subtypeTail, sizeof subtypeTail) != 0)
            return Failure(Error::unsupported_encoding, body + 24);
        const auto subtype = U16(b, body + 24);
        if (subtype == formatFloat) return Failure(Error::unsupported_float, body + 24);
        if (subtype != formatPcm) return Failure(Error::unsupported_encoding, body + 24);
        valid = U16(b, body + 18);
        mask = U32(b, body + 20);
        if (valid == 0 || valid > bits) return Failure(Error::invalid_header, body + 18);
    } else if (tag == formatPcm) {
        if (size >= 18 && U16(b, body + 16) > size - 18) return Failure(Error::invalid_header, body + 16);
    } else if (tag == formatFloat) {
        return Failure(Error::unsupported_float, body);
    } else {
        return Failure(Error::unsupported_encoding, body);
    }
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return Failure(Error::unsupported_bit_depth, body + 14);
    if (valid < 4) return Failure(Error::unsupported_bit_depth, body + 18);
    if (channels == 0 || channels > limits.maximumChannels) return Failure(Error::channel_limit, body + 2);
    if (rate == 0 || rate < limits.minimumSampleRate || rate > limits.maximumSampleRate)
        return Failure(Error::sample_rate_limit, body + 4);
    if (align != channels * (bits / 8)) return Failure(Error::invalid_header, body + 12);
    if (std::uint64_t(average) != std::uint64_t(rate) * align) return Failure(Error::invalid_header, body + 8);
    out = {channels, rate, bits, valid, align, mask};
    return {};
}

std::int32_t ReadContainer(Bytes b, std::size_t p, std::uint32_t bits) noexcept {
    switch (bits) {
    case 8: return std::int32_t(U8(b, p)) - 128;
    case 16: return std::int16_t(U16(b, p));
    case 24: return std::int32_t(std::uint32_t(U16(b, p) | (U8(b, p + 2) << 16)) << 8) >> 8;
    default: return std::int32_t(U32(b, p));
    }
}
} // namespace

DecodeResult DecodeWav(std::span<const std::byte> bytes, const Limits& limits) noexcept {
    try {
        if (bytes.size() > limits.maximumSourceBytes) return Failure(Error::input_too_large);
        if (Is(bytes, 0, "RF64") || Is(bytes, 0, "BW64") || Is(bytes, 0, "RIFX") || Is(bytes, 0, "riff"))
            return Failure(Error::unsupported_container);
        if (bytes.size() < 12) return Failure(Error::truncated);
        if (!Is(bytes, 0, "RIFF") || !Is(bytes, 8, "WAVE")) return Failure(Error::wrong_magic);
        const auto extent = std::uint64_t(U32(bytes, 4)) + 8;
        if (extent > bytes.size()) return Failure(Error::truncated, 4);
        if (extent < bytes.size()) return Failure(Error::invalid_layout, extent);
        Format format;
        bool haveFormat = false, haveData = false;
        std::size_t dataOffset = 0;
        std::uint32_t dataSize = 0, chunks = 0;
        DecodeReport report;
        for (std::size_t p = 12; p < extent;) {
            if (++chunks > limits.maximumMetadataBlocks) return Failure(Error::metadata_limit, p);
            if (!Range(bytes, p, 8)) return Failure(Error::truncated, p);
            const auto size = U32(bytes, p + 4);
            const auto body = p + 8;
            if (!Range(bytes, body, std::uint64_t(size) + (size & 1u))) return Failure(Error::truncated, p);
            if (Is(bytes, p, "data")) {
                if (!haveFormat || haveData) return Failure(Error::invalid_layout, p);
                haveData = true;
                dataOffset = body;
                dataSize = size;
            } else {
                if (size > limits.maximumMetadataBlockBytes) return Failure(Error::metadata_limit, p);
                if (Is(bytes, p, "fmt ")) {
                    if (haveFormat || haveData) return Failure(Error::invalid_layout, p);
                    auto parsed = ParseFormat(bytes, body, size, limits, format);
                    if (!parsed) return parsed;
                    haveFormat = true;
                } else {
                    ++report.skippedChunks;
                }
            }
            p = body + size + (size & 1u);
        }
        if (!haveFormat || !haveData) return Failure(Error::invalid_layout);
        if (dataSize % format.blockAlign) return Failure(Error::invalid_header, dataOffset - 4);
        const std::uint64_t frames = dataSize / format.blockAlign;
        if (frames > limits.maximumTotalSamples / format.channels) return Failure(Error::sample_limit, dataOffset - 4);
        DecodeResult result;
        result.report = report;
        auto& audio = result.audio;
        audio.sampleRate = format.sampleRate;
        audio.channels = format.channels;
        audio.bitsPerSample = format.validBits;
        audio.channelMask = format.channelMask;
        audio.frames = frames;
        audio.samples.resize(std::size_t(frames * format.channels));
        const std::uint32_t width = format.containerBits / 8, shift = format.containerBits - format.validBits;
        const std::int32_t padding = shift ? std::int32_t((1u << shift) - 1u) : 0;
        std::size_t p = dataOffset;
        for (auto& sample : audio.samples) {
            const auto stored = ReadContainer(bytes, p, format.containerBits);
            if (stored & padding) return Failure(Error::invalid_sample_padding, p);
            sample = stored >> shift;
            p += width;
        }
        return result;
    } catch (const std::bad_alloc&) {
        return Failure(Error::allocation_failed);
    } catch (...) {
        return Failure(Error::invalid_audio);
    }
}

EncodeResult EncodeWav(const Audio& audio, const Limits& limits) noexcept {
    try {
        if (const auto invalid = ValidateAudio(audio, limits); invalid != Error::none) return {invalid, {}};
        const std::uint32_t containerBits = (audio.bitsPerSample + 7) / 8 * 8;
        const std::uint32_t width = containerBits / 8, shift = containerBits - audio.bitsPerSample;
        const bool extensible = audio.channelMask.has_value() || shift != 0;
        const std::uint32_t formatSize = extensible ? 40 : 16;
        const std::uint64_t blockAlign = std::uint64_t(audio.channels) * width;
        if (blockAlign > 0xFFFF || std::uint64_t(audio.sampleRate) * blockAlign > 0xFFFFFFFFu)
            return {Error::invalid_audio, {}};
        const std::uint64_t dataSize = audio.frames * blockAlign;
        const std::uint64_t total = 12 + 8 + formatSize + 8 + dataSize + (dataSize & 1u);
        if (total - 8 > 0xFFFFFFFFu || total > limits.maximumOutputBytes) return {Error::output_limit, {}};
        EncodeResult result;
        auto& out = result.bytes;
        out.resize(std::size_t(total));
        std::size_t p = 0;
        const auto put = [&](std::uint64_t value, unsigned count) {
            for (unsigned i = 0; i < count; ++i) out[p++] = std::byte((value >> (8 * i)) & 0xFF);
        };
        const auto tag = [&](const char* id) {
            for (int i = 0; i < 4; ++i) out[p++] = std::byte(std::uint8_t(id[i]));
        };
        tag("RIFF"); put(total - 8, 4); tag("WAVE");
        tag("fmt "); put(formatSize, 4);
        put(extensible ? formatExtensible : formatPcm, 2);
        put(audio.channels, 2); put(audio.sampleRate, 4);
        put(audio.sampleRate * blockAlign, 4); put(blockAlign, 2); put(containerBits, 2);
        if (extensible) {
            put(22, 2); put(audio.bitsPerSample, 2); put(audio.channelMask.value_or(0), 4);
            put(formatPcm, 2);
            for (const auto byte : subtypeTail) out[p++] = std::byte(byte);
        }
        tag("data"); put(dataSize, 4);
        for (const auto sample : audio.samples) {
            const auto stored = std::uint32_t(sample) << shift;
            if (containerBits == 8) out[p++] = std::byte(std::uint8_t(stored + 128u));
            else put(stored, width);
        }
        // A trailing pad byte (odd data size) is already zero from resize().
        return result;
    } catch (const std::bad_alloc&) {
        return {Error::allocation_failed, {}};
    } catch (...) {
        return {Error::invalid_audio, {}};
    }
}
} // namespace arken::audio::v1
