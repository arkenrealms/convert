#include "block_decode.hpp"
#include "reader.hpp"
#include <array>

namespace arken::image::v1::detail {
namespace {
// Shared BLP/DDS block decoder; preserve BLP truncation and Oasis DDS nearest rounding.
struct Rgba final
{
    std::uint8_t red = 0u;
    std::uint8_t green = 0u;
    std::uint8_t blue = 0u;
    std::uint8_t alpha = 255u;
};

[[nodiscard]] Rgba rgb565(const std::uint16_t value) noexcept
{
    const std::uint32_t red = (value >> 11u) & 31u;
    const std::uint32_t green = (value >> 5u) & 63u;
    const std::uint32_t blue = value & 31u;
    return {
        static_cast<std::uint8_t>((red * 255u + 15u) / 31u),
        static_cast<std::uint8_t>((green * 255u + 31u) / 63u),
        static_cast<std::uint8_t>((blue * 255u + 15u) / 31u),
        255u,
    };
}

[[nodiscard]] Rgba interpolate(
    const Rgba& left,
    const Rgba& right,
    const std::uint32_t left_weight,
    const std::uint32_t right_weight,
    const std::uint32_t divisor,
    BlockRounding rounding) noexcept
{
    return {
        static_cast<std::uint8_t>((left.red * left_weight
            + right.red * right_weight + (rounding == BlockRounding::nearest ? divisor / 2 : 0)) / divisor),
        static_cast<std::uint8_t>((left.green * left_weight
            + right.green * right_weight + (rounding == BlockRounding::nearest ? divisor / 2 : 0)) / divisor),
        static_cast<std::uint8_t>((left.blue * left_weight
            + right.blue * right_weight + (rounding == BlockRounding::nearest ? divisor / 2 : 0)) / divisor),
        255u,
    };
}

void write_pixel(
    std::vector<std::uint8_t>& output,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t x,
    const std::uint32_t y,
    const Rgba& value) noexcept
{
    if(x >= width || y >= height) return;
    const std::size_t offset =
        (static_cast<std::size_t>(y) * width + x) * 4u;
    output[offset] = value.red;
    output[offset + 1u] = value.green;
    output[offset + 2u] = value.blue;
    output[offset + 3u] = value.alpha;
}

void decode_color_block(
    const std::span<const std::byte> block,
    const bool force_four_color,
    std::array<Rgba, 16u>& output, BlockRounding rounding) noexcept
{
    const std::uint16_t color_zero = U16(block, 0u);
    const std::uint16_t color_one = U16(block, 2u);
    std::array<Rgba, 4u> colors{};
    colors[0] = rgb565(color_zero);
    colors[1] = rgb565(color_one);
    if(force_four_color || color_zero > color_one)
    {
        colors[2] = interpolate(colors[0], colors[1], 2u, 1u, 3u, rounding);
        colors[3] = interpolate(colors[0], colors[1], 1u, 2u, 3u, rounding);
    }
    else
    {
        colors[2] = interpolate(colors[0], colors[1], 1u, 1u, 2u, rounding);
        colors[3] = {0u, 0u, 0u, 0u};
    }
    const std::uint32_t selectors = U32(block, 4u);
    for(std::size_t index = 0u; index < output.size(); ++index)
        output[index] = colors[(selectors >> (index * 2u)) & 3u];
}

[[nodiscard]] std::array<std::uint8_t, 8u> dxt5_alpha_table(
    const std::uint8_t first,
    const std::uint8_t second, BlockRounding rounding) noexcept
{
    std::array<std::uint8_t, 8u> table{};
    table[0] = first;
    table[1] = second;
    if(first > second)
    {
        for(std::uint32_t index = 1u; index <= 6u; ++index)
            table[index + 1u] = static_cast<std::uint8_t>(
                ((7u - index) * first + index * second + (rounding == BlockRounding::nearest ? 3u : 0u)) / 7u);
    }
    else
    {
        for(std::uint32_t index = 1u; index <= 4u; ++index)
            table[index + 1u] = static_cast<std::uint8_t>(
                ((5u - index) * first + index * second + (rounding == BlockRounding::nearest ? 2u : 0u)) / 5u);
        table[6] = 0u;
        table[7] = 255u;
    }
    return table;
}

}

DecodeError DecodeDxt(
    const std::span<const std::byte> source,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint8_t alpha_type,
    std::vector<std::uint8_t>& output, BlockRounding rounding)
{
    const bool dxt3 = alpha_type == 1u;
    const bool dxt5 = alpha_type == 7u;
    const std::uint32_t bytes_per_block = dxt3 || dxt5 ? 16u : 8u;
    const std::uint64_t blocks_x = (static_cast<std::uint64_t>(width) + 3u) / 4u;
    const std::uint64_t blocks_y = (static_cast<std::uint64_t>(height) + 3u) / 4u;
    if(!width || !height || width > 8192 || height > 8192 ||
       output.size() != static_cast<std::uint64_t>(width) * height * 4)
        return DecodeError::invalid_dimensions;
    std::uint64_t const required_bytes = blocks_x * blocks_y * bytes_per_block;
    if(source.size() < required_bytes) return DecodeError::truncated;
    if(source.size() > required_bytes) return DecodeError::trailing_bytes;
    for(std::uint32_t block_y = 0u; block_y < blocks_y; ++block_y)
    {
        for(std::uint32_t block_x = 0u; block_x < blocks_x; ++block_x)
        {
            const std::size_t block_index =
                static_cast<std::size_t>(block_y * blocks_x + block_x);
            const std::span<const std::byte> block = source.subspan(
                block_index * bytes_per_block, bytes_per_block);
            std::array<Rgba, 16u> pixels{};
            decode_color_block(
                block.subspan(dxt3 || dxt5 ? 8u : 0u, 8u),
                dxt3 || dxt5,
                pixels, rounding);
            if(dxt3)
            {
                for(std::size_t index = 0u; index < pixels.size(); ++index)
                {
                    const std::uint8_t packed = U8(block, index / 2u);
                    const std::uint8_t alpha = index % 2u == 0u
                        ? packed & 0x0fu
                        : packed >> 4u;
                    pixels[index].alpha = static_cast<std::uint8_t>(
                        (alpha << 4u) | alpha);
                }
            }
            else if(dxt5)
            {
                const auto table = dxt5_alpha_table(U8(block, 0u), U8(block, 1u), rounding);
                std::uint64_t selectors = 0u;
                for(std::size_t index = 0u; index < 6u; ++index)
                    selectors |= static_cast<std::uint64_t>(U8(block, index + 2u))
                        << (index * 8u);
                for(std::size_t index = 0u; index < pixels.size(); ++index)
                    pixels[index].alpha = table[(selectors >> (index * 3u)) & 7u];
            }
            for(std::uint32_t y = 0u; y < 4u; ++y)
                for(std::uint32_t x = 0u; x < 4u; ++x)
                    write_pixel(output, width, height,
                        block_x * 4u + x, block_y * 4u + y,
                        pixels[y * 4u + x]);
        }
    }
    return DecodeError::none;
}
}
