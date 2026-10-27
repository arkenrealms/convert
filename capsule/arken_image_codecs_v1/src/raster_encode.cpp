#include "arken_image_codecs_v1/encode.hpp"
#include "resource_profile.hpp"
#include <new>
// JPEG's floating-point DCT must round the same way on native CPUs and
// WebAssembly. Fusing multiply-adds changes coefficients near quantization ties.
#pragma STDC FP_CONTRACT OFF
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace arken::image::v1 {
namespace {
EncodeResult failure(EncodeError error){return {error,false,{}};}
struct Output {std::vector<std::byte> bytes;std::uint64_t limit;EncodeError error=EncodeError::none;};
void append(void* context,void* data,int count) noexcept {
    auto& output=*static_cast<Output*>(context);
    if(output.error!=EncodeError::none)return;
    if(count<0||std::uint64_t(count)>output.limit-output.bytes.size()){output.error=EncodeError::output_limit;return;}
    if(!count)return;
    try{const auto* first=static_cast<const std::byte*>(data);output.bytes.insert(output.bytes.end(),first,first+count);}
    catch(const std::bad_alloc&){output.error=EncodeError::allocation_failed;}
    catch(...){output.error=EncodeError::encoding_failed;}
}
}
EncodeResult EncodeImage(Format format,std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba,const EncodeOptions& options) noexcept {
    if(format!=Format::png&&format!=Format::bmp&&format!=Format::jpeg&&format!=Format::tga)return failure(EncodeError::unsupported_format);
    if(!detail::ValidProfile(options.resourceProfile,format)
        ||!options.maximumOutputBytes||options.maximumOutputBytes>detail::EncodeByteCeiling(options.resourceProfile)
        ||!options.maximumPixels||options.maximumPixels>detail::EncodePixelCeiling(options.resourceProfile,format)
        ||!options.maximumDimension||options.maximumDimension>8192
        ||options.jpegQuality<1||options.jpegQuality>100
        ||(options.alphaPolicy!=AlphaPolicy::preserve&&options.alphaPolicy!=AlphaPolicy::flatten_white)
        ||(format!=Format::jpeg&&options.alphaPolicy!=AlphaPolicy::preserve))return failure(EncodeError::invalid_options);
    const auto pixels=std::uint64_t(width)*height;
    if(!width||!height||width>options.maximumDimension||height>options.maximumDimension||pixels>options.maximumPixels)return failure(EncodeError::invalid_dimensions);
    if(rgba.size()!=pixels*4)return failure(EncodeError::invalid_extent);
    bool alpha=false;for(std::size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255){alpha=true;break;}
    if(format==Format::jpeg&&alpha&&options.alphaPolicy!=AlphaPolicy::flatten_white)return failure(EncodeError::alpha_requires_matte);
    try{
        Output output{{},options.maximumOutputBytes};const auto w=int(width),h=int(height);int accepted=0;
        if(format==Format::png)accepted=stbi_write_png_to_func(append,&output,w,h,4,rgba.data(),w*4);
        else if(format==Format::bmp)accepted=stbi_write_bmp_to_func(append,&output,w,h,4,rgba.data());
        else if(format==Format::tga)accepted=stbi_write_tga_to_func(append,&output,w,h,4,rgba.data());
        else {
            std::vector<std::uint8_t> rgb(static_cast<std::size_t>(pixels)*3);
            for(std::size_t i=0;i<pixels;++i)for(unsigned channel=0;channel<3;++channel){
                const auto a=rgba[i*4+3];rgb[i*3+channel]=std::uint8_t((unsigned(rgba[i*4+channel])*a+255u*(255u-a)+127u)/255u);
            }
            accepted=stbi_write_jpg_to_func(append,&output,w,h,3,rgb.data(),int(options.jpegQuality));
        }
        if(output.error!=EncodeError::none)return failure(output.error);
        if(!accepted||output.bytes.empty())return failure(EncodeError::encoding_failed);
        return {EncodeError::none,format==Format::jpeg&&alpha,std::move(output.bytes)};
    }catch(const std::bad_alloc&){return failure(EncodeError::allocation_failed);}
    catch(...){return failure(EncodeError::encoding_failed);}
}
std::string_view EncodeErrorMessage(EncodeError error) noexcept {
    switch(error){
    case EncodeError::none:return "Image encoded";
    case EncodeError::unsupported_format:return "This output format is not supported";
    case EncodeError::invalid_dimensions:return "Image dimensions exceed the supported limits";
    case EncodeError::invalid_extent:return "RGBA bytes do not match the image dimensions";
    case EncodeError::invalid_options:return "Image export options are invalid";
    case EncodeError::alpha_requires_matte:return "JPEG transparency requires an explicit white matte";
    case EncodeError::output_limit:return "Encoded image exceeds the output byte limit";
    case EncodeError::allocation_failed:return "Image encoding could not allocate memory";
    default:return "Image encoding failed";
    }
}
PngEncodeResult EncodePng(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba) noexcept {
    auto result=EncodeImage(Format::png,width,height,rgba);
    return {std::move(result.bytes),result?std::string_view{}:EncodeErrorMessage(result.error)};
}
}
