#include "arken_file_conversion_v1/api.hpp"
#include "image_preview.hpp"
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
namespace {
// Each surface names its own admitted formats. Sharing the codec does not share admission.
using Admission=bool(*)(image::Format) noexcept;
bool HostedInput(image::Format format) noexcept { return SupportsInput(format); }
bool HostedOutput(image::Format format) noexcept { return SupportsOutput(format); }
// The resident browser profile is qualified for the symmetric PNG/JPEG/BMP/TGA set only.
bool BrowserInput(image::Format format) noexcept { return SupportsInput(format)&&SupportsOutput(format); }
bool StandaloneInput(image::Format format) noexcept { return SupportsStandaloneInput(format); }
bool StandaloneOutput(image::Format format) noexcept { return SupportsStandaloneOutput(format); }
struct Profile {
    std::uint32_t sourceBytes,outputBytes,pixels,dimension;
    image::RasterProfile codec;
    Admission input,output;
};
constexpr Profile hosted{maximumSourceBytes,maximumOutputBytes,maximumPixels,maximumDimension,image::RasterProfile::bounded,HostedInput,HostedOutput};
constexpr Profile standalone{maximumSourceBytes,maximumOutputBytes,maximumPixels,maximumDimension,image::RasterProfile::bounded,StandaloneInput,StandaloneOutput};
constexpr Profile browser{browserMaximumSourceBytes,browserMaximumOutputBytes,browserMaximumPixels,browserMaximumDimension,image::RasterProfile::largeRaster,BrowserInput,HostedOutput};
ImageResult Convert(std::span<const std::byte> source,const ImageOptions& options,const Profile& profile) noexcept {
    if (source.empty() || source.size() > profile.sourceBytes) return Failure(Error::input_limit);
    if (!profile.input(options.input)) return Failure(Error::input_format_unavailable);
    if (!profile.output(options.output)) return Failure(Error::output_format_unavailable);
    if (options.jpegQuality < 1 || options.jpegQuality > 100
        || (options.output != image::Format::jpeg && options.jpegQuality != 95)
        || (options.alphaPolicy != image::AlphaPolicy::preserve
            && options.alphaPolicy != image::AlphaPolicy::flatten_white)
        || (options.output != image::Format::jpeg && options.alphaPolicy != image::AlphaPolicy::preserve))
        return Failure(Error::invalid_options);
    try {
        image::DecodeOptions decode;
        decode.maximumSourceBytes = profile.sourceBytes;
        decode.maximumPixels = profile.pixels;
        decode.maximumDimension = profile.dimension;
        decode.resourceProfile = profile.codec;
        auto decoded = image::DecodeImage(options.input, source, decode);
        if (!decoded) {
            auto result = Failure(Error::decode_failed);
            result.decodeError = decoded.issue.code;
            return result;
        }
        image::EncodeOptions encode;
        encode.maximumOutputBytes = profile.outputBytes;
        encode.maximumPixels = profile.pixels;
        encode.maximumDimension = profile.dimension;
        encode.resourceProfile = profile.codec;
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
        if(profile.codec==image::RasterProfile::largeRaster&&options.output==image::Format::tga)
            detail::SetPreview(result,std::move(decoded.image));
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
}
ImageResult ConvertImage(std::span<const std::byte> source,const ImageOptions& options) noexcept {
    return Convert(source,options,hosted);
}
ImageResult ConvertStandaloneImage(std::span<const std::byte> source,const ImageOptions& options) noexcept {
    return Convert(source,options,standalone);
}
ImageResult ConvertBrowserImage(std::span<const std::byte> source,const ImageOptions& options) noexcept {
    return Convert(source,options,browser);
}
ImageResult InspectBrowserImage(std::span<const std::byte> source,image::Format format) noexcept {
    if(source.empty()||source.size()>browser.sourceBytes)return Failure(Error::input_limit);
    if(!SupportsOutput(format))return Failure(Error::input_format_unavailable);
    try {
        image::DecodeOptions options;
        options.maximumSourceBytes=browser.sourceBytes;options.maximumPixels=browser.pixels;
        options.maximumDimension=browser.dimension;options.resourceProfile=browser.codec;
        auto decoded=image::DecodeImage(format,source,options);
        if(!decoded){auto result=Failure(Error::decode_failed);result.decodeError=decoded.issue.code;return result;}
        ImageResult result;result.width=decoded.image.width;result.height=decoded.image.height;
        if(format==image::Format::tga)detail::SetPreview(result,std::move(decoded.image));
        return result;
    }catch(const std::bad_alloc&){return Failure(Error::allocation_failed);}
    catch(...){return Failure(Error::internal_failure);}
}
std::string_view BrowserImageErrorMessage(Error error) noexcept {
    if(error==Error::input_limit)return "Choose a nonempty image no larger than 128 MiB";
    return ErrorMessage(error);
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
