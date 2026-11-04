#pragma once
#include "arken_image_codecs_v1/test_support/raster_fixtures.hpp"
#include <memory>
#include <string_view>
#include <utility>

namespace raster_fixture {
// Small independently encoded PNGs exercise pass shape, empty passes and
// sub-byte row padding. They are test data, not a second production encoder.
inline std::vector<std::byte> LayoutPng(unsigned width,unsigned height,bool adam7,bool packed=false,bool extra=false){
    std::vector<unsigned char> raw;
    const auto pixels=Pixels(width,height,true);
    constexpr unsigned starts[][2]={{0,0},{4,0},{0,4},{2,0},{0,2},{1,0},{0,1}};
    constexpr unsigned steps[][2]={{8,8},{8,8},{4,8},{4,4},{2,4},{2,2},{1,2}};
    for(unsigned pass=0;pass<(adam7?7u:1u);++pass){
        const unsigned sx=adam7?starts[pass][0]:0,sy=adam7?starts[pass][1]:0;
        const unsigned dx=adam7?steps[pass][0]:1,dy=adam7?steps[pass][1]:1;
        if(sx>=width||sy>=height)continue;
        for(unsigned y=sy;y<height;y+=dy){
            raw.push_back(0);
            if(packed){for(unsigned x=0;x<width;x+=8)raw.push_back(0xff);}
            else for(unsigned x=sx;x<width;x+=dx){
                const auto i=(std::size_t(y)*width+x)*4;
                raw.insert(raw.end(),pixels.begin()+i,pixels.begin()+i+4);
            }
        }
    }
    if(extra)raw.push_back(0);
    int length=0;
    std::unique_ptr<unsigned char,decltype(&std::free)> compressed(
        stbi_zlib_compress(raw.data(),static_cast<int>(raw.size()),&length,5),std::free);
    if(!compressed||length<=0)throw std::runtime_error("PNG layout fixture compression failed");
    std::vector<std::byte> out;
    for(unsigned v:{137,80,78,71,13,10,26,10})out.push_back(std::byte(v));
    const auto be=[](auto& b,std::uint32_t v){for(int shift=24;shift>=0;shift-=8)b.push_back(std::byte((v>>shift)&255));};
    const auto chunk=[&](std::string_view name,std::span<const std::byte> data){
        be(out,data.size());const auto start=out.size();
        for(char c:name)out.push_back(std::byte(c));
        out.insert(out.end(),data.begin(),data.end());
        std::uint32_t crc=0xffffffff;
        for(std::size_t i=start;i<out.size();++i){crc^=std::to_integer<unsigned>(out[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
        be(out,~crc);
    };
    std::vector<std::byte> header;be(header,width);be(header,height);
    for(unsigned v:{packed?1u:8u,packed?0u:6u,0u,0u,adam7?1u:0u})header.push_back(std::byte(v));
    chunk("IHDR",header);
    const auto bytes=std::span(reinterpret_cast<const std::byte*>(compressed.get()),static_cast<std::size_t>(length));
    const auto split=bytes.size()/2;
    chunk("IDAT",bytes.first(split));chunk("IDAT",bytes.subspan(split));chunk("IEND",{});
    return out;
}
inline std::vector<std::byte> BeWords(std::initializer_list<std::uint32_t> values){
    std::vector<std::byte> out;
    for(auto v:values)for(int shift=24;shift>=0;shift-=8)out.push_back(std::byte((v>>shift)&255));
    return out;
}
inline std::vector<std::byte> PngChunk(std::string_view name,const std::vector<std::byte>& data){
    std::vector<std::byte> out;
    for(int shift=24;shift>=0;shift-=8)out.push_back(std::byte((data.size()>>shift)&255));
    for(char c:name)out.push_back(std::byte(c));
    out.insert(out.end(),data.begin(),data.end());
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=4;i<out.size();++i){crc^=std::to_integer<unsigned>(out[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    crc=~crc;
    for(int shift=24;shift>=0;shift-=8)out.push_back(std::byte((crc>>shift)&255));
    return out;
}
// Inserts a chunk directly after IHDR (before PLTE/IDAT, as colour chunks require).
inline std::vector<std::byte> WithChunk(std::vector<std::byte> png,std::string_view name,const std::vector<std::byte>& data){
    const auto chunk=PngChunk(name,data);
    png.insert(png.begin()+33,chunk.begin(),chunk.end());
    return png;
}
// Independently packed PNG of any colour type at bit depth 1..8: `samples` holds
// row-major samples (channels per pixel); `chunks` precede IDAT (PLTE, tRNS...).
inline std::vector<std::byte> SamplePng(unsigned width,unsigned height,unsigned depth,unsigned type,bool adam7,
    const std::vector<std::uint8_t>& samples,const std::vector<std::pair<std::string_view,std::vector<std::byte>>>& chunks={}){
    const unsigned channels=type==0||type==3?1:type==2?3:type==4?2:4;
    constexpr unsigned starts[][2]={{0,0},{4,0},{0,4},{2,0},{0,2},{1,0},{0,1}};
    constexpr unsigned steps[][2]={{8,8},{8,8},{4,8},{4,4},{2,4},{2,2},{1,2}};
    std::vector<unsigned char> raw;
    for(unsigned pass=0;pass<(adam7?7u:1u);++pass){
        const unsigned sx=adam7?starts[pass][0]:0,sy=adam7?starts[pass][1]:0;
        const unsigned dx=adam7?steps[pass][0]:1,dy=adam7?steps[pass][1]:1;
        if(sx>=width||sy>=height)continue;
        for(unsigned y=sy;y<height;y+=dy){
            raw.push_back(0);
            unsigned accumulator=0,bits=0;
            for(unsigned x=sx;x<width;x+=dx)for(unsigned c=0;c<channels;++c){
                accumulator=(accumulator<<depth)|samples.at((std::size_t(y)*width+x)*channels+c);bits+=depth;
                if(bits==8){raw.push_back(static_cast<unsigned char>(accumulator));accumulator=0;bits=0;}
            }
            if(bits)raw.push_back(static_cast<unsigned char>(accumulator<<(8-bits)));
        }
    }
    int length=0;
    std::unique_ptr<unsigned char,decltype(&std::free)> compressed(
        stbi_zlib_compress(raw.data(),static_cast<int>(raw.size()),&length,5),std::free);
    if(!compressed||length<=0)throw std::runtime_error("PNG sample fixture compression failed");
    std::vector<std::byte> out;
    for(unsigned v:{137,80,78,71,13,10,26,10})out.push_back(std::byte(v));
    auto header=BeWords({width,height});
    for(unsigned v:{depth,type,0u,0u,adam7?1u:0u})header.push_back(std::byte(v));
    const auto append=[&](const std::vector<std::byte>& chunk){out.insert(out.end(),chunk.begin(),chunk.end());};
    append(PngChunk("IHDR",header));
    for(const auto& [name,data]:chunks)append(PngChunk(name,data));
    const auto* begin=reinterpret_cast<const std::byte*>(compressed.get());
    append(PngChunk("IDAT",std::vector<std::byte>(begin,begin+length)));
    append(PngChunk("IEND",{}));
    return out;
}
}
