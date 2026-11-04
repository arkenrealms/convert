#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
// --- Container builders for the broadened policy (all fixtures built in code). ---
Bytes Serial(const char* type,std::span<const std::byte> payload) {
    Bytes b(8);Tag(b,0,type);Put(b,4,std::uint32_t(payload.size()));
    b.insert(b.end(),payload.begin(),payload.end());
    if(payload.size()&1)b.push_back(std::byte{});
    return b;
}
Bytes Riff(std::initializer_list<Bytes> chunks) {
    Bytes b(12);Tag(b,0,"RIFF");Tag(b,8,"WEBP");
    for(const auto& c:chunks)b.insert(b.end(),c.begin(),c.end());
    Put(b,4,std::uint32_t(b.size()-8));return b;
}
Bytes Vp8x(std::uint8_t flags,unsigned w,unsigned h) {
    Bytes p(10);p[0]=std::byte(flags);Put(p,4,w-1,3);Put(p,7,h-1,3);return Serial("VP8X",p);
}
// Image chunks of an encoded file (ALPH+VP8 or VP8L), without RIFF/VP8X.
Bytes ImageChunks(const Bytes& file) {
    const std::size_t at=std::memcmp(file.data()+12,"VP8X",4)?12:30;return Bytes(file.begin()+std::ptrdiff_t(at),file.end());
}
Bytes Anim(std::array<std::uint8_t,4> rgba) {
    Bytes p(6);p[0]=std::byte(rgba[2]);p[1]=std::byte(rgba[1]);p[2]=std::byte(rgba[0]);p[3]=std::byte(rgba[3]);return Serial("ANIM",p);
}
Bytes Anmf(unsigned x,unsigned y,unsigned w,unsigned h,bool blend,const Bytes& image) {
    Bytes p(16);Put(p,0,x/2,3);Put(p,3,y/2,3);Put(p,6,w-1,3);Put(p,9,h-1,3);Put(p,12,100,3);
    p[15]=std::byte(blend?0:2);p.insert(p.end(),image.begin(),image.end());return Serial("ANMF",p);
}
void PutBe(Bytes& b,std::size_t p,std::uint32_t n){for(unsigned i=0;i<4;++i)b.at(p+i)=std::byte((n>>(24-8*i))&255);}
std::uint32_t Fixed(double v){return std::uint32_t(std::int32_t(std::lround(v*65536.0)));}
// Minimal ICC v4 display profile: Display P3 primaries (D50-adapted) with the sRGB
// parametric curve, or a gray profile (kTRC) that must be rejected as non-RGB.
Bytes Icc(bool gray=false) {
    Bytes b(132);
    const auto addTags=[&](std::vector<std::pair<const char*,Bytes>> tags) {
        PutBe(b,128,std::uint32_t(tags.size()));b.resize(132+12*tags.size());
        for(std::size_t i=0;i<tags.size();++i){
            Tag(b,132+12*i,tags[i].first);PutBe(b,136+12*i,std::uint32_t(b.size()));PutBe(b,140+12*i,std::uint32_t(tags[i].second.size()));
            b.insert(b.end(),tags[i].second.begin(),tags[i].second.end());
            while(b.size()%4)b.push_back(std::byte{});
        }
    };
    PutBe(b,8,0x04300000u);Tag(b,12,"mntr");Tag(b,16,gray?"GRAY":"RGB ");Tag(b,20,"XYZ ");Tag(b,36,"acsp");
    PutBe(b,68,Fixed(0.9642));PutBe(b,72,Fixed(1.0));PutBe(b,76,Fixed(0.8249));
    Bytes para(32);Tag(para,0,"para");para[9]=std::byte{3};
    const double curve[]={2.4,1/1.055,0.055/1.055,1/12.92,0.04045};
    for(unsigned i=0;i<5;++i)PutBe(para,12+4*i,Fixed(curve[i]));
    const auto xyz=[](double x,double y,double z){Bytes t(20);Tag(t,0,"XYZ ");PutBe(t,8,Fixed(x));PutBe(t,12,Fixed(y));PutBe(t,16,Fixed(z));return t;};
    if(gray)addTags({{"kTRC",para}});
    else addTags({{"rXYZ",xyz(0.515102,0.241182,-0.001050)},{"gXYZ",xyz(0.291965,0.692236,0.041882)},
        {"bXYZ",xyz(0.157153,0.066582,0.784378)},{"rTRC",para},{"gTRC",para},{"bTRC",para}});
    PutBe(b,0,std::uint32_t(b.size()));return b;
}
std::uint32_t Crc(std::span<const std::byte> b) {
    std::uint32_t c=0xffffffffu;
    for(auto v:b){c^=std::to_integer<std::uint32_t>(v);for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
    return ~c;
}
// The PNG path is the reference for ICC handling: the same RGBA8 and profile
// stored as PNG iCCP (zlib stored block) must decode to identical sRGB pixels.
I::DecodeResult PngWithIcc(unsigned w,unsigned h,const std::vector<std::uint8_t>& rgba,const Bytes& icc) {
    const auto png=I::EncodeImage(I::Format::png,w,h,rgba);Check(bool(png),"PNG reference encode failed");
    const auto n=std::uint32_t(icc.size());Check(n<65536,"reference ICC too large for one stored block");
    Bytes data{std::byte{'p'},std::byte{'3'},std::byte{0},std::byte{0},std::byte{0x78},std::byte{0x01},std::byte{1},
        std::byte(n&255),std::byte(n>>8),std::byte(~n&255),std::byte((~n>>8)&255)};
    data.insert(data.end(),icc.begin(),icc.end());
    std::uint32_t a=1,s=0;
    for(auto v:icc){a=(a+std::to_integer<std::uint32_t>(v))%65521;s=(s+a)%65521;}
    data.resize(data.size()+4);PutBe(data,data.size()-4,(s<<16)|a);
    Bytes chunk(8);PutBe(chunk,0,std::uint32_t(data.size()));Tag(chunk,4,"iCCP");chunk.insert(chunk.end(),data.begin(),data.end());
    chunk.resize(chunk.size()+4);PutBe(chunk,chunk.size()-4,Crc(std::span(chunk).subspan(4,chunk.size()-8)));
    Bytes file(png.bytes.begin(),png.bytes.begin()+33);file.insert(file.end(),chunk.begin(),chunk.end());
    file.insert(file.end(),png.bytes.begin()+33,png.bytes.end());
    auto d=I::DecodeImage(I::Format::png,file);Check(bool(d),"PNG iCCP reference rejected");return d;
}
std::vector<std::uint8_t> Smooth(unsigned w,unsigned h) {
    std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const auto p=(std::size_t(y)*w+x)*4;
        out[p]=std::uint8_t(x*255/(w-1));out[p+1]=std::uint8_t(y*255/(h-1));out[p+2]=std::uint8_t(128+(x+y)%32);
        out[p+3]=x<w/4?0:(x<w/2?std::uint8_t(64+y):255);
    }
    return out;
}
I::DecodeError Code(const Bytes& b,const I::DecodeOptions& o={}){return I::DecodeImage(I::Format::webp,b,o).issue.code;}
// Every truncation fails without pixels; byte mutations either fail without
// pixels or produce a complete, correctly sized image.
void Sweep(const Bytes& file,const char* what) {
    for(std::size_t i=0;i<file.size();++i){
        const auto d=I::DecodeImage(I::Format::webp,std::span(file).first(i));Check(!d&&d.image.rgba8.empty(),what);
    }
    for(std::size_t i=12;i<file.size();i+=std::max<std::size_t>(1,file.size()/400)){
        for(auto mask:{std::byte{0x80},std::byte{0x01},std::byte{0xff}}){
            auto bad=file;bad[i]^=mask;const auto d=I::DecodeImage(I::Format::webp,bad);
            Check(d||d.image.rgba8.empty(),"failed mutation retained pixels");
            if(d)Check(d.image.rgba8.size()==std::size_t(d.image.width)*d.image.height*4,"mutation returned wrong extent");
        }
    }
}
bool HasTag(const Bytes& b,const char* tag){
    return std::search(b.begin(),b.end(),reinterpret_cast<const std::byte*>(tag),reinterpret_cast<const std::byte*>(tag)+4)!=b.end();
}
void IccPolicy(const Bytes& lossless,const Bytes& lossy,const Bytes& body) {
    using E=I::DecodeError;const auto icc=Icc();
    // ICCP: lossless and lossy inputs convert to sRGB exactly like PNG iCCP; alpha untouched.
    for(const auto* source:{&lossless,&lossy}){
        const auto plain=I::DecodeImage(I::Format::webp,*source);
        const auto tagged=Riff({Vp8x(0x30,31,17),Serial("ICCP",icc),ImageChunks(*source)});
        const auto d=I::DecodeImage(I::Format::webp,tagged);Check(bool(d)&&d.image.width==31&&d.image.height==17,"ICC-tagged WebP rejected");
        Check(d.image.rgba8==PngWithIcc(31,17,plain.image.rgba8,icc).image.rgba8,"WebP ICC conversion differs from the PNG path");
        Check(d.image.rgba8!=plain.image.rgba8,"Display P3 profile was not applied");
        bool alpha=true;
        for(std::size_t i=3;i<d.image.rgba8.size();i+=4)alpha&=d.image.rgba8[i]==plain.image.rgba8[i];
        Check(alpha,"ICC conversion changed alpha");
        Sweep(tagged,"truncated ICC WebP admitted");
    }
    {   // Known colours: P3 gray stays gray; P3 pure green clips to sRGB green.
        const std::vector<std::uint8_t> two{128,128,128,255,0,255,0,77};
        const auto e=I::EncodeImage(I::Format::webp,2,1,two);
        const auto d=I::DecodeImage(I::Format::webp,Riff({Vp8x(0x30,2,1),Serial("ICCP",icc),ImageChunks(e.bytes)}));
        Check(bool(d),"known-colour ICC fixture rejected");const auto& p=d.image.rgba8;
        Check(std::abs(p[0]-128)<=1&&std::abs(p[1]-128)<=1&&std::abs(p[2]-128)<=1&&p[3]==255,"P3 gray not neutral");
        Check(p[4]==0&&p[5]==255&&p[6]==0&&p[7]==77,"P3 green not mapped to clipped sRGB green");
    }
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",Icc(true)),body}))==E::unsupported_content,"gray ICC admitted");
    Bytes garbage(200);PutBe(garbage,0,200);
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",garbage),body}))==E::invalid_layout,"malformed ICC admitted");
    auto lying=icc;PutBe(lying,0,std::uint32_t(icc.size()+4));
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",lying),body}))==E::invalid_layout,"ICC size mismatch admitted");
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",{}),body}))==E::invalid_layout,"empty ICCP admitted");
    Check(Code(Riff({Vp8x(0x30,31,17),body}))==E::invalid_layout,"ICC flag without ICCP admitted");
    Check(Code(Riff({Vp8x(0x10,31,17),Serial("ICCP",icc),body}))==E::invalid_layout,"ICCP without flag admitted");
    Check(Code(Riff({Vp8x(0x30,31,17),body,Serial("ICCP",icc)}))==E::invalid_layout,"ICCP after image admitted");
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",icc),Serial("ICCP",icc),body}))==E::invalid_layout,"duplicate ICCP admitted");
    Bytes huge(4*1024*1024+2);PutBe(huge,0,std::uint32_t(huge.size()));
    Check(Code(Riff({Vp8x(0x30,31,17),Serial("ICCP",huge),body}))==E::resource_limit,"oversized ICC admitted");
}
void MetadataPolicy(const std::vector<std::uint8_t>& pixels,const Bytes& body,const Bytes& lossy) {
    using E=I::DecodeError;
    // EXIF/XMP: ignored; Exif orientation 6 (rotate 90) is NOT applied.
    const unsigned char exifData[]={'M','M',0,42,0,0,0,8,0,1,0x01,0x12,0,3,0,0,0,1,0,6,0,0,0,0,0,0};
    const auto exif=std::as_bytes(std::span(exifData));const std::string xmpText="<x:xmpmeta xmlns:x='adobe:ns:meta/'/>";
    const auto xmp=std::as_bytes(std::span(xmpText.data(),xmpText.size()));
    const auto d=I::DecodeImage(I::Format::webp,Riff({Vp8x(0x1c,31,17),body,Serial("EXIF",exif),Serial("XMP ",xmp)}));
    Check(bool(d)&&d.image.width==31&&d.image.height==17&&d.image.rgba8==pixels,"EXIF/XMP not ignored or orientation applied");
    const auto icc=Icc();
    const auto both=Riff({Vp8x(0x3c,31,17),Serial("ICCP",icc),body,Serial("EXIF",exif),Serial("XMP ",xmp)});
    Check(I::DecodeImage(I::Format::webp,both).image.rgba8==PngWithIcc(31,17,pixels,icc).image.rgba8,"ICC with EXIF/XMP failed");
    Check(Code(Riff({Vp8x(0x10,31,17),body,Serial("EXIF",exif)}))==E::invalid_layout,"EXIF without flag admitted");
    Check(Code(Riff({Vp8x(0x18,31,17),body}))==E::invalid_layout,"EXIF flag without chunk admitted");
    Check(Code(Riff({Vp8x(0x18,31,17),body,Serial("EXIF",exif),Serial("EXIF",exif)}))==E::invalid_layout,"duplicate EXIF admitted");
    Check(Code(Riff({Vp8x(0x18,31,17),Serial("EXIF",exif),body}))==E::none,"EXIF before image rejected");
    // Unknown chunks are skipped in simple and extended files, but never between ALPH and VP8.
    const std::array<std::byte,3> junk{std::byte{1},std::byte{2},std::byte{3}};
    Check(I::DecodeImage(I::Format::webp,Riff({body,Serial("abcd",junk)})).image.rgba8==pixels,"unknown chunk after simple image rejected");
    Check(I::DecodeImage(I::Format::webp,Riff({Vp8x(0x10,31,17),Serial("abcd",junk),body,Serial("wxyz",{})})).image.rgba8==pixels,"unknown chunks rejected");
    const auto alphaBody=ImageChunks(lossy);Check(std::memcmp(alphaBody.data(),"ALPH",4)==0,"lossy fixture lacks ALPH");
    const auto alphSize=std::to_integer<std::size_t>(alphaBody[4])|(std::to_integer<std::size_t>(alphaBody[5])<<8)|(std::to_integer<std::size_t>(alphaBody[6])<<16);
    const auto split=std::ptrdiff_t(8+alphSize+(alphSize&1));
    Bytes interleaved(alphaBody.begin(),alphaBody.begin()+split);const auto junkChunk=Serial("abcd",junk);
    interleaved.insert(interleaved.end(),junkChunk.begin(),junkChunk.end());interleaved.insert(interleaved.end(),alphaBody.begin()+split,alphaBody.end());
    Check(Code(Riff({Vp8x(0x10,31,17),interleaved}))==E::invalid_layout,"chunk between ALPH and VP8 admitted");
    Check(I::DecodeImage(I::Format::webp,Riff({Vp8x(0x10,31,17),Serial("abcd",junk),alphaBody})).image.rgba8==I::DecodeImage(I::Format::webp,lossy).image.rgba8,"unknown chunk before ALPH rejected");
}
void AnimationPolicy(const Bytes& body) {
    using E=I::DecodeError;
    // Animation: first frame composited on the declared background; frameCount reported.
    const std::array<std::uint8_t,4> background{10,20,30,128};
    I::EncodeOptions lossyOptions;lossyOptions.webpLossless=false;
    const auto frame=Pixels(17,19);const auto first=I::EncodeImage(I::Format::webp,17,19,frame);
    const auto second=I::EncodeImage(I::Format::webp,40,30,Pixels(40,30),lossyOptions);
    Check(bool(first)&&bool(second),"animation frame encode failed");
    const auto firstBody=ImageChunks(first.bytes);
    const auto build=[&](bool blend,const Bytes& firstChunks,unsigned frames){
        Bytes b=Riff({Vp8x(0x12,40,30),Anim(background),Anmf(6,4,17,19,blend,firstChunks)});
        for(unsigned i=1;i<frames;++i){const auto c=Anmf(0,0,40,30,true,ImageChunks(second.bytes));b.insert(b.end(),c.begin(),c.end());}
        Put(b,4,std::uint32_t(b.size()-8));return b;
    };
    const auto expect=[&](bool blend,const std::vector<std::uint8_t>& source){
        std::vector<std::uint8_t> out(40*30*4);
        for(std::size_t i=0;i<out.size();i+=4)std::copy(background.begin(),background.end(),out.begin()+std::ptrdiff_t(i));
        for(unsigned y=0;y<19;++y)for(unsigned x=0;x<17;++x){
            const auto* s=source.data()+(y*17+x)*4;auto* d=out.data()+((4+y)*40+6+x)*4;
            if(!blend){std::copy(s,s+4,d);continue;}
            const double sa=s[3]/255.0,da=d[3]/255.0*(1-sa),a=sa+da;
            for(unsigned c=0;c<3;++c)d[c]=a==0?0:std::uint8_t(std::lround((s[c]*sa+d[c]*da)/a));
            d[3]=std::uint8_t(std::lround(a*255));
        }
        return out;
    };
    const auto still=build(false,firstBody,3);
    const auto d=I::DecodeImage(I::Format::webp,still);
    Check(bool(d)&&d.image.width==40&&d.image.height==30&&d.image.frameCount==3,"animated WebP canvas/frameCount wrong");
    Check(d.image.rgba8==expect(false,frame),"animated first frame (no blend) composition wrong");
    const auto blended=I::DecodeImage(I::Format::webp,build(true,firstBody,2));
    const auto reference=expect(true,frame);bool close=bool(blended)&&blended.image.frameCount==2;
    for(std::size_t i=0;close&&i<reference.size();++i)close=std::abs(int(reference[i])-int(blended.image.rgba8[i]))<=1;
    Check(close,"animated first frame alpha blend wrong");
    const auto lossyFirst=I::EncodeImage(I::Format::webp,17,19,frame,lossyOptions);
    const auto lossyFrame=I::DecodeImage(I::Format::webp,build(false,ImageChunks(lossyFirst.bytes),1));
    Check(bool(lossyFrame)&&lossyFrame.image.frameCount==1
        &&lossyFrame.image.rgba8==expect(false,I::DecodeImage(I::Format::webp,lossyFirst.bytes).image.rgba8),"lossy ALPH frame wrong");
    const auto icc=Icc();
    const auto iccAnimation=Riff({Vp8x(0x32,40,30),Serial("ICCP",icc),Anim(background),Anmf(6,4,17,19,false,firstBody)});
    Check(I::DecodeImage(I::Format::webp,iccAnimation).image.rgba8==PngWithIcc(40,30,expect(false,frame),icc).image.rgba8,"ICC animation not converted");
    // Unknown chunk inside a frame after its bitstream is skipped.
    auto inner=firstBody;const auto extra=Serial("abcd",{});inner.insert(inner.end(),extra.begin(),extra.end());
    Check(I::DecodeImage(I::Format::webp,build(false,inner,1)).image.rgba8==expect(false,frame),"unknown frame subchunk rejected");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background),Anmf(24,4,17,19,false,firstBody)}))==E::invalid_layout,"frame outside canvas admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background),Anmf(6,4,18,19,false,firstBody)}))==E::invalid_layout,"frame size mismatch admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anmf(6,4,17,19,false,firstBody),Anim(background)}))==E::invalid_layout,"ANMF before ANIM admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background)}))==E::unsupported_content,"frameless animation admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background),Anim(background),Anmf(6,4,17,19,false,firstBody)}))==E::invalid_layout,"duplicate ANIM admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background),body}))==E::invalid_layout,"top-level image in animation admitted");
    Check(Code(Riff({Vp8x(0x10,40,30),Anim(background),Anmf(6,4,17,19,false,firstBody)}))==E::invalid_layout,"ANIM without animation flag admitted");
    Check(Code(Riff({Vp8x(0x12,40,30),Anim(background),Anmf(6,4,17,19,false,{})}))==E::invalid_layout,"empty frame admitted");
    I::DecodeOptions o;o.maximumPixels=40*30-1;Check(Code(still,o)==E::invalid_dimensions,"canvas pixel limit ignored");
    o={};o.maximumDimension=39;Check(Code(still,o)==E::invalid_dimensions,"canvas dimension limit ignored");
    o={};o.frameIndex=1;Check(Code(still,o)==E::selection_out_of_range,"frame selection admitted");
    o={};o.maximumSourceBytes=still.size()-1;Check(Code(still,o)==E::input_too_large,"animation source budget ignored");
    // Frame count is bounded at 4096; total chunks (frame subchunks included) at 16384.
    const std::vector<std::uint8_t> dot{9,8,7,255};const auto dotBody=ImageChunks(I::EncodeImage(I::Format::webp,1,1,dot).bytes);
    Bytes many=Riff({Vp8x(0x12,40,30),Anim(background)});const auto f=Anmf(2,2,1,1,false,dotBody);
    for(unsigned i=0;i<4096;++i)many.insert(many.end(),f.begin(),f.end());
    Put(many,4,std::uint32_t(many.size()-8));
    const auto most=I::DecodeImage(I::Format::webp,many);
    Check(bool(most)&&most.image.frameCount==4096&&most.image.rgba8[(2*40+2)*4]==9&&most.image.rgba8[0]==10,"4096-frame animation rejected");
    many.insert(many.end(),f.begin(),f.end());Put(many,4,std::uint32_t(many.size()-8));
    Check(Code(many)==E::resource_limit,"unbounded frame count admitted");
    Bytes chunky=Riff({body});const auto empty=Serial("abcd",{});
    for(unsigned i=0;i<16383;++i)chunky.insert(chunky.end(),empty.begin(),empty.end());
    Put(chunky,4,std::uint32_t(chunky.size()-8));Check(Code(chunky)==E::none,"16384 chunks rejected");
    chunky.insert(chunky.end(),empty.begin(),empty.end());Put(chunky,4,std::uint32_t(chunky.size()-8));
    Check(Code(chunky)==E::resource_limit,"unbounded chunk count admitted");
    Sweep(still,"truncated animation admitted");
}
void LossyEncodePolicy() {
    // Lossy encode: quality range, PSNR, exact alpha, determinism, output limit.
    const auto smooth=Smooth(64,48);
    I::EncodeOptions lossyOptions;lossyOptions.webpLossless=false;
    for(std::uint32_t quality:{1u,50u,90u,100u}){
        I::EncodeOptions o;o.webpLossless=false;o.webpQuality=quality;
        const auto e=I::EncodeImage(I::Format::webp,64,48,smooth,o);Check(bool(e),"lossy encode failed");
        Check(HasTag(e.bytes,"VP8 ")&&HasTag(e.bytes,"ALPH")&&!HasTag(e.bytes,"VP8L"),"lossy output is not VP8+ALPH");
        Check(I::EncodeImage(I::Format::webp,64,48,smooth,o).bytes==e.bytes,"lossy WebP not deterministic");
        const auto d=I::DecodeImage(I::Format::webp,e.bytes);Check(bool(d)&&d.image.width==64&&d.image.height==48,"lossy round trip rejected");
        double error=0;std::size_t samples=0;bool alpha=true;
        for(std::size_t i=0;i<smooth.size();i+=4){
            alpha&=d.image.rgba8[i+3]==smooth[i+3];
            if(smooth[i+3]!=255)continue;
            for(unsigned c=0;c<3;++c){const double v=double(d.image.rgba8[i+c])-smooth[i+c];error+=v*v;++samples;}
        }
        const double psnr=10*std::log10(255.0*255.0/std::max(error/double(samples),1e-9));
        Check(alpha,"lossy WebP alpha not preserved");
        Check(psnr>(quality>=90?38.0:quality>=50?32.0:20.0),"lossy WebP PSNR too low");
        if(quality==90)Check(I::EncodeImage(I::Format::webp,64,48,smooth,lossyOptions).bytes==e.bytes,"default webpQuality is not 90");
        auto limited=o;limited.maximumOutputBytes=e.bytes.size()-1;
        const auto cut=I::EncodeImage(I::Format::webp,64,48,smooth,limited);
        Check(cut.error==I::EncodeError::output_limit&&cut.bytes.empty(),"lossy output budget ignored");
        limited.maximumOutputBytes=e.bytes.size();
        Check(I::EncodeImage(I::Format::webp,64,48,smooth,limited).bytes==e.bytes,"exact lossy budget rejected");
    }
    for(bool losslessMode:{false,true})for(std::uint32_t quality:{0u,101u,1000u}){
        I::EncodeOptions o;o.webpLossless=losslessMode;o.webpQuality=quality;
        const auto e=I::EncodeImage(I::Format::webp,64,48,smooth,o);
        Check(e.error==I::EncodeError::invalid_options&&e.bytes.empty(),"out-of-range webpQuality admitted");
    }
    // Opaque input: no ALPH chunk is written and decoded alpha is exactly 255.
    std::vector<std::uint8_t> opaque(64*48*4,200);
    for(std::size_t i=3;i<opaque.size();i+=4)opaque[i]=255;
    const auto e=I::EncodeImage(I::Format::webp,64,48,opaque,lossyOptions);const auto d=I::DecodeImage(I::Format::webp,e.bytes);
    bool near=bool(d)&&!HasTag(e.bytes,"ALPH");
    for(std::size_t i=0;near&&i<opaque.size();++i)near=i%4==3?d.image.rgba8[i]==255:std::abs(int(d.image.rgba8[i])-200)<=2;
    Check(near,"opaque lossy round trip drifted");
    const I::EncodeOptions defaults;Check(defaults.webpLossless&&defaults.webpQuality==90,"WebP defaults changed");
    const auto lossless=I::EncodeImage(I::Format::webp,64,48,smooth);
    Check(HasTag(lossless.bytes,"VP8L")&&I::DecodeImage(I::Format::webp,lossless.bytes).image.rgba8==smooth,"lossless default not exact");
    Sweep(I::EncodeImage(I::Format::webp,64,48,smooth,lossyOptions).bytes,"truncated lossy WebP admitted");
}
void Policy() {
    const auto pixels=Pixels(31,17);const auto lossless=I::EncodeImage(I::Format::webp,31,17,pixels);
    I::EncodeOptions lossyOptions;lossyOptions.webpLossless=false;
    const auto lossy=I::EncodeImage(I::Format::webp,31,17,pixels,lossyOptions);
    Check(bool(lossless)&&bool(lossy),"policy fixtures failed");
    const auto body=ImageChunks(lossless.bytes);
    IccPolicy(lossless.bytes,lossy.bytes,body);
    MetadataPolicy(pixels,body,lossy.bytes);
    AnimationPolicy(body);
    LossyEncodePolicy();
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
    // Flags declaring animation, EXIF or ICC without the matching chunks are malformed.
    for(auto flag:{0x02,0x08,0x20}){wrong=bad;wrong[20]|=std::byte(flag);Check(I::DecodeImage(I::Format::webp,wrong).issue.code==I::DecodeError::invalid_layout,"inconsistent metadata/frame flag admitted");}
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
    Policy();
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
