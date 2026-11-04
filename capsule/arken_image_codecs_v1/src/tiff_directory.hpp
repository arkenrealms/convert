#pragma once
#include "reader.hpp"
#include <map>

namespace arken::image::v1::detail {
inline void TiffRequire(bool accepted,DecodeError error){if(!accepted)throw error;}
struct TiffBytes {
    Bytes bytes;
    bool little;
    std::uint16_t u16(std::size_t p)const {
        TiffRequire(Range(bytes,p,2),DecodeError::truncated);
        return little?U16(bytes,p):std::uint16_t((std::uint16_t(U8(bytes,p))<<8)|U8(bytes,p+1));
    }
    std::uint32_t u32(std::size_t p)const {
        TiffRequire(Range(bytes,p,4),DecodeError::truncated);
        return little?U32(bytes,p):(std::uint32_t(u16(p))<<16)|u16(p+2);
    }
};
struct TiffEntry {std::uint16_t type;std::uint32_t count;std::size_t position;};
struct TiffRange {std::uint64_t begin,size;};
struct TiffDirectory {
    TiffBytes file;
    std::map<std::uint16_t,TiffEntry> entries;
    std::uint32_t next=0;
    TiffRange directory{};
    explicit TiffDirectory(Bytes bytes):file{bytes,false} {
        TiffRequire(bytes.size()>=8,DecodeError::truncated);
        file.little=U8(bytes,0)==73&&U8(bytes,1)==73;
        TiffRequire(file.little||(U8(bytes,0)==77&&U8(bytes,1)==77),DecodeError::wrong_magic);
        TiffRequire(file.u16(2)==42,DecodeError::unsupported_content);
        const auto offset=file.u32(4);
        TiffRequire(offset>=8,DecodeError::invalid_layout);
        const auto count=file.u16(offset);
        TiffRequire(count&&count<=256,DecodeError::resource_limit);
        directory={offset,std::uint64_t(count)*12+6};
        TiffRequire(Range(bytes,directory.begin,directory.size),DecodeError::truncated);
        next=file.u32(std::size_t(offset)+2+count*12);
        for(unsigned i=0;i<count;++i){const auto p=std::size_t(offset)+2+i*12;
            TiffRequire(entries.emplace(file.u16(p),TiffEntry{file.u16(p+2),file.u32(p+4),p+8}).second,DecodeError::invalid_layout);
        }
    }
    std::vector<std::uint32_t> values(std::uint16_t tag)const {
        const auto it=entries.find(tag);
        TiffRequire(it!=entries.end(),DecodeError::invalid_layout);
        const auto& e=it->second;
        TiffRequire((e.type==3||e.type==4)&&e.count&&e.count<=8192,DecodeError::unsupported_content);
        const std::size_t size=e.type==3?2:4,extent=size*e.count;
        const std::size_t p=extent<=4?e.position:file.u32(e.position);
        TiffRequire(Range(file.bytes,p,extent),DecodeError::truncated);
        std::vector<std::uint32_t> result;result.reserve(e.count);
        for(unsigned i=0;i<e.count;++i)result.push_back(size==2?file.u16(p+i*size):file.u32(p+i*size));
        return result;
    }
    std::uint32_t scalar(std::uint16_t tag,std::uint32_t fallback)const {
        if(!entries.contains(tag))return fallback;
        const auto v=values(tag);TiffRequire(v.size()==1,DecodeError::invalid_layout);return v.front();
    }
    std::vector<TiffRange> metadata()const {
        std::vector<TiffRange> ranges{{0,8},directory};
        constexpr unsigned sizes[]={0,1,1,2,4,8,1,1,2,4,8,4,8,4};
        for(const auto& [tag,e]:entries){
            (void)tag;
            if(e.type>=std::size(sizes)||!sizes[e.type])continue;
            const auto extent=std::uint64_t(sizes[e.type])*e.count;
            if(extent<=4)continue;
            const auto offset=file.u32(e.position);
            TiffRequire(Range(file.bytes,offset,extent),DecodeError::truncated);
            ranges.push_back({offset,extent});
        }
        return ranges;
    }
};
inline bool TiffOverlap(TiffRange a,TiffRange b){return a.begin<b.begin+b.size&&b.begin<a.begin+a.size;}
}
