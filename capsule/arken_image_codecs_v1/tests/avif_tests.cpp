#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

// Native qualification of the memory-only still-AVIF decoder and encoder against the pinned
// libheif source tree (examples/example.avif, tests/data and fuzzing/data/corpus).
namespace {
namespace I=arken::image::v1;
namespace fs=std::filesystem;
using Bytes=std::vector<std::byte>;
using Pixels=std::vector<std::uint8_t>;
unsigned checks=0;
void Check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
Bytes Read(const fs::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);Check(bool(f),"cannot open "+p.string());
    const auto size=f.tellg();Check(size>0&&size<=32*1024*1024,"fixture size out of range: "+p.string());
    Bytes b{std::size_t(size)};f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),size);Check(bool(f),"fixture read failed");return b;
}
std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t h=14695981039346656037ull;
    for(const auto b:bytes){h^=b;h*=1099511628211ull;}
    return h;
}
I::DecodeResult Decode(const Bytes& b,I::DecodeOptions o={}){return I::DecodeImage(I::Format::avif,b,o);}
void Failed(const I::DecodeResult& r,const std::string& what) {
    Check(!r&&r.image.rgba8.empty()&&!r.image.width&&!r.image.height,what+": rejection leaked pixels or dimensions");
}
I::DecodeError Rejection(const Bytes& b,const std::string& what){const auto r=Decode(b);Failed(r,what);return r.issue.code;}
bool Opaque(const Pixels& p){for(std::size_t i=3;i<p.size();i+=4)if(p[i]!=255)return false;return true;}

