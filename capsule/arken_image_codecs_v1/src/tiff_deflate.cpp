#include "tiff.hpp"
#include <cstdlib>
#include <limits>
#include <memory>

// Private stb_image_write instantiation used only for its deterministic zlib
// compressor (TIFF compression 8). The PNG/BMP/TGA/JPEG instance in
// raster_encode.cpp is static to that translation unit.
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace arken::image::v1::detail {
bool CompressTiffZlib(std::span<const std::uint8_t> data,std::vector<std::byte>& output){
    if(data.size()>std::size_t(std::numeric_limits<int>::max()/2))return false;
    int size=0;
    std::unique_ptr<unsigned char,decltype(&std::free)> compressed(
        stbi_zlib_compress(const_cast<unsigned char*>(data.data()),int(data.size()),&size,8),std::free);
    if(!compressed||size<6)return false;
    const auto* first=reinterpret_cast<const std::byte*>(compressed.get());
    output.assign(first,first+size);
    return true;
}
}
