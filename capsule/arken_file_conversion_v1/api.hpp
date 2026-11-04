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
    // WebP output only (invalid_options for any other output when changed). Lossless keeps
    // exact RGBA; webpLossless=false writes lossy VP8 at webpQuality (1-100) with exact
    // alpha. webpQuality other than 90 requires webpLossless=false.
    bool webpLossless = true;
    std::uint32_t webpQuality = 90;
    // TIFF output only: Deflate with horizontal predictor instead of uncompressed strips.
    bool tiffDeflate = false;
};
struct ImageResult {
    Error error = Error::none;
    image::DecodeError decodeError = image::DecodeError::none;
    image::EncodeError encodeError = image::EncodeError::none;
    std::uint32_t width = 0, height = 0;
    bool alphaFlattened = false;
    // Frames, pages or renditions in the source (Image::frameCount). Every profile converts
    // exactly one: the first GIF/WebP frame, the first TIFF page or the largest ICO/CUR
    // rendition. A value above 1 means the output does not represent the whole source.
    std::uint32_t sourceFrames = 0;
    // 16 when a 16-bit PNG or TIFF was rounded to the 8-bit output (standalone profile only).
    std::uint32_t sourceBitsPerChannel = 0;
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
// admits GIF, ICO/CUR, TIFF and WebP input and ICO, TIFF and WebP output. Each conversion
// takes one image: the first GIF or WebP animation frame, the first TIFF page, the largest
// ICO/CUR rendition; ImageResult::sourceFrames reports how many the source held. Embedded
// ICC profiles are applied (JPEG, PNG, TIFF, WebP); Exif/XMP are not copied. Unlike the
// hosted and browser profiles, 16-bit PNG and TIFF are rounded to 8 bits per channel and
// reported through sourceBitsPerChannel. HEIF/AVIF are not admitted by any profile here.
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
