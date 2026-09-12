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
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] bool SupportsInput(image::Format) noexcept;
[[nodiscard]] bool SupportsOutput(image::Format) noexcept;
[[nodiscard]] ImageResult ConvertImage(std::span<const std::byte>, const ImageOptions& = {}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
}
