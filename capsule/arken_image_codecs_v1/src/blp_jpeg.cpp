#include "reader.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
constexpr std::size_t maximum_jpeg_allocation=64u*1024u*1024u+8192u;
void* jpeg_allocate(std::size_t size){return size<=maximum_jpeg_allocation?std::malloc(size):nullptr;}
[[maybe_unused]] void* jpeg_reallocate(void* pointer,std::size_t size){return size<=maximum_jpeg_allocation?std::realloc(pointer,size):nullptr;}
}

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(size) jpeg_allocate(size)
#define STBI_REALLOC(pointer,size) jpeg_reallocate(pointer,size)
#define STBI_FREE(pointer) std::free(pointer)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace arken::image::v1::detail {
DecodeResult DecodeBlpJpegWithAlpha(
    Bytes jpeg,const DecodeOptions& options,std::uint32_t expected_width,
    std::uint32_t expected_height,std::uint32_t alpha_depth) {
    if(alpha_depth!=0&&alpha_depth!=8)return Failure(DecodeError::unsupported_alpha_depth,8);
    if(jpeg.empty()||jpeg.size()>options.maximumSourceBytes
        ||jpeg.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Failure(DecodeError::input_too_large);
    int width=0,height=0,components=0;
    const auto* data=reinterpret_cast<const unsigned char*>(jpeg.data());
    if(!stbi_info_from_memory(data,static_cast<int>(jpeg.size()),&width,&height,&components))
        return Failure(DecodeError::invalid_layout);
    if(width<=0||height<=0||static_cast<std::uint32_t>(width)!=expected_width
        ||static_cast<std::uint32_t>(height)!=expected_height
        ||!Dimensions(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),options))
        return Failure(DecodeError::invalid_dimensions);
    int output_width=0,output_height=0,output_components=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> rgb(
        stbi_load_from_memory(data,static_cast<int>(jpeg.size()),&output_width,&output_height,
            &output_components,4),stbi_image_free);
    if(!rgb||output_width!=width||output_height!=height)
        return Failure(DecodeError::invalid_layout);

    stbi__context context{};
    stbi__start_mem(&context,data,static_cast<int>(jpeg.size()));
    stbi__jpeg decoder{};
    decoder.s=&context;
    stbi__setup_jpeg(&decoder);
    if(!stbi__decode_jpeg_image(&decoder)){
        stbi__cleanup_jpeg(&decoder);
        return Failure(DecodeError::invalid_layout);
    }
    if(context.img_n!=4||decoder.app14_color_transform==0||decoder.app14_color_transform==2){
        stbi__cleanup_jpeg(&decoder);
        return Failure(DecodeError::unsupported_content);
    }
    const auto& alpha=decoder.img_comp[3];
    // BLP1's JPEG-alpha profile stores a full-resolution fourth component.
    // Reject subsampled alpha rather than silently resampling a game-specific
    // channel with a color-oriented JPEG policy.
    if(alpha.h!=decoder.img_h_max||alpha.v!=decoder.img_v_max
        ||alpha.x<width||alpha.y<height||alpha.w2<width){
        stbi__cleanup_jpeg(&decoder);
        return Failure(DecodeError::unsupported_content);
    }
    DecodeResult result;result.image.width=expected_width;result.image.height=expected_height;
    result.image.rgba8.resize(std::size_t(width)*height*4);
    for(int y=0;y<height;++y){
        const auto* alpha_row=alpha.data+std::size_t(y)*alpha.w2;
        for(int x=0;x<width;++x){
            const auto source=std::size_t(y)*width*4+std::size_t(x)*4;
            const auto target=source;
            result.image.rgba8[target]=rgb.get()[source];
            result.image.rgba8[target+1]=rgb.get()[source+1];
            result.image.rgba8[target+2]=rgb.get()[source+2];
            result.image.rgba8[target+3]=alpha_depth==0?255:alpha_row[x];
        }
    }
    stbi__cleanup_jpeg(&decoder);
    return result;
}
}
