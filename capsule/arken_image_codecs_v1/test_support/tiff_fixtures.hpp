#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace tiff_fixture {
using Bytes=std::vector<std::byte>;
inline void Put(Bytes& b,std::size_t p,std::uint32_t v,unsigned size,bool little=true){
    for(unsigned i=0;i<size;++i)b[p+i]=std::byte((v>>(8*(little?i:size-i-1)))&255u);
}
inline std::uint32_t Get(const Bytes& b,std::size_t p,unsigned size=4){
    std::uint32_t v=0;for(unsigned i=0;i<size;++i)v|=std::uint32_t(std::to_integer<unsigned>(b.at(p+i)))<<(i*8);return v;
}
inline std::size_t Field(const Bytes& b,unsigned tag){
    for(unsigned i=0;i<Get(b,8,2);++i){const auto p=10+i*12;if(Get(b,p,2)==tag)return p;}
    return 0;
}
inline void Scalar(Bytes& b,unsigned tag,std::uint32_t v){const auto p=Field(b,tag);Put(b,p+8,v,Get(b,p+2,2)==3?2:4);}
inline Bytes Zlib(const Bytes& b){
    Bytes z(7);z[0]=std::byte{0x78};z[1]=std::byte{1};z[2]=std::byte{1};Put(z,3,b.size(),2);Put(z,5,~std::uint32_t(b.size()),2);
    z.insert(z.end(),b.begin(),b.end());std::uint32_t a=1,s=0;
    for(auto c:b){a=(a+std::to_integer<unsigned>(c))%65521;s=(s+a)%65521;}
    const auto p=z.size();z.resize(p+4);Put(z,p,(s<<16)|a,4,false);return z;
}
struct Image {Bytes bytes;std::vector<std::uint8_t> rgba;};
inline Image Make(unsigned channels=4,unsigned photo=2,bool little=true,unsigned compression=1,
    unsigned predictor=1,unsigned rows=2){
    constexpr unsigned w=7,h=3;
    Image result;std::vector<Bytes> strips;
    for(unsigned y=0;y<h;y+=rows){
        Bytes raw;const auto n=std::min(rows,h-y);
        for(unsigned row=0;row<n;++row)for(unsigned x=0;x<w;++x){
            const auto red=std::uint8_t(11+x*17),green=std::uint8_t(27+(y+row)*31),blue=std::uint8_t(201-x*13);
            const auto alpha=std::uint8_t(x%3==0?0:x%3==1?127:255);
            raw.push_back(std::byte(red));
            if(photo==2){raw.push_back(std::byte(green));raw.push_back(std::byte(blue));}
            const bool has_alpha=channels==(photo==2?4u:2u);
            if(has_alpha)raw.push_back(std::byte(alpha));
            if(photo==2)result.rgba.insert(result.rgba.end(),{red,green,blue});
            else result.rgba.insert(result.rgba.end(),3,photo==0?std::uint8_t(255-red):red);
            result.rgba.push_back(has_alpha?alpha:255);
        }
        if(predictor==2)for(unsigned row=0;row<n;++row)
            for(std::size_t i=w*channels;i-->channels;){const auto p=row*w*channels+i;
                raw[p]=std::byte((std::to_integer<unsigned>(raw[p])-std::to_integer<unsigned>(raw[p-channels]))&255u);}
        if(compression==8||compression==32946)strips.push_back(Zlib(raw));
        else if(compression==32773){
            Bytes packed;
            for(unsigned row=0;row<n;++row){packed.push_back(std::byte{128});packed.push_back(std::byte(w*channels-1));
                packed.insert(packed.end(),raw.begin()+row*w*channels,raw.begin()+(row+1)*w*channels);}
            strips.push_back(std::move(packed));
        }else strips.push_back(std::move(raw));
    }
    using Entry=std::pair<unsigned,std::vector<std::uint32_t>>;
    std::map<unsigned,Entry> tags{{256,{4,{w}}},{257,{4,{h}}},{258,{3,std::vector<std::uint32_t>(channels,8)}},
        {259,{3,{compression}}},{262,{3,{photo}}},{273,{4,std::vector<std::uint32_t>(strips.size())}},
        {274,{3,{1}}},{277,{3,{channels}}},{278,{4,{rows}}},{279,{4,{}}},{284,{3,{1}}},{317,{3,{predictor}}}};
    if(channels==(photo==2?4u:2u))tags[338]={3,{2}};
    for(const auto& s:strips)tags[279].second.push_back(s.size());
    auto& b=result.bytes;b.resize(8+2+tags.size()*12+4);
    b[0]=b[1]=std::byte(little?'I':'M');Put(b,2,42,2,little);Put(b,4,8,4,little);Put(b,8,tags.size(),2,little);
    std::size_t at=10,offsets=0;
    for(const auto& [id,e]:tags){
        Put(b,at,id,2,little);Put(b,at+2,e.first,2,little);Put(b,at+4,e.second.size(),4,little);
        const auto size=e.first==3?2u:4u;
        const auto extent=std::size_t(size)*e.second.size();
        std::size_t data=at+8;
        if(extent>4){data=b.size();Put(b,at+8,data,4,little);b.resize(data+extent);}
        if(id==273)offsets=data;
        for(std::size_t i=0;i<e.second.size();++i)Put(b,data+i*size,e.second[i],size,little);
        at+=12;
    }
    for(std::size_t i=0;i<strips.size();++i){Put(b,offsets+i*4,b.size(),4,little);b.insert(b.end(),strips[i].begin(),strips[i].end());}
    return result;
}
}
