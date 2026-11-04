#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace I=arken::image::v1;
using Bytes=std::vector<std::byte>;
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
std::vector<std::uint8_t> Pixels(unsigned w,unsigned h) {
    std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const auto p=(std::size_t(y)*w+x)*4;
        out[p]=std::uint8_t(x*31+y*7);out[p+1]=std::uint8_t(x*13+y*17);
        out[p+2]=std::uint8_t(x*3+y*29);out[p+3]=std::uint8_t((x+y)%5?255-(x%3)*80:0);
    }
    return out;
}
void Put(Bytes& b,std::size_t p,std::uint32_t n,unsigned count=4){for(unsigned i=0;i<count;++i)b.at(p+i)=std::byte((n>>(8*i))&255);}
void Tag(Bytes& b,std::size_t p,const char* s){for(unsigned i=0;i<4;++i)b.at(p+i)=std::byte(s[i]);}
Bytes Extended(const Bytes& base,unsigned w,unsigned h,std::uint8_t flags) {
    Bytes b(30);Tag(b,0,"RIFF");Tag(b,8,"WEBP");Tag(b,12,"VP8X");Put(b,16,10);
    b[20]=std::byte(flags);Put(b,24,w-1,3);Put(b,27,h-1,3);
    b.insert(b.end(),base.begin()+12,base.end());Put(b,4,std::uint32_t(b.size()-8));return b;
}
void Chunk(Bytes& b,const char* type,std::span<const std::byte> payload) {
    const auto at=b.size();b.resize(at+8);Tag(b,at,type);Put(b,at+4,std::uint32_t(payload.size()));
    b.insert(b.end(),payload.begin(),payload.end());if(payload.size()&1)b.push_back(std::byte{});
    Put(b,4,std::uint32_t(b.size()-8));
}
Bytes Read(const std::filesystem::path& p) {
    const auto size=std::filesystem::file_size(p);Check(size<=16*1024*1024,"fixture too large");
    Bytes b(size);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(size));
    Check(bool(f),"fixture read failed");return b;
}
void Write(const std::filesystem::path& p,std::span<const std::byte> b) {
    Check(!std::filesystem::exists(p),"fixture destination exists");
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    Check(bool(f),"fixture write failed");
}
void Emit(const std::filesystem::path& p) {
    Check(!std::filesystem::exists(p),"use a new fixture directory");std::filesystem::create_directories(p);
    auto rgba=Pixels(96,64);auto webp=I::EncodeImage(I::Format::webp,96,64,rgba);
    auto png=I::EncodeImage(I::Format::png,96,64,rgba);Check(bool(webp)&&bool(png),"fixture encode failed");
    Write(p/"native.webp",webp.bytes);Write(p/"input.png",png.bytes);Write(p/"reference.rgba",std::as_bytes(std::span(rgba)));
}
void Verify(const std::filesystem::path& encoded,const std::filesystem::path& expected) {
    const auto b=Read(encoded),reference=Read(expected);const auto d=I::DecodeImage(I::Format::webp,b);
    Check(bool(d),"external WebP rejected");Check(reference.size()==d.image.rgba8.size(),"external extent differs");
    Check(std::memcmp(reference.data(),d.image.rgba8.data(),reference.size())==0,"external pixels differ");
}
void References(const std::filesystem::path& source,const std::filesystem::path& directory) {
    Check(!std::filesystem::exists(directory),"use a new reference directory");
    const auto bytes=Read(source);const auto decoded=I::DecodeImage(I::Format::webp,bytes);
    Check(bool(decoded),"reference source WebP rejected");std::filesystem::create_directories(directory);
    Write(directory/"input.webp",bytes);
    const auto& image=decoded.image;
    for(const auto [format,name]:{std::pair{I::Format::png,"expected.png"},std::pair{I::Format::jpeg,"expected.jpg"},std::pair{I::Format::webp,"expected.webp"}}){
        I::EncodeOptions options;if(format==I::Format::jpeg)options.alphaPolicy=I::AlphaPolicy::flatten_white;
        const auto output=I::EncodeImage(format,image.width,image.height,image.rgba8,options);
        Check(bool(output),"reference output failed");Write(directory/name,output.bytes);
    }
    const auto jpeg=I::DecodeImage(I::Format::jpeg,Read(directory/"expected.jpg"));Check(bool(jpeg),"reference JPEG rejected");
    const auto webp=I::EncodeImage(I::Format::webp,jpeg.image.width,jpeg.image.height,jpeg.image.rgba8);
    Check(bool(webp),"JPEG to WebP reference failed");Write(directory/"jpeg-source.webp",webp.bytes);
    const auto dimensions="{\"width\":"+std::to_string(image.width)+",\"height\":"+std::to_string(image.height)+"}\n";
    Write(directory/"dimensions.json",std::as_bytes(std::span(dimensions.data(),dimensions.size())));
}
void Test() {
    // Native callers can enter upstream initialization concurrently.
    std::atomic<bool> failed=false;std::vector<std::thread> threads;
    for(unsigned i=0;i<4;++i)threads.emplace_back([&]{for(unsigned n=0;n<3;++n){
        const auto pixels=Pixels(17,19);auto e=I::EncodeImage(I::Format::webp,17,19,pixels);
        auto d=I::DecodeImage(I::Format::webp,e.bytes);if(!e||!d||d.image.rgba8!=pixels)failed=true;
    }});
    for(auto& t:threads)t.join();Check(!failed,"concurrent WebP calls failed");
    for(auto dimensions:{std::pair{1u,1u},std::pair{31u,17u},std::pair{96u,64u}}){
        const auto [w,h]=dimensions;auto pixels=Pixels(w,h);const auto e=I::EncodeImage(I::Format::webp,w,h,pixels);
        Check(bool(e),"WebP encode failed");const auto d=I::DecodeImage(I::Format::webp,e.bytes);
        Check(bool(d)&&d.image.width==w&&d.image.height==h&&d.image.rgba8==pixels,"lossless RGBA changed");
        Check(I::EncodeImage(I::Format::webp,w,h,pixels).bytes==e.bytes,"WebP bytes not deterministic");
        const auto png=I::EncodeImage(I::Format::png,w,h,d.image.rgba8);const auto back=I::DecodeImage(I::Format::png,png.bytes);
        Check(bool(png)&&bool(back)&&back.image.rgba8==pixels,"WebP to PNG parity failed");
        auto limited=I::EncodeOptions{};limited.maximumOutputBytes=16;
        const auto shortOutput=I::EncodeImage(I::Format::webp,w,h,pixels,limited);
        Check(shortOutput.error==I::EncodeError::output_limit&&shortOutput.bytes.empty(),"output budget admitted partial bytes");
        auto source=I::DecodeOptions{};source.maximumSourceBytes=e.bytes.size()-1;
        Check(I::DecodeImage(I::Format::webp,e.bytes,source).issue.code==I::DecodeError::input_too_large,"source budget ignored");
    }
    auto pixels=Pixels(31,17);const auto e=I::EncodeImage(I::Format::webp,31,17,pixels);
    auto bad=e.bytes;bad[0]=std::byte{};Check(!I::DecodeImage(I::Format::webp,bad),"wrong magic admitted");
    for(std::size_t i=0;i<e.bytes.size();++i){const auto d=I::DecodeImage(I::Format::webp,std::span(e.bytes).first(i));Check(!d&&d.image.rgba8.empty(),"truncated file admitted");}
    bad=e.bytes;bad.push_back(std::byte{});Check(!I::DecodeImage(I::Format::webp,bad),"trailing bytes admitted");
    bad=Extended(e.bytes,31,17,0x10);Check(bool(I::DecodeImage(I::Format::webp,bad)),"valid extended alpha rejected");
    auto wrong=bad;Put(wrong,24,32,3);Check(!I::DecodeImage(I::Format::webp,wrong),"mismatched canvas admitted");
    wrong=bad;wrong[21]=std::byte{1};Check(!I::DecodeImage(I::Format::webp,wrong),"reserved header admitted");
    for(auto flag:{0x02,0x08,0x20}){wrong=bad;wrong[20]|=std::byte(flag);Check(I::DecodeImage(I::Format::webp,wrong).issue.code==I::DecodeError::unsupported_content,"unsupported metadata/frame admitted");}
    const std::array<std::byte,1> metadata{std::byte{1}};
    Chunk(bad,"note",metadata);Check(bool(I::DecodeImage(I::Format::webp,bad)),"unknown bounded metadata rejected");
    bad.back()=std::byte{1};Check(!I::DecodeImage(I::Format::webp,bad),"nonzero padding admitted");
    bad=e.bytes;bad.insert(bad.end(),e.bytes.begin()+12,e.bytes.end());Put(bad,4,std::uint32_t(bad.size()-8));
    Check(!I::DecodeImage(I::Format::webp,bad),"duplicate image payload admitted");
    bad=Extended(e.bytes,31,17,0x14);Chunk(bad,"XMP ",metadata);Check(bool(I::DecodeImage(I::Format::webp,bad)),"XMP metadata rejected");
    I::DecodeOptions opts;opts.maximumPixels=31*17-1;Check(!I::DecodeImage(I::Format::webp,e.bytes,opts),"pixel limit ignored");
    opts={};opts.frameIndex=1;Check(I::DecodeImage(I::Format::webp,e.bytes,opts).issue.code==I::DecodeError::selection_out_of_range,"nonexistent frame ignored");
    opts={};opts.resourceProfile=I::RasterProfile::largeRaster;Check(!I::DecodeImage(I::Format::webp,e.bytes,opts),"unqualified large profile admitted");
    I::EncodeOptions jpeg;jpeg.alphaPolicy=I::AlphaPolicy::flatten_white;
    const auto j=I::EncodeImage(I::Format::jpeg,31,17,pixels,jpeg);const auto jd=I::DecodeImage(I::Format::jpeg,j.bytes);
    Check(bool(j)&&bool(jd),"JPEG fixture failed");const auto w=I::EncodeImage(I::Format::webp,31,17,jd.image.rgba8);
    Check(I::DecodeImage(I::Format::webp,w.bytes).image.rgba8==jd.image.rgba8,"JPEG to WebP changed decoded pixels");
    for(std::size_t i=12;i<std::min<std::size_t>(e.bytes.size(),140);++i){bad=e.bytes;bad[i]^=std::byte{0x80};const auto d=I::DecodeImage(I::Format::webp,bad);Check(d||d.image.rgba8.empty(),"failed mutation retained pixels");}
}
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-webp-codec-tests r6362\n";return 0;}
    try {
        if(argc==3&&std::string_view(argv[1])=="--emit")Emit(argv[2]);
        else if(argc==4&&std::string_view(argv[1])=="--check")Verify(argv[2],argv[3]);
        else if(argc==4&&std::string_view(argv[1])=="--references")References(argv[2],argv[3]);
        else if(argc==3&&std::string_view(argv[1])=="--reject")Check(I::DecodeImage(I::Format::webp,Read(argv[2])).issue.code==I::DecodeError::unsupported_content,"fixture not classified as unsupported");
        else if(argc==1)Test();else throw std::runtime_error("invalid arguments");
        std::cout<<"WebP checks passed: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
