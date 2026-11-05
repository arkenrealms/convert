#include "arken_image_codecs_v1/test_support/raster_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/scalar_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/icc_fixtures.hpp"
#include "arken_image_codecs_v1/test_support/png_layout_fixtures.hpp"
#include "arken_image_codecs_v1/encode.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
namespace image=arken::image::v1;
void RunIcoTests();
void PrepareIcoFixtures(const std::filesystem::path&);
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void RunWidenedRasterTests();
// Corpus regressions (CORPUS_REPORT.md): synthetic fixtures for layouts that
// real encoders write and the decoder previously rejected.
namespace corpus {
std::vector<std::byte> Chunk(std::uint32_t type,std::vector<std::uint8_t> data){
    std::vector<std::byte> chunk;
    const auto put=[&](std::uint32_t value){for(int shift=24;shift>=0;shift-=8)chunk.push_back(std::byte((value>>shift)&255));};
    put(static_cast<std::uint32_t>(data.size()));put(type);
    for(auto byte:data)chunk.push_back(std::byte{byte});
    std::uint32_t crc=0xffffffff;
    for(std::size_t i=4;i<chunk.size();++i){crc^=std::to_integer<std::uint8_t>(chunk[i]);for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    put(~crc);return chunk;
}
std::vector<std::uint8_t> Be32(std::initializer_list<std::uint32_t> values){
    std::vector<std::uint8_t> out;for(auto value:values)for(int shift=24;shift>=0;shift-=8)out.push_back(std::uint8_t((value>>shift)&255));return out;
}
std::vector<std::uint8_t> FrameControl(std::uint32_t sequence,std::uint32_t width,std::uint32_t height,std::uint32_t x,std::uint32_t y){
    auto data=Be32({sequence,width,height,x,y});for(std::uint8_t byte:{0,1,0,10,0,0})data.push_back(byte);return data;
}
void Run(){
    // JPEG: bytes after the primary image's EOI (Ultra HDR gain map, MPF
    // secondary image, camera trailer, zero padding) are ignored; a stream
    // without EOI is still truncated.
    const auto jpeg=raster_fixture::Encode(image::Format::jpeg,32,16);const auto base=image::DecodeImage(image::Format::jpeg,jpeg);
    require(bool(base),"JPEG fixture");
    for(const std::vector<std::uint8_t>& trailer:{std::vector<std::uint8_t>(64,0),std::vector<std::uint8_t>{0xff,0xd8,0xff,0xe1,0,4,'M','P',0xff,0xd9,1,2,3}}){
        auto padded=jpeg;for(auto byte:trailer)padded.push_back(std::byte{byte});
        const auto decoded=image::DecodeImage(image::Format::jpeg,padded);
        require(bool(decoded)&&decoded.image.rgba8==base.image.rgba8,"JPEG with data after EOI rejected or decoded differently");
    }
    auto cut=jpeg;cut.resize(cut.size()-2);
    require(image::DecodeImage(image::Format::jpeg,cut).issue.code==image::DecodeError::truncated,"JPEG without EOI accepted");
    for(auto byte:{0x00,0x11,0x22})cut.push_back(std::byte(byte));
    require(image::DecodeImage(image::Format::jpeg,cut).issue.code==image::DecodeError::truncated,"JPEG without EOI but with trailing bytes accepted");
    // Exif Orientation written as LONG (4) or SLONG (9) by some cameras and
    // editors is read at its own width; other types stay rejected.
    for(unsigned type:{4u,9u}){
        auto tagged=raster_fixture::WithOrientation(jpeg,6);tagged[2+22]=std::byte(type);
        const auto decoded=image::DecodeImage(image::Format::jpeg,tagged);
        require(bool(decoded)&&decoded.image.width==16&&decoded.image.height==32,"LONG/SLONG Exif orientation rejected or ignored");
        tagged[2+29]=std::byte{1}; // 0x106: out of range at LONG width
        require(!image::DecodeImage(image::Format::jpeg,tagged),"Out-of-range LONG orientation accepted");
    }
    auto rational=raster_fixture::WithOrientation(jpeg,6);rational[2+22]=std::byte{5};
    require(!image::DecodeImage(image::Format::jpeg,rational),"RATIONAL Exif orientation accepted");
    // APNG whose default image is the first frame: the IDAT image is decoded
    // exactly, fdAT frames are skipped and frameCount reports acTL.
    const auto png=raster_fixture::Encode(image::Format::png,32,16,true);const auto still=image::DecodeImage(image::Format::png,png);
    require(bool(still),"PNG fixture");
    const auto animate=[&](std::vector<std::uint8_t> control,bool withControl){
        auto out=png;auto actl=Chunk(0x6163544cu,Be32({3,0}));
        auto fctl=Chunk(0x6663544cu,control);
        auto fdat=Chunk(0x66644154u,Be32({1,0xdeadbeef}));
        out.insert(out.end()-12,fdat.begin(),fdat.end());
        if(withControl)out.insert(out.begin()+33,fctl.begin(),fctl.end());
        out.insert(out.begin()+33,actl.begin(),actl.end());
        return out;
    };
    const auto first=image::DecodeImage(image::Format::png,animate(FrameControl(0,32,16,0,0),true));
    require(bool(first)&&first.image.rgba8==still.image.rgba8&&first.image.frameCount==3,"APNG first frame not decoded exactly");
    require(image::DecodeImage(image::Format::png,animate(FrameControl(0,32,16,0,0),false)).issue.code==image::DecodeError::unsupported_content,
        "APNG default image outside the animation accepted");
    require(image::DecodeImage(image::Format::png,animate(FrameControl(0,16,16,0,0),true)).issue.code==image::DecodeError::invalid_layout,
        "APNG first frame smaller than the canvas accepted");
    require(image::DecodeImage(image::Format::png,animate(FrameControl(0,32,16,1,0),true)).issue.code==image::DecodeError::invalid_layout,
        "APNG first frame offset accepted");
}
}
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1])=="--version"){std::cout<<"arken-raster-codec-tests-r6338\n";return 0;}
    try{
        if(argc==3&&std::string_view(argv[1])=="--prepare-ico"){PrepareIcoFixtures(argv[2]);return 0;}
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
        require(argc==1,"Usage: arken-raster-codec-tests [--convert-png INPUT NEW-OUTPUT | --prepare-ico NEW-OUTPUT]");
        RunIcoTests();
        for(const auto size:{std::pair{1u,1u},std::pair{9u,7u}}){
            const auto decoded=image::DecodeImage(image::Format::png,raster_fixture::LayoutPng(size.first,size.second,true));
            require(decoded&&decoded.image.rgba8==raster_fixture::Pixels(size.first,size.second,true),"Adam7 pass extent or empty pass rejected");
        }
        const auto packed=image::DecodeImage(image::Format::png,raster_fixture::LayoutPng(9,2,false,true));
        require(packed&&packed.image.rgba8==std::vector<std::uint8_t>(9*2*4,255),"Sub-byte PNG row padding rejected");
        require(!image::DecodeImage(image::Format::png,raster_fixture::LayoutPng(1,1,false,false,true)),"Excess inflated PNG bytes accepted");
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
        // WithChromaticities(false) moves the white point x by 0.00001: writer
        // rounding of the sRGB set, decoded without a transform.
        require(image::DecodeImage(image::Format::png,raster_fixture::WithChromaticities(raster_fixture::Encode(image::Format::png,32,16),false)).image.rgba8==raster_fixture::Pixels(32,16),"Near-sRGB PNG primaries transformed");
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
        // A calibrated V4 block with a degenerate red primary (X only) is malformed.
        require(image::DecodeImage(image::Format::bmp,calibrated).issue.code==image::DecodeError::invalid_layout,"Calibrated BMP silently treated as sRGB");
        RunWidenedRasterTests();
        corpus::Run();
        std::cout<<"PNG/BMP/JPEG and8 EXIF orientations; exact pixels, limits, corruption and selection checks passed\n";return 0;
    }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
