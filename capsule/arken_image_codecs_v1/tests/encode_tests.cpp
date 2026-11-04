#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <future>
#include <filesystem>
#include <fstream>
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
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-image-encode-tests-r6353\n";return 0;}
    try{
        if(argc==3&&std::string_view(argv[1])=="--prepare-ico-output"){
            const std::filesystem::path root=argv[2];require(root.is_absolute()&&!std::filesystem::exists(root),"New absolute fixture directory required");std::filesystem::create_directories(root);
            const auto write=[&](const std::string& name,const std::vector<std::byte>& bytes){std::ofstream file(root/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();require(bool(file),"ICO fixture write failed");};
            for(const auto size:{32u,256u,257u}){
                const auto rgba=pixels(size,size);const auto name=std::string("icon-")+std::to_string(size);
                const auto png=image::EncodeImage(image::Format::png,size,size,rgba);require(bool(png),"ICO source fixture failed");write(name+".png",png.bytes);
                if(size<=image::maximumIconDimension){const auto ico=image::EncodeImage(image::Format::ico,size,size,rgba);require(bool(ico),"ICO reference failed");write(name+".ico",ico.bytes);}
            }
            std::cout<<"ICO output fixtures prepared\n";return 0;
        }
        require(argc==1,"Unknown image encoding test arguments");
        for(const auto format:{image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga})for(const auto extent:{std::pair{1u,1u},std::pair{3u,7u},std::pair{32u,17u},std::pair{129u,3u}})exercise(format,extent.first,extent.second);
        for(const auto extent:{std::pair{1u,1u},std::pair{3u,7u},std::pair{32u,17u},std::pair{256u,256u}}){
            exercise(image::Format::ico,extent.first,extent.second);
            const auto original=pixels(extent.first,extent.second),copy=original;
            const auto ico=image::EncodeImage(image::Format::ico,extent.first,extent.second,original);
            const auto png=image::EncodeImage(image::Format::png,extent.first,extent.second,original);
            auto word=[&](std::size_t at,unsigned count){std::uint32_t value=0;for(unsigned i=0;i<count;++i)value|=std::uint32_t(std::to_integer<unsigned>(ico.bytes.at(at+i)))<<(8*i);return value;};
            require(word(0,2)==0&&word(2,2)==1&&word(4,2)==1,"ICO directory header");
            require(word(6,1)==extent.first%256&&word(7,1)==extent.second%256&&word(8,2)==0&&word(10,2)==1&&word(12,2)==32,"ICO rendition metadata and256marker");
            require(word(14,4)==png.bytes.size()&&word(18,4)==22&&ico.bytes.size()==png.bytes.size()+22&&std::equal(png.bytes.begin(),png.bytes.end(),ico.bytes.begin()+22),"ICO contains exact PNG, no DIB or mask");
            image::EncodeOptions limit;limit.maximumOutputBytes=ico.bytes.size();
            require(image::EncodeImage(image::Format::ico,extent.first,extent.second,original,limit).bytes==ico.bytes,"Exact ICO output limit");
            --limit.maximumOutputBytes;const auto shortOutput=image::EncodeImage(image::Format::ico,extent.first,extent.second,original,limit);
            require(shortOutput.error==image::EncodeError::output_limit&&shortOutput.bytes.empty()&&original==copy,"ICO directory included in budget; immutable source");
        }
        const auto iconPixel=pixels(1,1);
        for(const auto extent:{std::pair{0u,1u},std::pair{257u,1u},std::pair{1u,257u}})
            require(image::EncodeImage(image::Format::ico,extent.first,extent.second,iconPixel).error==image::EncodeError::invalid_dimensions,"ICO cannot resize or exceed256");
        require(image::EncodeImage(image::Format::ico,1,1,{}).error==image::EncodeError::invalid_extent,"ICO extent validation");
        image::EncodeOptions iconOptions;iconOptions.resourceProfile=image::RasterProfile::largeRaster;
        require(image::EncodeImage(image::Format::ico,1,1,iconPixel,iconOptions).error==image::EncodeError::invalid_options,"ICO does not enlarge large-raster profile");
        iconOptions={};iconOptions.alphaPolicy=image::AlphaPolicy::flatten_white;
        require(!image::EncodeImage(image::Format::ico,1,1,iconPixel,iconOptions),"ICO cannot silently flatten alpha");
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
        std::cout<<"PNG/BMP/JPEG/TGA/ICO encoding, exact alpha/orientation, container fields, RLE boundaries, explicit matte, bounds and concurrent determinism passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
