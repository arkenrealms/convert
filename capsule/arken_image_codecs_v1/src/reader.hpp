#pragma once
#include "arken_image_codecs_v1/api.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace arken::image::v1::detail {
using Bytes=std::span<const std::byte>;
inline std::uint8_t U8(Bytes b,std::size_t p){return std::to_integer<std::uint8_t>(b[p]);}
inline std::uint16_t U16(Bytes b,std::size_t p){return U8(b,p)|(std::uint16_t(U8(b,p+1))<<8);}
inline std::uint32_t U32(Bytes b,std::size_t p){return U16(b,p)|(std::uint32_t(U16(b,p+2))<<16);}
inline std::int32_t I32(Bytes b,std::size_t p){return std::bit_cast<std::int32_t>(U32(b,p));}
inline bool Range(Bytes b,std::uint64_t p,std::uint64_t n){return p<=b.size()&&n<=b.size()-p;}
inline DecodeResult Failure(DecodeError code,std::uint64_t offset=0){DecodeResult r;r.issue={code,offset};return r;}
inline bool Dimensions(std::uint32_t w,std::uint32_t h,const DecodeOptions& o){
    return w&&h&&w<=o.maximumDimension&&h<=o.maximumDimension&&std::uint64_t(w)*h<=o.maximumPixels;
}
inline void PalettePixel(Image& out,std::size_t pixel,Bytes palette,std::uint8_t index,bool bgr=false){
    const auto source=std::size_t(index)*3, target=pixel*4;
    out.rgba8[target]=U8(palette,source+(bgr?2:0));out.rgba8[target+1]=U8(palette,source+1);
    out.rgba8[target+2]=U8(palette,source+(bgr?0:2));out.rgba8[target+3]=255;
}
DecodeResult DecodePcx(Bytes,const DecodeOptions&);
DecodeResult DecodeCel(Bytes,const DecodeOptions&);
DecodeResult DecodeDc6(Bytes,const DecodeOptions&);
DecodeResult DecodeBlp(Bytes,const DecodeOptions&);
}
