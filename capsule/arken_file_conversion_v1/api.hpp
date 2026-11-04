#pragma once
#include "arken_image_codecs_v1/encode.hpp"

namespace arken::file_conversion::v1 {
namespace image = arken::image::v1;
// One small-image profile for the first hosted capability. These limits do
// not replace the larger limits used by local Canvas and Paint consumers.
inline constexpr std::uint32_t maximumSourceBytes = 8u * 1024u * 1024u;
inline constexpr std::uint32_t maximumOutputBytes = 8u * 1024u * 1024u;
inline constexpr std::uint32_t maximumPixels = 1024u * 1024u;
inline constexpr std::uint32_t maximumDimension = 4096u;
enum class Error : std::uint32_t {
    none, input_limit, input_format_unavailable, output_format_unavailable,
    invalid_options, decode_failed, encode_failed, allocation_failed,
    internal_failure, input_not_prepared
};
struct ImageOptions {
    image::Format input = image::Format::png;
    image::Format output = image::Format::png;
    std::uint32_t jpegQuality = 95;
    image::AlphaPolicy alphaPolicy = image::AlphaPolicy::preserve;
};
struct ImageResult {
    Error error = Error::none;
    image::DecodeError decodeError = image::DecodeError::none;
    image::EncodeError encodeError = image::EncodeError::none;
    std::uint32_t width = 0, height = 0;
    bool alphaFlattened = false;
    std::vector<std::byte> bytes;
    // Bounded presentation proxy; never used as the encoded output authority.
    std::uint32_t previewWidth=0,previewHeight=0;
    std::vector<std::uint8_t> previewRgba8;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] bool SupportsInput(image::Format) noexcept;
[[nodiscard]] constexpr bool SupportsOutput(image::Format format) noexcept {
    return format==image::Format::png||format==image::Format::bmp
        ||format==image::Format::jpeg||format==image::Format::tga;
}
[[nodiscard]] ImageResult ConvertImage(std::span<const std::byte>, const ImageOptions& = {}) noexcept;
// Separate standalone native MCP admission at the same bounded limits. Hosted ConvertImage
// keeps its unchanged input and output sets; the client-launched standalone server also
// admits the still-image codecs already qualified by arken_image_codecs_v1. GIF converts
// its first frame only; ICO converts its largest rendition and writes one PNG-backed
// rendition of at most 256x256; TIFF and WebP are still images without animation, ICC or Exif.
[[nodiscard]] constexpr bool SupportsStandaloneInput(image::Format format) noexcept {
    return format==image::Format::png||format==image::Format::bmp||format==image::Format::jpeg
        ||format==image::Format::dds||format==image::Format::tga||format==image::Format::gif
        ||format==image::Format::ico||format==image::Format::tiff||format==image::Format::webp;
}
[[nodiscard]] constexpr bool SupportsStandaloneOutput(image::Format format) noexcept {
    return SupportsOutput(format)||format==image::Format::ico||format==image::Format::tiff
        ||format==image::Format::webp;
}
[[nodiscard]] ImageResult ConvertStandaloneImage(std::span<const std::byte>, const ImageOptions& = {}) noexcept;
// Separate resident browser profile. Hosted callers keep ConvertImage and its
// unchanged limits; browser admission must not use this before runtime qualification.
inline constexpr std::uint32_t browserMaximumSourceBytes=128u*1024u*1024u;
inline constexpr std::uint32_t browserMaximumOutputBytes=160u*1024u*1024u;
inline constexpr std::uint32_t browserMaximumPixels=32u*1024u*1024u;
inline constexpr std::uint32_t browserMaximumDimension=8192u;
[[nodiscard]] ImageResult ConvertBrowserImage(std::span<const std::byte>,const ImageOptions& = {}) noexcept;
[[nodiscard]] ImageResult InspectBrowserImage(std::span<const std::byte>,image::Format) noexcept;
[[nodiscard]] std::string_view BrowserImageErrorMessage(Error) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
}
