#include "arken_image_codecs_v1/test_support/raster_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/scalar_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/icc_fixtures.hpp"
#include "arken_image_codecs_v1/encode.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace image=arken::image::v1;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-raster-codec-tests-r4566\n";return 0;}
    try{
        if(argc==4&&std::string_view(argv[1])=="--convert-png"){
            std::ifstream input(argv[2],std::ios::binary|std::ios::ate);const auto size=input.tellg();
            require(bool(input)&&size>0&&size<=50*1024*1024,"PNG input unavailable or too large");
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),size);
            require(bool(input),"PNG input read failed");const auto decoded=image::DecodeImage(image::Format::png,bytes);
            if(!decoded)throw std::runtime_error("PNG decode rejected: "+std::to_string(unsigned(decoded.issue.code)));
            require(std::filesystem::create_directory(argv[3]),"Output directory must be new");
            for(const auto& [name,format]:{std::pair{"expected.png",image::Format::png},std::pair{"expected.jpeg",image::Format::jpeg}}){
                image::EncodeOptions options;if(format==image::Format::jpeg)options.alphaPolicy=image::AlphaPolicy::flatten_white;
                const auto encoded=image::EncodeImage(format,decoded.image.width,decoded.image.height,decoded.image.rgba8,options);
                require(bool(encoded),"Output encode failed");
                std::ofstream output(std::filesystem::path(argv[3])/name,std::ios::binary);output.write(reinterpret_cast<const char*>(encoded.bytes.data()),encoded.bytes.size());require(bool(output),"Output write failed");
            }
            std::cout<<"Converted PNG to sRGB PNG and JPEG: "<<decoded.image.width<<'x'<<decoded.image.height<<'\n';return 0;
        }
        require(argc==1,"Usage: arken-raster-codec-tests [--convert-png INPUT NEW-OUTPUT]");
        const std::vector<std::uint8_t> grey{128,128,128,37,64,64,64,255};
        const auto plain=image::EncodePng(2,1,grey);require(bool(plain),"ICC fixture PNG encode");
        const auto icc=raster_fixture::IccPayload(raster_fixture::LinearRgbProfile());
        const auto tagged=raster_fixture::WithIcc(plain.bytes,icc);
        const auto managed=image::DecodeImage(image::Format::png,tagged);require(bool(managed),"Linear RGB ICC rejected");
        for(unsigned c=0;c<3;++c)require(std::abs(int(managed.image.rgba8[c])-188)<=1&&std::abs(int(managed.image.rgba8[4+c])-137)<=1,"ICC colour not converted to sRGB");
        require(managed.image.rgba8[3]==37&&managed.image.rgba8[7]==255,"ICC changed unassociated alpha");
        require(image::DecodeImage(image::Format::png,plain.bytes).image.rgba8==grey,"Untagged PNG changed");
        require(!image::DecodeImage(image::Format::png,raster_fixture::WithIcc(tagged,icc)),"Duplicate ICC accepted");
        auto damaged=icc;damaged.back()^=std::byte{1};require(!image::DecodeImage(image::Format::png,raster_fixture::WithIcc(plain.bytes,damaged)),"ICC Adler corruption accepted");
        auto trailing=icc;trailing.insert(trailing.end()-4,std::byte{0});require(!image::DecodeImage(image::Format::png,raster_fixture::WithIcc(plain.bytes,trailing)),"ICC trailing zlib data accepted");
        auto invalid=raster_fixture::LinearRgbProfile();raster_fixture::Be(invalid,136,0xfffffff0);require(!image::DecodeImage(image::Format::png,raster_fixture::WithIcc(plain.bytes,raster_fixture::IccPayload(invalid))),"ICC tag overrun accepted");
        require(!image::DecodeImage(image::Format::png,raster_fixture::WithIcc(plain.bytes,raster_fixture::IccPayload(std::vector<std::byte>(4*1024*1024+1)))),"ICC inflate bound ignored");
        require(bool(image::DecodeScalar16(image::ScalarFormat::png16,raster_fixture::WithIcc(raster_fixture::Png16(true),icc))),"ICC altered scalar PNG policy");
        const auto scalarPng=image::DecodeScalar16(image::ScalarFormat::png16,raster_fixture::Png16(true));
    require(scalarPng&&scalarPng.samples==std::vector<std::uint16_t>{0,32896,65535,257},"Scalar PNG lost low bits");
    const auto dataChromaticities=image::DecodeScalar16(image::ScalarFormat::png16,raster_fixture::WithChromaticities(raster_fixture::Png16(true),false));
    require(dataChromaticities&&dataChromaticities.samples==scalarPng.samples,"Scalar values remapped by colour metadata");
        require(!image::DecodeScalar16(image::ScalarFormat::png16,raster_fixture::Png16()),"Scalar import admitted RGBA");
        for(bool little:{false,true})for(bool predicted:{false,true}){
            auto bytes=scalar_fixture::Tiff16(little,predicted);
            const auto decoded=image::DecodeScalar16(image::ScalarFormat::tiff16,bytes);
            require(decoded&&decoded.width==3&&decoded.height==2&&decoded.samples==std::vector<std::uint16_t>{1,257,65535,32768,10,65534},"TIFF scalar endian/predictor mismatch");
            image::DecodeOptions limit;limit.maximumPixels=5;
            require(!image::DecodeScalar16(image::ScalarFormat::tiff16,bytes,limit),"Scalar import ignored pixel bound");
            bytes.pop_back();require(!image::DecodeScalar16(image::ScalarFormat::tiff16,bytes),"Scalar TIFF truncation accepted");
        }
        const auto highPrecision=raster_fixture::Png16();
        require(image::DecodeImage(image::Format::png,highPrecision).issue.code==image::DecodeError::unsupported_content,"PNG16 silently lost precision");
        image::DecodeOptions derivative;derivative.allowPng16BitRenderingDerivative=true;
        const auto reduced=image::DecodeImage(image::Format::png,highPrecision,derivative);
        require(reduced&&reduced.image.sourceBitsPerChannel==16&&reduced.image.rgba8==std::vector<std::uint8_t>{0,128,255,1},"Explicit PNG16 rendering conversion changed channel meaning");
        const auto pixels=raster_fixture::Pixels(32,16,true);auto encoded=image::EncodePng(32,16,pixels);
        require(bool(encoded),"production PNG encode failed");
        require(image::DecodeImage(image::Format::png,encoded.bytes).image.rgba8==pixels,"production PNG exact RGBA roundtrip");
        require(image::EncodePng(32,16,pixels).bytes==encoded.bytes,"production PNG is not deterministic");
        require(!image::EncodePng(32,15,pixels)&&!image::EncodePng(0,0,{})&&!image::EncodePng(8193,1,{}),"PNG encoder ignored bounds");
        for(auto format:{image::Format::png,image::Format::bmp,image::Format::jpeg}){
            auto bytes=raster_fixture::Encode(format,32,16);auto decoded=image::DecodeImage(format,bytes);
            require(bool(decoded)&&decoded.image.width==32&&decoded.image.height==16,"Raster decode failed");
            require(decoded.image.rgba8.size()==32*16*4,"Pixel extent");
            require(image::DecodeImage(format,bytes).image.rgba8==decoded.image.rgba8,"Nondeterministic decode");
            if(format!=image::Format::jpeg)require(decoded.image.rgba8==raster_fixture::Pixels(32,16),"PNG/BMP not lossless");
            auto wrong=image::DecodeImage(format==image::Format::png?image::Format::jpeg:image::Format::png,bytes);require(!wrong,"Format mismatch accepted");
            image::DecodeOptions options;options.maximumPixels=64;require(!image::DecodeImage(format,bytes,options),"Pixel limit ignored");
            options={};options.frameIndex=1;require(!image::DecodeImage(format,bytes,options),"Nonexistent frame accepted");
            bytes.pop_back();require(!image::DecodeImage(format,bytes),"Truncated image accepted");
        }
        const auto gif=raster_fixture::Gif2x1();
        const auto decodedGif=image::DecodeImage(image::Format::gif,gif);
        require(bool(decodedGif)&&decodedGif.image.width==2&&decodedGif.image.height==1
            &&decodedGif.image.rgba8==std::vector<std::uint8_t>{0,0,0,255,255,0,0,255},
            "GIF first frame did not decode through the local raster owner");
        auto truncatedGif=gif;truncatedGif.pop_back();
        require(image::DecodeImage(image::Format::gif,truncatedGif).issue.code==image::DecodeError::truncated,
            "Truncated GIF accepted");
        const auto alpha=image::DecodeImage(image::Format::png,raster_fixture::Encode(image::Format::png,32,16,true));
        require(bool(alpha)&&alpha.image.rgba8==raster_fixture::Pixels(32,16,true),"PNG alpha was premultiplied or discarded");
        require(bool(image::DecodeImage(image::Format::png,raster_fixture::WithChromaticities(raster_fixture::Encode(image::Format::png,32,16),true))),"sRGB PNG chromaticities rejected");
        require(!image::DecodeImage(image::Format::png,raster_fixture::WithChromaticities(raster_fixture::Encode(image::Format::png,32,16),false)),"Custom PNG primaries silently treated as sRGB");
        auto png=raster_fixture::Encode(image::Format::png,32,16);png[png.size()-1]^=std::byte{1};require(!image::DecodeImage(image::Format::png,png),"PNG CRC ignored");
        auto jpeg=raster_fixture::Encode(image::Format::jpeg,32,16);auto base=image::DecodeImage(image::Format::jpeg,jpeg);
        for(unsigned orientation=1;orientation<=8;++orientation){auto result=image::DecodeImage(image::Format::jpeg,raster_fixture::WithOrientation(jpeg,orientation));
            require(bool(result),"EXIF orientation rejected");require(result.image.width==(orientation>=5?16u:32u)&&result.image.height==(orientation>=5?32u:16u),"EXIF dimensions wrong");
            unsigned dx=0,dy=0;switch(orientation){case 2:dx=31;break;case 3:dx=31;dy=15;break;case 4:dy=15;break;case 6:dx=15;break;case 7:dx=15;dy=31;break;case 8:dy=31;break;default:break;}
            require(std::memcmp(result.image.rgba8.data()+(dy*result.image.width+dx)*4,base.image.rgba8.data(),4)==0,"EXIF pixel mapping wrong");
        }
        require(!image::DecodeImage(image::Format::jpeg,raster_fixture::WithOrientation(jpeg,9)),"Invalid EXIF accepted");
        auto calibrated=raster_fixture::Encode(image::Format::bmp,32,16);
        require(calibrated.size()>74,"BMP V4 fixture missing");
        for(unsigned i=70;i<74;++i)calibrated[i]=std::byte{0};calibrated[74]=std::byte{1};
        require(!image::DecodeImage(image::Format::bmp,calibrated),"Calibrated BMP silently treated as sRGB");
        std::cout<<"PNG/BMP/JPEG and8 EXIF orientations; exact pixels, limits, corruption and selection checks passed\n";return 0;
    }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
