#include "reader.hpp"
#include <map>
#include <stdexcept>

namespace arken::image::v1::detail {
namespace {
void require(bool value,DecodeError error){if(!value)throw error;}
struct Tiff {
    Bytes bytes;bool little;
    std::uint16_t u16(std::size_t p)const{
        require(Range(bytes,p,2),DecodeError::truncated);
        return little?U16(bytes,p):std::uint16_t((std::uint16_t(U8(bytes,p))<<8)|U8(bytes,p+1));
    }
    std::uint32_t u32(std::size_t p)const{
        require(Range(bytes,p,4),DecodeError::truncated);
        return little?U32(bytes,p):(std::uint32_t(u16(p))<<16)|u16(p+2);
    }
};
struct Entry {std::uint16_t type;std::uint32_t count;std::size_t position;};
}
ScalarDecodeResult DecodeScalarTiff16(Bytes bytes,const DecodeOptions& options){
    require(bytes.size()>=8,DecodeError::truncated);
    const bool little=U8(bytes,0)==73&&U8(bytes,1)==73,big=U8(bytes,0)==77&&U8(bytes,1)==77;
    require(little||big,DecodeError::wrong_magic);Tiff f{bytes,little};
    require(f.u16(2)==42,DecodeError::unsupported_content);
    const auto directory=f.u32(4);require(directory>=8,DecodeError::invalid_layout);
    const auto count=f.u16(directory);require(count&&count<=256,DecodeError::resource_limit);
    require(Range(bytes,std::uint64_t(directory)+2,std::uint64_t(count)*12+4),DecodeError::truncated);
    require(f.u32(std::size_t(directory)+2+count*12)==0,DecodeError::unsupported_content);
    std::map<std::uint16_t,Entry> entries;
    for(unsigned i=0;i<count;++i){const auto p=std::size_t(directory)+2+i*12;
        require(entries.emplace(f.u16(p),Entry{f.u16(p+2),f.u32(p+4),p+8}).second,DecodeError::invalid_layout);}
    const auto values=[&](unsigned tag){
        const auto it=entries.find(tag);require(it!=entries.end(),DecodeError::invalid_layout);
        const auto e=it->second;require((e.type==3||e.type==4)&&e.count>0&&e.count<=8192,DecodeError::unsupported_content);
        const std::size_t size=e.type==3?2:4,extent=size*e.count;
        const std::size_t p=extent<=4?e.position:f.u32(e.position);require(Range(bytes,p,extent),DecodeError::truncated);
        std::vector<std::uint32_t> out;out.reserve(e.count);
        for(unsigned i=0;i<e.count;++i)out.push_back(size==2?f.u16(p+i*size):f.u32(p+i*size));return out;
    };
    const auto scalar=[&](unsigned tag,std::uint32_t fallback){
        if(!entries.contains(tag))return fallback;auto v=values(tag);
        require(v.size()==1,DecodeError::invalid_layout);return v.front();
    };
    const auto width=scalar(256,0),height=scalar(257,0);
    require(Dimensions(width,height,options),DecodeError::invalid_dimensions);
    const auto compression=scalar(259,1),predictor=scalar(317,1);
    require(scalar(258,0)==16&&scalar(262,0)==1&&scalar(277,1)==1&&scalar(284,1)==1
        &&scalar(274,1)==1&&scalar(339,1)==1&&scalar(266,1)==1&&scalar(254,0)==0
        &&!entries.contains(338)&&!entries.contains(322)&&!entries.contains(324)
        &&!entries.contains(330),DecodeError::unsupported_content);
    require((compression==1||compression==8||compression==32946)&&(predictor==1||predictor==2),DecodeError::unsupported_content);
    const auto rows=scalar(278,height);require(rows>0,DecodeError::invalid_layout);
    const auto strips=(std::uint64_t(height)+rows-1)/rows;
    auto offsets=values(273),sizes=values(279);
    require(offsets.size()==strips&&sizes.size()==strips,DecodeError::invalid_layout);
    ScalarDecodeResult out;out.width=width;out.height=height;out.samples.resize(std::size_t(width)*height);
    for(std::size_t s=0;s<strips;++s){const auto y0=std::uint64_t(s)*rows;
        const auto n=std::min<std::uint64_t>(rows,height-y0),extent=std::uint64_t(width)*n*2;
        require(sizes[s]&&Range(bytes,offsets[s],sizes[s]),DecodeError::truncated);
        const auto encoded=bytes.subspan(offsets[s],sizes[s]);std::vector<std::byte> inflated;
        Bytes data=encoded;
        if(compression!=1){inflated.resize(extent);require(InflateScalarStrip(encoded,inflated),DecodeError::invalid_layout);data=inflated;}
        else require(encoded.size()==extent,DecodeError::invalid_layout);
        Tiff pixels{data,little};
        for(std::size_t y=0;y<n;++y){std::uint16_t previous=0;
            for(std::size_t x=0;x<width;++x){auto sample=pixels.u16((y*width+x)*2);
                if(predictor==2)sample=std::uint16_t(sample+previous);previous=sample;
                out.samples[(y0+y)*width+x]=sample;}}
    }
    return out;
}
}
namespace arken::image::v1 {
ScalarDecodeResult DecodeScalar16(ScalarFormat format,std::span<const std::byte> bytes,const DecodeOptions& options)noexcept{
    const auto fail=[](DecodeError e){ScalarDecodeResult r;r.issue={e,0};return r;};
    try{
        if(bytes.empty())return fail(DecodeError::truncated);
        if(bytes.size()>options.maximumSourceBytes||bytes.size()>std::size_t(std::numeric_limits<int>::max()))return fail(DecodeError::input_too_large);
        if(options.maximumDimension>8192||options.maximumPixels>8ull*1024*1024)return fail(DecodeError::resource_limit);
        if(options.frameIndex||options.directionIndex||options.mipIndex)return fail(DecodeError::selection_out_of_range);
        switch(format){case ScalarFormat::png16:return detail::DecodeScalarPng16(bytes,options);
          case ScalarFormat::tiff16:return detail::DecodeScalarTiff16(bytes,options);}
        return fail(DecodeError::unsupported_content);
    }catch(DecodeError error){return fail(error);}catch(...){return fail(DecodeError::resource_limit);}
}
}
