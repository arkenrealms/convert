#pragma once
#include "arken_image_codecs_v1/api.hpp"
#include <stdexcept>

namespace ico_fixture {
using Bytes=std::vector<std::byte>;
inline void Put16(Bytes& b,std::size_t p,unsigned v){b[p]=std::byte(v&255);b[p+1]=std::byte((v>>8)&255);}
inline void Put32(Bytes& b,std::size_t p,std::uint32_t v){Put16(b,p,v&65535);Put16(b,p+2,v>>16);}
struct Entry {unsigned width,height,bits;Bytes bytes;};
inline Bytes Container(const std::vector<Entry>& entries){
    Bytes out(6+entries.size()*16);Put16(out,2,1);Put16(out,4,entries.size());
    for(std::size_t i=0;i<entries.size();++i){const auto& e=entries[i];const auto p=6+i*16;
        out[p]=std::byte(e.width&255);out[p+1]=std::byte(e.height&255);Put16(out,p+4,1);Put16(out,p+6,e.bits);
        Put32(out,p+8,e.bytes.size());Put32(out,p+12,out.size());out.insert(out.end(),e.bytes.begin(),e.bytes.end());
    }return out;
}
// CUR shares the ICO directory; entries store a hotspot in place of planes/bits.
inline Bytes Cursor(Bytes ico,unsigned hotspotX,unsigned hotspotY){
    Put16(ico,2,2);
    for(unsigned i=0;i<(std::to_integer<unsigned>(ico[4])|(std::to_integer<unsigned>(ico[5])<<8));++i){
        Put16(ico,6+i*16+4,hotspotX+i);Put16(ico,6+i*16+6,hotspotY+i);
    }
    return ico;
}
struct DibImage {Entry entry;std::vector<std::uint8_t> pixels;};
inline DibImage Dib(unsigned bits=32,bool alpha=true,unsigned width=9,unsigned height=7){
    const unsigned colors=bits<=8?1u<<bits:0;
    const auto stride=((width*bits+31)/32)*4,maskStride=((width+31)/32)*4;
    const auto pixelOffset=40+colors*4,maskOffset=pixelOffset+stride*height;
    DibImage out{{width,height,bits,Bytes(maskOffset+maskStride*height)},std::vector<std::uint8_t>(width*height*4)};
    auto& b=out.entry.bytes;Put32(b,0,40);Put32(b,4,width);Put32(b,8,height*2);Put16(b,12,1);Put16(b,14,bits);
    for(unsigned i=0;i<colors;++i){b[40+i*4]=std::byte(i);b[41+i*4]=std::byte(i);b[42+i*4]=std::byte(i);}
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto p=(y*width+x)*4,row=pixelOffset+(height-1-y)*stride;
        const bool transparent=(x+y)%5==0;
        const unsigned index=transparent||!colors?0:(x+y)%colors;
        unsigned red=0,green=0,blue=0;
        if(bits<=8){
            red=green=blue=index;
            if(bits==8)b[row+x]=std::byte(index);
            else if(bits==4)b[row+x/2]|=std::byte(index<<(x%2?0:4));
            else b[row+x/8]|=std::byte(index<<(7-x%8));
        }else{
            red=transparent?0:(x*23)%256;green=transparent?0:(y*37)%256;blue=transparent?0:120;
            const auto q=row+x*(bits/8);b[q]=std::byte(blue);b[q+1]=std::byte(green);b[q+2]=std::byte(red);
            if(bits==32)b[q+3]=std::byte(alpha?((x+y)%4)*85:0);
        }
        if(transparent)b[maskOffset+(height-1-y)*maskStride+x/8]|=std::byte(0x80u>>(x%8));
        out.pixels[p]=red;out.pixels[p+1]=green;out.pixels[p+2]=blue;
        out.pixels[p+3]=bits==32&&alpha?((x+y)%4)*85:transparent?0:255;
    }return out;
}
inline Entry Png(unsigned width=32,unsigned height=16){
    std::vector<std::uint8_t> pixels(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto p=(y*width+x)*4;
        pixels[p]=x*255/width;pixels[p+1]=y*255/height;pixels[p+2]=190;pixels[p+3]=(x+y)%4*85;}
    auto encoded=arken::image::v1::EncodePng(width,height,pixels);
    if(!encoded)throw std::runtime_error("ICO PNG fixture encode failed");
    return {width,height,32,std::move(encoded.bytes)};
}
}
