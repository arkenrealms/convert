#include "reader.hpp"

namespace arken::image::v1::detail {
namespace {
void Put32(std::vector<std::byte>& b,std::size_t p,std::uint32_t value){for(unsigned c=0;c<4;++c)b[p+c]=std::byte(value>>(c*8));}
DecodeResult SelectBlp2(Bytes b,const DecodeOptions& o){
    auto decoded=DecodeBlp2(b,o.maximumSourceBytes);
    if(!decoded)return Failure(decoded.issue.code,decoded.issue.byte_offset);
    if(o.mipIndex>=decoded.image.mips.size())return Failure(DecodeError::selection_out_of_range);
    auto& mip=decoded.image.mips[o.mipIndex];
    if(!Dimensions(mip.width,mip.height,o))return Failure(DecodeError::invalid_dimensions);
    DecodeResult result;result.image.width=mip.width;result.image.height=mip.height;
    result.image.mipCount=static_cast<std::uint32_t>(decoded.image.mips.size());
    result.image.rgba8=std::move(mip.rgba8);return result;
}
}
DecodeResult DecodeBlp(Bytes b,const DecodeOptions& o){
    if(b.size()<4)return Failure(DecodeError::truncated);
    if(U32(b,0)==0x32504c42)return SelectBlp2(b,o);
    if(U32(b,0)!=0x31504c42)return Failure(DecodeError::wrong_magic);
    if(b.size()<156)return Failure(DecodeError::truncated);
    if(!Dimensions(U32(b,12),U32(b,16),o))return Failure(DecodeError::invalid_dimensions,12);
    const auto compression=U32(b,4),alpha=U32(b,8);
    if(alpha!=0&&alpha!=1&&alpha!=4&&alpha!=8)return Failure(DecodeError::unsupported_alpha_depth,8);
    if(compression==1){
        if(b.size()<1180)return Failure(DecodeError::truncated);
        // BLP1 palette/channel data has the same wire representation as BLP2.
        // Normalize only the container header, then use the one shared decoder.
        std::vector<std::byte> normalized(b.size()-8);
        std::copy(b.begin()+156,b.end(),normalized.begin()+148);
        Put32(normalized,0,0x32504c42);Put32(normalized,4,1);
        normalized[8]=std::byte{1};normalized[9]=std::byte(alpha);
        normalized[11]=std::byte(U32(b,32)!=0);
        Put32(normalized,12,U32(b,12));Put32(normalized,16,U32(b,16));
        for(unsigned i=0;i<16;++i){
            const auto offset=U32(b,28+i*4),size=U32(b,92+i*4);
            if(offset&&offset<1180)return Failure(DecodeError::invalid_layout,28+i*4);
            Put32(normalized,20+i*4,offset?offset-8:0);Put32(normalized,84+i*4,size);
        }
        return SelectBlp2(normalized,o);
    }
    if(compression!=0)return Failure(DecodeError::unsupported_compression,4);
    if(b.size()<160)return Failure(DecodeError::truncated);
    const auto shared=U32(b,156);
    if(!Range(b,160,shared))return Failure(DecodeError::truncated,160);
    if(o.mipIndex>=16)return Failure(DecodeError::selection_out_of_range);
    const auto offset=U32(b,28+o.mipIndex*4),size=U32(b,92+o.mipIndex*4);
    if(!offset||!size)return Failure(DecodeError::selection_out_of_range);
    if(offset<160ull+shared||!Range(b,offset,size)||std::uint64_t(shared)+size>o.maximumSourceBytes)
        return Failure(DecodeError::invalid_layout,offset);
    DecodeResult result;result.image.width=std::max(1u,U32(b,12)>>o.mipIndex);
    result.image.height=std::max(1u,U32(b,16)>>o.mipIndex);
    auto& jpeg=result.image.embeddedJpeg;jpeg.reserve(std::size_t(shared)+size);
    jpeg.insert(jpeg.end(),b.begin()+160,b.begin()+160+shared);jpeg.insert(jpeg.end(),b.begin()+offset,b.begin()+offset+size);
    // JPEG-in-BLP can contain a nonstandard fourth component carrying alpha.
    // Browser CMYK conversion would silently corrupt that data: keep that
    // variant fail-closed until a component-preserving decoder is available.
    if(jpeg.size()<4||U8(jpeg,0)!=255||U8(jpeg,1)!=216)return Failure(DecodeError::invalid_layout,160);
    bool frameFound=false;
    for(std::size_t p=2;p+4<=jpeg.size();){
        if(U8(jpeg,p++)!=255)return Failure(DecodeError::invalid_layout,p-1);
        while(p<jpeg.size()&&U8(jpeg,p)==255)++p;
        if(p>=jpeg.size())return Failure(DecodeError::truncated,p);
        const auto marker=U8(jpeg,p++);
        if(marker==0xd9||marker==0xda)break;
        if(marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
        if(p+2>jpeg.size())return Failure(DecodeError::truncated,p);
        const std::size_t length=(unsigned(U8(jpeg,p))<<8)|U8(jpeg,p+1);
        if(length<2||!Range(jpeg,p,length))return Failure(DecodeError::truncated,p);
        if(marker==0xc0||marker==0xc1||marker==0xc2){
            if(length<8)return Failure(DecodeError::truncated,p);
            const auto component_count=U8(jpeg,p+7);
            const unsigned h=(unsigned(U8(jpeg,p+3))<<8)|U8(jpeg,p+4),w=(unsigned(U8(jpeg,p+5))<<8)|U8(jpeg,p+6);
            if(w!=result.image.width||h!=result.image.height)return Failure(DecodeError::invalid_dimensions,p);
            if(component_count==4u){
                if(alpha!=0u&&alpha!=8u)return Failure(DecodeError::unsupported_alpha_depth,p+7);
                return DecodeBlpJpegWithAlpha(jpeg,o,result.image.width,result.image.height,alpha);
            }
            if(component_count!=3u||alpha!=0u)return Failure(DecodeError::unsupported_content,p);
            frameFound=true;break;
        }
        p+=length;
    }
    if(!frameFound)return Failure(DecodeError::unsupported_content);
    // Ordinary three-component BLP1 JPEGs are decoded by the capsule's
    // pinned memory-only JPEG owner. This keeps Paint's local RGBA contract
    // identical to standalone JPEG import and never invokes browser or remote
    // image services.
    auto decoded=DecodeRaster(Format::jpeg,jpeg,o);
    if(!decoded)return decoded;
    if(decoded.image.width!=result.image.width
        || decoded.image.height!=result.image.height)
    {
        return Failure(DecodeError::invalid_dimensions,160);
    }
    decoded.image.mipCount=1;
    return decoded;
}
}
