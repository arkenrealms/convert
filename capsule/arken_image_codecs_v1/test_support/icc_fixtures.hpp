#pragma once
#include "raster_fixtures.hpp"
#include <cmath>

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
}
