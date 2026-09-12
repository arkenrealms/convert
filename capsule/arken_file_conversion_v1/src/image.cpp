#include "arken_file_conversion_v1/api.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
ImageResult Failure(Error error) { ImageResult result; result.error = error; return result; }
}
bool SupportsInput(image::Format format) noexcept {
    return format == image::Format::png || format == image::Format::bmp
        || format == image::Format::jpeg || format == image::Format::dds
        || format == image::Format::tga;
}
bool SupportsOutput(image::Format format) noexcept {
    return format == image::Format::png || format == image::Format::bmp
        || format == image::Format::jpeg || format == image::Format::tga;
}
ImageResult ConvertImage(std::span<const std::byte> source, const ImageOptions& options) noexcept {
    if (source.empty() || source.size() > maximumSourceBytes) return Failure(Error::input_limit);
    if (!SupportsInput(options.input)) return Failure(Error::input_format_unavailable);
    if (!SupportsOutput(options.output)) return Failure(Error::output_format_unavailable);
    if (options.jpegQuality < 1 || options.jpegQuality > 100
        || (options.output != image::Format::jpeg && options.jpegQuality != 95)
        || (options.alphaPolicy != image::AlphaPolicy::preserve
            && options.alphaPolicy != image::AlphaPolicy::flatten_white)
        || (options.output != image::Format::jpeg && options.alphaPolicy != image::AlphaPolicy::preserve))
        return Failure(Error::invalid_options);
    try {
        image::DecodeOptions decode;
        decode.maximumSourceBytes = maximumSourceBytes;
        decode.maximumPixels = maximumPixels;
        decode.maximumDimension = maximumDimension;
        auto decoded = image::DecodeImage(options.input, source, decode);
        if (!decoded) {
            auto result = Failure(Error::decode_failed);
            result.decodeError = decoded.issue.code;
            return result;
        }
        image::EncodeOptions encode;
        encode.maximumOutputBytes = maximumOutputBytes;
        encode.maximumPixels = maximumPixels;
        encode.maximumDimension = maximumDimension;
        encode.jpegQuality = options.jpegQuality;
        encode.alphaPolicy = options.alphaPolicy;
        auto encoded = image::EncodeImage(options.output, decoded.image.width,
            decoded.image.height, decoded.image.rgba8, encode);
        if (!encoded) {
            auto result = Failure(Error::encode_failed);
            result.encodeError = encoded.error;
            return result;
        }
        ImageResult result;
        result.width = decoded.image.width; result.height = decoded.image.height;
        result.alphaFlattened = encoded.alphaFlattened;
        result.bytes = std::move(encoded.bytes);
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view ErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Image converted";
    case Error::input_limit: return "Input must contain between 1 byte and 8 MiB";
    case Error::input_format_unavailable: return "Input format is not available in this conversion profile";
    case Error::output_format_unavailable: return "Output format is not available in this conversion profile";
    case Error::invalid_options: return "Conversion options are invalid for the selected output";
    case Error::decode_failed: return "Image encoding is invalid, unsupported or exceeds the profile limits";
    case Error::encode_failed: return "Image output could not be produced with the selected loss policy";
    case Error::allocation_failed: return "Conversion memory limit reached";
    case Error::input_not_prepared: return "Conversion input was not prepared";
    default: return "Conversion failed";
    }
}
}
