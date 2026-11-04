#pragma once
#include "blp.hpp"
#include <string_view>

namespace arken::image::v1 {
// heif: HEIC/HEIF still images (HEVC). Decoded only where the memory-only decoder is compiled in
// (ARKEN_IMAGE_CODECS_V1_HEIF); otherwise the format is reported as unsupported content.
enum class Format : std::uint32_t {pcx=1, cel=2, dc6=3, blp=4, dds=5, tga=6, png=7, bmp=8, jpeg=9, gif=10, ico=11, tiff=12, webp=13, heif=14};
enum class RasterProfile : std::uint32_t {bounded, largeRaster};
struct DecodeOptions {
    std::uint32_t frameIndex=0, directionIndex=0, mipIndex=0;
    std::uint32_t celWidth=0, celFrameHeaderBytes=0;
    std::span<const std::byte> palette;
    std::uint64_t maximumSourceBytes=32ull*1024*1024;
    // Offline map import can explicitly opt PNG into 32M pixels; the default and all other
    // formats remain bounded at 8M pixels.
    std::uint64_t maximumPixels=8ull*1024*1024;
    std::uint32_t maximumDimension=8192;
    // Explicit presentation conversion only. The importer must preserve the
    // encoded original; default callers never silently lose 16-bit precision.
    bool allowPng16BitRenderingDerivative=false;
    // Explicit resident-image opt-in; numeric budgets still apply per call.
    RasterProfile resourceProfile=RasterProfile::bounded;
};
struct Image {
    std::uint32_t width=0, height=0, frameCount=1, directionCount=1, mipCount=1;
    std::int32_t offsetX=0, offsetY=0;
    std::vector<std::uint8_t> rgba8;
    std::uint8_t sourceBitsPerChannel=8;
    // BLP1's independently framed JPEG is a platform codec payload, never a
    // replacement document model. Consumers must handle this disposition explicitly.
    std::vector<std::byte> embeddedJpeg;
};
struct DecodeResult {
    DecodeIssue issue;
    Image image;
    explicit operator bool() const noexcept {return issue.code==DecodeError::none;}
};
// TIFF: one classic top-left chunky image, unsigned8 RGB/straight RGBA/gray,
// uncompressed, PackBits or Deflate strips. Unsupported semantics reject.
// WebP: complete still VP8/VP8L, straight RGBA8; animation, ICC and Exif reject.
[[nodiscard]] DecodeResult DecodeImage(Format format,std::span<const std::byte> bytes,
    const DecodeOptions& options={}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(DecodeError error) noexcept;
// Height/data import, never a luminance or rendering conversion. Samples retain
// all unsigned 16-bit values in file row order; no gamma or range normalization.
enum class ScalarFormat {png16, tiff16};
struct ScalarDecodeResult {
    DecodeIssue issue;
    std::uint32_t width=0,height=0;
    std::vector<std::uint16_t> samples;
    explicit operator bool() const noexcept {return issue.code==DecodeError::none;}
};
[[nodiscard]] ScalarDecodeResult DecodeScalar16(ScalarFormat,std::span<const std::byte>,
    const DecodeOptions& options={}) noexcept;
struct PngEncodeResult {
    std::vector<std::byte> bytes;
    std::string_view error;
    explicit operator bool() const noexcept {return error.empty();}
};
// Memory-only RGBA8 encoding; exact pixel extent, 8192 dimension / 8M pixel cap.
[[nodiscard]] PngEncodeResult EncodePng(std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba) noexcept;
}
