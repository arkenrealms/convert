#include "detail.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace arken::document::v1::detail {
namespace {
constexpr std::uint32_t localSignature=0x04034b50,centralSignature=0x02014b50,endSignature=0x06054b50;
constexpr std::uint32_t zip64LocatorSignature=0x07064b50,descriptorSignature=0x08074b50;
constexpr std::uint16_t flagEncrypted=0x0001,flagDescriptor=0x0008,flagPatched=0x0020;
constexpr std::uint16_t flagStrongEncryption=0x0040,flagUtf8=0x0800,flagMaskedHeader=0x2000;
// Bits 1-2 are deflate tuning hints; every other undeclared bit is rejected.
constexpr std::uint16_t admittedFlags=0x0006|flagDescriptor|flagUtf8;

bool Range(Bytes b,std::uint64_t offset,std::uint64_t size) {return offset<=b.size()&&size<=b.size()-offset;}

void ValidateName(std::string_view name,const ZipLimits& limits) {
    if(name.empty()||name.size()>limits.maximumNameBytes)Fail(Error::zip_unsafe_name);
    if(!ValidUtf8(name))Fail(Error::zip_unsafe_name);
    for(const char c:name) {
        const auto u=std::uint8_t(c);
        if(u<0x20||u==0x7f||c=='\\')Fail(Error::zip_unsafe_name);
    }
    if(name.front()=='/')Fail(Error::zip_unsafe_name);
    if(name.size()>=2&&name[1]==':')Fail(Error::zip_unsafe_name); // Drive-qualified.
    std::size_t start=0;
    while(start<=name.size()) {
        auto end=name.find('/',start);
        if(end==std::string_view::npos)end=name.size();
        const auto segment=name.substr(start,end-start);
        const bool trailingDirectorySlash=segment.empty()&&end==name.size()&&start>0;
        if(!trailingDirectorySlash&&(segment.empty()||segment=="."||segment==".."))Fail(Error::zip_unsafe_name);
        start=end+1;
    }
}
void RejectZip64Extra(Bytes extra) {
    for(std::size_t p=0;p<extra.size();) {
        if(extra.size()-p<4)Fail(Error::zip_malformed);
        const auto id=U16(extra,p),size=U16(extra,p+2);
        if(extra.size()-p-4<size)Fail(Error::zip_malformed);
        if(id==0x0001)Fail(Error::zip_unsupported);
        if(id==0x9901)Fail(Error::zip_encrypted_entry); // WinZip AES.
        p+=4+std::size_t(size);
    }
}
}

std::uint32_t Crc32(std::string_view bytes) noexcept {
    static const auto table=[] {
        std::array<std::uint32_t,256> t{};
        for(std::uint32_t i=0;i<256;++i) {
            auto c=i;
            for(int k=0;k<8;++k)c=(c&1)?0xedb88320u^(c>>1):c>>1;
            t[i]=c;
        }
        return t;
    }();
    std::uint32_t c=0xffffffffu;
    for(const char b:bytes)c=table[(c^std::uint8_t(b))&0xff]^(c>>8);
    return ~c;
}

