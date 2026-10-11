#pragma once
#include "arken_image_codecs_v1/api.hpp"
#include <stdexcept>
#include <cstring>
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace raster_fixture {
inline std::vector<std::byte> Png16(){
    std::vector<std::byte> out;for(unsigned v:{137,80,78,71,13,10,26,10})out.push_back(std::byte(v));
    const auto be=[](auto& bytes,std::uint32_t v){for(int shift=24;shift>=0;shift-=8)bytes.push_back(std::byte((v>>shift)&255));};
    const auto chunk=[&](std::string_view name,const std::vector<std::byte>& data){be(out,data.size());const auto start=out.size();for(char c:name)out.push_back(std::byte(c));out.insert(out.end(),data.begin(),data.end());std::uint32_t crc=0xffffffff;
        for(std::size_t i=start;i<out.size();++i){crc^=std::to_integer<std::uint8_t>(out[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}be(out,~crc);};
    std::vector<std::byte> header;be(header,1);be(header,1);for(unsigned v:{16,6,0,0,0})header.push_back(std::byte(v));chunk("IHDR",header);
    // One scanline: RGBA 0000 / 8080 / ffff / 0101, no filter, stored DEFLATE.
    std::vector<std::byte> raw;for(unsigned v:{0,0,0,128,128,255,255,1,1})raw.push_back(std::byte(v));
    std::vector<std::byte> compressed;for(unsigned v:{0x78,0x01,0x01,9,0,0xf6,0xff})compressed.push_back(std::byte(v));compressed.insert(compressed.end(),raw.begin(),raw.end());
    std::uint32_t a=1,b=0;for(auto v:raw){a=(a+std::to_integer<unsigned>(v))%65521;b=(b+a)%65521;}be(compressed,(b<<16)|a);chunk("IDAT",compressed);chunk("IEND",{});return out;
}
inline std::vector<std::uint8_t> Pixels(unsigned width=320,unsigned height=192,bool alpha=false){
    std::vector<std::uint8_t> bytes(std::size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto p=(std::size_t(y)*width+x)*4;
        bytes[p]=static_cast<std::uint8_t>(x*255/width);bytes[p+1]=static_cast<std::uint8_t>(y*255/height);
        bytes[p+2]=static_cast<std::uint8_t>(((x/32+y/32)%2)?220:48);bytes[p+3]=alpha?static_cast<std::uint8_t>(x*255/width):255;
    }return bytes;
}
inline std::vector<std::byte> Encode(arken::image::v1::Format format,unsigned width=320,unsigned height=192,bool alpha=false){
    auto pixels=Pixels(width,height,alpha);std::vector<std::byte> output;
    const auto append=[](void* context,void* data,int size){auto& bytes=*static_cast<std::vector<std::byte>*>(context);const auto* begin=static_cast<std::byte*>(data);bytes.insert(bytes.end(),begin,begin+size);};
    const auto w=static_cast<int>(width),h=static_cast<int>(height);int result=0;
    if(format==arken::image::v1::Format::png)result=stbi_write_png_to_func(append,&output,w,h,4,pixels.data(),w*4);
    else if(format==arken::image::v1::Format::bmp)result=stbi_write_bmp_to_func(append,&output,w,h,4,pixels.data());
    else if(format==arken::image::v1::Format::jpeg)result=stbi_write_jpg_to_func(append,&output,w,h,4,pixels.data(),95);
    if(!result)throw std::runtime_error("Raster fixture encoding failed");return output;
}
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
inline std::vector<std::byte> WithOrientation(std::vector<std::byte> bytes,unsigned orientation){
    const unsigned char header[]={255,225,0,34,'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,1,0,18,1,3,0,1,0,0,0,static_cast<unsigned char>(orientation),0,0,0,0,0,0,0};
    bytes.insert(bytes.begin()+2,reinterpret_cast<const std::byte*>(header),reinterpret_cast<const std::byte*>(header)+sizeof header);return bytes;
}
inline std::vector<std::byte> WithChromaticities(std::vector<std::byte> png,bool standard){
    std::vector<std::byte> chunk;
    const auto append=[&](std::uint32_t value){for(int shift=24;shift>=0;shift-=8)chunk.push_back(std::byte((value>>shift)&255));};
    append(32);append(0x6348524d);
    for(auto value:{31270u,32900u,64000u,33000u,30000u,60000u,15000u,6000u})append(value);
    if(!standard)chunk[11]^=std::byte{1};
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=4;i<chunk.size();++i){crc^=std::to_integer<std::uint8_t>(chunk[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    append(~crc);png.insert(png.begin()+33,chunk.begin(),chunk.end());return png;
}
}
