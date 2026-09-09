#include "arken_image_codecs_v1/blp.hpp"
#include "block_decode.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace arken::image::v1 {
namespace {
constexpr std::size_t blp2_header_bytes=148, blp2_palette_bytes=1024;
constexpr std::uint32_t maximum_blp_dimension=8192;
std::uint8_t u8(std::span<const std::byte> b,std::size_t p) noexcept {return std::to_integer<std::uint8_t>(b[p]);}
std::uint32_t le32(std::span<const std::byte> b,std::size_t p) noexcept {
    return u8(b,p)|(std::uint32_t(u8(b,p+1))<<8)|(std::uint32_t(u8(b,p+2))<<16)|(std::uint32_t(u8(b,p+3))<<24);
}
bool checked_multiply(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
    if(a&&b>std::numeric_limits<std::uint64_t>::max()/a)return false;
    out=a*b;return true;
}
bool starts_with(std::span<const std::byte> b,const std::array<char,4>& magic) noexcept {
    return b.size()>=4&&std::equal(magic.begin(),magic.end(),b.begin(),[](char a,std::byte c){return a==std::to_integer<char>(c);});
}
DecodeIssue issue(DecodeError code,std::uint64_t offset=0) noexcept {return {code,offset};}


}
Blp2DecodeResult DecodeBlp2(
    const std::span<const std::byte> bytes, const std::uint64_t maximumBytes)
{
    Blp2DecodeResult result;
    if(bytes.size() > maximumBytes)
    {
        result.issue = issue(DecodeError::input_too_large);
        return result;
    }
    constexpr std::array<char, 4u> magic{{'B', 'L', 'P', '2'}};
    if(bytes.size() < blp2_header_bytes + blp2_palette_bytes)
    {
        result.issue = issue(DecodeError::truncated, bytes.size());
        return result;
    }
    if(!starts_with(bytes, magic))
    {
        result.issue = issue(DecodeError::wrong_magic);
        return result;
    }
    if(le32(bytes, 4u) != 1u)
    {
        result.issue = issue(DecodeError::unsupported_content, 4u);
        return result;
    }
    const std::uint8_t compression = u8(bytes, 8u);
    if(compression < static_cast<std::uint8_t>(Blp2Compression::palette)
        || compression > static_cast<std::uint8_t>(Blp2Compression::bgra8))
    {
        result.issue = issue(DecodeError::unsupported_compression, 8u);
        return result;
    }
    result.image.metadata.compression = static_cast<Blp2Compression>(compression);
    result.image.metadata.source_alpha_depth = u8(bytes, 9u);
    result.image.metadata.alpha_depth =
        result.image.metadata.source_alpha_depth;
    if(result.image.metadata.compression == Blp2Compression::bgra8
        && result.image.metadata.source_alpha_depth == 0x88u)
    {
        result.image.metadata.alpha_depth = 8u;
    }
    result.image.metadata.alpha_type = u8(bytes, 10u);
    result.image.metadata.has_mipmaps = u8(bytes, 11u) != 0u;
    result.image.metadata.width = le32(bytes, 12u);
    result.image.metadata.height = le32(bytes, 16u);
    if(result.image.metadata.width == 0u || result.image.metadata.height == 0u
        || result.image.metadata.width > maximum_blp_dimension
        || result.image.metadata.height > maximum_blp_dimension)
    {
        result.issue = issue(DecodeError::invalid_dimensions, 12u);
        return result;
    }
    if(result.image.metadata.alpha_depth != 0u
        && result.image.metadata.alpha_depth != 1u
        && result.image.metadata.alpha_depth != 4u
        && result.image.metadata.alpha_depth != 8u)
    {
        result.issue = issue(DecodeError::unsupported_alpha_depth, 9u);
        return result;
    }
    if(result.image.metadata.compression == Blp2Compression::dxt
        && result.image.metadata.alpha_type != 0u
        && result.image.metadata.alpha_type != 1u
        && result.image.metadata.alpha_type != 7u
        && result.image.metadata.alpha_depth != 0u)
    {
        result.issue = issue(DecodeError::unsupported_content, 10u);
        return result;
    }

    struct MipDescriptor final
    {
        std::uint32_t level = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t source_offset = 0u;
        std::uint32_t source_byte_length = 0u;
        std::uint64_t pixel_count = 0u;
        std::uint64_t output_byte_length = 0u;
        std::uint64_t decode_source_byte_length = 0u;
        bool legacy_packed_dxt = false;
    };
    std::array<MipDescriptor, 16u> descriptors{};
    std::uint32_t authored_mip_count = 0u;
    std::uint64_t aggregate_output_bytes = 0u;
    bool observed_gap = false;
    for(std::uint32_t level = 0u; level < descriptors.size(); ++level)
    {
        const std::size_t offset_field = 20u + level * 4u;
        const std::size_t size_field = 84u + level * 4u;
        const std::uint32_t source_offset = le32(bytes, offset_field);
        const std::uint32_t source_size = le32(bytes, size_field);
        if((source_offset == 0u) != (source_size == 0u))
        {
            result.issue = issue(DecodeError::invalid_layout,
                source_offset == 0u ? offset_field : size_field);
            return result;
        }
        if(source_offset == 0u)
        {
            observed_gap = true;
            continue;
        }
        if(observed_gap || (level > 0u && !result.image.metadata.has_mipmaps))
        {
            result.issue = issue(DecodeError::invalid_layout, offset_field);
            return result;
        }
        const std::uint64_t source_begin = source_offset;
        const std::uint64_t source_length = source_size;
        if(source_begin < blp2_header_bytes + blp2_palette_bytes
            || source_begin > bytes.size()
            || source_length > bytes.size() - source_begin)
        {
            result.issue = issue(DecodeError::invalid_layout, source_begin);
            return result;
        }
        for(std::uint32_t prior = 0u; prior < authored_mip_count; ++prior)
        {
            const std::uint64_t prior_begin =
                descriptors[prior].source_offset;
            const std::uint64_t prior_end = prior_begin
                + descriptors[prior].source_byte_length;
            const std::uint64_t source_end = source_begin + source_length;
            if(source_begin < prior_end && prior_begin < source_end)
            {
                result.issue = issue(
                    DecodeError::invalid_layout, source_begin);
                return result;
            }
        }

        MipDescriptor descriptor;
        descriptor.level = level;
        descriptor.width = std::max(1u,
            result.image.metadata.width >> level);
        descriptor.height = std::max(1u,
            result.image.metadata.height >> level);
        descriptor.source_offset = source_offset;
        descriptor.source_byte_length = source_size;
        if(!checked_multiply(descriptor.width, descriptor.height,
                descriptor.pixel_count)
            || !checked_multiply(descriptor.pixel_count, 4u,
                descriptor.output_byte_length))
        {
            result.issue = issue(DecodeError::integer_overflow);
            return result;
        }

        std::uint64_t expected_source_bytes = 0u;
        std::uint64_t legacy_packed_source_bytes = 0u;
        if(result.image.metadata.compression == Blp2Compression::palette)
        {
            const std::uint64_t alpha_bytes =
                result.image.metadata.alpha_depth == 0u ? 0u
                : result.image.metadata.alpha_depth == 1u
                    ? (descriptor.pixel_count + 7u) / 8u
                : result.image.metadata.alpha_depth == 4u
                    ? (descriptor.pixel_count + 1u) / 2u
                : descriptor.pixel_count;
            expected_source_bytes = descriptor.pixel_count + alpha_bytes;
        }
        else if(result.image.metadata.compression == Blp2Compression::bgra8)
        {
            expected_source_bytes = descriptor.output_byte_length;
        }
        else
        {
            const std::uint64_t blocks_x =
                (static_cast<std::uint64_t>(descriptor.width) + 3u) / 4u;
            const std::uint64_t blocks_y =
                (static_cast<std::uint64_t>(descriptor.height) + 3u) / 4u;
            std::uint64_t block_count = 0u;
            const std::uint64_t bytes_per_block =
                result.image.metadata.alpha_type == 1u
                    || result.image.metadata.alpha_type == 7u
                ? 16u : 8u;
            if(!checked_multiply(blocks_x, blocks_y, block_count)
                || !checked_multiply(block_count, bytes_per_block,
                    expected_source_bytes)
                || !checked_multiply(
                    1u, bytes_per_block, legacy_packed_source_bytes))
            {
                result.issue = issue(DecodeError::integer_overflow);
                return result;
            }
        }
        if(expected_source_bytes > maximumBytes)
        {
            result.issue = issue(DecodeError::resource_limit);
            return result;
        }
        descriptor.decode_source_byte_length = expected_source_bytes;
        if(source_length < expected_source_bytes)
        {
            const bool known_legacy_dxt_tail =
                result.image.metadata.compression == Blp2Compression::dxt
                && level > 0u
                && (descriptor.width < 4u || descriptor.height < 4u)
                && legacy_packed_source_bytes != 0u
                && source_length == legacy_packed_source_bytes;
            if(!known_legacy_dxt_tail)
            {
                result.issue = issue(
                    DecodeError::truncated, source_begin);
                return result;
            }
            descriptor.legacy_packed_dxt = true;
            ++result.image.metadata.legacy_packed_dxt_mip_count;
        }
        if(source_length > expected_source_bytes)
        {
            result.issue = issue(
                DecodeError::trailing_bytes, source_begin);
            return result;
        }
        if(descriptor.output_byte_length > maximumBytes
            || aggregate_output_bytes > maximumBytes
                - descriptor.output_byte_length)
        {
            result.issue = issue(DecodeError::resource_limit);
            return result;
        }
        aggregate_output_bytes += descriptor.output_byte_length;
        descriptors[authored_mip_count++] = descriptor;
    }
    if(authored_mip_count == 0u || descriptors[0u].level != 0u)
    {
        result.issue = issue(DecodeError::invalid_layout, 20u);
        return result;
    }
    result.image.metadata.authored_mip_count = authored_mip_count;

    try
    {
        result.image.mips.reserve(authored_mip_count);
        const auto palette = bytes.subspan(
            blp2_header_bytes, blp2_palette_bytes);
        for(std::uint32_t mip_index = 0u;
            mip_index < authored_mip_count; ++mip_index)
        {
            const MipDescriptor& descriptor = descriptors[mip_index];
            const auto source = bytes.subspan(descriptor.source_offset,
                descriptor.source_byte_length);
            Blp2MipLevel mip;
            mip.level = descriptor.level;
            mip.width = descriptor.width;
            mip.height = descriptor.height;
            mip.source_offset = descriptor.source_offset;
            mip.source_byte_length = descriptor.source_byte_length;
            mip.decoded = true;
            mip.legacy_packed_dxt = descriptor.legacy_packed_dxt;
            mip.rgba8.assign(
                static_cast<std::size_t>(descriptor.output_byte_length), 0u);
            if(result.image.metadata.compression == Blp2Compression::palette)
            {
                for(std::size_t index = 0u;
                    index < descriptor.pixel_count; ++index)
                {
                    const std::size_t palette_offset =
                        static_cast<std::size_t>(u8(source, index)) * 4u;
                    mip.rgba8[index * 4u] =
                        u8(palette, palette_offset + 2u);
                    mip.rgba8[index * 4u + 1u] =
                        u8(palette, palette_offset + 1u);
                    mip.rgba8[index * 4u + 2u] =
                        u8(palette, palette_offset);
                    std::uint8_t alpha = 255u;
                    const std::size_t alpha_offset =
                        static_cast<std::size_t>(descriptor.pixel_count);
                    if(result.image.metadata.alpha_depth == 1u)
                    {
                        alpha = ((u8(source, alpha_offset + index / 8u)
                            >> (index % 8u)) & 1u) != 0u ? 255u : 0u;
                    }
                    else if(result.image.metadata.alpha_depth == 4u)
                    {
                        const std::uint8_t packed =
                            u8(source, alpha_offset + index / 2u);
                        const std::uint8_t nibble = index % 2u == 0u
                            ? packed & 0x0fu
                            : packed >> 4u;
                        alpha = static_cast<std::uint8_t>(
                            (nibble << 4u) | nibble);
                    }
                    else if(result.image.metadata.alpha_depth == 8u)
                    {
                        alpha = u8(source, alpha_offset + index);
                    }
                    mip.rgba8[index * 4u + 3u] = alpha;
                }
            }
            else if(result.image.metadata.compression == Blp2Compression::bgra8)
            {
                for(std::size_t index = 0u;
                    index < descriptor.pixel_count; ++index)
                {
                    mip.rgba8[index * 4u] = u8(source, index * 4u + 2u);
                    mip.rgba8[index * 4u + 1u] = u8(source, index * 4u + 1u);
                    mip.rgba8[index * 4u + 2u] = u8(source, index * 4u);
                    mip.rgba8[index * 4u + 3u] = u8(source, index * 4u + 3u);
                }
            }
            else
            {
                std::vector<std::byte> padded_source;
                std::span<const std::byte> decode_source = source;
                if(descriptor.legacy_packed_dxt)
                {
                    padded_source.assign(
                        static_cast<std::size_t>(
                            descriptor.decode_source_byte_length),
                        std::byte{});
                    std::copy(source.begin(), source.end(),
                        padded_source.begin());
                    decode_source = padded_source;
                }
                const DecodeError dxt_error = detail::DecodeDxt(decode_source,
                    descriptor.width, descriptor.height,
                    result.image.metadata.alpha_type, mip.rgba8);
                if(dxt_error != DecodeError::none)
                {
                    result.issue = issue(
                        dxt_error, descriptor.source_offset);
                    result.image.mips.clear();
                    return result;
                }
            }
            result.image.mips.push_back(std::move(mip));
        }
    }
    catch(const std::bad_alloc&)
    {
        result.image.mips.clear();
        result.issue = issue(DecodeError::allocation_failed);
    }
    return result;
}


}
