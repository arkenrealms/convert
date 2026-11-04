#pragma once
#include "arken_image_codecs_v1/encode.hpp"
#include <vector>
namespace arken::image::v1::detail {
DecodeResult DecodeTiff(std::span<const std::byte>,const DecodeOptions&);
EncodeResult EncodeTiff(std::uint32_t,std::uint32_t,std::span<const std::uint8_t>,std::uint64_t,bool deflate=false);
// Private JPEG-only stb_image instantiation (tiff_jpeg.cpp). Decodes one complete
// JPEG interchange stream of exactly width x height with `components` source
// components (1 gray, 3 YCbCr/RGB) into tightly packed 8-bit samples. With three
// components, `rgb` states whether the stream must be RGB-coded (Photometric 2)
// rather than YCbCr-coded (Photometric 6).
DecodeError DecodeTiffJpeg(std::span<const std::byte> jpeg,std::uint32_t width,std::uint32_t height,
    unsigned components,bool rgb,std::vector<std::uint8_t>& samples);
// Private stb_image_write zlib instantiation (tiff_deflate.cpp). Deterministic.
bool CompressTiffZlib(std::span<const std::uint8_t> data,std::vector<std::byte>& output);
}
