#pragma once
#include "arken_image_codecs_v1/test_support/raster_fixtures.hpp"
#include <cstdlib>
#include <memory>

namespace conversion_fixture {
// The PNG declares one pixel but its small IDAT expands to 80 MiB. This is
// generated test data, never a donor asset or a host-side conversion fallback.
inline std::vector<std::byte> ExpansionBomb() {
    std::vector<unsigned char> expanded(80u * 1024u * 1024u, 0);
    int compressedSize = 0;
    std::unique_ptr<unsigned char, decltype(&std::free)> compressed(
        stbi_zlib_compress(expanded.data(), static_cast<int>(expanded.size()), &compressedSize, 5), std::free);
    if (!compressed || compressedSize <= 0) throw std::runtime_error("Cannot generate expansion fixture");
    std::vector<std::byte> png;
    for (auto value : {137,80,78,71,13,10,26,10}) png.push_back(std::byte(value));
    const auto put = [&](std::uint32_t value) {
        for (int shift=24; shift>=0; shift-=8) png.push_back(std::byte((value>>shift)&255));
    };
    const auto chunk = [&](std::uint32_t kind, std::span<const std::byte> bytes) {
        put(static_cast<std::uint32_t>(bytes.size()));
        const auto start=png.size(); put(kind); png.insert(png.end(),bytes.begin(),bytes.end());
        std::uint32_t crc=0xffffffffu;
        for (std::size_t i=start;i<png.size();++i) {
            crc ^= std::to_integer<std::uint8_t>(png[i]);
            for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
        }
        put(~crc);
    };
    const std::byte header[]={std::byte{0},std::byte{0},std::byte{0},std::byte{1},
        std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{8},std::byte{6},
        std::byte{0},std::byte{0},std::byte{0}};
    chunk(0x49484452u,header);
    chunk(0x49444154u,{reinterpret_cast<const std::byte*>(compressed.get()),static_cast<std::size_t>(compressedSize)});
    chunk(0x49454e44u,{});
    return png;
}
}
