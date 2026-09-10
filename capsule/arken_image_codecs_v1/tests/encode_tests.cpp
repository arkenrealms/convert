#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>

namespace image=arken::image::v1;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<std::uint8_t> pixels(unsigned w,unsigned h){
    std::vector<std::uint8_t> bytes(std::size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto at=(std::size_t(y)*w+x)*4;
        bytes[at]=std::uint8_t(x*7);bytes[at+1]=std::uint8_t(y*11);bytes[at+2]=91;bytes[at+3]=std::uint8_t((x+y)%3==0?0:(x+y)%3==1?128:255);}
    return bytes;
}
void exercise(image::Format format,unsigned w,unsigned h){
    const auto source=pixels(w,h);image::EncodeOptions options;
    if(format==image::Format::jpeg)options.alphaPolicy=image::AlphaPolicy::flatten_white;
    const auto encoded=image::EncodeImage(format,w,h,source,options);
    require(bool(encoded)&&!encoded.bytes.empty(),"Raster writer failed");
    require(image::EncodeImage(format,w,h,source,options).bytes==encoded.bytes,"Repeated encoding changed bytes");
    const auto decoded=image::DecodeImage(format,encoded.bytes);
    require(bool(decoded)&&decoded.image.width==w&&decoded.image.height==h,"Output did not decode with the actual format");
    if(format!=image::Format::jpeg){require(decoded.image.rgba8==source,"Lossless output must preserve all RGBA values and orientation");require(!encoded.alphaFlattened,"Lossless output reported matte");}
    else{require(encoded.alphaFlattened,"Transparent JPEG did not report matte");for(std::size_t i=3;i<decoded.image.rgba8.size();i+=4)require(decoded.image.rgba8[i]==255,"JPEG output is not opaque");}
    options.maximumOutputBytes=1;const auto limited=image::EncodeImage(format,w,h,source,options);
    require(limited.error==image::EncodeError::output_limit&&limited.bytes.empty(),"Failed output escaped the byte budget");
}
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-image-encode-tests-r4305\n";return 0;}
    try{
        for(const auto format:{image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga})for(const auto extent:{std::pair{1u,1u},std::pair{3u,7u},std::pair{32u,17u},std::pair{129u,3u}})exercise(format,extent.first,extent.second);
        // Independently inspect the TGA header and the packet boundary at128
        // pixels; a paired writer/reader round trip alone can hide a format bug.
        const std::vector<std::uint8_t> flat(129*4,91);
        const auto tga=image::EncodeImage(image::Format::tga,129,1,flat);
        require(bool(tga)&&tga.bytes.size()==28,"TGA repeated run must split into128+1 pixels");
        auto byte=[&](std::size_t at){return std::to_integer<unsigned>(tga.bytes.at(at));};
        require(byte(1)==0&&byte(2)==10&&byte(12)==129&&byte(13)==0&&byte(14)==1&&byte(15)==0&&byte(16)==32&&byte(17)==8,"TGA is32-bit truecolor RLE with8alpha bits and bottom origin");
        require(byte(18)==255&&byte(23)==0,"TGA packet boundary header mismatch");
        const auto flatDecoded=image::DecodeImage(image::Format::tga,tga.bytes);
        require(bool(flatDecoded)&&flatDecoded.image.rgba8==flat,"TGA run boundary lost alpha or pixels");
        auto truncated=tga.bytes;truncated.pop_back();
        require(!image::DecodeImage(image::Format::tga,truncated),"Truncated TGA output must be rejected");
        const auto one=pixels(1,1);
        require(!image::EncodeImage(image::Format::png,0,1,one),"Zero width accepted");
        require(!image::EncodeImage(image::Format::png,0xffffffffu,0xffffffffu,one),"Extent overflow accepted");
        require(!image::EncodeImage(image::Format::png,8193,1,one),"Dimension limit ignored");
        require(image::EncodeImage(image::Format::png,1,1,{}).error==image::EncodeError::invalid_extent,"Missing bytes accepted");
        require(image::EncodeImage(image::Format::dds,1,1,one).error==image::EncodeError::unsupported_format,"Unimplemented output accepted");
        require(image::EncodeImage(image::Format::jpeg,1,1,one).error==image::EncodeError::alpha_requires_matte,"JPEG silently discarded alpha");
        image::EncodeOptions options;options.jpegQuality=101;require(!image::EncodeImage(image::Format::jpeg,1,1,one,options),"Invalid quality accepted");
        options={};options.maximumPixels=0;require(!image::EncodeImage(image::Format::png,1,1,one,options),"Invalid budget accepted");
        options={};options.alphaPolicy=image::AlphaPolicy::flatten_white;
        require(!image::EncodeImage(image::Format::png,1,1,one,options),"Unexpected lossless-format matte accepted");
        std::vector<std::uint8_t> transparent(16*16*4,27);for(std::size_t i=3;i<transparent.size();i+=4)transparent[i]=0;
        const auto white=image::EncodeImage(image::Format::jpeg,16,16,transparent,options);
        const auto decoded=image::DecodeImage(image::Format::jpeg,white.bytes);require(bool(decoded),"White matte JPEG rejected");
        require(std::all_of(decoded.image.rgba8.begin(),decoded.image.rgba8.end(),[](auto v){return v>=254;}),"Transparent hidden RGB leaked through white matte");
        auto opaque=pixels(3,7);for(std::size_t i=3;i<opaque.size();i+=4)opaque[i]=255;
        require(bool(image::EncodeImage(image::Format::jpeg,3,7,opaque)),"Opaque JPEG unnecessarily requires matte");
        require(image::EncodePng(3,7,opaque).bytes==image::EncodeImage(image::Format::png,3,7,opaque).bytes,"Existing PNG API changed its actual writer");
        std::vector<std::future<void>> tasks;
        constexpr image::Format formats[]={image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga};
        for(unsigned i=0;i<12;++i)tasks.emplace_back(std::async(std::launch::async,[i,&formats]{exercise(formats[i%4],11+i,5+i);}));
        for(auto& task:tasks)task.get();
        std::cout<<"PNG/BMP/JPEG/TGA encoding, exact alpha/orientation, RLE boundaries, explicit matte, bounds and concurrent determinism passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
