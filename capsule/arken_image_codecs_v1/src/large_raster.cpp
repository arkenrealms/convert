#include "large_raster.hpp"
#include <cstdlib>
#include <limits>
#include <memory>

namespace {
// PNG IDAT accumulation and inflater capacity growth can exceed the decoded
// surface extent. Keep the ordinary decoder's smaller guard unchanged.
constexpr std::size_t maximumAllocation=arken::image::v1::detail::maximumLargeRasterAllocation;
void* Allocate(std::size_t size){return size<=maximumAllocation?std::malloc(size):nullptr;}
void* Reallocate(void* pointer,std::size_t size){return size<=maximumAllocation?std::realloc(pointer,size):nullptr;}
}
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(size) Allocate(size)
#define STBI_REALLOC(pointer,size) Reallocate(pointer,size)
#define STBI_FREE(pointer) std::free(pointer)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace arken::image::v1::detail {
DecodeResult DecodeLargeRasterPixels(std::span<const std::byte> bytes,std::uint32_t expectedWidth,std::uint32_t expectedHeight){
    DecodeResult result;
    if(bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())
        ||!expectedWidth||!expectedHeight||expectedWidth>8192||expectedHeight>8192
        ||std::uint64_t(expectedWidth)*expectedHeight>32ull*1024*1024){
        result.issue.code=DecodeError::resource_limit;return result;
    }
    int width=0,height=0,components=0;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(bytes.data()),static_cast<int>(bytes.size()),&width,&height,&components,4),stbi_image_free);
    if(!pixels){result.issue.code=DecodeError::invalid_layout;return result;}
    if(width!=static_cast<int>(expectedWidth)||height!=static_cast<int>(expectedHeight)){
        result.issue.code=DecodeError::invalid_dimensions;return result;
    }
    result.image.width=expectedWidth;result.image.height=expectedHeight;
    result.image.rgba8.assign(pixels.get(),pixels.get()+std::size_t(expectedWidth)*expectedHeight*4);
    return result;
}
}
