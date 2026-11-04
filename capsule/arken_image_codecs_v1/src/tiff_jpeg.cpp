#include "reader.hpp"
#include "tiff.hpp"
#include <cstdlib>
#include <memory>

// A private, JPEG-only stb_image instantiation for TIFF compression 7. stb's
// symbols are static per translation unit (STB_IMAGE_STATIC), so this mirrors
// blp_jpeg.cpp rather than reaching into raster.cpp's instance.
namespace {
constexpr std::size_t maximum_tiff_jpeg_allocation=64u*1024u*1024u+8192u;
void* tiff_jpeg_allocate(std::size_t size){return size<=maximum_tiff_jpeg_allocation?std::malloc(size):nullptr;}
[[maybe_unused]] void* tiff_jpeg_reallocate(void* pointer,std::size_t size){return size<=maximum_tiff_jpeg_allocation?std::realloc(pointer,size):nullptr;}
}
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(size) tiff_jpeg_allocate(size)
#define STBI_REALLOC(pointer,size) tiff_jpeg_reallocate(pointer,size)
#define STBI_FREE(pointer) std::free(pointer)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) && !defined(__clang__)
// GCC reports a false maybe-uninitialized read of stbi__context fields when it inlines
// stbi__refill_buffer into memory-only loads; the context is fully assigned by stbi__start_mem.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <stb_image.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace arken::image::v1::detail {
namespace {
// Mirrors stb's colour-transform decision for three components: no YCbCr->RGB
// conversion when the frame's component ids are 'R','G','B', or when an Adobe
// APP14 marker says transform 0 and no JFIF marker is present.
int rgb_coded(Bytes jpeg){
    bool jfif=false,adobe_rgb=false,rgb_ids=false;
    for(std::size_t at=2;at+4<=jpeg.size();){
        if(U8(jpeg,at)!=255)return -1;
        const auto marker=U8(jpeg,at+1);
        if(marker==255){++at;continue;}
        if(marker==0xda||marker==0xd9)break;
        const std::size_t length=(std::size_t(U8(jpeg,at+2))<<8)|U8(jpeg,at+3);
        if(length<2||!Range(jpeg,at+2,length))return -1;
        const auto payload=jpeg.subspan(at+4,length-2);
        if(marker==0xe0&&payload.size()>=5&&U8(payload,0)=='J'&&U8(payload,1)=='F'&&U8(payload,2)=='I'&&U8(payload,3)=='F'&&U8(payload,4)==0)jfif=true;
        if(marker==0xee&&payload.size()>=12&&U8(payload,0)=='A'&&U8(payload,1)=='d'&&U8(payload,2)=='o'&&U8(payload,3)=='b'&&U8(payload,4)=='e')
            adobe_rgb=U8(payload,11)==0;
        if((marker==0xc0||marker==0xc1||marker==0xc2)&&payload.size()>=6+9&&U8(payload,5)==3)
            rgb_ids=U8(payload,6)=='R'&&U8(payload,9)=='G'&&U8(payload,12)=='B';
        at+=2+length;
    }
    return rgb_ids||(adobe_rgb&&!jfif)?1:0;
}
}
DecodeError DecodeTiffJpeg(Bytes jpeg,std::uint32_t width,std::uint32_t height,
    unsigned components,bool rgb,std::vector<std::uint8_t>& samples){
    if(components!=1&&components!=3)return DecodeError::unsupported_content;
    if(jpeg.size()<4||jpeg.size()>std::size_t(std::numeric_limits<int>::max()))return DecodeError::invalid_layout;
    if(U8(jpeg,0)!=255||U8(jpeg,1)!=216)return DecodeError::invalid_layout;
    if(components==3){
        const auto coded=rgb_coded(jpeg);
        if(coded<0)return DecodeError::invalid_layout;
        // Photometric must agree with the stream: RGB (2) needs an untransformed
        // RGB stream, YCbCr (6) a stream the decoder converts from YCbCr.
        if((coded==1)!=rgb)return DecodeError::unsupported_content;
    }
    const auto* data=reinterpret_cast<const unsigned char*>(jpeg.data());
    int w=0,h=0,n=0;
    if(!stbi_info_from_memory(data,int(jpeg.size()),&w,&h,&n))return DecodeError::invalid_layout;
    if(w<=0||h<=0||std::uint32_t(w)!=width||std::uint32_t(h)!=height)return DecodeError::invalid_dimensions;
    if(unsigned(n)!=components)return DecodeError::unsupported_content;
    int ow=0,oh=0,on=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data,int(jpeg.size()),&ow,&oh,&on,int(components)),stbi_image_free);
    if(!pixels||ow!=w||oh!=h)return DecodeError::invalid_layout;
    samples.assign(pixels.get(),pixels.get()+std::size_t(w)*h*components);
    return DecodeError::none;
}
}
