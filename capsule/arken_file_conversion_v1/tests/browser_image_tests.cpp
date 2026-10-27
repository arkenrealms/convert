#include "arken_file_conversion_v1/api.hpp"
#include "../src/image_preview.hpp"
#include "arken/digest/v1/digest.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace c=arken::file_conversion::v1;
namespace image=arken::image::v1;
namespace {
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::string Digest(std::span<const std::byte> bytes){
    auto digest=arken::digest::v1::sha256(bytes);Check(digest.has_value(),"digest failed");
    std::array<char,71> text{};Check(arken::digest::v1::format_sha256(*digest,text),"digest spelling failed");
    return {text.data(),text.size()};
}
void Write(const std::filesystem::path& path,std::span<const std::byte> bytes){
    std::ofstream out(path,std::ios::binary);Check(bool(out),"fixture output unavailable");
    out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    out.close();Check(bool(out),"fixture write failed");
}
image::DecodeOptions DecodeOptions(){
    image::DecodeOptions options;options.resourceProfile=image::RasterProfile::largeRaster;
    options.maximumPixels=c::browserMaximumPixels;options.maximumSourceBytes=c::browserMaximumOutputBytes;
    return options;
}
image::EncodeOptions EncodeOptions(){
    image::EncodeOptions options;options.resourceProfile=image::RasterProfile::largeRaster;
    options.maximumPixels=c::browserMaximumPixels;options.maximumOutputBytes=c::browserMaximumOutputBytes;
    return options;
}
// Independent 24-bit BMP fixture: real high-entropy pixels, no unused padding.
std::vector<std::byte> Bmp(unsigned width,unsigned height,std::span<const std::uint8_t> rgba){
    const std::size_t stride=(std::size_t(width)*3+3)&~std::size_t(3);
    std::vector<std::byte> bytes(54+stride*height);
    const auto le=[&](unsigned at,std::uint32_t value,unsigned count=4){for(unsigned i=0;i<count;++i)bytes[at+i]=std::byte(value>>(i*8));};
    bytes[0]=std::byte{'B'};bytes[1]=std::byte{'M'};le(2,bytes.size());le(10,54);le(14,40);
    le(18,width);le(22,height);le(26,1,2);le(28,24,2);le(34,stride*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto from=(std::size_t(y)*width+x)*4,to=54+(height-1-y)*stride+x*3;
        bytes[to]=std::byte(rgba[from+2]);bytes[to+1]=std::byte(rgba[from+1]);bytes[to+2]=std::byte(rgba[from]);
    }return bytes;
}
void NegativeCases(){
    std::array<std::uint8_t,4> pixel{17,42,93,127};
    const auto small=image::EncodeImage(image::Format::png,1,1,pixel);Check(bool(small),"small PNG failed");
    auto d=DecodeOptions();d.resourceProfile=static_cast<image::RasterProfile>(99);
    Check(image::DecodeImage(image::Format::png,small.bytes,d).issue.code==image::DecodeError::resource_limit,"unknown decode profile accepted");
    d=DecodeOptions();
    for(auto f:{image::Format::gif,image::Format::dds,image::Format::cel,image::Format::dc6,image::Format::blp,image::Format::pcx})
        Check(image::DecodeImage(f,small.bytes,d).issue.code==image::DecodeError::resource_limit,"large profile leaked to other decoder");
    Check(image::DecodeScalar16(image::ScalarFormat::png16,small.bytes,d).issue.code==image::DecodeError::resource_limit,"large profile leaked to scalar import");
    d.maximumPixels++;Check(image::DecodeImage(image::Format::png,small.bytes,d).issue.code==image::DecodeError::resource_limit,"decode pixel ceiling ignored");
    d=DecodeOptions();d.maximumDimension++;Check(image::DecodeImage(image::Format::png,small.bytes,d).issue.code==image::DecodeError::resource_limit,"decode dimension ceiling ignored");
    auto e=EncodeOptions();e.resourceProfile=static_cast<image::RasterProfile>(99);
    Check(image::EncodeImage(image::Format::png,1,1,pixel,e).error==image::EncodeError::invalid_options,"unknown encode profile accepted");
    e=EncodeOptions();e.maximumPixels++;Check(image::EncodeImage(image::Format::png,1,1,pixel,e).error==image::EncodeError::invalid_options,"encode pixel ceiling ignored");
    e=EncodeOptions();e.maximumOutputBytes++;Check(image::EncodeImage(image::Format::png,1,1,pixel,e).error==image::EncodeError::invalid_options,"encode byte ceiling ignored");
    e=EncodeOptions();Check(image::EncodeImage(image::Format::png,8192,4097,pixel,e).error==image::EncodeError::invalid_dimensions,"actual pixel overflow accepted");
    for(auto format:{image::Format::png,image::Format::bmp,image::Format::jpeg,image::Format::tga}){
        e=EncodeOptions();e.maximumOutputBytes=1;if(format==image::Format::jpeg)e.alphaPolicy=image::AlphaPolicy::flatten_white;
        const auto result=image::EncodeImage(format,1,1,pixel,e);
        Check(result.error==image::EncodeError::output_limit&&result.bytes.empty(),"partial output escaped");
    }
    Check(c::ConvertBrowserImage({},{}).error==c::Error::input_limit,"empty input accepted");
    Check(c::InspectBrowserImage({},image::Format::png).error==c::Error::input_limit,"empty inspection accepted");
    Check(c::InspectBrowserImage(small.bytes,image::Format::dds).error==c::Error::input_format_unavailable,"inspection claims DDS");
    const auto inspected=c::InspectBrowserImage(small.bytes,image::Format::png);
    Check(inspected&&inspected.width==1&&inspected.height==1&&inspected.bytes.empty()&&inspected.previewRgba8.empty(),"inspection reencoded source or lost dimensions");
    Check(c::ConvertBrowserImage(small.bytes,{image::Format::dds,image::Format::png}).error==c::Error::input_format_unavailable,"browser profile claims DDS");
    Check(c::ConvertBrowserImage(small.bytes,{image::Format::png,image::Format::jpeg}).encodeError==image::EncodeError::alpha_requires_matte,"implicit alpha loss");
    std::vector<std::byte> over(c::browserMaximumSourceBytes+1);
    const auto rejected=c::ConvertBrowserImage(over);Check(rejected.error==c::Error::input_limit&&rejected.bytes.empty()&&!rejected.width,"oversized input accepted or output leaked");
    const auto cloud=c::ConvertImage(small.bytes),browser=c::ConvertBrowserImage(small.bytes);
    Check(cloud&&browser&&cloud.bytes==browser.bytes,"profile changes small-image output");
}
void PreviewEdges(){
    image::Image decoded;decoded.width=8191;decoded.height=2049;
    decoded.rgba8.resize(std::size_t(decoded.width)*decoded.height*4,0);
    const auto put=[&](unsigned x,unsigned y,std::array<std::uint8_t,4> value){
        std::copy(value.begin(),value.end(),decoded.rgba8.begin()+(std::size_t(y)*decoded.width+x)*4);
    };
    put(0,0,{255,0,0,255});put(1,0,{0,255,0,0});
    put(0,1,{0,0,255,85});put(1,1,{0,255,0,0});
    put(8190,2048,{7,31,219,113});
    c::ImageResult result;c::detail::SetPreview(result,std::move(decoded));
    Check(result.previewWidth==4096&&result.previewHeight==1025,"odd preview extent rounded down");
    Check(result.previewRgba8[0]==191&&result.previewRgba8[1]==0&&result.previewRgba8[2]==64&&result.previewRgba8[3]==85,"transparent colors polluted preview");
    Check(std::all_of(result.previewRgba8.begin()+4,result.previewRgba8.begin()+8,[](auto v){return v==0;}),"transparent block retained color");
    Check(std::equal(result.previewRgba8.end()-4,result.previewRgba8.end(),std::array<std::uint8_t,4>{7,31,219,113}.begin()),"odd edge lost pixel or alpha");
}
void LargeCases(const std::filesystem::path& fixtures){
    constexpr unsigned width=8192,height=4096;
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);std::uint32_t seed=0x5a17d9u;
    for(std::size_t i=0;i<pixels.size();++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;pixels[i]=i%4==3?255:static_cast<std::uint8_t>(seed);}
    auto source=Bmp(width,height,pixels);
    const auto inspected=c::InspectBrowserImage(source,image::Format::bmp);
    Check(inspected&&inspected.width==width&&inspected.height==height&&inspected.bytes.empty(),"large inspection failed");
    Check(source.size()>50u*1024u*1024u&&source.size()<c::browserMaximumSourceBytes,"fixture does not cross old source limit");
    Check(c::ConvertImage(source,{image::Format::bmp,image::Format::png}).error==c::Error::input_limit,"hosted source limit widened");
    Check(!image::DecodeImage(image::Format::bmp,source),"default codec budget widened");
    if(!fixtures.empty())Write(fixtures/"large.bmp",source);
    std::cout<<"sourceBytes="<<source.size()<<" pixels="<<std::size_t(width)*height<<" sourceDigest="<<Digest(source)<<'\n';
    for(auto format:{image::Format::png,image::Format::jpeg,image::Format::bmp,image::Format::tga}){
        c::ImageOptions options{image::Format::bmp,format};if(format==image::Format::jpeg)options.alphaPolicy=image::AlphaPolicy::flatten_white;
        const auto start=std::chrono::steady_clock::now();
        auto result=c::ConvertBrowserImage(source,options);
        if(!result)throw std::runtime_error("large conversion failed: format="+std::to_string(unsigned(format))+" error="+std::to_string(unsigned(result.error))+" decode="+std::to_string(unsigned(result.decodeError))+" encode="+std::to_string(unsigned(result.encodeError)));
        Check(result.width==width&&result.height==height&&!result.alphaFlattened,"large dimensions/matte changed");
        if(format==image::Format::tga){
            Check(result.previewWidth==4096&&result.previewHeight==2048&&result.previewRgba8.size()==32u*1024*1024,"large image preview not bounded");
            for(unsigned c=0;c<4;++c){const auto sum=unsigned(pixels[c])+pixels[4+c]+pixels[width*4+c]+pixels[width*4+4+c];Check(result.previewRgba8[c]==(sum+2)/4,"preview box changed");}
        }else Check(result.previewRgba8.empty(),"unexpected preview pixels");
        const auto decoded=image::DecodeImage(format,result.bytes,DecodeOptions());
        if(!decoded)throw std::runtime_error("large output cannot reopen: format="+std::to_string(unsigned(format))+" bytes="+std::to_string(result.bytes.size())+" error="+std::to_string(unsigned(decoded.issue.code)));
        if(format==image::Format::jpeg){
            std::uint64_t error=0;unsigned maximum=0;
            for(std::size_t i=0;i<pixels.size();++i){const auto difference=unsigned(std::abs(int(pixels[i])-int(decoded.image.rgba8[i])));error+=difference;maximum=std::max(maximum,difference);}
            Check(double(error)/(std::size_t(width)*height*3)<8.0,"JPEG error exceeded quality95 tolerance");
            std::cout<<"jpegMeanAbsoluteError="<<double(error)/(std::size_t(width)*height*3)<<" maximumChannelError="<<maximum<<'\n';
        }else Check(decoded.image.rgba8==pixels,"large lossless image changed pixels");
        const char* extension=format==image::Format::png?"png":format==image::Format::jpeg?"jpg":format==image::Format::bmp?"bmp":"tga";
        if(!fixtures.empty())Write(fixtures/(std::string("expected.")+extension),result.bytes);
        std::cout<<"format="<<extension<<" outputBytes="<<result.bytes.size()<<" digest="<<Digest(result.bytes)
            <<" validationMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';
    }
    // A compressed image larger than the hosted pixel profile remains rejected
    // even when its encoded bytes fit the hosted source budget.
    std::vector<std::uint8_t> flat(1025u*1024u*4u,255);
    auto compressed=image::EncodeImage(image::Format::png,1025,1024,flat);
    Check(bool(compressed),"compressed cloud-boundary fixture failed");
    Check(c::ConvertImage(compressed.bytes).error==c::Error::decode_failed,"hosted pixel limit widened");
    auto bmp=source;bmp[18]=std::byte{1};bmp[19]=std::byte{32}; //8193 pixels
    const auto rejected=c::ConvertBrowserImage(bmp,{image::Format::bmp,image::Format::png});
    Check(rejected.error==c::Error::decode_failed&&rejected.bytes.empty(),"oversized dimension produced output");
}
}
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-browser-image-profile-tests-r6160\n";return 0;}
    try{
        std::filesystem::path fixtures;
        Check(argc==1||(argc==3&&std::string_view(argv[1])=="--fixtures"),"Usage: arken-browser-image-profile-tests [--fixtures NEW_ABSOLUTE_DIRECTORY]");
        if(argc==3){fixtures=argv[2];Check(fixtures.is_absolute(),"fixture root must be absolute");Check(std::filesystem::create_directory(fixtures),"fixture root must not exist");}
        NegativeCases();PreviewEdges();LargeCases(fixtures);std::cout<<"passed "<<checks<<" checks; cloud defaults unchanged\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