namespace {
using Bytes=std::vector<std::byte>;
using Rgba=std::vector<std::uint8_t>;
namespace rf=raster_fixture;
image::DecodeError Code(image::Format format,const Bytes& bytes,const image::DecodeOptions& options={}){
    return image::DecodeImage(format,bytes,options).issue.code;
}
void Expect(image::Format format,const Bytes& bytes,image::DecodeError expected,const char* message){
    const auto observed=Code(format,bytes);
    if(observed!=expected)throw std::runtime_error(std::string(message)+" (code "+std::to_string(unsigned(observed))+")");
}
Rgba Decoded(image::Format format,const Bytes& bytes,const char* message){
    const auto result=image::DecodeImage(format,bytes);
    if(!result)throw std::runtime_error(std::string(message)+" rejected (code "+std::to_string(unsigned(result.issue.code))+")");
    return result.image.rgba8;
}
bool Near(const Rgba& actual,const Rgba& expected,int tolerance){
    if(actual.size()!=expected.size())return false;
    for(std::size_t i=0;i<actual.size();++i)if(std::abs(int(actual[i])-int(expected[i]))>tolerance)return false;
    return true;
}
// Every prefix must fail (when the format makes truncation detectable) and
// every single-byte corruption must either fail or produce a consistent extent.
void Sweep(image::Format format,const Bytes& bytes,bool truncationsFail,const char* message){
    for(std::size_t length=0;length<bytes.size();++length){
        const auto result=image::DecodeImage(format,std::span<const std::byte>(bytes).first(length));
        require(!truncationsFail||!result,message);
        require(!result||result.image.rgba8.size()==std::size_t(result.image.width)*result.image.height*4,message);
    }
    for(std::size_t i=0;i<bytes.size();++i)for(unsigned flip:{0x01u,0x10u,0xffu}){
        auto damaged=bytes;damaged[i]^=std::byte(flip);
        const auto result=image::DecodeImage(format,damaged);
        require(!result||result.image.rgba8.size()==std::size_t(result.image.width)*result.image.height*4,message);
    }
}
std::uint8_t Scale(std::uint32_t value,std::uint32_t maximum){return std::uint8_t((std::uint64_t(value)*510+maximum)/(2*maximum));}

void BmpTests(){
    constexpr unsigned W=5,H=3;
    std::vector<std::uint32_t> rgb(W*H);Rgba expected(W*H*4);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const unsigned r=x*50,g=y*100,b=200-x*10,p=y*W+x;
        rgb[p]=(r<<16)|(g<<8)|b;expected[p*4]=r;expected[p*4+1]=g;expected[p*4+2]=b;expected[p*4+3]=255;
    }
    rf::Bmp b;b.width=W;b.height=H;b.bits=24;b.pixels=rf::BmpRows(W,H,24,rgb);
    require(Decoded(image::Format::bmp,rf::MakeBmp(b),"BMP 24-bit")==expected,"BMP 24-bit bottom-up pixels");
    auto top=b;top.height=-int(H);top.pixels=rf::BmpRows(W,H,24,rgb,true);
    require(Decoded(image::Format::bmp,rf::MakeBmp(top),"BMP top-down")==expected,"BMP top-down (negative height) pixels");
    auto core=b;core.header=12;
    require(Decoded(image::Format::bmp,rf::MakeBmp(core),"OS/2 BMP 24-bit")==expected,"OS/2 BITMAPCOREHEADER 24-bit pixels");
    // Palettes: OS/2 3-byte entries, Windows 4-byte entries, 1/4/8-bit packing.
    const std::vector<std::array<std::uint8_t,3>> palette{{10,20,30},{40,50,60},{70,80,90},{200,210,220}};
    std::vector<std::uint32_t> indices(W*H);Rgba paletted(W*H*4);
    for(unsigned p=0;p<W*H;++p){indices[p]=(p*3)%4;for(unsigned c=0;c<3;++c)paletted[p*4+c]=palette[indices[p]][c];paletted[p*4+3]=255;}
    for(unsigned header:{12u,40u,108u})for(unsigned bits:{4u,8u}){
        rf::Bmp p;p.header=header;p.width=W;p.height=H;p.bits=bits;p.palette=palette;p.used=header==12?0:4;
        p.pixels=rf::BmpRows(W,H,bits,indices);
        require(Decoded(image::Format::bmp,rf::MakeBmp(p),"BMP palette")==paletted,"BMP palette/packing pixels");
        auto outside=p;outside.pixels=rf::BmpRows(W,H,bits,std::vector<std::uint32_t>(W*H,4));
        Expect(image::Format::bmp,rf::MakeBmp(outside),image::DecodeError::invalid_palette,"BMP index beyond declared palette");
    }
    {
        rf::Bmp mono;mono.width=W;mono.height=H;mono.bits=1;mono.palette={{0,0,0},{255,255,255}};
        std::vector<std::uint32_t> bitsSet(W*H);Rgba monoPixels(W*H*4,255);
        for(unsigned p=0;p<W*H;++p){bitsSet[p]=p%2;for(unsigned c=0;c<3;++c)monoPixels[p*4+c]=bitsSet[p]?255:0;}
        mono.pixels=rf::BmpRows(W,H,1,bitsSet);mono.height=-int(H);mono.pixels=rf::BmpRows(W,H,1,bitsSet,true);
        require(Decoded(image::Format::bmp,rf::MakeBmp(mono),"BMP 1-bit")==monoPixels,"BMP top-down 1-bit pixels");
    }
    // 16-bit: BI_RGB 5-5-5, BI_BITFIELDS 5-6-5 after a 40-byte header, 1-5-5-5
    // with a declared alpha mask (V4), 5-6-5 in a BITMAPV3 (56-byte) header.
    std::vector<std::uint32_t> words(W*H);Rgba five(W*H*4),six(W*H*4),alpha(W*H*4);
    for(unsigned p=0;p<W*H;++p){
        const unsigned r=p%32,g=(p*3)%32,bl=(31-p)%32,g6=(p*5)%64,a=p%2;
        words[p]=(r<<10)|(g<<5)|bl;
        five[p*4]=Scale(r,31);five[p*4+1]=Scale(g,31);five[p*4+2]=Scale(bl,31);five[p*4+3]=255;
        alpha[p*4]=five[p*4];alpha[p*4+1]=five[p*4+1];alpha[p*4+2]=five[p*4+2];alpha[p*4+3]=a?255:0;
        six[p*4]=Scale(r,31);six[p*4+1]=Scale(g6,63);six[p*4+2]=Scale(bl,31);six[p*4+3]=255;
    }
    rf::Bmp sixteen;sixteen.width=W;sixteen.height=H;sixteen.bits=16;sixteen.pixels=rf::BmpRows(W,H,16,words);
    require(Decoded(image::Format::bmp,rf::MakeBmp(sixteen),"BMP 555")==five,"BMP 16-bit BI_RGB 5-5-5 pixels");
    std::vector<std::uint32_t> words565(W*H),words1555(W*H);
    for(unsigned p=0;p<W*H;++p){words565[p]=((p%32)<<11)|(((p*5)%64)<<5)|((31-p)%32);words1555[p]=words[p]|((p%2)<<15);}
    auto bitfields=sixteen;bitfields.compression=3;bitfields.masks={0xf800,0x7e0,0x1f,0};bitfields.masksAfterHeader=3;
    bitfields.pixels=rf::BmpRows(W,H,16,words565);
    require(Decoded(image::Format::bmp,rf::MakeBmp(bitfields),"BMP 565")==six,"BMP 16-bit BI_BITFIELDS 5-6-5 pixels");
    auto v3=bitfields;v3.header=56;v3.masksAfterHeader=0;
    require(Decoded(image::Format::bmp,rf::MakeBmp(v3),"BMP V3 565")==six,"BMP BITMAPV3 masks misread");
    auto v4=bitfields;v4.header=108;v4.masksAfterHeader=0;v4.masks={0x7c00,0x3e0,0x1f,0x8000};v4.pixels=rf::BmpRows(W,H,16,words1555);
    require(Decoded(image::Format::bmp,rf::MakeBmp(v4),"BMP 1555")==alpha,"BMP 16-bit 1-5-5-5 declared alpha");
    auto alphaFields=sixteen;alphaFields.compression=6;alphaFields.masks={0x7c00,0x3e0,0x1f,0x8000};alphaFields.masksAfterHeader=4;
    alphaFields.pixels=v4.pixels;
    require(Decoded(image::Format::bmp,rf::MakeBmp(alphaFields),"BMP alpha bitfields")==alpha,"BMP BI_ALPHABITFIELDS alpha");
    // 32-bit alpha: BI_RGB reads the fourth byte unless every one is zero; a
    // three-mask BI_BITFIELDS header declares no alpha; a declared mask is honoured.
    std::vector<std::uint32_t> argb(W*H),xrgb(W*H);Rgba straight(W*H*4),opaque(W*H*4);
    for(unsigned p=0;p<W*H;++p){
        xrgb[p]=rgb[p];argb[p]=rgb[p]|((p*17u)<<24);
        for(unsigned c=0;c<4;++c){opaque[p*4+c]=expected[p*4+c];straight[p*4+c]=c==3?std::uint8_t(p*17):expected[p*4+c];}
    }
    rf::Bmp wide;wide.width=W;wide.height=H;wide.bits=32;wide.pixels=rf::BmpRows(W,H,32,argb);
    require(Decoded(image::Format::bmp,rf::MakeBmp(wide),"BMP 32 alpha")==straight,"BMP 32-bit BI_RGB alpha byte");
    wide.pixels=rf::BmpRows(W,H,32,xrgb);
    require(Decoded(image::Format::bmp,rf::MakeBmp(wide),"BMP 32 xrgb")==opaque,"BMP 32-bit BI_RGB all-zero alpha is opaque");
    auto noAlpha=wide;noAlpha.compression=3;noAlpha.masks={0xff0000,0xff00,0xff,0};noAlpha.masksAfterHeader=3;noAlpha.pixels=rf::BmpRows(W,H,32,argb);
    require(Decoded(image::Format::bmp,rf::MakeBmp(noAlpha),"BMP 32 bitfields")==opaque,"BMP 32-bit three-mask bitfields invented alpha");
    auto declared=noAlpha;declared.header=124;declared.masksAfterHeader=0;declared.masks={0xff0000,0xff00,0xff,0xff000000u};declared.pixels=rf::BmpRows(W,H,32,xrgb);
    Rgba transparent=opaque;for(std::size_t i=3;i<transparent.size();i+=4)transparent[i]=0;
    require(Decoded(image::Format::bmp,rf::MakeBmp(declared),"BMP 32 declared alpha")==transparent,"BMP declared alpha mask not honoured literally");
    auto tenBit=noAlpha;tenBit.masks={0x3ff00000,0xffc00,0x3ff,0};
    std::vector<std::uint32_t> ten(W*H);Rgba tenExpected(W*H*4,255);
    for(unsigned p=0;p<W*H;++p){
        const unsigned r=p*60,g=1023-p*60,bl=p*7;ten[p]=(r<<20)|(g<<10)|bl;
        tenExpected[p*4]=Scale(r,1023);tenExpected[p*4+1]=Scale(g,1023);tenExpected[p*4+2]=Scale(bl,1023);
    }
    tenBit.pixels=rf::BmpRows(W,H,32,ten);
    require(Decoded(image::Format::bmp,rf::MakeBmp(tenBit),"BMP 10-bit masks")==tenExpected,"BMP wide channel masks");
    for(const auto& masks:{std::array<std::uint32_t,4>{0xff0000,0xff0000,0xff,0},std::array<std::uint32_t,4>{0,0xff00,0xff,0},
            std::array<std::uint32_t,4>{0xf0f000,0xff00,0xff,0},std::array<std::uint32_t,4>{0xff0000,0xff00,0xff,0xff}}){
        auto bad=noAlpha;bad.masks=masks;bad.masksAfterHeader=4;bad.compression=6;
        Expect(image::Format::bmp,rf::MakeBmp(bad),image::DecodeError::invalid_layout,"BMP invalid masks admitted");
    }
    {
        auto wideMask=bitfields;wideMask.masks={0x1f0000,0x7e0,0x1f,0};
        Expect(image::Format::bmp,rf::MakeBmp(wideMask),image::DecodeError::invalid_layout,"BMP 16-bit mask beyond word");
        auto truecolour=b;truecolour.compression=3;truecolour.masksAfterHeader=3;truecolour.masks={0xff0000,0xff00,0xff,0};
        Expect(image::Format::bmp,rf::MakeBmp(truecolour),image::DecodeError::invalid_layout,"BMP 24-bit bitfields admitted");
        auto missingMasks=bitfields;missingMasks.masksAfterHeader=2;
        Expect(image::Format::bmp,rf::MakeBmp(missingMasks),image::DecodeError::invalid_layout,"BMP masks overlapping pixel data admitted");
    }
    // RLE8: encoded runs, a padded odd absolute run, EOL, delta, an even
    // absolute run and EOF. Skipped pixels are transparent black.
    {
        constexpr unsigned w=6,h=4;
        const std::vector<std::array<std::uint8_t,3>> colours{{1,2,3},{40,50,60},{100,110,120},{250,240,230}};
        const std::vector<unsigned> stream{3,1, 0,3,2,3,1,0, 0,0, 2,3, 0,2,2,1, 2,0, 0,0, 0,4,1,2,3,0, 0,1};
        Bytes data;for(auto v:stream)data.push_back(std::byte(v));
        const int rows[h][w]={{1,2,3,0,-1,-1},{-1,-1,-1,-1,0,0},{3,3,-1,-1,-1,-1},{1,1,1,2,3,1}};
        Rgba want(w*h*4,0);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(rows[y][x]>=0){
            for(unsigned c=0;c<3;++c)want[(y*w+x)*4+c]=colours[rows[y][x]][c];want[(y*w+x)*4+3]=255;
        }
        rf::Bmp rle;rle.width=w;rle.height=h;rle.bits=8;rle.compression=1;rle.palette=colours;rle.used=4;rle.pixels=data;
        const auto encoded=rf::MakeBmp(rle);
        require(Decoded(image::Format::bmp,encoded,"RLE8")==want,"BMP RLE8 runs/absolute/delta/EOL/EOF");
        auto overflow=rle;overflow.pixels[0]=std::byte{7};
        Expect(image::Format::bmp,rf::MakeBmp(overflow),image::DecodeError::invalid_run,"RLE8 run past the row admitted");
        auto absolute=rle;absolute.pixels[3]=std::byte{4};
        Expect(image::Format::bmp,rf::MakeBmp(absolute),image::DecodeError::invalid_run,"RLE8 absolute run past the row admitted");
        auto delta=rle;delta.pixels[15]=std::byte{4};
        Expect(image::Format::bmp,rf::MakeBmp(delta),image::DecodeError::invalid_run,"RLE8 delta past the image admitted");
        auto rows5=rle;rows5.pixels.insert(rows5.pixels.end()-2,{std::byte{0},std::byte{0},std::byte{0},std::byte{0}});
        rows5.pixels.insert(rows5.pixels.end()-2,{std::byte{1},std::byte{1}});
        Expect(image::Format::bmp,rf::MakeBmp(rows5),image::DecodeError::invalid_run,"RLE8 rows past the image admitted");
        auto noEnd=rle;noEnd.pixels.resize(noEnd.pixels.size()-2);
        Expect(image::Format::bmp,rf::MakeBmp(noEnd),image::DecodeError::truncated,"RLE8 without end-of-bitmap admitted");
        auto shortPalette=rle;shortPalette.used=3;shortPalette.palette.pop_back();
        Expect(image::Format::bmp,rf::MakeBmp(shortPalette),image::DecodeError::invalid_palette,"RLE8 index beyond palette admitted");
        auto topDownRle=rle;topDownRle.height=-int(h);
        Expect(image::Format::bmp,rf::MakeBmp(topDownRle),image::DecodeError::invalid_layout,"Top-down RLE admitted");
        auto wrongDepth=rle;wrongDepth.bits=4;wrongDepth.palette=colours;
        Expect(image::Format::bmp,rf::MakeBmp(wrongDepth),image::DecodeError::invalid_layout,"RLE8 at 4 bits admitted");
        Sweep(image::Format::bmp,encoded,true,"RLE8 corruption sweep");
    }
    // RLE4: alternating-nibble runs and a padded absolute run.
    {
        constexpr unsigned w=7,h=2;
        std::vector<std::array<std::uint8_t,3>> colours;
        for(unsigned i=0;i<16;++i)colours.push_back({std::uint8_t(i*16),std::uint8_t(255-i*16),std::uint8_t(i)});
        const std::vector<unsigned> stream{4,0x12, 0,3,0x34,0x50, 0,0, 0,5,0x67,0x89,0xa0,0, 0,1};
        Bytes data;for(auto v:stream)data.push_back(std::byte(v));
        const int rows[h][w]={{6,7,8,9,10,-1,-1},{1,2,1,2,3,4,5}};
        Rgba want(w*h*4,0);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(rows[y][x]>=0){
            for(unsigned c=0;c<3;++c)want[(y*w+x)*4+c]=colours[rows[y][x]][c];want[(y*w+x)*4+3]=255;
        }
        rf::Bmp rle;rle.width=w;rle.height=h;rle.bits=4;rle.compression=2;rle.palette=colours;rle.pixels=data;
        const auto encoded=rf::MakeBmp(rle);
        require(Decoded(image::Format::bmp,encoded,"RLE4")==want,"BMP RLE4 nibble runs and absolute runs");
        auto overflow=rle;overflow.pixels[0]=std::byte{8};
        Expect(image::Format::bmp,rf::MakeBmp(overflow),image::DecodeError::invalid_run,"RLE4 run past the row admitted");
        auto cut=rle;cut.pixels.resize(9);
        Expect(image::Format::bmp,rf::MakeBmp(cut),image::DecodeError::truncated,"RLE4 cut absolute run admitted");
        Sweep(image::Format::bmp,encoded,true,"RLE4 corruption sweep");
    }
    // V4/V5 colour spaces.
    {
        const std::vector<std::uint32_t> pair{0x808080,0xc83232};
        const Rgba plain{128,128,128,255,200,50,50,255};
        rf::Bmp v;v.header=108;v.width=2;v.height=1;v.bits=24;v.pixels=rf::BmpRows(2,1,24,pair);
        require(Decoded(image::Format::bmp,rf::MakeBmp(v),"BMP sRGB")==plain,"BMP sRGB colour space transformed");
        v.colourSpace=0x57696e20;
        require(Decoded(image::Format::bmp,rf::MakeBmp(v),"BMP Windows colour space")==plain,"BMP LCS_WINDOWS_COLOR_SPACE transformed");
        v.colourSpace=0;
        require(Decoded(image::Format::bmp,rf::MakeBmp(v),"BMP untagged")==plain,"BMP zero calibration block transformed");
        // sRGB (D65) primaries as CIEXYZ endpoints in 2.30 fixed point.
        const double xyz[9]={0.4124,0.2126,0.0193,0.3576,0.7152,0.1192,0.1805,0.0722,0.9505};
        for(unsigned i=0;i<9;++i)v.endpoints[i]=std::int32_t(std::lround(xyz[i]*1073741824.0));
        v.gamma={65536,65536,65536};
        const auto linear=Decoded(image::Format::bmp,rf::MakeBmp(v),"BMP calibrated linear");
        require(Near(Rgba(linear.begin(),linear.begin()+4),{188,188,188,255},1),"BMP calibrated linear endpoints not converted");
        require(linear[4]>200&&linear[5]<140&&linear[5]>120,"BMP calibrated linear red not converted");
        v.gamma={0x23333,0x23333,0x23333};
        const auto gamma22=Decoded(image::Format::bmp,rf::MakeBmp(v),"BMP calibrated 2.2");
        // A pure 2.2 power differs from the sRGB curve by up to ~4 codes in the shadows.
        require(Near(gamma22,plain,5)&&gamma22!=plain,"BMP calibrated gamma 2.2 sRGB endpoints drifted");
        v.gamma={0x23333,0,0x23333};
        Expect(image::Format::bmp,rf::MakeBmp(v),image::DecodeError::unsupported_content,"BMP endpoints without tone response admitted");
        v.gamma={65536,65536,65536};v.endpoints[1]=0;
        Expect(image::Format::bmp,rf::MakeBmp(v),image::DecodeError::invalid_layout,"BMP degenerate endpoint admitted");
        rf::Bmp v5=v;v5.header=124;v5.endpoints={};v5.gamma={};v5.colourSpace=0x4d424544;v5.profile=rf::LinearRgbProfile();
        const auto embedded=Decoded(image::Format::bmp,rf::MakeBmp(v5),"BMP embedded profile");
        require(Near(Rgba(embedded.begin(),embedded.begin()+4),{188,188,188,255},1),"BMP V5 embedded ICC not converted");
        require(Near(embedded,linear,1),"BMP embedded and calibrated linear sRGB disagree");
        auto bmp=rf::MakeBmp(v5);bmp.resize(bmp.size()-1);rf::PatchLe32(bmp,2,std::uint32_t(bmp.size()));
        Expect(image::Format::bmp,bmp,image::DecodeError::truncated,"BMP truncated embedded profile admitted");
        bmp=rf::MakeBmp(v5);rf::PatchLe32(bmp,14+112,40);
        Expect(image::Format::bmp,bmp,image::DecodeError::invalid_layout,"BMP profile inside header admitted");
        bmp=rf::MakeBmp(v5);rf::PatchLe32(bmp,14+116,4*1024*1024+1);
        Expect(image::Format::bmp,bmp,image::DecodeError::resource_limit,"BMP profile size bound ignored");
        auto gray=v5;gray.profile=rf::LinearGrayProfile();
        Expect(image::Format::bmp,rf::MakeBmp(gray),image::DecodeError::unsupported_content,"BMP gray profile on RGB admitted");
        auto damaged=v5;rf::Be(damaged.profile,136,0xfffffff0);
        Expect(image::Format::bmp,rf::MakeBmp(damaged),image::DecodeError::invalid_layout,"BMP malformed embedded profile admitted");
        auto v4Embedded=v5;v4Embedded.header=108;v4Embedded.profile.clear();
        Expect(image::Format::bmp,rf::MakeBmp(v4Embedded),image::DecodeError::invalid_layout,"BMP V4 embedded profile admitted");
        auto linked=v5;linked.colourSpace=0x4c494e4b;
        Expect(image::Format::bmp,rf::MakeBmp(linked),image::DecodeError::unsupported_content,"BMP linked profile followed");
        auto unknown=v5;unknown.colourSpace=0x41424344;
        Expect(image::Format::bmp,rf::MakeBmp(unknown),image::DecodeError::unsupported_content,"BMP unknown colour space admitted");
        Sweep(image::Format::bmp,rf::MakeBmp(v5),false,"BMP V5 corruption sweep");
    }
    // Explicit rejections kept.
    {
        auto jpeg=b;jpeg.compression=4;
        Expect(image::Format::bmp,rf::MakeBmp(jpeg),image::DecodeError::unsupported_compression,"BMP embedded JPEG admitted");
        jpeg.compression=5;Expect(image::Format::bmp,rf::MakeBmp(jpeg),image::DecodeError::unsupported_compression,"BMP embedded PNG admitted");
        jpeg.compression=11;Expect(image::Format::bmp,rf::MakeBmp(jpeg),image::DecodeError::unsupported_compression,"BMP unknown compression admitted");
        auto os2v2=rf::MakeBmp(b);rf::PatchLe32(os2v2,14,64);
        Expect(image::Format::bmp,os2v2,image::DecodeError::unsupported_content,"OS/2 2.x header admitted");
        auto odd=b;odd.bits=2;odd.palette=palette;
        Expect(image::Format::bmp,rf::MakeBmp(odd),image::DecodeError::unsupported_content,"BMP 2-bit admitted");
        auto core16=b;core16.header=12;core16.bits=16;core16.pixels=rf::BmpRows(W,H,16,words);
        Expect(image::Format::bmp,rf::MakeBmp(core16),image::DecodeError::unsupported_content,"OS/2 16-bit admitted");
        auto planes=rf::MakeBmp(b);planes[26]=std::byte{2};
        Expect(image::Format::bmp,planes,image::DecodeError::invalid_layout,"BMP planes admitted");
        auto zero=b;zero.width=0;Expect(image::Format::bmp,rf::MakeBmp(zero),image::DecodeError::invalid_dimensions,"BMP zero width");
        zero=b;zero.height=std::numeric_limits<std::int32_t>::min();
        Expect(image::Format::bmp,rf::MakeBmp(zero),image::DecodeError::invalid_dimensions,"BMP minimum height");
        auto big=rf::MakeBmp(b);image::DecodeOptions limit;limit.maximumPixels=W*H-1;
        require(Code(image::Format::bmp,big,limit)==image::DecodeError::invalid_dimensions,"BMP pixel limit");
        big.pop_back();Expect(image::Format::bmp,big,image::DecodeError::truncated,"BMP truncated rows");
        auto shortRows=b;shortRows.pixels.resize(shortRows.pixels.size()-1);
        Expect(image::Format::bmp,rf::MakeBmp(shortRows),image::DecodeError::truncated,"BMP short pixel array");
    }
    // Large-raster callers reach the same decoder.
    {
        image::DecodeOptions large;large.resourceProfile=image::RasterProfile::largeRaster;large.maximumPixels=32ull*1024*1024;
        const auto decoded=image::DecodeImage(image::Format::bmp,rf::MakeBmp(top),large);
        require(decoded&&decoded.image.rgba8==expected,"BMP large-raster profile");
    }
    Sweep(image::Format::bmp,rf::MakeBmp(v4),true,"BMP V4 corruption sweep");
    Sweep(image::Format::bmp,rf::MakeBmp(core),true,"OS/2 BMP corruption sweep");
}

