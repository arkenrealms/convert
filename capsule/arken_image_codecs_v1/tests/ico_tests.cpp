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
    {
        // A non-black XOR colour under a set AND bit (screen inversion) becomes
        // fully transparent; its stored colour is kept but carries no weight.
        const auto inverted=image::DecodeImage(image::Format::ico,bad);
        auto expected=legacy.pixels;expected[2]=255;
        Require(inverted&&inverted.image.rgba8==expected&&inverted.image.rgba8[3]==0,"ICO screen-XOR pixel not transparent");
        auto all=f::Container({legacy.entry});
        const auto pixelBytes=28*legacy.entry.height;
        for(std::size_t i=22+40;i<22+40+pixelBytes;++i)all[i]=std::byte{0x5a};
        const auto masked=image::DecodeImage(image::Format::ico,all);
        Require(bool(masked),"ICO non-black masked pixels rejected");
        for(std::size_t i=0;i<legacy.pixels.size();i+=4)
            Require(masked.image.rgba8[i+3]==legacy.pixels[i+3]&&(legacy.pixels[i+3]==0||masked.image.rgba8[i]==0x5a),"ICO AND mask alpha changed");
    }
    auto png=f::Png();const auto small=f::Png(2,2);
    const auto multiple=f::Container({small,png});
    auto result=image::DecodeImage(image::Format::ico,multiple);
    Require(result&&result.image.width==png.width&&result.image.height==png.height&&result.image.frameCount==2,"ICO largest rendition selection and rendition count");
    Require(result.image.rgba8==image::DecodeImage(image::Format::png,png.bytes).image.rgba8,"ICO embedded PNG altered pixels");
    {
        // frameIndex walks renditions largest first (then deeper, then directory order).
        image::DecodeOptions pick;pick.frameIndex=1;
        const auto second=image::DecodeImage(image::Format::ico,multiple,pick);
        Require(second&&second.image.width==2&&second.image.height==2&&second.image.frameCount==2
            &&second.image.rgba8==image::DecodeImage(image::Format::png,small.bytes).image.rgba8,"ICO frameIndex rendition selection");
        pick.frameIndex=2;
        Require(image::DecodeImage(image::Format::ico,multiple,pick).issue.code==image::DecodeError::selection_out_of_range,"ICO rendition index bound");
        const auto three=f::Container({f::Dib(8,false,4,4).entry,png,small});
        pick.frameIndex=0;Require(image::DecodeImage(image::Format::ico,three,pick).image.width==32,"ICO preferred rendition");
        pick.frameIndex=1;Require(image::DecodeImage(image::Format::ico,three,pick).image.width==4,"ICO second rendition");
        pick.frameIndex=2;Require(image::DecodeImage(image::Format::ico,three,pick).image.width==2,"ICO third rendition");
        auto damagedLargest=f::Png();damagedLargest.bytes[0]=std::byte{0};
        pick.frameIndex=1;
        Require(bool(image::DecodeImage(image::Format::ico,f::Container({small,damagedLargest}),pick)),"ICO explicit rendition depends on unselected payload");
        pick.directionIndex=1;pick.frameIndex=0;
        Require(image::DecodeImage(image::Format::ico,multiple,pick).issue.code==image::DecodeError::selection_out_of_range,"ICO direction selection");
    }
    {
        // CUR: same directory, hotspot in place of planes/bit count, ignored.
        const auto cursor=image::DecodeImage(image::Format::ico,f::Cursor(multiple,5,9));
        Require(cursor&&cursor.image.rgba8==result.image.rgba8&&cursor.image.frameCount==2,"CUR PNG rendition");
        const auto dibCursor=image::DecodeImage(image::Format::ico,f::Cursor(alphaBytes,3,4));
        Require(dibCursor&&dibCursor.image.rgba8==alpha.pixels,"CUR DIB rendition");
        const auto legacyCursor=image::DecodeImage(image::Format::ico,f::Cursor(f::Container({legacy.entry}),300,2));
        Require(legacyCursor&&legacyCursor.image.rgba8==legacy.pixels,"CUR masked DIB rendition");
        auto reserved=f::Cursor(alphaBytes,3,4);reserved[9]=std::byte{1};Reject(reserved,"CUR reserved entry byte");
        auto truncatedCursor=f::Cursor(alphaBytes,3,4);truncatedCursor.pop_back();Reject(truncatedCursor,"CUR truncated mask");
    }
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
    bad=alphaBytes;f::Put16(bad,2,3);Require(image::DecodeImage(image::Format::ico,bad).issue.code==image::DecodeError::wrong_magic,"ICO unknown resource type admitted");
    bad=alphaBytes;f::Put16(bad,0,1);Require(image::DecodeImage(image::Format::ico,bad).issue.code==image::DecodeError::wrong_magic,"ICO reserved header word admitted");
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
    // Corruption sweep: every truncation fails; every single-byte change either
    // fails or yields an image of the declared extent (never a crash or overrun).
    for(const auto& sample:{alphaBytes,multiple,f::Cursor(f::Container({legacy.entry,f::Dib(4,false,5,3).entry}),1,1)}){
        for(std::size_t length=0;length<sample.size();++length)Reject(f::Bytes(sample.begin(),sample.begin()+length),"ICO truncation admitted");
        for(std::size_t i=0;i<sample.size();++i)for(unsigned flip:{0x01u,0x80u,0xffu}){
            auto damaged=sample;damaged[i]^=std::byte(flip);
            const auto decoded=image::DecodeImage(image::Format::ico,damaged);
            Require(!decoded||decoded.image.rgba8.size()==std::size_t(decoded.image.width)*decoded.image.height*4,"ICO corruption produced a bad extent");
        }
    }
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
