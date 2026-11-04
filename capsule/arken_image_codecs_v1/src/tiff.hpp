#pragma once
#include "arken_image_codecs_v1/encode.hpp"
namespace arken::image::v1::detail {
DecodeResult DecodeTiff(std::span<const std::byte>,const DecodeOptions&);
EncodeResult EncodeTiff(std::uint32_t,std::uint32_t,std::span<const std::uint8_t>,std::uint64_t);
}