void Sample(const fs::path& path) {
    const auto bytes=Read(path);
    const auto decoded=Decode(bytes);
    Check(bool(decoded),std::string("accepted sample did not decode: ")+std::string(I::ErrorMessage(decoded.issue.code)));
    Check(decoded.image.width==800&&decoded.image.height==533,"sample dimensions changed");
    Check(decoded.image.frameCount==1&&decoded.image.sourceBitsPerChannel==8,"sample top-level image count or bit depth changed");
    Check(decoded.image.rgba8.size()==std::size_t(800)*533*4,"sample RGBA extent mismatch");
    // Parity with the accepted AVIF dependency milestone (avif-deps-1).
    Check(Fnv1a64(decoded.image.rgba8)==0xfa6ee36f0fba9dfdull,"decoded pixels differ from the accepted dependency fingerprint");
    Check(Opaque(decoded.image.rgba8),"sample without alpha produced non-opaque pixels");
    Check(Decode(bytes).image.rgba8==decoded.image.rgba8,"repeated decode is not deterministic");

    I::DecodeOptions o;o.maximumPixels=400*533;
    auto r=Decode(bytes,o);Failed(r,"pixel limit");Check(r.issue.code==I::DecodeError::invalid_dimensions,"pixel limit not reported as dimensions");
    o={};o.maximumDimension=799;r=Decode(bytes,o);Failed(r,"dimension limit");Check(r.issue.code==I::DecodeError::invalid_dimensions,"dimension limit not reported");
    o={};o.frameIndex=1;r=Decode(bytes,o);Failed(r,"frame selection");Check(r.issue.code==I::DecodeError::selection_out_of_range,"frame selection accepted");
    o={};o.maximumSourceBytes=bytes.size()-1;r=Decode(bytes,o);Check(r.issue.code==I::DecodeError::input_too_large,"source byte limit ignored");
    o={};o.resourceProfile=I::RasterProfile::largeRaster;r=Decode(bytes,o);Check(r.issue.code==I::DecodeError::resource_limit,"AVIF admitted to the resident raster profile");
    const auto png=I::EncodeImage(I::Format::png,decoded.image.width,decoded.image.height,decoded.image.rgba8);
    Check(bool(png),"decoded AVIF could not be encoded as PNG");
    const auto reopened=I::DecodeImage(I::Format::png,png.bytes);
    Check(bool(reopened)&&reopened.image.rgba8==decoded.image.rgba8,"PNG round trip changed AVIF pixels");

    // Truncation: the shortest prefix yielding the complete image; any shorter cut rejects with no
    // pixels, and no prefix ever yields a different (partial) image.
    const auto prefixDecodes=[&](std::size_t n) {
        const auto r=Decode(Bytes(bytes.begin(),bytes.begin()+std::ptrdiff_t(n)));
        if(r)Check(r.image.rgba8==decoded.image.rgba8,"a truncated file produced a different image");
        else Failed(r,"truncated sample");
        return bool(r);
    };
    std::size_t low=12,high=bytes.size();
    while(high-low>1){const auto mid=low+(high-low)/2;if(prefixDecodes(mid))high=mid;else low=mid;}
    const auto shortest=high;
    Check(shortest>4096&&shortest<=bytes.size(),"shortest decodable prefix is implausible");
    for(const auto cut:{shortest-1,shortest-64,shortest-1024,shortest/2,shortest/8,std::size_t(13)})Check(!prefixDecodes(cut),"truncated image data decoded");
    std::cout<<"shortest decodable prefix: "<<shortest<<" of "<<bytes.size()<<" bytes\n";
    // Corruption sweeps: box structure, then the coded AV1 payload. A corrupted payload may decode
    // to different pixels (AV1 has no checksum) but must never leak a partial or mis-sized image.
    for(std::size_t i=4;i<std::min<std::size_t>(bytes.size(),640);i+=3) {
        auto bad=bytes;bad[i]^=std::byte{0x80};const auto d=Decode(bad);
        Check(d?d.image.rgba8.size()==std::size_t(d.image.width)*d.image.height*4:d.image.rgba8.empty(),"corrupted header leaked pixels");
    }
    for(std::size_t i=640;i<bytes.size();i+=997) {
        auto bad=bytes;bad[i]^=std::byte{0x55};const auto d=Decode(bad);
        Check(d?(d.image.width==800&&d.image.height==533&&d.image.rgba8.size()==std::size_t(800)*533*4):d.image.rgba8.empty(),"corrupted payload leaked a partial image");
    }
    const auto notAvif=I::EncodeImage(I::Format::png,2,2,std::vector<std::uint8_t>(16,255));
    Check(bool(notAvif),"png fixture");
    r=Decode(notAvif.bytes);Failed(r,"png as avif");Check(r.issue.code==I::DecodeError::wrong_magic,"PNG bytes not rejected by magic");
    // One libheif instance decodes both codecs; each wrapper admits only its own.
    r=Decode(Read(path.parent_path()/"example.heic"));Failed(r,"example.heic as avif");
    Check(r.issue.code==I::DecodeError::unsupported_content,"HEIC decoded through the AVIF wrapper");
    r=I::DecodeImage(I::Format::heif,bytes);Failed(r,"example.avif as heif");
    Check(r.issue.code==I::DecodeError::unsupported_content,"AVIF decoded through the HEIC wrapper");
    Bytes tiny{std::byte{0},std::byte{0},std::byte{0},std::byte{8},std::byte{'f'},std::byte{'t'},std::byte{'y'},std::byte{'p'}};
    Failed(Decode(tiny),"bare ftyp");
}

