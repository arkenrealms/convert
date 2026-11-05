#include "reader.hpp"
#include "resource_profile.hpp"
#include "tiff.hpp"
#include "webp.hpp"
#if defined(ARKEN_IMAGE_HEIF_DECODER)
#include "heif.hpp"
#include "avif.hpp"
#endif
#include <new>

namespace arken::image::v1 {
DecodeResult DecodeImage(Format format,std::span<const std::byte> bytes,const DecodeOptions& options) noexcept {
    if(bytes.empty())return detail::Failure(DecodeError::truncated);
    if(bytes.size()>options.maximumSourceBytes)return detail::Failure(DecodeError::input_too_large);
    if(!detail::ValidProfile(options.resourceProfile,format)
        ||!options.maximumPixels||options.maximumPixels>detail::DecodePixelCeiling(options.resourceProfile,format)
        ||!options.maximumDimension||options.maximumDimension>8192)
        return detail::Failure(DecodeError::resource_limit);
    try{
        switch(format){
            case Format::pcx:return detail::DecodePcx(bytes,options);
            case Format::cel:return detail::DecodeCel(bytes,options);
            case Format::dc6:return detail::DecodeDc6(bytes,options);
            case Format::blp:return detail::DecodeBlp(bytes,options);
            case Format::dds:return detail::DecodeDds(bytes,options);
            case Format::tga:return detail::DecodeTga(bytes,options);
            case Format::ico:return detail::DecodeIco(bytes,options);
            case Format::tiff:return detail::DecodeTiff(bytes,options);
            case Format::webp:return detail::DecodeWebp(bytes,options);
#if defined(ARKEN_IMAGE_HEIF_DECODER)
            case Format::heif:return detail::DecodeHeif(bytes,options);
            case Format::avif:return detail::DecodeAvif(bytes,options);
#else
            case Format::heif:case Format::avif:return detail::Failure(DecodeError::unsupported_content);
#endif
            case Format::png:case Format::bmp:case Format::jpeg:case Format::gif:return detail::DecodeRaster(format,bytes,options);
        }
        return detail::Failure(DecodeError::unsupported_content);
    }catch(const std::bad_alloc&){return detail::Failure(DecodeError::allocation_failed);}
    catch(...){return detail::Failure(DecodeError::invalid_layout);}
}
std::string_view ErrorMessage(DecodeError e) noexcept {
    switch(e){
        case DecodeError::none:return "Decoded locally.";
        case DecodeError::palette_required:return "This indexed image needs its external 768-byte palette (.pal for CEL; BGR .dat for DC6).";
        case DecodeError::invalid_palette:return "The image palette is invalid or incomplete.";
        case DecodeError::width_required:return "Diablo CEL does not store image width. Enter its frame width to import it.";
        case DecodeError::selection_out_of_range:return "The selected frame, direction or mip level does not exist.";
        case DecodeError::invalid_dimensions:return "Image dimensions are invalid or exceed the local pixel limit.";
        case DecodeError::input_too_large:case DecodeError::resource_limit:return "Image exceeds the bounded local decoder limit.";
        case DecodeError::truncated:return "The image data is truncated.";
        case DecodeError::invalid_run:return "The image contains an invalid pixel run.";
        case DecodeError::wrong_magic:return "The image header does not match its format.";
        case DecodeError::unsupported_content:case DecodeError::unsupported_compression:case DecodeError::unsupported_alpha_depth:return "This image encoding variant is not supported.";
        case DecodeError::allocation_failed:return "There is not enough memory to decode this image.";
        case DecodeError::embedded_jpeg:return "The embedded JPEG requires the browser image codec.";
        default:return "The image contains an invalid layout or size.";
    }
}
}
