#include "reader.hpp"

#include <array>

namespace arken::image::v1::detail {
namespace {
bool FrameTable(Bytes b){
    if(b.size()<12)return false;
    const auto count=U32(b,0);
    if(!count||count>4096||!Range(b,4,(std::uint64_t(count)+1)*4))return false;
    if(U32(b,4)!=4*(count+2)||U32(b,4+count*4)!=b.size())return false;
    for(std::uint32_t i=0;i<count;++i)if(U32(b,4+i*4)>=U32(b,8+i*4))return false;
    return true;
}
}
DecodeResult DecodeCel(Bytes b,const DecodeOptions& o){
    if(b.size()<12)return Failure(DecodeError::truncated);
    std::uint32_t groups=1;
    if(!FrameTable(b)){
        const auto tableSize=U32(b,0);
        if(!tableSize||tableSize%4||tableSize/4>32||tableSize>=b.size())return Failure(DecodeError::invalid_layout);
        groups=tableSize/4;
        if(o.directionIndex>=groups)return Failure(DecodeError::selection_out_of_range);
        Bytes selected;
        for(std::uint32_t g=0;g<groups;++g){
            const auto begin=U32(b,g*4);
            const auto end=g+1<groups?std::size_t(U32(b,(g+1)*4)):b.size();
            if(begin<tableSize||begin>=end||end>b.size())return Failure(DecodeError::invalid_layout,g*4);
            const auto group=b.subspan(begin,end-begin);
            if(!FrameTable(group))return Failure(DecodeError::invalid_layout,begin);
            if(g==o.directionIndex)selected=group;
        }
        b=selected;
    }else if(o.directionIndex)return Failure(DecodeError::selection_out_of_range);
    const auto count=U32(b,0);
    if(!count||count>4096||!Range(b,4,(std::uint64_t(count)+1)*4))return Failure(DecodeError::invalid_layout);
    if(U32(b,4)!=4*(count+2)||U32(b,4+count*4)!=b.size())return Failure(DecodeError::unsupported_content);
    DecodeResult r;auto& image=r.image;image.frameCount=count;image.directionCount=groups;
    if(o.frameIndex>=count)return Failure(DecodeError::selection_out_of_range);
    for(std::uint32_t i=0;i<count;++i)if(U32(b,4+i*4)>=U32(b,8+i*4))return Failure(DecodeError::invalid_layout,4+i*4);
    auto begin=std::size_t(U32(b,4+o.frameIndex*4)),end=std::size_t(U32(b,8+o.frameIndex*4));
    const auto frameBegin=begin;
    std::array<std::uint16_t,5> tileOffsets{};
    bool tileSpecific=false;
    if(o.celFrameHeaderBytes!=0&&o.celFrameHeaderBytes!=10)return Failure(DecodeError::unsupported_content);
    if(o.celFrameHeaderBytes){
        if(end-begin<10||U16(b,begin)!=10)return Failure(DecodeError::invalid_layout,begin);
        for(unsigned i=0;i<tileOffsets.size();++i){
            const auto offset=U16(b,begin+i*2);tileOffsets[i]=offset;
            if(offset&&(offset<10||offset>end-frameBegin))return Failure(DecodeError::invalid_layout,begin+i*2);
        }
        // A one-block clipped frame has the same first word (10) but no
        // subsequent block offset; only a nonzero second offset identifies the
        // tile-specific table.
        tileSpecific=tileOffsets[0]==10&&tileOffsets[1]!=0;
        if(!tileSpecific){
            for(unsigned i=1;i<tileOffsets.size();++i)
                if(tileOffsets[i])return Failure(DecodeError::invalid_layout,begin+i*2);
        }
        bool gap=false;
        for(unsigned i=0;i<tileOffsets.size();++i){
            if(!tileOffsets[i]){gap=tileSpecific;continue;}
            if(tileSpecific&&(gap||(i&&tileOffsets[i]<=tileOffsets[i-1])))
                return Failure(DecodeError::invalid_layout,begin+i*2);
        }
        begin+=10;
    }
    if(!o.celWidth){r.issue.code=DecodeError::width_required;return r;}
    std::vector<std::pair<std::size_t,std::size_t>> segments;
    if(tileSpecific){
        for(unsigned i=0;i<tileOffsets.size()&&tileOffsets[i];++i){
            const auto segmentBegin=frameBegin+tileOffsets[i];
            const auto segmentEnd=(i+1<tileOffsets.size()&&tileOffsets[i+1])?frameBegin+tileOffsets[i+1]:end;
            if(segmentBegin>=segmentEnd||segmentEnd>end)return Failure(DecodeError::invalid_layout,segmentBegin);
            segments.emplace_back(segmentBegin,segmentEnd);
        }
        if(segments.empty())return Failure(DecodeError::invalid_layout,frameBegin);
    }else segments.emplace_back(begin,end);
    std::uint64_t pixelCount=0;
    for(std::size_t segmentIndex=0;segmentIndex<segments.size();++segmentIndex){
        auto [cursor,segmentEnd]=segments[segmentIndex];std::uint64_t segmentPixels=0;
        while(cursor<segmentEnd){
            const auto code=U8(b,cursor++);if(!code)return Failure(DecodeError::invalid_run,cursor-1);
            const auto n=code<128?code:256u-code;
            if(pixelCount>o.maximumPixels||n>o.maximumPixels-pixelCount)return Failure(DecodeError::resource_limit,cursor);
            if(code<128){if(n>segmentEnd-cursor)return Failure(DecodeError::truncated,cursor);cursor+=n;}
            pixelCount+=n;segmentPixels+=n;
            if(tileSpecific&&segmentPixels>std::uint64_t(o.celWidth)*32u)return Failure(DecodeError::invalid_dimensions,cursor);
        }
        if(tileSpecific&&segmentIndex+1<segments.size()&&segmentPixels!=std::uint64_t(o.celWidth)*32u)
            return Failure(DecodeError::invalid_dimensions,segments[segmentIndex].first);
    }
    image.width=o.celWidth;
    if(pixelCount%image.width||pixelCount/image.width>std::numeric_limits<std::uint32_t>::max())return Failure(DecodeError::invalid_dimensions);
    image.height=static_cast<std::uint32_t>(pixelCount/image.width);
    if(!Dimensions(image.width,image.height,o))return Failure(DecodeError::invalid_dimensions);
    if(o.palette.size()!=768){r.issue.code=o.palette.empty()?DecodeError::palette_required:DecodeError::invalid_palette;return r;}
    image.rgba8.assign(static_cast<std::size_t>(pixelCount)*4,0);
    std::size_t pixel=0;
    for(const auto& [segmentBegin,segmentEnd]:segments){
        auto cursor=segmentBegin;
        while(cursor<segmentEnd){
            const auto code=U8(b,cursor++);const auto n=code<128?code:256u-code;
            if(n>image.width-pixel%image.width)return Failure(DecodeError::invalid_run,cursor-1);
            if(code<128)for(unsigned i=0;i<n;++i){const auto logical=pixel+i;
                const auto position=std::size_t(image.height-1-logical/image.width)*image.width+logical%image.width;
                PalettePixel(image,position,o.palette,U8(b,cursor+i));}
            if(code<128)cursor+=n;pixel+=n;
        }
    }
    return r;
}
}
