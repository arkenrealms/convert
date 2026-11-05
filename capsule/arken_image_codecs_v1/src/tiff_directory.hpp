#pragma once
#include "reader.hpp"
#include <map>
#include <set>

namespace arken::image::v1::detail {
inline void TiffRequire(bool accepted,DecodeError error){if(!accepted)throw error;}
struct TiffBytes {
    Bytes bytes;
    bool little;
    std::uint16_t u16(std::uint64_t p)const {
        TiffRequire(Range(bytes,p,2),DecodeError::truncated);
        return little?U16(bytes,p):std::uint16_t((std::uint16_t(U8(bytes,p))<<8)|U8(bytes,p+1));
    }
    std::uint32_t u32(std::uint64_t p)const {
        TiffRequire(Range(bytes,p,4),DecodeError::truncated);
        return little?U32(bytes,p):(std::uint32_t(u16(p))<<16)|u16(p+2);
    }
    std::uint64_t u64(std::uint64_t p)const {
        TiffRequire(Range(bytes,p,8),DecodeError::truncated);
        return little?(std::uint64_t(u32(p+4))<<32)|u32(p):(std::uint64_t(u32(p))<<32)|u32(p+4);
    }
};
// Classic TIFF (42) uses 32-bit offsets and 12-byte entries; BigTIFF (43)
// uses 64-bit offsets/counts and 20-byte entries. Both byte orders.
struct TiffHeader {
    bool little=true,big=false;
    std::uint64_t first=0;
    static TiffHeader Parse(Bytes bytes){
        TiffRequire(bytes.size()>=8,DecodeError::truncated);
        TiffHeader h;h.little=U8(bytes,0)==73&&U8(bytes,1)==73;
        TiffRequire(h.little||(U8(bytes,0)==77&&U8(bytes,1)==77),DecodeError::wrong_magic);
        const TiffBytes file{bytes,h.little};
        const auto version=file.u16(2);
        TiffRequire(version==42||version==43,DecodeError::unsupported_content);
        h.big=version==43;
        if(h.big){
            TiffRequire(file.u16(4)==8&&file.u16(6)==0,DecodeError::unsupported_content);
            h.first=file.u64(8);TiffRequire(h.first>=16,DecodeError::invalid_layout);
        }else{h.first=file.u32(4);TiffRequire(h.first>=8,DecodeError::invalid_layout);}
        return h;
    }
};
// Aggregate work budget for one decode. Directory entries parsed, value
// elements read and metadata extents checked all draw from one counter, so the
// total directory work is bounded by the source size whatever the page chain
// looks like (overlapping directories, shared arrays). The per-byte allowance
// is several times what any honest layout needs (an entry is at least 12
// bytes, an array element at least 1); the fixed allowance covers small files;
// the ceiling sits above the most a well-formed decode can spend (4096 pages x
// 512 entries walked twice plus 2^19 offsets and counts read twice, ~6.3M) so
// it only clips adversarial input, keeping any decode's directory work near a
// second at worst.
struct TiffWork {
    static constexpr std::uint64_t perSourceByte=4,allowance=1ull<<20,ceiling=1ull<<24;
    std::uint64_t remaining;
    explicit TiffWork(std::uint64_t source_bytes)
        :remaining(source_bytes>=(ceiling-allowance)/perSourceByte?ceiling:allowance+perSourceByte*source_bytes) {}
    void charge(std::uint64_t units){TiffRequire(units<=remaining,DecodeError::resource_limit);remaining-=units;}
};
struct TiffEntry {std::uint16_t type;std::uint64_t count;std::uint64_t position;};
struct TiffRange {std::uint64_t begin,size;};
inline unsigned TiffTypeSize(unsigned type){
    constexpr unsigned sizes[]={0,1,1,2,4,8,1,1,2,4,8,4,8,4,0,0,8,8,8};
    return type<std::size(sizes)?sizes[type]:0;
}
struct TiffDirectory {
    static constexpr unsigned maximumEntries=512;
    TiffBytes file;
    bool big=false;
    std::map<std::uint16_t,TiffEntry> entries;
    std::uint64_t next=0;
    TiffRange directory{};
    std::uint64_t headerSize=8;
    TiffWork* work=nullptr;
    TiffDirectory(Bytes bytes,TiffWork& budget):TiffDirectory(bytes,TiffHeader::Parse(bytes),budget) {}
    TiffDirectory(Bytes bytes,const TiffHeader& h,TiffWork& budget):TiffDirectory(bytes,h,h.first,budget) {}
    TiffDirectory(Bytes bytes,const TiffHeader& h,std::uint64_t offset,TiffWork& budget)
        :file{bytes,h.little},big(h.big),headerSize(h.big?16:8),work(&budget) {
        TiffRequire(offset>=headerSize,DecodeError::invalid_layout);
        const auto count=big?file.u64(offset):file.u16(offset);
        TiffRequire(count&&count<=maximumEntries,DecodeError::resource_limit);
        work->charge(count);
        const unsigned entry=big?20:12,prefix=big?8:2,suffix=big?8:4;
        directory={offset,count*entry+prefix+suffix};
        TiffRequire(Range(bytes,directory.begin,directory.size),DecodeError::truncated);
        const auto last=offset+prefix+count*entry;
        next=big?file.u64(last):file.u32(last);
        for(std::uint64_t i=0;i<count;++i){const auto p=offset+prefix+i*entry;
            const TiffEntry e{file.u16(p+2),big?file.u64(p+4):file.u32(p+4),p+(big?12:8)};
            TiffRequire(entries.emplace(file.u16(p),e).second,DecodeError::invalid_layout);
        }
    }
    // The page chain: every IFD offset in file order, bounded and loop-checked.
    static std::vector<std::uint64_t> Chain(Bytes bytes,const TiffHeader& h,std::size_t limit,TiffWork& work){
        std::vector<std::uint64_t> pages;std::set<std::uint64_t> seen;
        for(auto offset=h.first;offset;){
            TiffRequire(seen.insert(offset).second,DecodeError::invalid_layout);
            TiffRequire(pages.size()<limit,DecodeError::resource_limit);
            work.charge(1);
            pages.push_back(offset);
            const TiffBytes file{bytes,h.little};
            const auto count=h.big?file.u64(offset):file.u16(offset);
            TiffRequire(count&&count<=maximumEntries,DecodeError::resource_limit);
            const auto last=offset+(h.big?8:2)+count*(h.big?20:12);
            offset=h.big?file.u64(last):file.u32(last);
        }
        return pages;
    }
    bool has(std::uint16_t tag)const {return entries.contains(tag);}
    const TiffEntry& entry(std::uint16_t tag)const {
        const auto it=entries.find(tag);
        TiffRequire(it!=entries.end(),DecodeError::invalid_layout);
        return it->second;
    }
    // Location of an entry's values: inline (4 bytes classic, 8 BigTIFF) or offset.
    std::uint64_t location(const TiffEntry& e,std::uint64_t extent)const {
        if(extent<=(big?8u:4u))return e.position;
        return big?file.u64(e.position):file.u32(e.position);
    }
    std::vector<std::uint64_t> values(std::uint16_t tag,std::uint64_t limit=8192)const {
        const auto& e=entry(tag);
        TiffRequire(e.type==1||e.type==3||e.type==4||e.type==16,DecodeError::unsupported_content);
        TiffRequire(e.count&&e.count<=limit,DecodeError::unsupported_content);
        work->charge(e.count);
        const unsigned size=TiffTypeSize(e.type);const auto extent=std::uint64_t(size)*e.count;
        const auto p=location(e,extent);
        TiffRequire(Range(file.bytes,p,extent),DecodeError::truncated);
        std::vector<std::uint64_t> result;result.reserve(std::size_t(e.count));
        for(std::uint64_t i=0;i<e.count;++i){const auto at=p+i*size;
            result.push_back(size==1?U8(file.bytes,at):size==2?file.u16(at):size==4?file.u32(at):file.u64(at));}
        return result;
    }
    std::uint32_t scalar(std::uint16_t tag,std::uint32_t fallback)const {
        if(!has(tag))return fallback;
        const auto v=values(tag);TiffRequire(v.size()==1,DecodeError::invalid_layout);
        TiffRequire(v.front()<=UINT32_MAX,DecodeError::invalid_layout);
        return std::uint32_t(v.front());
    }
    std::vector<double> rationals(std::uint16_t tag,std::uint64_t count)const {
        const auto& e=entry(tag);
        TiffRequire(e.type==5&&e.count==count,DecodeError::unsupported_content);
        work->charge(count);
        const auto p=location(e,8*count);
        std::vector<double> result;
        for(std::uint64_t i=0;i<count;++i){const auto d=file.u32(p+i*8+4);
            TiffRequire(d!=0,DecodeError::invalid_layout);result.push_back(double(file.u32(p+i*8))/d);}
        return result;
    }
    // BYTE/UNDEFINED payloads such as ICC profiles and JPEGTables.
    Bytes raw(std::uint16_t tag,std::uint64_t limit)const {
        const auto& e=entry(tag);
        TiffRequire(e.type==1||e.type==7,DecodeError::unsupported_content);
        TiffRequire(e.count,DecodeError::invalid_layout);
        TiffRequire(e.count<=limit,DecodeError::resource_limit);
        const auto p=location(e,e.count);
        TiffRequire(Range(file.bytes,p,e.count),DecodeError::truncated);
        return file.bytes.subspan(std::size_t(p),std::size_t(e.count));
    }
    std::vector<TiffRange> metadata()const {
        work->charge(entries.size());
        std::vector<TiffRange> ranges{{0,headerSize},directory};
        for(const auto& [tag,e]:entries){
            (void)tag;
            const auto size=TiffTypeSize(e.type);
            if(!size)continue;
            TiffRequire(e.count<=file.bytes.size(),DecodeError::truncated);
            const auto extent=std::uint64_t(size)*e.count;
            if(extent<=(big?8u:4u))continue;
            const auto offset=location(e,extent);
            TiffRequire(Range(file.bytes,offset,extent),DecodeError::truncated);
            ranges.push_back({offset,extent});
        }
        return ranges;
    }
};
inline bool TiffOverlap(TiffRange a,TiffRange b){return a.begin<b.begin+b.size&&b.begin<a.begin+a.size;}
}
