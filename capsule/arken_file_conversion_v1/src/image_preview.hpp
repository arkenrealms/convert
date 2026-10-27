#pragma once
#include "../api.hpp"
#include <algorithm>

namespace arken::file_conversion::v1::detail {
// Presentation only. The full-resolution encoded result is never resampled.
inline void SetPreview(ImageResult& result,image::Image&& decoded) {
    constexpr std::uint64_t maximumBytes=64ull*1024*1024;
    std::uint32_t divisor=1;
    auto extent=[&](std::uint32_t value){return (value+divisor-1)/divisor;};
    while(std::uint64_t(extent(decoded.width))*extent(decoded.height)*4>maximumBytes)++divisor;
    result.previewWidth=extent(decoded.width);result.previewHeight=extent(decoded.height);
    if(divisor==1){result.previewRgba8=std::move(decoded.rgba8);return;}
    result.previewRgba8.resize(std::size_t(result.previewWidth)*result.previewHeight*4);
    for(std::uint32_t y=0;y<result.previewHeight;++y)for(std::uint32_t x=0;x<result.previewWidth;++x){
        std::uint64_t color[3]{},alpha=0,count=0;
        for(auto sy=y*divisor;sy<std::min(decoded.height,(y+1)*divisor);++sy)
            for(auto sx=x*divisor;sx<std::min(decoded.width,(x+1)*divisor);++sx){
                const auto at=(std::size_t(sy)*decoded.width+sx)*4;
                const auto a=decoded.rgba8[at+3];alpha+=a;++count;
                for(unsigned c=0;c<3;++c)color[c]+=std::uint64_t(decoded.rgba8[at+c])*a;
            }
        const auto at=(std::size_t(y)*result.previewWidth+x)*4;
        for(unsigned c=0;c<3;++c)result.previewRgba8[at+c]=alpha?static_cast<std::uint8_t>((color[c]+alpha/2)/alpha):0;
        result.previewRgba8[at+3]=static_cast<std::uint8_t>((alpha+count/2)/count);
    }
}
}