ZipArchive::ZipArchive(Bytes bytes,const ZipLimits& limits):bytes_(bytes),limits_(limits) {
    if(bytes.size()<22)Fail(Error::zip_not_archive);
    // The end record must close the file exactly (its comment reaches EOF); the
    // last such candidate wins. No search beyond the 64 KiB comment window.
    std::uint64_t end=0;bool found=false;
    const std::uint64_t lowest=bytes.size()>22+65535?bytes.size()-22-65535:0;
    for(std::uint64_t p=bytes.size()-22;;--p) {
        if(U32(bytes,p)==endSignature&&p+22+U16(bytes,p+20)==bytes.size()) {end=p;found=true;break;}
        if(p==lowest)break;
    }
    if(!found)Fail(Error::zip_not_archive);
    if(end>=20&&U32(bytes,end-20)==zip64LocatorSignature)Fail(Error::zip_unsupported);
    const auto disk=U16(bytes,end+4),centralDisk=U16(bytes,end+6);
    const auto diskEntries=U16(bytes,end+8),totalEntries=U16(bytes,end+10);
    const std::uint64_t centralSize=U32(bytes,end+12),centralOffset=U32(bytes,end+16);
    if(diskEntries==0xffff||totalEntries==0xffff||centralSize==0xffffffffu||centralOffset==0xffffffffu)Fail(Error::zip_unsupported);
    if(disk||centralDisk||diskEntries!=totalEntries)Fail(Error::zip_unsupported);
    if(totalEntries>limits.maximumEntries)Fail(Error::zip_entry_limit);
    // No prefixed data (self-extracting stubs) or gaps: the directory ends at the end record.
    if(centralOffset+centralSize!=end)Fail(Error::zip_malformed);
    std::set<std::string> seen;
    entries_.reserve(totalEntries);
    std::uint64_t p=centralOffset;
    for(std::uint32_t i=0;i<totalEntries;++i) {
        if(!Range(bytes,p,46)||p+46>end||U32(bytes,p)!=centralSignature)Fail(Error::zip_malformed);
        ZipEntry e;
        e.flags=U16(bytes,p+8);e.method=U16(bytes,p+10);e.crc=U32(bytes,p+16);
        e.compressed=U32(bytes,p+20);e.expanded=U32(bytes,p+24);
        const std::uint64_t nameLength=U16(bytes,p+28),extraLength=U16(bytes,p+30),commentLength=U16(bytes,p+32);
        const auto startDisk=U16(bytes,p+34);
        e.localOffset=U32(bytes,p+42);
        const auto record=46+nameLength+extraLength+commentLength;
        if(p+record>end)Fail(Error::zip_malformed);
        if(e.flags&(flagEncrypted|flagStrongEncryption|flagMaskedHeader)||e.method==99)Fail(Error::zip_encrypted_entry);
        if(e.flags&flagPatched)Fail(Error::zip_unsupported);
        if(e.flags&~admittedFlags)Fail(Error::zip_unsupported);
        if(e.compressed==0xffffffffu||e.expanded==0xffffffffu||e.localOffset==0xffffffffu||startDisk)Fail(Error::zip_unsupported);
        if(e.method!=0&&e.method!=8)Fail(Error::zip_unsupported);
        if(e.method==0&&e.compressed!=e.expanded)Fail(Error::zip_malformed);
        e.name.assign(reinterpret_cast<const char*>(bytes.data()+p+46),nameLength);
        ValidateName(e.name,limits);
        RejectZip64Extra(bytes.subspan(p+46+nameLength,extraLength));
        e.directory=e.name.back()=='/';
        if(e.directory&&e.expanded)Fail(Error::zip_malformed);
        auto key=e.name;
        if(key.back()=='/')key.pop_back();
        if(!seen.insert(AsciiLower(key)).second)Fail(Error::zip_duplicate_name);
        entries_.push_back(std::move(e));
        p+=record;
    }
    if(p!=end)Fail(Error::zip_malformed);
    // Local headers must agree with the directory, and member byte ranges
    // (header, data and any descriptor) must be disjoint: overlapping members
    // are the classic quine/bomb construction.
    std::vector<std::pair<std::uint64_t,std::uint64_t>> spans;
    spans.reserve(entries_.size());
    for(auto& e:entries_) {
        const auto o=e.localOffset;
        if(!Range(bytes,o,30)||o+30>centralOffset||U32(bytes,o)!=localSignature)Fail(Error::zip_malformed);
        const auto flags=U16(bytes,o+6),method=U16(bytes,o+8);
        const auto crc=U32(bytes,o+14);
        const std::uint64_t compressed=U32(bytes,o+18),expanded=U32(bytes,o+22);
        const std::uint64_t nameLength=U16(bytes,o+26),extraLength=U16(bytes,o+28);
        if(flags!=e.flags||method!=e.method)Fail(Error::zip_malformed);
        const bool descriptor=(e.flags&flagDescriptor)!=0;
        const auto agrees=[&](std::uint64_t local,std::uint64_t central) {return local==central||(descriptor&&local==0);};
        if(!agrees(crc,e.crc)||!agrees(compressed,e.compressed)||!agrees(expanded,e.expanded))Fail(Error::zip_malformed);
        if(compressed==0xffffffffu||expanded==0xffffffffu)Fail(Error::zip_unsupported);
        if(o+30+nameLength+extraLength>centralOffset)Fail(Error::zip_malformed);
        if(nameLength!=e.name.size()||std::memcmp(bytes.data()+o+30,e.name.data(),nameLength)!=0)Fail(Error::zip_malformed);
        RejectZip64Extra(bytes.subspan(o+30+nameLength,extraLength));
        e.dataOffset=o+30+nameLength+extraLength;
        if(e.compressed>centralOffset-e.dataOffset)Fail(Error::zip_malformed);
        auto spanEnd=e.dataOffset+e.compressed;
        if(descriptor) {
            // Optional signature, then CRC-32 and 32-bit sizes that must repeat the directory.
            auto q=spanEnd;
            if(Range(bytes,q,4)&&q+4<=centralOffset&&U32(bytes,q)==descriptorSignature)q+=4;
            if(!Range(bytes,q,12)||q+12>centralOffset)Fail(Error::zip_malformed);
            if(U32(bytes,q)!=e.crc||U32(bytes,q+4)!=e.compressed||U32(bytes,q+8)!=e.expanded)Fail(Error::zip_malformed);
            spanEnd=q+12;
        }
        spans.emplace_back(o,spanEnd);
    }
    std::sort(spans.begin(),spans.end());
    for(std::size_t i=1;i<spans.size();++i)if(spans[i].first<spans[i-1].second)Fail(Error::zip_malformed);
}

const ZipEntry* ZipArchive::Find(std::string_view name) const {
    for(const auto& e:entries_)if(!e.directory&&EqualsNoCase(e.name,name))return &e;
    return nullptr;
}

std::string ZipArchive::Read(const ZipEntry& e) {
    if(e.directory)return {};
    if(e.compressed>limits_.maximumEntryCompressedBytes||e.expanded>limits_.maximumEntryExpandedBytes)Fail(Error::zip_size_limit);
    // Charge the declared size before expanding; expansion beyond it is rejected.
    if(e.expanded>limits_.maximumTotalExpandedBytes-expanded_)Fail(Error::zip_size_limit);
    expanded_+=e.expanded;
    const auto data=bytes_.subspan(e.dataOffset,e.compressed);
    std::string out;
    if(e.method==0) {
        out.assign(reinterpret_cast<const char*>(data.data()),data.size());
    } else {
        const auto status=InflateRaw(data,e.expanded,out);
        if(status==InflateStatus::overflow)Fail(Error::zip_size_limit);
        if(status!=InflateStatus::ok)Fail(Error::zip_corrupt_data);
    }
    if(Crc32(out)!=e.crc)Fail(Error::zip_crc_mismatch);
    return out;
}
}
