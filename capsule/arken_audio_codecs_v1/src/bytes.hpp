#pragma once
#include "arken_audio_codecs_v1/api.hpp"
#include <cstring>

namespace arken::audio::v1::detail {
using Bytes = std::span<const std::byte>;
inline bool Range(Bytes b, std::uint64_t offset, std::uint64_t size) noexcept {
    return offset <= b.size() && size <= b.size() - offset;
}
inline std::uint8_t U8(Bytes b, std::size_t p) noexcept { return std::to_integer<std::uint8_t>(b[p]); }
inline std::uint16_t U16(Bytes b, std::size_t p) noexcept {
    return std::uint16_t(U8(b, p) | (U8(b, p + 1) << 8));
}
inline std::uint32_t U32(Bytes b, std::size_t p) noexcept {
    return std::uint32_t(U8(b, p)) | (std::uint32_t(U8(b, p + 1)) << 8)
        | (std::uint32_t(U8(b, p + 2)) << 16) | (std::uint32_t(U8(b, p + 3)) << 24);
}
inline std::uint16_t B16(Bytes b, std::size_t p) noexcept {
    return std::uint16_t((U8(b, p) << 8) | U8(b, p + 1));
}
inline std::uint32_t B24(Bytes b, std::size_t p) noexcept {
    return (std::uint32_t(U8(b, p)) << 16) | (std::uint32_t(U8(b, p + 1)) << 8) | U8(b, p + 2);
}
inline std::uint32_t B32(Bytes b, std::size_t p) noexcept {
    return (std::uint32_t(U8(b, p)) << 24) | B24(b, p + 1);
}
inline bool Is(Bytes b, std::size_t p, const char* id) noexcept {
    return Range(b, p, 4) && std::memcmp(b.data() + p, id, 4) == 0;
}
inline DecodeResult Failure(Error code, std::uint64_t offset = 0) {
    DecodeResult result;
    result.issue = {code, offset};
    return result;
}
} // namespace arken::audio::v1::detail
