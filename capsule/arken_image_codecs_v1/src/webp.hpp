#pragma once
#include "arken_image_codecs_v1/encode.hpp"

namespace arken::image::v1::detail {
DecodeResult DecodeWebp(std::span<const std::byte>,const DecodeOptions&);
EncodeResult EncodeWebp(std::uint32_t,std::uint32_t,std::span<const std::uint8_t>,const EncodeOptions&);
}