void Data(const fs::path& data) {
    Check(fs::is_directory(data),"libheif tests/data directory missing");
    const auto decode=[&](const fs::path& p){return Decode(Read(p));};
    // Parity with the milestone's transform sample (clap, irot and imir applied by libheif).
    const auto t=decode(data/"clap_cropped_irot_imir.avif");
    Check(bool(t)&&t.image.width==64&&t.image.height==64&&t.image.frameCount==1,"clap_cropped_irot_imir is not 64x64");
    Check(Fnv1a64(t.image.rgba8)==0x9a67e30862ebb142ull,"transform sample differs from the accepted dependency fingerprint");
    const auto c=decode(data/"clap_cropped.avif");
    Check(bool(c)&&c.image.width==64&&c.image.height==64,"clap_cropped is not 64x64");
    Check(c.image.rgba8!=t.image.rgba8,"irot/imir had no visible effect");
    // The reduced-header 'mif3'/'mini' form is outside the admitted AVIF profile.
    for(const auto* name:{"simple_osm_tile_alpha.avif","simple_osm_tile_meta.avif","mini_size_zero.avif"})
        Check(Rejection(Read(data/name),name)==I::DecodeError::unsupported_content,std::string(name)+" (mini) not unsupported");
    // Malformed upstream regression inputs: typed failure, never pixels.
    for(const auto* name:{"clap_oversized_ispe_height.avif","clap_oversized_ispe_width.avif","clap_oversized_ispe_irot90.avif",
        "clap_oversized_ispe_irot180.avif","meta_size_zero.avif"}) {
        const auto r=decode(data/name);Check(r||r.image.rgba8.empty(),std::string(name)+" leaked pixels");
        if(r)Check(r.image.rgba8.size()==std::size_t(r.image.width)*r.image.height*4,std::string(name)+" extent mismatch");
        std::cout<<name<<": "<<(r?"decoded":std::string(I::ErrorMessage(r.issue.code)))<<"\n";
    }
    const auto corpus=data.parent_path().parent_path()/"fuzzing"/"data"/"corpus";
    Check(fs::is_directory(corpus),"libheif fuzzing corpus missing");
    const auto avif32=decode(corpus/"avif32.heif"); // ftyp avif/mif1/miaf with an av01 primary
    Check(bool(avif32)&&avif32.image.frameCount==1,"corpus avif32.heif did not decode");
    std::cout<<"avif32.heif: "<<avif32.image.width<<"x"<<avif32.image.height<<(Opaque(avif32.image.rgba8)?" opaque":" with alpha")<<"\n";
    Check(Rejection(Read(corpus/"avif32-mini.heif"),"avif32-mini")==I::DecodeError::unsupported_content,"avif32-mini not unsupported");
    // HEVC-coded corpus files are not AVIF, whatever else they contain.
    for(const auto* name:{"colors-with-alpha.heic","hevc32.heif","hevc32-mini.heif","avc32.heif","av1-huge-frame-header-oom.heic"})
        Check(Rejection(Read(corpus/name),name)==I::DecodeError::unsupported_content,std::string(name)+" not unsupported as AVIF");
    unsigned files=0;
    for(const auto& entry:fs::directory_iterator(corpus)) {
        if(!entry.is_regular_file()||entry.file_size()>8*1024*1024)continue;
        const auto r=Decode(Read(entry.path()));Check(r||r.image.rgba8.empty(),"corpus entry leaked pixels: "+entry.path().filename().string());++files;
    }
    Check(files>=40,"corpus unexpectedly small");
}

