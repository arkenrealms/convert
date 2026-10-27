#pragma once
#include "arken_image_codecs_v1/api.hpp"

namespace arken::image::v1::detail {
constexpr bool LargeRasterFormat(Format format) noexcept {
    return format==Format::png||format==Format::bmp||format==Format::jpeg||format==Format::tga;
}
constexpr bool ValidProfile(RasterProfile profile,Format format) noexcept {
    return profile==RasterProfile::bounded
        ||(profile==RasterProfile::largeRaster&&LargeRasterFormat(format));
}
constexpr std::uint64_t DecodePixelCeiling(RasterProfile profile,Format format) noexcept {
    return (profile==RasterProfile::largeRaster||format==Format::png?32ull:8ull)*1024*1024;
}
constexpr std::uint64_t EncodePixelCeiling(RasterProfile profile,Format format) noexcept {
    return (profile==RasterProfile::largeRaster?32ull:format==Format::png?16ull:8ull)*1024*1024;
}
constexpr std::uint64_t EncodeByteCeiling(RasterProfile profile) noexcept {
    return (profile==RasterProfile::largeRaster?160ull:64ull)*1024*1024;
}
}
