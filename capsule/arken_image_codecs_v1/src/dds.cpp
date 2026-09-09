#include "reader.hpp"
#include "block_decode.hpp"
#include <array>

namespace arken::image::v1::detail {
namespace {
bool ContiguousMask(std::uint32_t mask, unsigned bits) {
    if(!mask) return false;
    if(bits < 32 && (mask >> bits)) return false;
    const auto shifted = mask >> std::countr_zero(mask);
    return (shifted & (std::uint64_t(shifted) + 1)) == 0;
}
std::uint8_t Channel(std::uint32_t pixel, std::uint32_t mask) {
    if(!mask) return 255;
    const auto shift = std::countr_zero(mask);
    const std::uint64_t maximum = mask >> shift;
    return static_cast<std::uint8_t>((std::uint64_t((pixel & mask) >> shift) * 255 + maximum / 2) / maximum);
}
}

// DDS header and pixel-mask laws reused from Oasis aotm_dds.cpp. Shared block
// decoding preserves that producer's nearest-integer interpolation explicitly.
DecodeResult DecodeDds(Bytes bytes, const DecodeOptions& options) {
    if(bytes.size() < 128) return Failure(DecodeError::truncated);
    if(U32(bytes,0) != 0x20534444) return Failure(DecodeError::wrong_magic);
    if(U32(bytes,4) != 124 || U32(bytes,76) != 32) return Failure(DecodeError::unsupported_content,4);
    const auto width = U32(bytes,16), height = U32(bytes,12);
    if(!Dimensions(width,height,options)) return Failure(DecodeError::invalid_dimensions,12);
    if((U32(bytes,112) & 0x20fe00) || U32(bytes,24) > 1)
        return Failure(DecodeError::unsupported_content,112); // Cube/volume needs a multi-surface contract.
    const auto flags = U32(bytes,8), pixel_flags = U32(bytes,80), fourcc = U32(bytes,84);
    const bool compressed = (pixel_flags & 4) != 0;
    std::uint8_t alpha_type = 0;
    std::uint32_t block_bytes = 0;
    const auto bits = U32(bytes,88);
    std::array<std::uint32_t,4> masks{U32(bytes,92),U32(bytes,96),U32(bytes,100),U32(bytes,104)};
    if(compressed) {
        if(fourcc == 0x31545844) block_bytes = 8;
        else if(fourcc == 0x33545844) { block_bytes = 16; alpha_type = 1; }
        else if(fourcc == 0x35545844) { block_bytes = 16; alpha_type = 7; }
        else return Failure(DecodeError::unsupported_compression,84);
    } else {
        if(!(pixel_flags & 0x40) || (bits != 24 && bits != 32)) return Failure(DecodeError::unsupported_content,80);
        for(unsigned i = 0; i < 4; ++i) {
            if(i == 3 && !masks[i]) continue;
            if(!ContiguousMask(masks[i],bits)) return Failure(DecodeError::invalid_layout,92+i*4);
            for(unsigned j = 0; j < i; ++j)
                if(masks[i] & masks[j]) return Failure(DecodeError::invalid_layout,92+i*4);
        }
    }
    std::uint32_t mip_count = (flags & 0x20000) ? U32(bytes,28) : 1;
    const auto maximum_mips = static_cast<std::uint32_t>(std::bit_width(std::max(width,height)));
    if(!mip_count || mip_count > maximum_mips) return Failure(DecodeError::invalid_layout,28);
    if(!compressed && mip_count > 1 && (bits == 24 || ((flags & 8) && U32(bytes,20) != width*4)))
        return Failure(DecodeError::unsupported_content,20); // Lower-mip row padding is not described by this header.
    if(options.mipIndex >= mip_count) return Failure(DecodeError::selection_out_of_range,28);
    std::uint64_t offset = 128, selected_offset = 0, selected_length = 0, selected_pitch = 0;
    std::uint32_t selected_width = 0, selected_height = 0;
    for(std::uint32_t level = 0; level < mip_count; ++level) {
        const auto w = std::max(1u,width >> level), h = std::max(1u,height >> level);
        std::uint64_t pitch = std::uint64_t(w) * (bits/8);
        if(!compressed && level == 0 && (flags & 8)) {
            if(U32(bytes,20) < pitch) return Failure(DecodeError::invalid_layout,20);
            pitch = U32(bytes,20);
        }
        const std::uint64_t length = compressed ? std::uint64_t((w+3)/4)*((h+3)/4)*block_bytes : pitch*h;
        if(!Range(bytes,offset,length)) return Failure(DecodeError::truncated,offset);
        if(level == options.mipIndex) {
            selected_offset=offset; selected_length=length; selected_pitch=pitch;
            selected_width=w; selected_height=h;
        }
        offset += length;
    }
    DecodeResult result;
    auto& image = result.image;
    image.width=selected_width; image.height=selected_height; image.mipCount=mip_count;
    image.rgba8.resize(std::size_t(image.width)*image.height*4);
    if(compressed) {
        const auto error = DecodeDxt(bytes.subspan(static_cast<std::size_t>(selected_offset),static_cast<std::size_t>(selected_length)),
            image.width,image.height,alpha_type,image.rgba8,BlockRounding::nearest);
        if(error != DecodeError::none) return Failure(error,selected_offset);
    } else {
        for(std::uint32_t y=0; y<image.height; ++y) for(std::uint32_t x=0; x<image.width; ++x) {
            const auto at=static_cast<std::size_t>(selected_offset+y*selected_pitch+std::uint64_t(x)*(bits/8));
            std::uint32_t pixel=U8(bytes,at)|(std::uint32_t(U8(bytes,at+1))<<8)|(std::uint32_t(U8(bytes,at+2))<<16);
            if(bits == 32) pixel |= std::uint32_t(U8(bytes,at+3))<<24;
            for(unsigned channel=0; channel<4; ++channel)
                image.rgba8[(std::size_t(y)*image.width+x)*4+channel]=Channel(pixel,masks[channel]);
        }
    }
    return result;
}
}
