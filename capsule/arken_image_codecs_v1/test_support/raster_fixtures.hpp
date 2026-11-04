#pragma once
#include "arken_image_codecs_v1/api.hpp"
#include "arken_image_codecs_v1/test_support/gif_fixtures.hpp"
#include <array>
#include <bit>
#include <stdexcept>
#include <cstring>
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace raster_fixture {
inline std::vector<std::byte> Png16(bool scalar=false){
    std::vector<std::byte> out;for(unsigned v:{137,80,78,71,13,10,26,10})out.push_back(std::byte(v));
    const auto be=[](auto& bytes,std::uint32_t v){for(int shift=24;shift>=0;shift-=8)bytes.push_back(std::byte((v>>shift)&255));};
    const auto chunk=[&](std::string_view name,const std::vector<std::byte>& data){be(out,data.size());const auto start=out.size();for(char c:name)out.push_back(std::byte(c));out.insert(out.end(),data.begin(),data.end());std::uint32_t crc=0xffffffff;
        for(std::size_t i=start;i<out.size();++i){crc^=std::to_integer<std::uint8_t>(out[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}be(out,~crc);};
    std::vector<std::byte> header;be(header,scalar?4:1);be(header,1);for(unsigned v:{16u,scalar?0u:6u,0u,0u,0u})header.push_back(std::byte(v));chunk("IHDR",header);
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
inline void PutLe(std::vector<std::byte>& out,std::uint32_t value,unsigned count){
    for(unsigned i=0;i<count;++i)out.push_back(std::byte((value>>(8*i))&255));
}
inline void PatchLe32(std::vector<std::byte>& out,std::size_t at,std::uint32_t value){
    for(unsigned i=0;i<4;++i)out.at(at+i)=std::byte((value>>(8*i))&255);
}
// Independently assembled BMP test data: any header size, compression, mask
// set, palette, V4/V5 colour block or embedded profile. Not a production encoder.
struct Bmp {
    std::uint32_t header=40;
    std::int32_t width=1,height=1;
    std::uint16_t bits=24;
    std::uint32_t compression=0,used=0;
    std::array<std::uint32_t,4> masks{};
    unsigned masksAfterHeader=0; // BITMAPINFOHEADER BI_BITFIELDS: 3 (or 4) trailing masks.
    std::vector<std::array<std::uint8_t,3>> palette; // RGB
    std::vector<std::byte> pixels; // stored rows or RLE stream
    std::uint32_t colourSpace=0x73524742u;
    std::array<std::int32_t,9> endpoints{};
    std::array<std::uint32_t,3> gamma{};
    std::vector<std::byte> profile;
};
inline std::vector<std::byte> MakeBmp(const Bmp& b){
    std::vector<std::byte> out{std::byte{'B'},std::byte{'M'}};
    PutLe(out,0,4);PutLe(out,0,4);PutLe(out,0,4);PutLe(out,b.header,4);
    if(b.header==12){PutLe(out,std::uint32_t(b.width),2);PutLe(out,std::uint32_t(b.height),2);PutLe(out,1,2);PutLe(out,b.bits,2);}
    else{
        PutLe(out,std::uint32_t(b.width),4);PutLe(out,std::uint32_t(b.height),4);PutLe(out,1,2);PutLe(out,b.bits,2);
        PutLe(out,b.compression,4);PutLe(out,std::uint32_t(b.pixels.size()),4);PutLe(out,2835,4);PutLe(out,2835,4);
        PutLe(out,b.used,4);PutLe(out,0,4);
        if(b.header>=52)for(unsigned i=0;i<3;++i)PutLe(out,b.masks[i],4);
        if(b.header>=56)PutLe(out,b.masks[3],4);
        if(b.header>=108){
            PutLe(out,b.colourSpace,4);
            for(auto e:b.endpoints)PutLe(out,std::uint32_t(e),4);
            for(auto g:b.gamma)PutLe(out,g,4);
        }
        if(b.header>=124){PutLe(out,4,4);PutLe(out,0,4);PutLe(out,std::uint32_t(b.profile.size()),4);PutLe(out,0,4);}
    }
    for(unsigned i=0;i<b.masksAfterHeader;++i)PutLe(out,b.masks[i],4);
    for(const auto& c:b.palette){
        out.push_back(std::byte(c[2]));out.push_back(std::byte(c[1]));out.push_back(std::byte(c[0]));
        if(b.header!=12)out.push_back(std::byte{0});
    }
    PatchLe32(out,10,std::uint32_t(out.size()));
    out.insert(out.end(),b.pixels.begin(),b.pixels.end());
    if(!b.profile.empty()){
        if(b.header==124)PatchLe32(out,14+112,std::uint32_t(out.size()-14));
        out.insert(out.end(),b.profile.begin(),b.profile.end());
    }
    PatchLe32(out,2,std::uint32_t(out.size()));
    return out;
}
// Stored BMP rows for top-down row-major samples: indices (bits<=8), 16-bit
// words, 0xRRGGBB (24-bit) or 32-bit words; each row padded to 4 bytes.
inline std::vector<std::byte> BmpRows(unsigned width,unsigned height,unsigned bits,const std::vector<std::uint32_t>& values,bool topDown=false){
    const auto stride=((width*bits+31)/32)*4;
    std::vector<std::byte> out(std::size_t(stride)*height);
    for(unsigned y=0;y<height;++y){
        const auto row=std::size_t(topDown?y:height-1-y)*stride;
        for(unsigned x=0;x<width;++x){
            const auto v=values.at(std::size_t(y)*width+x);
            if(bits<=8){
                const auto at=row+(x*bits)/8;
                out[at]|=std::byte((v&((1u<<bits)-1))<<(8-bits-(x*bits)%8));
            }else{
                for(unsigned i=0;i<bits/8;++i)out[row+x*(bits/8)+i]=std::byte((v>>(8*i))&255);
            }
        }
    }
    return out;
}
// Hand-assembled JPEG: one 8-pixel row of 8x8 MCUs, every component at 1x1
// sampling and a flat (DC-only) block per component per MCU. A flat block of
// sample v decodes exactly to v with a unit quantiser. Supports any SOF marker
// and precision byte, 1/3/4 components, the Adobe APP14 transform flag,
// progressive DC-only scans (SOF2), a DAC segment and arbitrary extra segments.
// stb_image_write cannot produce CMYK, YCCK, Adobe RGB, grayscale or progressive JPEG.
struct HandJpeg {
    std::uint8_t sof=0xc0,precision=8;
    unsigned components=3;
    int adobeTransform=-1;
    bool arithmeticConditioning=false;
    std::vector<std::vector<std::uint8_t>> mcus{{128,128,128}}; // [mcu][component]
    std::vector<std::vector<std::byte>> segments; // complete marker segments after SOI
};
inline std::vector<std::byte> MakeJpeg(const HandJpeg& j){
    std::vector<std::byte> out;
    const auto bytes=[&](std::initializer_list<unsigned> values){for(auto v:values)out.push_back(std::byte(v));};
    const auto be16=[&](unsigned v){bytes({(v>>8)&255,v&255});};
    bytes({0xff,0xd8});
    for(const auto& segment:j.segments)out.insert(out.end(),segment.begin(),segment.end());
    if(j.adobeTransform>=0){bytes({0xff,0xee});be16(14);bytes({'A','d','o','b','e',0,100,0,0,0,0,unsigned(j.adobeTransform)});}
    bytes({0xff,0xdb});be16(67);bytes({0});
    for(unsigned i=0;i<64;++i)bytes({1});
    if(j.arithmeticConditioning){bytes({0xff,0xcc});be16(4);bytes({0x00,0x10});}
    const auto width=unsigned(j.mcus.size())*8;
    bytes({0xff,j.sof});be16(8+3*j.components);bytes({j.precision});be16(8);be16(width);bytes({j.components});
    for(unsigned c=0;c<j.components;++c)bytes({c+1,0x11,0});
    // DC: categories 0..11, each a 4-bit code equal to the category. AC: EOB is "0".
    bytes({0xff,0xc4});be16(2+29+18);bytes({0x00});
    for(unsigned i=0;i<16;++i)bytes({i==3?12u:0u});
    for(unsigned i=0;i<12;++i)bytes({i});
    bytes({0x10});
    for(unsigned i=0;i<16;++i)bytes({i==0?1u:0u});
    bytes({0x00});
    const bool progressive=j.sof==0xc2;
    bytes({0xff,0xda});be16(6+2*j.components);bytes({j.components});
    for(unsigned c=0;c<j.components;++c)bytes({c+1,0x00});
    bytes({0,progressive?0u:63u,0});
    unsigned accumulator=0,count=0;
    const auto emit=[&](unsigned b){out.push_back(std::byte(b));if(b==0xff)out.push_back(std::byte{0});};
    const auto put=[&](unsigned code,unsigned length){
        for(unsigned i=length;i-->0;){
            accumulator=(accumulator<<1)|((code>>i)&1u);
            if(++count==8){emit(accumulator&255);accumulator=0;count=0;}
        }
    };
    std::vector<int> previous(j.components,0);
    for(const auto& mcu:j.mcus)for(unsigned c=0;c<j.components;++c){
        const int dc=(int(mcu.at(c))-128)*8,diff=dc-previous[c];
        previous[c]=dc;
        const unsigned magnitude=unsigned(diff<0?-diff:diff),category=unsigned(std::bit_width(magnitude));
        put(category,4);
        if(category)put(unsigned(diff>0?diff:diff+(1<<category)-1),category);
        if(!progressive)put(0,1);
    }
    while(count)put(1,1);
    bytes({0xff,0xd9});
    return out;
}
// A complete marker segment with the given payload.
inline std::vector<std::byte> Segment(unsigned marker,const std::vector<std::byte>& payload){
    std::vector<std::byte> out{std::byte{0xff},std::byte(marker),std::byte(((payload.size()+2)>>8)&255),std::byte((payload.size()+2)&255)};
    out.insert(out.end(),payload.begin(),payload.end());
    return out;
}
inline std::vector<std::byte> InsertAfterSoi(std::vector<std::byte> jpeg,const std::vector<std::vector<std::byte>>& segments){
    std::size_t at=2;
    for(const auto& segment:segments){jpeg.insert(jpeg.begin()+at,segment.begin(),segment.end());at+=segment.size();}
    return jpeg;
}
}