// --- Encoder --------------------------------------------------------------------------------
I::EncodeResult Encode(std::uint32_t w,std::uint32_t h,const Pixels& p,I::EncodeOptions o={}){return I::EncodeImage(I::Format::avif,w,h,p,o);}
Pixels Pattern(std::uint32_t w,std::uint32_t h,unsigned kind) {
    Pixels p(std::size_t(w)*h*4);
    std::uint32_t state=0x9e3779b9u^kind;
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x) {
        auto* q=p.data()+(std::size_t(y)*w+x)*4;
        state=state*1664525u+1013904223u;
        q[0]=std::uint8_t(state>>24);q[1]=std::uint8_t(x*37u+y*11u);q[2]=std::uint8_t((x*5u)^(y*29u));
        switch(kind) {
            case 0:q[3]=255;break;                                         // opaque noise
            case 1:q[3]=std::uint8_t(x*13u+y*7u);break;                    // every coverage level, RGB under alpha 0
            case 2:q[3]=((x/3+y/2)%2)?255:0;break;                         // binary mask
            default:q[3]=0;break;                                          // fully transparent, arbitrary RGB
        }
    }
    return p;
}
Pixels Smooth(std::uint32_t w,std::uint32_t h) {
    Pixels p(std::size_t(w)*h*4);
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x) {
        auto* q=p.data()+(std::size_t(y)*w+x)*4;
        q[0]=std::uint8_t(x*255u/(w-1));q[1]=std::uint8_t(y*255u/(h-1));
        q[2]=std::uint8_t(128.0+100.0*std::sin(double(x+y)/9.0));
        q[3]=std::uint8_t(x<w/4?255:(x*y)%256);
    }
    return p;
}
double Psnr(const Pixels& a,const Pixels& b) {
    double error=0;std::size_t n=0;
    for(std::size_t i=0;i<a.size();++i){if(i%4==3)continue;const double d=double(a[i])-double(b[i]);error+=d*d;++n;}
    return error==0?99.0:10.0*std::log10(255.0*255.0/(error/double(n)));
}
bool AlphaEqual(const Pixels& a,const Pixels& b){for(std::size_t i=3;i<a.size();i+=4)if(a[i]!=b[i])return false;return true;}

