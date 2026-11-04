#include "reader.hpp"
#include <algorithm>
#include <array>

namespace arken::image::v1::detail {
namespace {
struct Entry {
    std::uint32_t width=0,height=0,depth=0,offset=0,length=0;
};
void Put32(std::vector<std::byte>& bytes,std::size_t offset,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes[offset+i]=std::byte((value>>(i*8))&255);
}
DecodeResult Dib(Bytes bytes,const Entry& entry,const DecodeOptions& options) {
    if(bytes.size()<40)return Failure(DecodeError::truncated);
    if(U32(bytes,0)!=40)return Failure(DecodeError::unsupported_content);
    if(I32(bytes,4)!=static_cast<std::int32_t>(entry.width)
        ||I32(bytes,8)!=static_cast<std::int32_t>(entry.height*2))
        return Failure(DecodeError::invalid_dimensions,4);
    const auto bits=U16(bytes,14);
    if(U16(bytes,12)!=1)return Failure(DecodeError::invalid_layout,12);
    if(U32(bytes,16)!=0)return Failure(DecodeError::unsupported_compression,16);
    if(bits!=1&&bits!=4&&bits!=8&&bits!=24&&bits!=32)
        return Failure(DecodeError::unsupported_content,14);
    if(entry.depth&&entry.depth!=bits)return Failure(DecodeError::invalid_layout,14);
    const auto used=U32(bytes,32);
    if((bits<=8&&used>(1u<<bits))||(bits>8&&used))
        return Failure(DecodeError::invalid_palette,32);
    const std::uint64_t colors=bits<=8?(used?used:(1u<<bits)):0;
    const std::uint64_t pixelsOffset=40+colors*4;
    const std::uint64_t stride=((std::uint64_t(entry.width)*bits+31)/32)*4;
    const std::uint64_t pixelBytes=stride*entry.height;
    const std::uint64_t maskStride=((entry.width+31)/32)*4;
    const std::uint64_t maskOffset=pixelsOffset+pixelBytes;
    const std::uint64_t maskBytes=maskStride*entry.height;
    if(!Range(bytes,pixelsOffset,pixelBytes)||!Range(bytes,maskOffset,maskBytes))
        return Failure(DecodeError::truncated,maskOffset);
    if(maskOffset+maskBytes!=bytes.size())return Failure(DecodeError::trailing_bytes,maskOffset+maskBytes);

    // Normalize only the selected, bounded DIB to reuse the existing BMP decoder.
    std::vector<std::byte> bitmap(14+maskOffset);
    bitmap[0]=std::byte{'B'};bitmap[1]=std::byte{'M'};
    Put32(bitmap,2,static_cast<std::uint32_t>(bitmap.size()));
    Put32(bitmap,10,static_cast<std::uint32_t>(14+pixelsOffset));
    std::copy_n(bytes.begin(),maskOffset,bitmap.begin()+14);
    Put32(bitmap,22,entry.height);Put32(bitmap,34,static_cast<std::uint32_t>(pixelBytes));
    auto result=DecodeRaster(Format::bmp,bitmap,options);
    if(!result){result.issue.byte_offset=result.issue.byte_offset>=14?result.issue.byte_offset-14:0;return result;}
    if(result.image.width!=entry.width||result.image.height!=entry.height)
        return Failure(DecodeError::invalid_dimensions);
    bool hasAlpha=false;
    if(bits==32)for(std::uint32_t y=0;y<entry.height;++y)for(std::uint32_t x=0;x<entry.width;++x)
        hasAlpha=hasAlpha||U8(bytes,pixelsOffset+y*stride+x*4+3)!=0;
    if(hasAlpha)return result;
    for(std::uint32_t y=0;y<entry.height;++y)for(std::uint32_t x=0;x<entry.width;++x){
        const auto mask=U8(bytes,maskOffset+(entry.height-1-y)*maskStride+x/8)&(0x80u>>(x%8));
        // Every AND-masked pixel is fully transparent. A nonzero XOR colour
        // under the mask (a screen-inverting cursor pixel) depends on the
        // destination; RGBA has no such operation, and the colour under a
        // zero alpha is irrelevant to RGBA consumers, so it is kept as decoded.
        result.image.rgba8[(std::size_t(y)*entry.width+x)*4+3]=mask?0:255;
    }
    return result;
}
}
// ICO (type 1) and CUR (type 2) share the directory. A cursor entry stores its
// hotspot where an icon stores planes/bit count; the hotspot is not image data
// and is ignored. Renditions are ordered largest area first, then deeper bit
// count, then directory order: frameIndex 0 is the preferred (largest)
// rendition and frameCount reports how many renditions the file holds.
DecodeResult DecodeIco(Bytes bytes,const DecodeOptions& options) {
    if(options.directionIndex||options.mipIndex)
        return Failure(DecodeError::selection_out_of_range);
    if(bytes.size()<6)return Failure(DecodeError::truncated);
    const auto type=U16(bytes,2);
    if(U16(bytes,0)!=0||(type!=1&&type!=2))return Failure(DecodeError::wrong_magic);
    const bool cursor=type==2;
    const auto count=U16(bytes,4);
    if(!count)return Failure(DecodeError::invalid_layout,4);
    if(count>256)return Failure(DecodeError::resource_limit,4);
    const std::uint64_t directoryEnd=6+std::uint64_t(count)*16;
    if(!Range(bytes,6,directoryEnd-6))return Failure(DecodeError::truncated,6);
    std::vector<Entry> entries;entries.reserve(count);
    for(unsigned i=0;i<count;++i){
        const auto p=6+std::size_t(i)*16;
        if(U8(bytes,p+3)||(!cursor&&U16(bytes,p+4)>1))return Failure(DecodeError::invalid_layout,p+3);
        Entry entry{U8(bytes,p)?U8(bytes,p):256u,U8(bytes,p+1)?U8(bytes,p+1):256u,
            cursor?0u:U16(bytes,p+6),U32(bytes,p+12),U32(bytes,p+8)};
        if(!entry.length||entry.offset<directoryEnd||!Range(bytes,entry.offset,entry.length))
            return Failure(DecodeError::invalid_layout,p+8);
        entries.push_back(entry);
    }
    std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){
        const auto left=a.width*a.height,right=b.width*b.height;
        return left!=right?left>right:a.depth>b.depth;
    });
    if(options.frameIndex>=entries.size())return Failure(DecodeError::selection_out_of_range);
    const auto& selected=entries[options.frameIndex];
    if(!Dimensions(selected.width,selected.height,options))return Failure(DecodeError::invalid_dimensions,selected.offset);
    const auto payload=bytes.subspan(selected.offset,selected.length);
    constexpr std::array signature{std::byte{137},std::byte{'P'},std::byte{'N'},std::byte{'G'},
        std::byte{13},std::byte{10},std::byte{26},std::byte{10}};
    const bool png=payload.size()>=signature.size()&&std::equal(signature.begin(),signature.end(),payload.begin());
    auto bounded=options;
    bounded.frameIndex=0;
    bounded.maximumPixels=std::uint64_t(selected.width)*selected.height;
    bounded.maximumDimension=std::max(selected.width,selected.height);
    auto result=png?DecodeRaster(Format::png,payload,bounded):Dib(payload,selected,bounded);
    if(!result){result.issue.byte_offset+=selected.offset;return result;}
    if(result.image.width!=selected.width||result.image.height!=selected.height)
        return Failure(DecodeError::invalid_dimensions,selected.offset);
    result.image.frameCount=count;
    return result;
}
}