void GifTests(){
    const auto still=rf::Gif2x1();
    const auto one=image::DecodeImage(image::Format::gif,still);
    require(one&&one.image.frameCount==1,"GIF still frame count");
    const auto animated=image::DecodeImage(image::Format::gif,rf::AnimatedGif2x1());
    require(animated&&animated.image.frameCount==2&&animated.image.rgba8==Rgba{0,0,0,255,255,0,0,255},"GIF animation frame count/first frame");
    image::DecodeOptions second;second.frameIndex=1;
    require(Code(image::Format::gif,rf::AnimatedGif2x1(),second)==image::DecodeError::selection_out_of_range,"GIF later frame decoded");
    auto bad=still;bad.push_back(std::byte{0});
    Expect(image::Format::gif,bad,image::DecodeError::trailing_bytes,"GIF bytes after trailer admitted");
    bad=still;bad[19]=std::byte{0x99};
    Expect(image::Format::gif,bad,image::DecodeError::invalid_layout,"GIF unknown block admitted");
    bad=Bytes(still.begin(),still.begin()+19);bad.push_back(std::byte{0x3b});
    Expect(image::Format::gif,bad,image::DecodeError::invalid_layout,"GIF without frames admitted");
    bad=still;bad[24]=std::byte{3};
    Expect(image::Format::gif,bad,image::DecodeError::invalid_layout,"GIF frame outside screen admitted");
    {
        // A later (never decoded) frame overhanging the logical screen is
        // clipped by browsers; it is counted, not rejected.
        auto overhang=rf::AnimatedGif2x1();overhang.at(55)=std::byte{9};
        const auto clipped=image::DecodeImage(image::Format::gif,overhang);
        require(clipped&&clipped.image.frameCount==2&&clipped.image.rgba8==animated.image.rgba8,"GIF later overhanging frame rejected");
        auto chained=rf::AnimatedGif2x1();chained.at(61)=std::byte{200};
        Expect(image::Format::gif,chained,image::DecodeError::truncated,"GIF later frame sub-block overrun admitted");
    }
    bad=still;bad[29]=std::byte{0};
    Expect(image::Format::gif,bad,image::DecodeError::invalid_layout,"GIF LZW code size 0 admitted");
    bad=still;bad[29]=std::byte{12};
    Expect(image::Format::gif,bad,image::DecodeError::invalid_layout,"GIF LZW code size 12 admitted");
    bad=still;bad[30]=std::byte{200};
    Expect(image::Format::gif,bad,image::DecodeError::truncated,"GIF sub-block overrun admitted");
    bad=still;bad[10]=std::byte{0x87};
    Expect(image::Format::gif,bad,image::DecodeError::truncated,"GIF colour table overrun admitted");
    Sweep(image::Format::gif,rf::AnimatedGif2x1(),true,"GIF corruption sweep");
}

