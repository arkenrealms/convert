#pragma once
#include "raster_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace raster_fixture {
inline void Be(std::vector<std::byte>& bytes,std::size_t at,std::uint32_t value){
    for(unsigned i=0;i<4;++i)bytes.at(at+i)=std::byte((value>>(24-8*i))&255);
}
inline std::vector<std::byte> LinearRgbProfile(){
    std::vector<std::byte> profile(348);
    Be(profile,0,profile.size());Be(profile,8,0x02100000);Be(profile,12,0x6d6e7472);
    Be(profile,16,0x52474220);Be(profile,20,0x58595a20);Be(profile,36,0x61637370);
    Be(profile,68,63190);Be(profile,72,65536);Be(profile,76,54061);Be(profile,128,6);
    const std::uint32_t tags[]={0x7258595a,0x6758595a,0x6258595a,0x72545243,0x67545243,0x62545243};
    const double xyz[][3]={{0.4360747,0.2225045,0.0139322},{0.3850649,0.7168786,0.0971045},{0.1430804,0.0606169,0.7141733}};
    std::size_t at=204;
    for(unsigned i=0;i<6;++i){
        Be(profile,132+12*i,tags[i]);Be(profile,136+12*i,at);Be(profile,140+12*i,i<3?20:14);
        if(i<3){Be(profile,at,0x58595a20);for(unsigned c=0;c<3;++c)Be(profile,at+8+4*c,std::uint32_t(std::lround(xyz[i][c]*65536)));at+=20;}
        else{Be(profile,at,0x63757276);Be(profile,at+8,1);profile[at+12]=std::byte{1};at+=28;}
    }
    return profile;
}
inline std::vector<std::byte> IccPayload(std::vector<std::byte> profile){
    int size=0;
    auto* compressed=stbi_zlib_compress(reinterpret_cast<unsigned char*>(profile.data()),int(profile.size()),&size,8);
    if(!compressed)throw std::runtime_error("ICC fixture compression failed");
    std::vector<std::byte> payload{std::byte{'R'},std::byte{'G'},std::byte{'B'},std::byte{0},std::byte{0}};
    payload.insert(payload.end(),reinterpret_cast<std::byte*>(compressed),reinterpret_cast<std::byte*>(compressed)+size);
    std::free(compressed);return payload;
}
inline std::vector<std::byte> WithIcc(std::vector<std::byte> png,const std::vector<std::byte>& payload){
    std::vector<std::byte> chunk(payload.size()+12);Be(chunk,0,payload.size());Be(chunk,4,0x69434350);
    std::copy(payload.begin(),payload.end(),chunk.begin()+8);
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=4;i<chunk.size()-4;++i){crc^=std::to_integer<unsigned>(chunk[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    Be(chunk,chunk.size()-4,~crc);png.insert(png.begin()+33,chunk.begin(),chunk.end());return png;
}
inline void IccHeader(std::vector<std::byte>& profile,std::uint32_t deviceClass,std::uint32_t space,std::uint32_t pcs,std::uint32_t tags){
    Be(profile,0,std::uint32_t(profile.size()));Be(profile,8,0x02100000);Be(profile,12,deviceClass);
    Be(profile,16,space);Be(profile,20,pcs);Be(profile,36,0x61637370);
    Be(profile,68,63190);Be(profile,72,65536);Be(profile,76,54061);Be(profile,128,tags);
}
// Gray, XYZ PCS, identity (linear) kTRC.
inline std::vector<std::byte> LinearGrayProfile(){
    std::vector<std::byte> profile(156);
    IccHeader(profile,0x6d6e7472,0x47524159,0x58595a20,1);
    Be(profile,132,0x6b545243);Be(profile,136,144);Be(profile,140,12);
    Be(profile,144,0x63757276);
    return profile;
}
// CMYK output profile, Lab PCS, one lut8 (mft1) A2B0: identity input and output
// curves around a 2x2x2x2 CLUT that depends only on K. With Adobe's inverted
// storage (as skcms assumes), stored K=255 is no ink (L*=100) and K=0 is full
// ink (L*=0); stored C/M/Y are ignored. The simple CMYK inversion differs.
inline std::vector<std::byte> KOnlyCmykProfile(){
    constexpr std::size_t tag=48+4*256+16*3+3*256;
    std::vector<std::byte> profile(144+tag);
    IccHeader(profile,0x70727472,0x434d594b,0x4c616220,1);
    Be(profile,132,0x41324230);Be(profile,136,144);Be(profile,140,std::uint32_t(tag));
    auto at=std::size_t(144);
    Be(profile,at,0x6d667431);profile[at+8]=std::byte{4};profile[at+9]=std::byte{3};profile[at+10]=std::byte{2};
    for(unsigned i=0;i<3;++i)Be(profile,at+12+i*16,0x00010000);
    at+=48;
    for(unsigned c=0;c<4;++c)for(unsigned i=0;i<256;++i)profile[at++]=std::byte(i);
    for(unsigned entry=0;entry<16;++entry){
        profile[at++]=std::byte((entry&1)?0:255);profile[at++]=std::byte{128};profile[at++]=std::byte{128};
    }
    for(unsigned c=0;c<3;++c)for(unsigned i=0;i<256;++i)profile[at++]=std::byte(i);
    return profile;
}
// JPEG APP2 ICC_PROFILE segments (ICC.1 Annex B.4) carrying the profile split
// into `parts`; `reverse` emits them in descending sequence order.
inline std::vector<std::vector<std::byte>> IccSegments(const std::vector<std::byte>& profile,unsigned parts,bool reverse=false){
    std::vector<std::vector<std::byte>> segments;
    const auto step=(profile.size()+parts-1)/parts;
    for(unsigned i=0;i<parts;++i){
        std::vector<std::byte> payload;
        for(char c:std::string_view("ICC_PROFILE\0",12))payload.push_back(std::byte(c));
        payload.push_back(std::byte(i+1));payload.push_back(std::byte(parts));
        const auto begin=std::min(profile.size(),i*step),end=std::min(profile.size(),(i+1)*step);
        payload.insert(payload.end(),profile.begin()+begin,profile.begin()+end);
        segments.push_back(Segment(0xe2,payload));
    }
    if(reverse)std::reverse(segments.begin(),segments.end());
    return segments;
}
}
