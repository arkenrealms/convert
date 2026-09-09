#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texture_fixture {
using Bytes=std::vector<std::byte>;
inline void Put(Bytes& b,std::size_t p,std::uint32_t value,unsigned length=4) {
    for(unsigned i=0;i<length;++i)b.at(p+i)=static_cast<std::byte>(value>>(i*8));
}
inline Bytes Tga(unsigned width=8,unsigned height=4,bool rle=false,unsigned origin=0x20,bool gray=false) {
    Bytes b(18); b[2]=std::byte(gray?(rle?11:3):(rle?10:2));
    Put(b,12,width,2);Put(b,14,height,2);b[16]=std::byte(gray?8:32);b[17]=std::byte(origin|(gray?0:8));
    for(unsigned i=0;i<width*height;++i) {
        if(rle)b.push_back(std::byte{}); // One literal pixel per packet.
        const auto x=i%width,y=i/width;
        const auto target_x=(origin&0x10)?width-1-x:x,target_y=(origin&0x20)?y:height-1-y;
        const auto value=static_cast<unsigned char>((target_x*11+target_y*7)%128);
        b.push_back(std::byte(value));
        if(!gray){b.push_back(std::byte{40});b.push_back(std::byte{220});b.push_back(std::byte{255});}
    }
    return b;
}
inline Bytes Dds(unsigned width=8,unsigned height=4,unsigned levels=1,unsigned compression=1) {
    Bytes b(128);Put(b,0,0x20534444);Put(b,4,124);Put(b,8,levels>1?0x21007:0x1007);
    Put(b,12,height);Put(b,16,width);Put(b,28,levels);Put(b,76,32);Put(b,80,4);
    Put(b,84,compression==1?0x31545844:compression==3?0x33545844:0x35545844);Put(b,108,0x1000);
    for(unsigned level=0;level<levels;++level) {
        const auto w=std::max(1u,width>>level),h=std::max(1u,height>>level);
        for(unsigned i=0;i<((w+3)/4)*((h+3)/4);++i) {
            const auto p=b.size();b.resize(p+(compression==1?8:16));
            if(compression==3)for(unsigned j=0;j<8;++j)b[p+j]=std::byte{255};
            if(compression==5)b[p]=std::byte{255};
            Put(b,p+(compression==1?0:8),level?0x07e0:0xf800,2);
        }
    }
    return b;
}
}
