#include "tiff_directory.hpp"

namespace arken::image::v1::detail {
namespace {
void require(bool value,DecodeError error){if(!value)throw error;}
}
ScalarDecodeResult DecodeScalarTiff16(Bytes bytes,const DecodeOptions& options){
    const TiffDirectory f(bytes);const auto& entries=f.entries;
    const bool little=f.file.little;
    require(!f.next,DecodeError::unsupported_content);
    const auto values=[&](unsigned tag){return f.values(tag);};
    const auto scalar=[&](unsigned tag,std::uint32_t fallback){return f.scalar(tag,fallback);};
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
        if(compression!=1){inflated.resize(extent);require(InflateTiffStrip(encoded,inflated),DecodeError::invalid_layout);data=inflated;}
        else require(encoded.size()==extent,DecodeError::invalid_layout);
        TiffBytes pixels{data,little};
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
        if(options.resourceProfile!=RasterProfile::bounded||options.maximumDimension>8192||options.maximumPixels>8ull*1024*1024)return fail(DecodeError::resource_limit);
        if(options.frameIndex||options.directionIndex||options.mipIndex)return fail(DecodeError::selection_out_of_range);
        switch(format){case ScalarFormat::png16:return detail::DecodeScalarPng16(bytes,options);
          case ScalarFormat::tiff16:return detail::DecodeScalarTiff16(bytes,options);}
        return fail(DecodeError::unsupported_content);
    }catch(DecodeError error){return fail(error);}catch(...){return fail(DecodeError::resource_limit);}
}
}
