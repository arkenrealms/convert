#pragma once
#include <cstddef>
#include <vector>

namespace raster_fixture {
inline std::vector<std::byte> Gif2x1(){
    // GIF89a, two opaque palette entries, one non-interlaced frame. The
    // packed LZW stream emits indices 0 and 1 and an explicit end code.
    return {
        std::byte{'G'},std::byte{'I'},std::byte{'F'},std::byte{'8'},std::byte{'9'},std::byte{'a'},
        std::byte{2},std::byte{0},std::byte{1},std::byte{0},std::byte{0x80},std::byte{0},std::byte{0},
        std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{0},std::byte{0},
        std::byte{0x2c},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{2},std::byte{0},
        std::byte{1},std::byte{0},std::byte{0},std::byte{2},std::byte{2},std::byte{0x44},std::byte{0x0a},
        std::byte{0},std::byte{0x3b}
    };
}
inline std::vector<std::byte> AnimatedGif2x1(){
    const auto still=Gif2x1();
    std::vector<std::byte> output(still.begin(),still.begin()+19);
    const std::vector<std::byte> control={std::byte{0x21},std::byte{0xf9},std::byte{4},std::byte{4},
        std::byte{10},std::byte{0},std::byte{0},std::byte{0}};
    for(unsigned frame=0;frame<2;++frame){
        output.insert(output.end(),control.begin(),control.end());
        const auto start=output.size();
        output.insert(output.end(),still.begin()+19,still.end()-1);
        // Second frame reverses the palette indices: red, then black.
        if(frame==1)output[start+12]=std::byte{0x0c};
    }
    output.push_back(std::byte{0x3b});
    return output;
}
}