void Encoder() {
    // Lossless: exact RGBA (including RGB under transparent pixels) at odd, tiny and wide sizes.
    for(const auto& [w,h]:std::initializer_list<std::pair<std::uint32_t,std::uint32_t>>{{1,1},{2,3},{17,9},{61,37},{64,64},{255,3},{3,255},{300,200}})
        for(unsigned kind=0;kind<4;++kind) {
            const auto source=Pattern(w,h,kind);
            const auto encoded=Encode(w,h,source);
            const auto what="lossless "+std::to_string(w)+"x"+std::to_string(h)+" pattern "+std::to_string(kind);
            Check(bool(encoded),what+" did not encode: "+std::string(I::EncodeErrorMessage(encoded.error)));
            Check(!encoded.alphaFlattened,what+": alpha flattened");
            const auto decoded=Decode(encoded.bytes);
            Check(bool(decoded)&&decoded.image.width==w&&decoded.image.height==h&&decoded.image.frameCount==1,what+" did not decode");
            Check(decoded.image.rgba8==source,what+": round trip is not byte-exact");
            Check(I::DecodeImage(I::Format::heif,encoded.bytes).issue.code==I::DecodeError::unsupported_content,what+": accepted as HEIF");
            if(w==300&&kind==1)std::cout<<"lossless 300x200 with alpha: "<<encoded.bytes.size()<<" bytes\n";
        }
    // Determinism: identical bytes for identical input and options.
    const auto noisy=Pattern(97,61,1);
    Check(Encode(97,61,noisy).bytes==Encode(97,61,noisy).bytes,"lossless output is not deterministic");
    I::EncodeOptions lossy;lossy.avifLossless=false;
    Check(Encode(97,61,noisy,lossy).bytes==Encode(97,61,noisy,lossy).bytes,"lossy output is not deterministic");

    // Lossy: bounded error on a smooth image, alpha exact, higher quality is closer and larger.
    const std::uint32_t w=160,h=120;const auto smooth=Smooth(w,h);
    double previous=0;std::size_t previousBytes=0;
    for(const unsigned quality:{20u,60u,90u}) {
        auto o=lossy;o.avifQuality=quality;
        const auto encoded=Encode(w,h,smooth,o);
        Check(bool(encoded),"lossy q"+std::to_string(quality)+" did not encode");
        const auto decoded=Decode(encoded.bytes);
        Check(bool(decoded)&&decoded.image.width==w&&decoded.image.height==h,"lossy q"+std::to_string(quality)+" did not decode");
        const auto psnr=Psnr(decoded.image.rgba8,smooth);
        std::cout<<"lossy q"<<quality<<": "<<encoded.bytes.size()<<" bytes, RGB PSNR "<<psnr<<" dB\n";
        Check(AlphaEqual(decoded.image.rgba8,smooth),"lossy q"+std::to_string(quality)+": alpha not exact");
        Check(psnr>=(quality>=60?32.0:26.0),"lossy q"+std::to_string(quality)+": PSNR below threshold");
        Check(psnr>previous&&encoded.bytes.size()>previousBytes,"higher quality was not closer and larger");
        previous=psnr;previousBytes=encoded.bytes.size();
    }
    // Opaque input writes no alpha auxiliary and decodes opaque.
    const auto opaque=Pattern(40,30,0);
    const auto plain=Encode(40,30,opaque,lossy);
    Check(bool(plain)&&Opaque(Decode(plain.bytes).image.rgba8),"lossy opaque output not opaque");
    Check(plain.bytes.size()<Encode(40,30,Pattern(40,30,1),lossy).bytes.size(),"opaque image was not smaller than its alpha variant");

    // Options and limits.
    for(const unsigned quality:{0u,101u,1000u}) {
        auto o=I::EncodeOptions{};o.avifQuality=quality;
        Check(Encode(4,4,opaque,o).error==I::EncodeError::invalid_options,"avifQuality "+std::to_string(quality)+" accepted");
        o.avifLossless=false;Check(Encode(4,4,opaque,o).error==I::EncodeError::invalid_options,"lossy avifQuality "+std::to_string(quality)+" accepted");
    }
    for(const unsigned quality:{1u,100u}){auto o=lossy;o.avifQuality=quality;Check(bool(Encode(40,30,opaque,o)),"avifQuality "+std::to_string(quality)+" rejected");}
    const auto reference=Encode(61,37,Pattern(61,37,1));
    auto o=I::EncodeOptions{};o.maximumOutputBytes=reference.bytes.size();
    Check(Encode(61,37,Pattern(61,37,1),o).bytes==reference.bytes,"exact output byte limit rejected");
    o.maximumOutputBytes=reference.bytes.size()-1;
    const auto limited=Encode(61,37,Pattern(61,37,1),o);
    Check(limited.error==I::EncodeError::output_limit&&limited.bytes.empty(),"output byte limit not enforced");
    o={};o.resourceProfile=I::RasterProfile::largeRaster;
    Check(Encode(4,4,Pattern(4,4,0),o).error==I::EncodeError::invalid_options,"AVIF encode admitted to the resident raster profile");
    o={};o.maximumPixels=8ull*1024*1024+1;
    Check(Encode(4,4,Pattern(4,4,0),o).error==I::EncodeError::invalid_options,"pixel ceiling above 8 Mi accepted");
    o={};o.maximumDimension=63;
    Check(Encode(64,1,Pattern(64,1,0),o).error==I::EncodeError::invalid_dimensions,"dimension limit not enforced");
    o={};o.maximumPixels=100;
    Check(Encode(11,10,Pattern(11,10,0),o).error==I::EncodeError::invalid_dimensions,"pixel limit not enforced");
    Check(Encode(4,4,Pixels(63)).error==I::EncodeError::invalid_extent,"short RGBA extent accepted");
    Check(Encode(0,4,{}).error==I::EncodeError::invalid_dimensions,"zero width accepted");
    o={};o.alphaPolicy=I::AlphaPolicy::flatten_white;
    Check(Encode(4,4,Pattern(4,4,1),o).error==I::EncodeError::invalid_options,"AVIF accepted a JPEG-only alpha policy");
}
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-avif-codec-tests r6366-dev\n";return 0;}
    try {
        fs::path sample,data;
        for(int i=1;i+1<argc;i+=2) {
            if(std::string_view(argv[i])=="--sample")sample=argv[i+1];
            else if(std::string_view(argv[i])=="--data")data=argv[i+1];
            else throw std::runtime_error("usage: --sample examples/example.avif --data tests/data");
        }
        Check(!sample.empty()&&!data.empty(),"--sample and --data are required");
        Sample(sample);Data(data);Encoder();
        std::cout<<"AVIF checks passed: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
