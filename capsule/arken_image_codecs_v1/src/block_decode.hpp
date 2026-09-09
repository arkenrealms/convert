#pragma once
#include "arken_image_codecs_v1/blp.hpp"

namespace arken::image::v1::detail {
enum class BlockRounding { truncate, nearest };
// alpha_type follows the existing BLP contract: 0=BC1, 1=BC2, 7=BC3.
DecodeError DecodeDxt(std::span<std::byte const> source, std::uint32_t width,
    std::uint32_t height, std::uint8_t alpha_type, std::vector<std::uint8_t>& output,
    BlockRounding rounding = BlockRounding::truncate);
}
