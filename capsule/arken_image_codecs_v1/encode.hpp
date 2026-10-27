#pragma once
#include "api.hpp"
namespace arken::image::v1 {
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
};
struct EncodeResult {
    EncodeError error=EncodeError::none;
    bool alphaFlattened=false;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept {return error==EncodeError::none;}
};
// Top-left, tightly packed, straight-alpha RGBA8; PNG/BMP/TGA preserve all channels.
// JPEG transparency requires explicit white-matte consent. No I/O or metadata copy.
[[nodiscard]] EncodeResult EncodeImage(Format,std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba8,const EncodeOptions& options={}) noexcept;
[[nodiscard]] std::string_view EncodeErrorMessage(EncodeError) noexcept;
}