void PngTests(){
    // Palette + tRNS, packed bit depths, grayscale keys, gray+alpha and Adam7.
    const std::vector<std::array<std::uint8_t,3>> palette{{255,0,0},{0,255,0},{0,0,255},{9,8,7}};
    Bytes plte;for(const auto& c:palette)for(auto v:c)plte.push_back(std::byte(v));
    const Bytes trns{std::byte{0},std::byte{128}};
    for(const auto size:{std::pair{5u,3u},std::pair{9u,7u}})for(bool adam7:{false,true}){
        const auto w=size.first,h=size.second;
        std::vector<std::uint8_t> indices(w*h);Rgba want(w*h*4);
        for(unsigned p=0;p<w*h;++p){
            indices[p]=std::uint8_t((p*3+p/w)%4);
            for(unsigned c=0;c<3;++c)want[p*4+c]=palette[indices[p]][c];
            want[p*4+3]=indices[p]==0?0:indices[p]==1?128:255;
        }
        const auto png=rf::SamplePng(w,h,2,3,adam7,indices,{{"PLTE",plte},{"tRNS",trns}});
        require(Decoded(image::Format::png,png,"PNG palette tRNS")==want,"PNG palette/tRNS/packing/interlace pixels");
        if(w==5&&!adam7)Sweep(image::Format::png,png,true,"PNG palette corruption sweep");
    }
    {
        constexpr unsigned w=7,h=3;
        std::vector<std::uint8_t> nibbles(w*h),bytes(w*h),pairs(w*h*2),rgb(w*h*3),bits(w*h);
        Rgba gray4(w*h*4),gray8(w*h*4),grayAlpha(w*h*4),rgbKey(w*h*4),gray1(w*h*4);
        for(unsigned p=0;p<w*h;++p){
            nibbles[p]=std::uint8_t(p%16);bytes[p]=std::uint8_t(p*16);bits[p]=std::uint8_t(p%2);
            pairs[p*2]=std::uint8_t(p*9);pairs[p*2+1]=std::uint8_t(255-p*7);
            for(unsigned c=0;c<3;++c)rgb[p*3+c]=std::uint8_t(p%3==0?10*c:p*c+1);
            for(unsigned c=0;c<3;++c){
                gray4[p*4+c]=std::uint8_t(nibbles[p]*17);gray8[p*4+c]=bytes[p];grayAlpha[p*4+c]=pairs[p*2];
                rgbKey[p*4+c]=rgb[p*3+c];gray1[p*4+c]=bits[p]?255:0;
            }
            gray4[p*4+3]=nibbles[p]==5?0:255;gray8[p*4+3]=bytes[p]==0x50?0:255;grayAlpha[p*4+3]=pairs[p*2+1];
            rgbKey[p*4+3]=p%3==0?0:255;gray1[p*4+3]=255;
        }
        require(Decoded(image::Format::png,rf::SamplePng(w,h,4,0,false,nibbles,{{"tRNS",{std::byte{0},std::byte{5}}}}),"PNG gray4")==gray4,"PNG 4-bit gray tRNS key");
        require(Decoded(image::Format::png,rf::SamplePng(w,h,8,0,true,bytes,{{"tRNS",{std::byte{0},std::byte{0x50}}}}),"PNG gray8")==gray8,"PNG 8-bit gray interlaced tRNS key");
        require(Decoded(image::Format::png,rf::SamplePng(w,h,8,4,false,pairs),"PNG gray alpha")==grayAlpha,"PNG gray+alpha");
        require(Decoded(image::Format::png,rf::SamplePng(w,h,8,2,false,rgb,{{"tRNS",{std::byte{0},std::byte{0},std::byte{0},std::byte{10},std::byte{0},std::byte{20}}}}),"PNG rgb key")==rgbKey,"PNG RGB tRNS key");
        require(Decoded(image::Format::png,rf::SamplePng(w,h,1,0,true,bits),"PNG gray1")==gray1,"PNG 1-bit gray interlaced");
    }
    // cHRM / gAMA / sRGB / iCCP precedence.
    const std::vector<std::uint8_t> two{128,128,128,200,50,50};
    const Rgba plain{128,128,128,255,200,50,50,255};
    const auto base=rf::SamplePng(2,1,8,2,false,two);
    require(Decoded(image::Format::png,base,"PNG untagged")==plain,"PNG untagged pixels");
    const auto linearGamma=rf::BeWords({100000});
    const auto srgbChromaticities=rf::BeWords({31270,32900,64000,33000,30000,60000,15000,6000});
    const auto p3=rf::BeWords({31270,32900,68000,32000,26500,69000,15000,6000});
    const auto linear=Decoded(image::Format::png,rf::WithChunk(base,"gAMA",linearGamma),"PNG linear gAMA");
    require(Near(Rgba(linear.begin(),linear.begin()+4),{188,188,188,255},1)&&linear[4]>200&&linear[5]>120&&linear[7]==255,"PNG lone gAMA not converted with sRGB primaries");
    require(Near(Decoded(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",linearGamma),"cHRM",srgbChromaticities),"PNG cHRM+gAMA"),linear,1),"PNG sRGB cHRM + linear gAMA disagree with lone gAMA");
    require(Decoded(image::Format::png,rf::WithChunk(base,"gAMA",rf::BeWords({45455})),"PNG sRGB gAMA")==plain,"PNG sRGB-equivalent gAMA transformed");
    require(Decoded(image::Format::png,rf::WithChunk(base,"gAMA",rf::BeWords({45000})),"PNG rounded gAMA")==plain,"PNG rounded sRGB gAMA transformed");
    require(Decoded(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",rf::BeWords({45455})),"cHRM",srgbChromaticities),"PNG sRGB pair")==plain,"PNG sRGB cHRM+gAMA transformed");
    const auto wide=Decoded(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",rf::BeWords({45455})),"cHRM",p3),"PNG P3");
    require(std::abs(int(wide[0])-int(wide[1]))<=1&&std::abs(int(wide[1])-int(wide[2]))<=1&&std::abs(int(wide[0])-128)<=3,"PNG custom primaries moved neutral grey");
    require(wide[4]>200&&wide[5]<50&&wide[6]<50,"PNG wide-gamut red not converted to sRGB");
    const auto curveOnly=Decoded(image::Format::png,rf::WithChunk(base,"cHRM",p3),"PNG lone cHRM");
    require(Near(Rgba(curveOnly.begin(),curveOnly.begin()+4),{128,128,128,255},1)&&curveOnly[4]>200&&curveOnly[5]<50,"PNG lone cHRM not converted with sRGB curve");
    require(Decoded(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",linearGamma),"sRGB",{std::byte{0}}),"PNG sRGB wins")==plain,"PNG sRGB chunk did not override gAMA");
    const auto icc=rf::IccPayload(rf::LinearRgbProfile());
    const auto iccOnly=Decoded(image::Format::png,rf::WithIcc(base,icc),"PNG iCCP");
    require(Decoded(image::Format::png,rf::WithIcc(rf::WithChunk(rf::WithChunk(base,"gAMA",rf::BeWords({45455})),"cHRM",p3),icc),"PNG iCCP wins")==iccOnly,"PNG iCCP did not override cHRM/gAMA");
    Expect(image::Format::png,rf::WithIcc(rf::WithChunk(base,"sRGB",{std::byte{0}}),icc),image::DecodeError::invalid_layout,"PNG sRGB and iCCP together admitted");
    Expect(image::Format::png,rf::WithChunk(base,"gAMA",rf::BeWords({0})),image::DecodeError::invalid_layout,"PNG zero gAMA admitted");
    Expect(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",linearGamma),"gAMA",linearGamma),image::DecodeError::invalid_layout,"PNG duplicate gAMA admitted");
    Expect(image::Format::png,rf::WithChunk(rf::WithChunk(base,"cHRM",p3),"cHRM",p3),image::DecodeError::invalid_layout,"PNG duplicate cHRM admitted");
    Expect(image::Format::png,rf::WithChunk(base,"cHRM",Bytes(p3.begin(),p3.end()-1)),image::DecodeError::invalid_layout,"PNG short cHRM admitted");
    Expect(image::Format::png,rf::WithChunk(base,"cHRM",rf::BeWords({31270,32900,68000,0,26500,69000,15000,6000})),image::DecodeError::invalid_layout,"PNG zero primary y admitted");
    Expect(image::Format::png,rf::WithChunk(base,"cHRM",rf::BeWords({31270,32900,168000,32000,26500,69000,15000,6000})),image::DecodeError::invalid_layout,"PNG chromaticity above 1 admitted");
    Expect(image::Format::png,rf::WithChunk(base,"cHRM",rf::BeWords({31270,32900,31270,32900,31270,32900,31270,32900})),image::DecodeError::invalid_layout,"PNG degenerate primaries admitted");
    // 16-bit: still gated; the explicit derivative applies the same colour policy.
    const auto deep=rf::WithChunk(rf::Png16(),"gAMA",linearGamma);
    Expect(image::Format::png,deep,image::DecodeError::unsupported_content,"PNG16 gate bypassed by gAMA");
    image::DecodeOptions derivative;derivative.allowPng16BitRenderingDerivative=true;
    const auto reduced=image::DecodeImage(image::Format::png,deep,derivative);
    require(reduced&&reduced.image.sourceBitsPerChannel==16&&Near(reduced.image.rgba8,{0,188,255,1},1),"PNG16 derivative gAMA not converted");
    const auto scalar=image::DecodeScalar16(image::ScalarFormat::png16,rf::WithChunk(rf::WithChunk(rf::Png16(true),"gAMA",linearGamma),"cHRM",p3));
    require(scalar&&scalar.samples==std::vector<std::uint16_t>{0,32896,65535,257},"Scalar PNG remapped by gAMA/cHRM");
    Sweep(image::Format::png,rf::WithChunk(rf::WithChunk(base,"gAMA",linearGamma),"cHRM",p3),true,"PNG colour chunk corruption sweep");
}

void JpegTests(){
    using rf::HandJpeg;
    const auto pixel=[](const Rgba& rgba,unsigned width,unsigned x,unsigned y){
        const auto at=(std::size_t(y)*width+x)*4;return Rgba(rgba.begin()+at,rgba.begin()+at+4);
    };
    // Adobe RGB (transform 0), YCbCr, grayscale, progressive and extended sequential.
    HandJpeg adobe;adobe.adobeTransform=0;adobe.mcus={{200,100,50},{10,20,30}};
    const auto rgb=Decoded(image::Format::jpeg,rf::MakeJpeg(adobe),"Adobe RGB JPEG");
    require(pixel(rgb,16,0,0)==Rgba{200,100,50,255}&&pixel(rgb,16,15,7)==Rgba{10,20,30,255},"Adobe RGB JPEG pixels");
    auto progressive=adobe;progressive.sof=0xc2;
    require(Decoded(image::Format::jpeg,rf::MakeJpeg(progressive),"progressive JPEG")==rgb,"Progressive JPEG pixels");
    auto extended=adobe;extended.sof=0xc1;
    require(Decoded(image::Format::jpeg,rf::MakeJpeg(extended),"extended JPEG")==rgb,"Extended sequential JPEG pixels");
    HandJpeg gray;gray.components=1;gray.mcus={{128},{64}};
    const auto grayPixels=Decoded(image::Format::jpeg,rf::MakeJpeg(gray),"gray JPEG");
    require(pixel(grayPixels,16,0,0)==Rgba{128,128,128,255}&&pixel(grayPixels,16,8,0)==Rgba{64,64,64,255},"Gray JPEG pixels");
    HandJpeg ycc;ycc.mcus={{128,128,128},{128,128,128}};
    ycc.mcus={{128,128,128},{76,85,255}};
    const auto yccPixels=Decoded(image::Format::jpeg,rf::MakeJpeg(ycc),"YCbCr JPEG");
    require(pixel(yccPixels,16,0,0)==Rgba{128,128,128,255}&&Near(pixel(yccPixels,16,8,0),{255,0,0,255},2),"YCbCr JPEG pixels");
    // Explicit rejections: arithmetic, lossless, hierarchical, 12-bit, 2 components.
    for(unsigned sof:{0xc9u,0xcau,0xcbu,0xcdu,0xceu,0xcfu,0xc3u,0xc5u,0xc6u,0xc7u}){
        auto bad=adobe;bad.sof=std::uint8_t(sof);
        Expect(image::Format::jpeg,rf::MakeJpeg(bad),image::DecodeError::unsupported_compression,"Arithmetic/lossless/hierarchical JPEG admitted");
    }
    auto conditioning=adobe;conditioning.arithmeticConditioning=true;
    Expect(image::Format::jpeg,rf::MakeJpeg(conditioning),image::DecodeError::unsupported_compression,"JPEG DAC segment admitted");
    auto twelve=adobe;twelve.sof=0xc1;twelve.precision=12;
    Expect(image::Format::jpeg,rf::MakeJpeg(twelve),image::DecodeError::unsupported_content,"12-bit JPEG admitted");
    auto twoChannels=adobe;twoChannels.components=2;twoChannels.mcus={{1,2},{3,4}};
    Expect(image::Format::jpeg,rf::MakeJpeg(twoChannels),image::DecodeError::unsupported_content,"Two-component JPEG admitted");
    // CMYK/YCCK: stb's inversion (stored values are Adobe-inverted) without a
    // profile; skcms A2B evaluation with one.
    HandJpeg cmyk;cmyk.components=4;cmyk.adobeTransform=0;
    cmyk.mcus={{255,255,255,255},{0,255,255,255},{255,255,255,128},{255,255,255,0}};
    const auto inverted=Decoded(image::Format::jpeg,rf::MakeJpeg(cmyk),"CMYK JPEG");
    require(pixel(inverted,32,0,0)==Rgba{255,255,255,255}&&pixel(inverted,32,8,0)==Rgba{0,255,255,255}
        &&pixel(inverted,32,16,0)==Rgba{128,128,128,255}&&pixel(inverted,32,24,0)==Rgba{0,0,0,255},"CMYK JPEG simple inversion");
    const auto cmykProfile=rf::KOnlyCmykProfile();
    auto managed=cmyk;managed.segments=rf::IccSegments(cmykProfile,2,true);
    const auto managedPixels=Decoded(image::Format::jpeg,rf::MakeJpeg(managed),"CMYK ICC JPEG");
    require(Near(pixel(managedPixels,32,0,0),{255,255,255,255},1)&&Near(pixel(managedPixels,32,8,0),{255,255,255,255},1)
        &&Near(pixel(managedPixels,32,16,0),{119,119,119,255},2)&&Near(pixel(managedPixels,32,24,0),{0,0,0,255},1),"CMYK JPEG A2B profile not applied");
    HandJpeg ycck;ycck.components=4;ycck.adobeTransform=2;ycck.mcus={{0,128,128,255},{255,128,128,255}};
    const auto ycckPlain=Decoded(image::Format::jpeg,rf::MakeJpeg(ycck),"YCCK JPEG");
    require(pixel(ycckPlain,16,0,0)==Rgba{255,255,255,255}&&pixel(ycckPlain,16,8,0)==Rgba{0,0,0,255},"YCCK JPEG simple inversion");
    ycck.segments=rf::IccSegments(cmykProfile,1);
    const auto ycckManaged=Decoded(image::Format::jpeg,rf::MakeJpeg(ycck),"YCCK ICC JPEG");
    require(Near(pixel(ycckManaged,16,0,0),{255,255,255,255},1)&&Near(pixel(ycckManaged,16,8,0),{255,255,255,255},1),"YCCK JPEG A2B profile not applied");
    {
        image::DecodeOptions large;large.resourceProfile=image::RasterProfile::largeRaster;large.maximumPixels=32ull*1024*1024;
        const auto decoded=image::DecodeImage(image::Format::jpeg,rf::MakeJpeg(managed),large);
        require(decoded&&decoded.image.rgba8==managedPixels,"CMYK ICC JPEG under the large-raster profile");
    }
    auto bare=cmyk;bare.adobeTransform=-1;
    Expect(image::Format::jpeg,rf::MakeJpeg(bare),image::DecodeError::unsupported_content,"CMYK JPEG without Adobe marker admitted");
    bare.adobeTransform=1;
    Expect(image::Format::jpeg,rf::MakeJpeg(bare),image::DecodeError::unsupported_content,"Four-component YCbCr JPEG admitted");
    auto wrongProfile=cmyk;wrongProfile.segments=rf::IccSegments(rf::LinearRgbProfile(),1);
    Expect(image::Format::jpeg,rf::MakeJpeg(wrongProfile),image::DecodeError::unsupported_content,"RGB profile on CMYK JPEG admitted");
    // RGB/gray profiles: segmented, out of order, after orientation.
    const auto linearProfile=rf::LinearRgbProfile();
    HandJpeg grey;grey.mcus={{128,128,128},{128,128,128}};
    const auto greyTagged=rf::MakeJpeg([&]{auto j=grey;j.segments=rf::IccSegments(linearProfile,3,true);return j;}());
    const auto converted=Decoded(image::Format::jpeg,greyTagged,"ICC JPEG");
    require(Near(converted,[]{Rgba v(16*8*4,188);for(std::size_t i=3;i<v.size();i+=4)v[i]=255;return v;}(),1),"JPEG ICC profile not converted");
    {
        const auto oriented=image::DecodeImage(image::Format::jpeg,rf::WithOrientation(greyTagged,6));
        require(oriented&&oriented.image.width==8&&oriented.image.height==16&&Near(oriented.image.rgba8,converted,0),"JPEG ICC with Exif orientation");
        const auto stbTagged=rf::InsertAfterSoi(rf::Encode(image::Format::jpeg,32,16),rf::IccSegments(linearProfile,2));
        const auto stbConverted=image::DecodeImage(image::Format::jpeg,stbTagged);
        const auto stbPlain=image::DecodeImage(image::Format::jpeg,rf::Encode(image::Format::jpeg,32,16));
        require(stbConverted&&stbPlain&&stbConverted.image.rgba8!=stbPlain.image.rgba8,"stb-written JPEG ICC not applied");
    }
    auto grayTagged=gray;grayTagged.segments=rf::IccSegments(rf::LinearGrayProfile(),1);
    const auto grayConverted=Decoded(image::Format::jpeg,rf::MakeJpeg(grayTagged),"gray ICC JPEG");
    require(Near(pixel(grayConverted,16,0,0),{188,188,188,255},1),"Gray JPEG gray profile not converted");
    grayTagged.segments=rf::IccSegments(linearProfile,1);
    Expect(image::Format::jpeg,rf::MakeJpeg(grayTagged),image::DecodeError::unsupported_content,"RGB profile on gray JPEG admitted");
    auto rgbGray=grey;rgbGray.segments=rf::IccSegments(rf::LinearGrayProfile(),1);
    Expect(image::Format::jpeg,rf::MakeJpeg(rgbGray),image::DecodeError::unsupported_content,"Gray profile on RGB JPEG admitted");
    auto rgbCmyk=grey;rgbCmyk.segments=rf::IccSegments(cmykProfile,1);
    Expect(image::Format::jpeg,rf::MakeJpeg(rgbCmyk),image::DecodeError::unsupported_content,"CMYK profile on RGB JPEG admitted");
    // Segment assembly errors.
    const auto withSegments=[&](std::vector<Bytes> segments){auto j=grey;j.segments=std::move(segments);return rf::MakeJpeg(j);};
    auto segments=rf::IccSegments(linearProfile,3);
    Expect(image::Format::jpeg,withSegments({segments[0],segments[2]}),image::DecodeError::invalid_layout,"JPEG missing ICC segment admitted");
    Expect(image::Format::jpeg,withSegments({segments[0],segments[1],segments[1],segments[2]}),image::DecodeError::invalid_layout,"JPEG duplicate ICC segment admitted");
    auto zeroSequence=segments;zeroSequence[0][16]=std::byte{0};
    Expect(image::Format::jpeg,withSegments(zeroSequence),image::DecodeError::invalid_layout,"JPEG ICC sequence 0 admitted");
    auto beyond=segments;beyond[0][16]=std::byte{4};
    Expect(image::Format::jpeg,withSegments(beyond),image::DecodeError::invalid_layout,"JPEG ICC sequence beyond count admitted");
    auto mismatch=segments;mismatch[1][17]=std::byte{4};
    Expect(image::Format::jpeg,withSegments(mismatch),image::DecodeError::invalid_layout,"JPEG ICC count mismatch admitted");
    Expect(image::Format::jpeg,withSegments({rf::Segment(0xe2,{std::byte{'I'},std::byte{'C'},std::byte{'C'},std::byte{'_'},std::byte{'P'},std::byte{'R'},
        std::byte{'O'},std::byte{'F'},std::byte{'I'},std::byte{'L'},std::byte{'E'},std::byte{0},std::byte{1}})}),image::DecodeError::invalid_layout,"JPEG short ICC segment admitted");
    auto sized=linearProfile;rf::Be(sized,0,std::uint32_t(sized.size()+1));
    Expect(image::Format::jpeg,withSegments(rf::IccSegments(sized,1)),image::DecodeError::invalid_layout,"JPEG ICC size mismatch admitted");
    auto overrun=linearProfile;rf::Be(overrun,136,0xfffffff0);
    Expect(image::Format::jpeg,withSegments(rf::IccSegments(overrun,2)),image::DecodeError::invalid_layout,"JPEG ICC tag overrun admitted");
    {
        std::vector<Bytes> huge;
        for(unsigned i=0;i<65;++i){
            Bytes payload;for(char c:std::string_view("ICC_PROFILE\0",12))payload.push_back(std::byte(c));
            payload.push_back(std::byte(i+1));payload.push_back(std::byte{65});payload.resize(65533);
            huge.push_back(rf::Segment(0xe2,payload));
        }
        Expect(image::Format::jpeg,withSegments(huge),image::DecodeError::resource_limit,"JPEG ICC 4 MiB bound ignored");
    }
    Sweep(image::Format::jpeg,greyTagged,false,"JPEG ICC corruption sweep");
    Sweep(image::Format::jpeg,rf::MakeJpeg(managed),false,"CMYK JPEG corruption sweep");
}
}
void RunWidenedRasterTests(){
    BmpTests();
    GifTests();
    PngTests();
    JpegTests();
    std::cout<<"BMP RLE/bitfields/OS2/top-down/colour spaces, GIF frame counts, PNG cHRM/gAMA/tRNS, JPEG ICC/CMYK/progressive and rejections passed\n";
}
