#include "reader.hpp"
#include <array>

namespace arken::image::v1::detail {
// Adapted from Oasis oasis_aotm_decode_tga_rgba_v1, without its process-global
// error store. Header, packet and origin semantics stay in this portable codec.
DecodeResult DecodeTga(Bytes bytes, const DecodeOptions& options) {
    if(bytes.size()<18) return Failure(DecodeError::truncated);
    if(options.mipIndex) return Failure(DecodeError::selection_out_of_range);
    const auto type=U8(bytes,2), depth=U8(bytes,16), descriptor=U8(bytes,17);
    const bool gray=type==3||type==11, rle=type==10||type==11;
    if(U8(bytes,1) || (!gray && type!=2 && type!=10) || (descriptor&0xc0))
        return Failure(DecodeError::unsupported_content,1);
    if((gray && depth!=8) || (!gray && depth!=24 && depth!=32))
        return Failure(DecodeError::unsupported_content,16);
    const auto width=U16(bytes,12), height=U16(bytes,14);
    if(!Dimensions(width,height,options)) return Failure(DecodeError::invalid_dimensions,12);
    const unsigned bytes_per_pixel=depth/8;
    const auto pixel_count=std::uint64_t(width)*height;
    std::uint64_t cursor=18+U8(bytes,0), pixel_index=0;
    if(cursor>bytes.size()) return Failure(DecodeError::truncated,cursor);
    DecodeResult result;
    auto& image=result.image; image.width=width; image.height=height;
    image.rgba8.resize(static_cast<std::size_t>(pixel_count)*4);
    while(pixel_index<pixel_count) {
        std::uint32_t run=1; bool repeated=false;
        if(rle) {
            if(cursor==bytes.size()) return Failure(DecodeError::truncated,cursor);
            const auto packet=U8(bytes,static_cast<std::size_t>(cursor++));
            repeated=(packet&0x80)!=0; run=(packet&0x7f)+1;
        }
        if(run>pixel_count-pixel_index) return Failure(DecodeError::invalid_run,cursor);
        const auto required=std::uint64_t(repeated?1:run)*bytes_per_pixel;
        if(!Range(bytes,cursor,required)) return Failure(DecodeError::truncated,cursor);
        for(std::uint32_t i=0;i<run;++i) {
            const auto at=static_cast<std::size_t>(cursor+(repeated?0:i*bytes_per_pixel));
            const auto x=static_cast<std::uint32_t>(pixel_index%width), y=static_cast<std::uint32_t>(pixel_index/width);
            const auto target_x=(descriptor&0x10)?width-1-x:x, target_y=(descriptor&0x20)?y:height-1-y;
            const auto target=(std::size_t(target_y)*width+target_x)*4;
            image.rgba8[target]=U8(bytes,at+(gray?0:2));
            image.rgba8[target+1]=U8(bytes,at+(gray?0:1));
            image.rgba8[target+2]=U8(bytes,at);
            image.rgba8[target+3]=bytes_per_pixel==4?U8(bytes,at+3):255;
            ++pixel_index;
        }
        cursor+=required;
    }
    return result;
}
}
