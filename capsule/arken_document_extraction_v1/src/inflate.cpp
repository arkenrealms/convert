#include "detail.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>

// Only stb_image's memory-only zlib inflater is used, always with a fixed
// caller-owned output buffer (z_expandable=0), so it never allocates output.
// Every other decoder is compiled out.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) && !defined(__clang__)
// Same GCC false positive as arken_image_codecs_v1/src/raster.cpp; the static
// API functions of this private implementation are intentionally unused here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace arken::document::v1::detail {
InflateStatus InflateRaw(Bytes input,std::size_t expected,std::string& output) {
    // The inflater speculatively reads ahead and substitutes zero bits at the end
    // of input. Eight explicit zero guard bytes keep every legitimate read-ahead
    // inside real memory, so the exact consumed bit count is observable: the
    // stream must end within the last real byte and never touch the guard.
    constexpr std::size_t guard=8;
    // Room for one maximal match (258) beyond the declared size distinguishes
    // "expands past its declared size" (a lying header or bomb) from corruption.
    constexpr std::size_t slack=259;
    if(expected>std::size_t(std::numeric_limits<int>::max())-slack)return InflateStatus::overflow;
    std::vector<stbi_uc> source(input.size()+guard,0);
    if(!input.empty())std::memcpy(source.data(),input.data(),input.size());
    output.assign(expected+slack,'\0');
    stbi__zbuf z{};
    z.zbuffer=source.data();
    z.zbuffer_end=source.data()+source.size();
    const auto ok=stbi__do_zlib(&z,output.data(),int(output.size()),0,0);
    const auto produced=std::size_t(z.zout-z.zout_start);
    if(!ok)return produced>expected?InflateStatus::overflow:InflateStatus::corrupt;
    if(produced>expected)return InflateStatus::overflow;
    if(produced!=expected)return InflateStatus::corrupt;
    if(z.hit_zeof_once||z.num_bits<0||z.zbuffer>=z.zbuffer_end)return InflateStatus::corrupt;
    const auto loadedBits=std::uint64_t(z.zbuffer-source.data())*8;
    if(loadedBits<std::uint64_t(z.num_bits))return InflateStatus::corrupt;
    const auto consumed=loadedBits-std::uint64_t(z.num_bits);
    const auto available=std::uint64_t(input.size())*8;
    if(consumed>available||consumed+8<=available)return InflateStatus::corrupt;
    output.resize(expected);
    return InflateStatus::ok;
}
}
