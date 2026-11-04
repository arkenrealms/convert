#pragma once
#include "api.hpp"
namespace arken::image::v1 {
inline constexpr std::uint32_t maximumIconDimension=256;
enum class EncodeError : std::uint32_t {none,unsupported_format,invalid_dimensions,invalid_extent,invalid_options,alpha_requires_matte,output_limit,allocation_failed,encoding_failed};
enum class AlphaPolicy : std::uint32_t {preserve,flatten_white};
struct EncodeOptions {
    std::uint64_t maximumOutputBytes=40ull*1024*1024;
    // PNG callers may explicitly admit up to 16M pixels (a 4096-square atlas).
    // Existing defaults and other encodings retain the 8M pixel budget.
    std::uint64_t maximumPixels=8ull*1024*1024;
    std::uint32_t maximumDimension=8192,jpegQuality=95;
    AlphaPolicy alphaPolicy=AlphaPolicy::preserve;
    RasterProfile resourceProfile=RasterProfile::bounded;
    // WebP: lossless (default) keeps exact RGBA. When false, lossy VP8 at
    // webpQuality (1-100, like jpegQuality; validated for every WebP encode)
    // with a losslessly coded alpha plane; transparent RGB is not preserved.
    bool webpLossless=true;
    std::uint32_t webpQuality=90;
};
struct EncodeResult {
    EncodeError error=EncodeError::none;
    bool alphaFlattened=false;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept {return error==EncodeError::none;}
};
// Top-left, tightly packed, straight-alpha RGBA8; PNG/BMP/TGA/ICO/TIFF preserve all channels.
// ICO writes one PNG-backed rendition, at most256x256, without resizing.
// TIFF writes one classic little-endian uncompressed chunky RGBA8 image.
// WebP writes one still image: lossless by default, preserving exact RGBA including
// transparent RGB; or lossy (webpLossless=false) at webpQuality, method 4, exact=0,
// alpha kept exactly via a lossless alpha plane. Single-threaded and deterministic.
// JPEG transparency requires explicit white-matte consent. No I/O or metadata copy.
[[nodiscard]] EncodeResult EncodeImage(Format,std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba8,const EncodeOptions& options={}) noexcept;
[[nodiscard]] std::string_view EncodeErrorMessage(EncodeError) noexcept;
}
