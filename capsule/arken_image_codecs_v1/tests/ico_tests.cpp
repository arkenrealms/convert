#include "arken_image_codecs_v1/test_support/ico_fixtures.hpp"
#include "arken_image_codecs_v1/source_formats.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace image=arken::image::v1;
namespace f=ico_fixture;
namespace {
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Reject(const f::Bytes& bytes,const char* message){Require(!image::DecodeImage(image::Format::ico,bytes),message);}
void Write(const std::filesystem::path& path,std::span<const std::byte> bytes){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();
    Require(bool(out),"ICO fixture write failed");
}
}
void RunIcoTests(){
    const auto* source=image::FindSourceFormat("ICO");Require(source&&source->format==image::Format::ico&&!source->firstFrameOnly,"ICO source catalogue");
    for(unsigned bits:{1u,4u,8u,24u,32u}){
        const auto dib=f::Dib(bits,false);const auto bytes=f::Container({dib.entry});
        const auto decoded=image::DecodeImage(image::Format::ico,bytes);
        if(!decoded)throw std::runtime_error("ICO DIB rejected depth="+std::to_string(bits)+" code="+std::to_string(unsigned(decoded.issue.code)));
        Require(decoded.image.rgba8==dib.pixels,"ICO mask/palette/row alignment/orientation");
        auto truncated=bytes;truncated.pop_back();Reject(truncated,"ICO truncated mask");
    }
    const auto alpha=f::Dib();const auto alphaBytes=f::Container({alpha.entry});
    auto shortPalette=f::Dib(8,false).entry;
    f::Put32(shortPalette.bytes,32,1);
    shortPalette.bytes.erase(shortPalette.bytes.begin()+44,shortPalette.bytes.begin()+40+256*4);
    Require(image::DecodeImage(image::Format::ico,f::Container({shortPalette})).issue.code==image::DecodeError::invalid_palette,
        "ICO index outside declared palette admitted");
    const auto shortStride=((shortPalette.width*8+31)/32)*4;
    std::fill_n(shortPalette.bytes.begin()+44,shortStride*shortPalette.height,std::byte{0});
    Require(bool(image::DecodeImage(image::Format::ico,f::Container({shortPalette}))),"Valid one-color ICO palette rejected");
    Require(image::DecodeImage(image::Format::ico,alphaBytes).image.rgba8==alpha.pixels,"ICO partial alpha");
    auto bad=alphaBytes;f::Put32(bad,22,108);Reject(bad,"ICO unsupported DIB header");
    bad=alphaBytes;f::Put32(bad,38,3);Reject(bad,"ICO unsupported bitfields");
    bad=alphaBytes;f::Put16(bad,36,16);Reject(bad,"ICO unsupported 16-bit");
    bad=alphaBytes;f::Put32(bad,30,alpha.entry.height*2-1);Reject(bad,"ICO odd stored height");
    bad=alphaBytes;f::Put32(bad,54,1);Reject(bad,"ICO unexpected truecolor palette");
    const auto legacy=f::Dib(24,false);
    bad=f::Container({legacy.entry});bad[22+40+(legacy.entry.height-1)*28]=std::byte{255};
    Require(image::DecodeImage(image::Format::ico,bad).issue.code==image::DecodeError::unsupported_content,"ICO screen-XOR silently approximated");
    auto png=f::Png();const auto small=f::Png(2,2);
    const auto multiple=f::Container({small,png});
    auto result=image::DecodeImage(image::Format::ico,multiple);
    Require(result&&result.image.width==png.width&&result.image.height==png.height&&result.image.frameCount==1,"ICO largest rendition selection");
    Require(result.image.rgba8==image::DecodeImage(image::Format::png,png.bytes).image.rgba8,"ICO embedded PNG altered pixels");
    auto alternate=f::Png();alternate.bits=0;
    auto deep=f::Dib(32,true,png.width,png.height);
    result=image::DecodeImage(image::Format::ico,f::Container({alternate,deep.entry}));
    Require(result&&result.image.rgba8==deep.pixels,"ICO depth tie breaker");
    result=image::DecodeImage(image::Format::ico,f::Container({deep.entry,png}));
    Require(result&&result.image.rgba8==deep.pixels,"ICO stable entry order tie breaker");
    png.bytes[0]=std::byte{0};Reject(f::Container({small,png}),"ICO silently fell back from damaged largest entry");
    bad=multiple;bad[22]=std::byte{1};Reject(bad,"ICO embedded dimension mismatch");
    const auto full=image::DecodeImage(image::Format::ico,f::Container({f::Png(256,256)}));
    Require(full&&full.image.width==256&&full.image.height==256,"ICO zero dimension means 256");
    for(std::size_t length=0;length<22;++length)Reject(f::Bytes(alphaBytes.begin(),alphaBytes.begin()+length),"ICO truncated directory");
    bad=alphaBytes;f::Put16(bad,2,2);Reject(bad,"ICO cursor admitted");
    bad=alphaBytes;f::Put16(bad,4,0);Reject(bad,"ICO empty directory");
    bad=alphaBytes;f::Put16(bad,4,257);Reject(bad,"ICO directory budget");
    bad=alphaBytes;f::Put32(bad,18,0xfffffff0);Reject(bad,"ICO range overflow");
    bad=alphaBytes;f::Put32(bad,18,6);Reject(bad,"ICO payload overlaps directory");
    bad=alphaBytes;bad[9]=std::byte{1};Reject(bad,"ICO reserved entry byte");
    bad=alphaBytes;f::Put16(bad,10,2);Reject(bad,"ICO invalid planes");
    bad=alphaBytes;bad.push_back(std::byte{0});f::Put32(bad,14,bad.size()-22);Reject(bad,"ICO DIB trailing bytes");
    image::DecodeOptions limit;limit.maximumPixels=62;Require(!image::DecodeImage(image::Format::ico,alphaBytes,limit),"ICO pixel limit");
    limit={};limit.maximumDimension=8;Require(!image::DecodeImage(image::Format::ico,alphaBytes,limit),"ICO dimension limit");
    limit={};limit.maximumSourceBytes=alphaBytes.size()-1;Require(!image::DecodeImage(image::Format::ico,alphaBytes,limit),"ICO source limit");
    limit={};limit.frameIndex=1;Require(!image::DecodeImage(image::Format::ico,alphaBytes,limit),"ICO alternatives treated as animation");
    limit={};limit.resourceProfile=image::RasterProfile::largeRaster;Require(!image::DecodeImage(image::Format::ico,alphaBytes,limit),"ICO widened large-raster admission");
    std::cout<<"ICO PNG/DIB selection, exact alpha/mask pixels and malformed-input bounds passed\n";
}
void PrepareIcoFixtures(const std::filesystem::path& directory){
    Require(std::filesystem::create_directory(directory),"ICO fixture destination must be new");
    const auto alpha=f::Dib(32,true,64,48),mask=f::Dib(24,false,33,25);
    for(const auto& [name,bytes]:{std::pair{"png",f::Container({f::Png(16,16),f::Png(128,128)})},
            std::pair{"alpha",f::Container({alpha.entry})},std::pair{"mask",f::Container({mask.entry})}}){
        const auto decoded=image::DecodeImage(image::Format::ico,bytes);Require(bool(decoded),"ICO browser fixture decode");
        const auto png=image::EncodePng(decoded.image.width,decoded.image.height,decoded.image.rgba8);Require(bool(png),"ICO browser reference encode");
        Write(directory/(std::string(name)+".ico"),bytes);Write(directory/(std::string(name)+".png"),png.bytes);
    }
}
