#include "reader.hpp"

namespace arken::image::v1::detail {
// Format reference: Lectem/Worldstone's documented DC6 header/packet layout.
// No external implementation or game palette is linked or copied.
DecodeResult DecodeDc6(Bytes b,const DecodeOptions& o){
    if(b.size()<24)return Failure(DecodeError::truncated);
    if(U32(b,0)!=6)return Failure(DecodeError::wrong_magic);
    if(U32(b,8)!=0)return Failure(DecodeError::unsupported_content,8);
    DecodeResult r;auto& image=r.image;image.directionCount=U32(b,16);image.frameCount=U32(b,20);
    const std::uint64_t count=std::uint64_t(image.frameCount)*image.directionCount;
    if(!count||count>4096||!Range(b,24,count*4))return Failure(DecodeError::invalid_layout,16);
    if(o.frameIndex>=image.frameCount||o.directionIndex>=image.directionCount)return Failure(DecodeError::selection_out_of_range);
    const auto selected=std::uint64_t(o.directionIndex)*image.frameCount+o.frameIndex;
    std::uint32_t frame=0,length=0;std::size_t begin=0;
    for(std::uint64_t i=0;i<count;++i){
        const auto p=U32(b,24+i*4);
        if(p<24+count*4||!Range(b,p,32))return Failure(DecodeError::invalid_layout,24+i*4);
        const auto n=U32(b,p+28);
        if(!Range(b,std::uint64_t(p)+32,n))return Failure(DecodeError::truncated,p);
        if(i==selected){frame=p;length=n;begin=std::size_t(p)+32;}
    }
    image.width=U32(b,frame+4);image.height=U32(b,frame+8);
    image.offsetX=I32(b,frame+12);image.offsetY=I32(b,frame+16);
    if(!Dimensions(image.width,image.height,o))return Failure(DecodeError::invalid_dimensions,frame+4);
    const auto flip=U32(b,frame);if(flip>1)return Failure(DecodeError::unsupported_content,frame);
    if(o.palette.size()!=768){r.issue.code=o.palette.empty()?DecodeError::palette_required:DecodeError::invalid_palette;return r;}
    // D2:Resurrected uses exactly three leading zero pad bytes outside length.
    if(Range(b,begin,3)&&U8(b,begin)==0&&U8(b,begin+1)==0&&U8(b,begin+2)==0)begin+=3;
    if(!Range(b,begin,length))return Failure(DecodeError::truncated,begin);
    const auto end=begin+length;image.rgba8.assign(std::size_t(image.width)*image.height*4,0);
    std::uint32_t x=0,row=0;
    while(begin<end){
        const auto code=U8(b,begin++);
        if(row>=image.height)return Failure(DecodeError::invalid_run,begin-1);
        if(code==128){x=0;++row;continue;}
        const std::uint32_t count=code&127;
        if(!count||count>image.width-x)return Failure(DecodeError::invalid_run,begin-1);
        if(!(code&128)){
            if(count>end-begin)return Failure(DecodeError::truncated,begin);
            const auto y=flip?row:image.height-1-row;
            for(std::uint32_t i=0;i<count;++i)PalettePixel(image,std::size_t(y)*image.width+x+i,o.palette,U8(b,begin+i),true);
            begin+=count;
        }
        x+=count;
    }
    if(row!=image.height||x)return Failure(DecodeError::invalid_run,end);
    return r;
}
}
