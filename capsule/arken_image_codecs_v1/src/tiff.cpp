#include "tiff.hpp"
#include "tiff_directory.hpp"
#include <cstring>

namespace arken::image::v1::detail {
namespace {
void unpack(Bytes source,std::span<std::byte> output,std::size_t row_bytes){
    std::size_t input=0;
    for(std::size_t row=0;row<output.size();row+=row_bytes){
        std::size_t written=0;
        while(written<row_bytes){
            TiffRequire(input<source.size(),DecodeError::truncated);
            const auto control=U8(source,input++);
            if(control==128)continue;
            const std::size_t count=control<128?control+1:257-control;
            TiffRequire(count<=row_bytes-written,DecodeError::invalid_run);
            const std::size_t consumed=control<128?count:1;
            TiffRequire(Range(source,input,consumed),DecodeError::truncated);
            if(control<128)std::copy_n(source.begin()+input,count,output.begin()+row+written);
            else std::fill_n(output.begin()+row+written,count,source[input]);
            input+=consumed;written+=count;
        }
    }
    while(input<source.size())TiffRequire(U8(source,input++)==128,DecodeError::invalid_run);
}
DecodeResult decode(Bytes bytes,const DecodeOptions& options){
    if(options.frameIndex||options.directionIndex||options.mipIndex)return Failure(DecodeError::selection_out_of_range);
    const TiffDirectory f(bytes);
    TiffRequire(!f.next,DecodeError::unsupported_content);
    const auto width=f.scalar(256,0),height=f.scalar(257,0);
    TiffRequire(Dimensions(width,height,options),DecodeError::invalid_dimensions);
    const auto photo=f.scalar(262,UINT32_MAX),channels=f.scalar(277,1);
    const bool rgb=photo==2;
    TiffRequire((rgb&&(channels==3||channels==4))||((photo==0||photo==1)&&(channels==1||channels==2)),DecodeError::unsupported_content);
    const auto bits=f.values(258);
    TiffRequire(bits.size()==channels&&std::all_of(bits.begin(),bits.end(),[](auto n){return n==8;}),DecodeError::unsupported_content);
    TiffRequire(f.scalar(274,1)==1&&f.scalar(284,1)==1&&f.scalar(266,1)==1&&f.scalar(254,0)==0,DecodeError::unsupported_content);
    for(auto tag:{290,291,301,318,319,320,322,323,324,325,330,532,34675})
        TiffRequire(!f.entries.contains(tag),DecodeError::unsupported_content);
    if(f.entries.contains(339)){
        const auto samples=f.values(339);
        TiffRequire(samples.size()==channels&&std::all_of(samples.begin(),samples.end(),[](auto n){return n==1;}),DecodeError::unsupported_content);
    }
    const bool alpha=channels==(rgb?4u:2u);
    TiffRequire(alpha?f.scalar(338,0)==2:!f.entries.contains(338),DecodeError::unsupported_content);
    const auto compression=f.scalar(259,1),predictor=f.scalar(317,1);
    const bool deflate=compression==8||compression==32946;
    TiffRequire(compression==1||compression==32773||deflate,DecodeError::unsupported_compression);
    TiffRequire(predictor==1||(deflate&&predictor==2),DecodeError::unsupported_content);
    const auto rows=f.scalar(278,UINT32_MAX);
    TiffRequire(rows>0,DecodeError::invalid_layout);
    const auto strip_count=(std::uint64_t(height)+rows-1)/rows;
    const auto offsets=f.values(273),sizes=f.values(279);
    TiffRequire(offsets.size()==strip_count&&sizes.size()==strip_count,DecodeError::invalid_layout);
    const auto metadata=f.metadata();std::vector<TiffRange> strips;
    for(std::size_t i=0;i<offsets.size();++i){
        TiffRequire(sizes[i]&&Range(bytes,offsets[i],sizes[i]),DecodeError::truncated);
        const TiffRange range{offsets[i],sizes[i]};
        for(auto m:metadata)TiffRequire(!TiffOverlap(range,m),DecodeError::invalid_layout);
        strips.push_back(range);
    }
    std::sort(strips.begin(),strips.end(),[](auto a,auto b){return a.begin<b.begin;});
    for(std::size_t i=1;i<strips.size();++i)TiffRequire(!TiffOverlap(strips[i-1],strips[i]),DecodeError::invalid_layout);
    DecodeResult result;auto& image=result.image;image.width=width;image.height=height;
    image.rgba8.resize(std::size_t(width)*height*4);
    for(std::size_t s=0;s<offsets.size();++s){
        const auto y0=std::uint64_t(s)*rows,n=std::min<std::uint64_t>(rows,height-y0);
        const auto row_bytes=std::size_t(width)*channels;
        const auto extent=row_bytes*n;
        const auto encoded=bytes.subspan(offsets[s],sizes[s]);
        std::vector<std::byte> decoded;
        Bytes samples=encoded;
        if(compression==1)TiffRequire(encoded.size()==extent,DecodeError::invalid_layout);
        else {
            decoded.resize(extent);
            if(deflate)TiffRequire(InflateTiffStrip(encoded,decoded),DecodeError::invalid_layout);
            else unpack(encoded,decoded,row_bytes);
            samples=decoded;
        }
        if(predictor==2){
            for(std::size_t y=0;y<n;++y)for(std::size_t i=channels;i<row_bytes;++i){
                const auto p=y*row_bytes+i;
                decoded[p]=std::byte((U8(samples,p)+U8(samples,p-channels))&255u);
            }
        }
        for(std::size_t i=0;i<std::size_t(width)*n;++i){
            const auto in=i*channels,out=(std::size_t(y0)*width+i)*4;
            if(rgb){for(unsigned c=0;c<3;++c)image.rgba8[out+c]=U8(samples,in+c);}
            else {const auto gray=std::uint8_t(photo==0?255-U8(samples,in):U8(samples,in));
                std::fill_n(image.rgba8.begin()+out,3,gray);}
            image.rgba8[out+3]=alpha?U8(samples,in+channels-1):255;
        }
    }
    return result;
}
}
DecodeResult DecodeTiff(Bytes bytes,const DecodeOptions& options){
    try{return decode(bytes,options);}catch(DecodeError error){return Failure(error);}
}
EncodeResult EncodeTiff(std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> rgba,std::uint64_t maximum_bytes){
    constexpr std::uint32_t count=15,ifd_end=8+2+count*12+4,bits=ifd_end,xres=bits+8,yres=xres+8,pixels=yres+8;
    const auto total=std::uint64_t(pixels)+rgba.size();
    if(total>maximum_bytes||total>UINT32_MAX)return {EncodeError::output_limit,false,{}};
    std::vector<std::byte> bytes(static_cast<std::size_t>(total));
    const auto put=[&](std::size_t offset,std::uint32_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes[offset+i]=std::byte((value>>(8*i))&255u);};
    bytes[0]=bytes[1]=std::byte{73};put(2,42,2);put(4,8,4);put(8,count,2);
    std::size_t entry=10;
    const auto tag=[&](unsigned id,unsigned type,std::uint32_t length,std::uint32_t value){
        put(entry,id,2);put(entry+2,type,2);put(entry+4,length,4);put(entry+8,value,4);entry+=12;
    };
    tag(256,4,1,width);tag(257,4,1,height);tag(258,3,4,bits);tag(259,3,1,1);
    tag(262,3,1,2);tag(273,4,1,pixels);tag(274,3,1,1);tag(277,3,1,4);
    tag(278,4,1,height);tag(279,4,1,static_cast<std::uint32_t>(rgba.size()));
    tag(282,5,1,xres);tag(283,5,1,yres);tag(284,3,1,1);tag(296,3,1,1);tag(338,3,1,2);
    for(unsigned i=0;i<4;++i)put(bits+i*2,8,2);
    put(xres,1,4);put(xres+4,1,4);put(yres,1,4);put(yres+4,1,4);
    std::memcpy(bytes.data()+pixels,rgba.data(),rgba.size());
    return {EncodeError::none,false,std::move(bytes)};
}
}
