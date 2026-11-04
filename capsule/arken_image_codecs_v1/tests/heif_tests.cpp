#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Native qualification of the memory-only HEIC decoder against the pinned libheif source tree:
// the accepted dependency sample (examples/example.heic) and the upstream fuzzing corpus.
namespace {
namespace I=arken::image::v1;
namespace fs=std::filesystem;
using Bytes=std::vector<std::byte>;
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
I::DecodeResult Decode(const Bytes& b,I::DecodeOptions o={}){return I::DecodeImage(I::Format::heif,b,o);}
void Failed(const I::DecodeResult& r,const std::string& what) {
    Check(!r&&r.image.rgba8.empty()&&!r.image.width&&!r.image.height,what+": rejection leaked pixels or dimensions");
}

void Sample(const fs::path& path) {
    const auto bytes=Read(path);
    const auto decoded=Decode(bytes);
    Check(bool(decoded),std::string("accepted sample did not decode: ")+std::string(I::ErrorMessage(decoded.issue.code)));
    Check(decoded.image.width==1280&&decoded.image.height==854,"sample dimensions changed");
    // The accepted sample holds two top-level images; only the primary is decoded and the count is reported.
    Check(decoded.image.frameCount==2&&decoded.image.sourceBitsPerChannel==8,"sample top-level image count or bit depth changed");
    Check(decoded.image.rgba8.size()==std::size_t(1280)*854*4,"sample RGBA extent mismatch");
    // Parity with the accepted dependency milestone (native and browser both observed this value).
    Check(Fnv1a64(decoded.image.rgba8)==0x9563ad71245621e0ull,"decoded pixels differ from the accepted dependency fingerprint");
    bool opaque=true;for(std::size_t i=3;i<decoded.image.rgba8.size();i+=4)opaque=opaque&&decoded.image.rgba8[i]==255;
    Check(opaque,"sample without alpha produced non-opaque pixels");
    Check(Decode(bytes).image.rgba8==decoded.image.rgba8,"repeated decode is not deterministic");

    I::DecodeOptions o;o.maximumPixels=1024*1024;
    auto r=Decode(bytes,o);Failed(r,"pixel limit");Check(r.issue.code==I::DecodeError::invalid_dimensions,"pixel limit not reported as dimensions");
    o={};o.maximumDimension=1000;r=Decode(bytes,o);Failed(r,"dimension limit");Check(r.issue.code==I::DecodeError::invalid_dimensions,"dimension limit not reported");
    o={};o.frameIndex=1;r=Decode(bytes,o);Failed(r,"frame selection");Check(r.issue.code==I::DecodeError::selection_out_of_range,"frame selection accepted");
    o={};o.maximumSourceBytes=bytes.size()-1;r=Decode(bytes,o);Check(r.issue.code==I::DecodeError::input_too_large,"source byte limit ignored");
    o={};o.resourceProfile=I::RasterProfile::largeRaster;r=Decode(bytes,o);Check(r.issue.code==I::DecodeError::resource_limit,"HEIF admitted to the resident raster profile");
    // Encoding the decoded image through the existing owners proves the RGBA8 model is well formed.
    const auto png=I::EncodeImage(I::Format::png,decoded.image.width,decoded.image.height,decoded.image.rgba8);
    Check(bool(png),"decoded HEIC could not be encoded as PNG");
    const auto reopened=I::DecodeImage(I::Format::png,png.bytes);
    Check(bool(reopened)&&reopened.image.rgba8==decoded.image.rgba8,"PNG round trip changed HEIC pixels");

    // Truncation: locate the shortest prefix that still yields the complete primary image, then
    // prove that cutting into that prefix rejects with no pixels, and that no prefix ever yields a
    // different (partial) image. The file also carries a second top-level image after the primary.
    const auto prefixDecodes=[&](std::size_t n){const auto r=Decode(Bytes(bytes.begin(),bytes.begin()+std::ptrdiff_t(n)));
        if(r)Check(r.image.rgba8==decoded.image.rgba8,"a truncated file produced a different primary image");
        else Failed(r,"truncated sample");return bool(r);};
    std::size_t low=12,high=bytes.size();
    while(high-low>1){const auto mid=low+(high-low)/2;if(prefixDecodes(mid))high=mid;else low=mid;}
    const auto shortest=high;
    Check(shortest>4096&&shortest<bytes.size(),"shortest decodable prefix is implausible");
    for(const auto cut:{shortest-1,shortest-64,shortest-1024,shortest/2,shortest/8})Check(!prefixDecodes(cut),"truncated primary data decoded");
    std::cout<<"shortest decodable prefix: "<<shortest<<" of "<<bytes.size()<<" bytes\n";
    // Header corruption sweep over the box structure before the coded data.
    for(std::size_t i=4;i<std::min<std::size_t>(bytes.size(),640);i+=3) {
        auto bad=bytes;bad[i]^=std::byte{0x80};const auto d=Decode(bad);
        Check(d||d.image.rgba8.empty(),"corrupted header leaked pixels");
    }
    const auto notHeif=I::EncodeImage(I::Format::png,2,2,std::vector<std::uint8_t>(16,255));
    Check(bool(notHeif),"png fixture");
    r=Decode(notHeif.bytes);Failed(r,"png as heif");Check(r.issue.code==I::DecodeError::wrong_magic,"PNG bytes not rejected by magic");
    Bytes tiny{std::byte{0},std::byte{0},std::byte{0},std::byte{8},std::byte{'f'},std::byte{'t'},std::byte{'y'},std::byte{'p'}};
    Failed(Decode(tiny),"bare ftyp");
}

void Corpus(const fs::path& dir) {
    Check(fs::is_directory(dir),"corpus directory missing");
    const auto decode=[&](const char* name){return Decode(Read(dir/name));};
    // Clean HEVC fixtures: opaque, alpha auxiliary, and thumbnails that must not be selected.
    auto plain=decode("colors-no-alpha.heic");
    Check(bool(plain)&&plain.image.width==64&&plain.image.height==64&&plain.image.frameCount==1,"colors-no-alpha did not decode as a single 64x64 image");
    Check(std::all_of(plain.image.rgba8.begin(),plain.image.rgba8.end(),[i=0](std::uint8_t v)mutable{return (i++%4)!=3||v==255;}),"opaque fixture produced alpha");
    auto alpha=decode("colors-with-alpha.heic");
    Check(bool(alpha)&&alpha.image.width==64&&alpha.image.height==64,"colors-with-alpha did not decode as 64x64");
    bool transparent=false;for(std::size_t i=3;i<alpha.image.rgba8.size();i+=4)transparent=transparent||alpha.image.rgba8[i]!=255;
    Check(transparent,"alpha auxiliary was discarded");
    for(std::size_t i=0;i<alpha.image.rgba8.size();i+=4) // Same colour image under the alpha plane.
        Check(std::equal(alpha.image.rgba8.begin()+std::ptrdiff_t(i),alpha.image.rgba8.begin()+std::ptrdiff_t(i)+3,plain.image.rgba8.begin()+std::ptrdiff_t(i)),"colour differs between alpha and opaque fixtures");
    auto thumb=decode("colors-no-alpha-thumbnail.heic");
    Check(bool(thumb)&&thumb.image.width==72&&thumb.image.height==72,"primary image not selected over its 64x64 thumbnail");
    auto alphaThumb=decode("colors-with-alpha-thumbnail.heic");
    Check(bool(alphaThumb)&&alphaThumb.image.width==72&&alphaThumb.image.height==72,"alpha primary not selected over its thumbnail");
    // Codecs the pinned build deliberately excludes are rejected as unsupported content, not decoded.
    for(const auto* name:{"uncompressed_comp_RGB.heic","uncompressed_comp_YUV_420.heic","rgb_generic_compressed_zlib.heic","j2k-siz-segment-undersized.heic"}) {
        const auto r=decode(name);Failed(r,name);
        Check(r.issue.code==I::DecodeError::unsupported_content,std::string(name)+" not classified as unsupported");
    }
    // Fuzzer-minimised and crash-regression inputs: typed failure, no pixels, no crash.
    for(const auto* name:{"av1-huge-frame-header-oom.heic","clap-overflow-divide-zero.heic","github_44.heic","github_45.heic","github_48.heic",
        "github_50.heic","github_138.heic","github_367_1.heic","github_367_2.heic","github_46_1.heic","github_15.heic","github_47.heic","github_49.heic",
        "region-mask-missing-refs.heic","hbo_heif_context.h_126_1.heic","uaf_heif_context.h_117_1.heic","hevc32-mini.heif","avif32-mini.heif"}) {
        if(!fs::exists(dir/name))continue;
        const auto r=decode(name);Check(r||r.image.rgba8.empty(),std::string(name)+" leaked pixels");
    }
    // Every file in the corpus must be handled without crashing.
    unsigned files=0;
    for(const auto& entry:fs::directory_iterator(dir)) {
        if(!entry.is_regular_file()||entry.file_size()>8*1024*1024)continue;
        const auto r=Decode(Read(entry.path()));Check(r||r.image.rgba8.empty(),"corpus entry leaked pixels: "+entry.path().filename().string());++files;
    }
    Check(files>=40,"corpus unexpectedly small");
}
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-heif-codec-tests r6365-dev\n";return 0;}
    try {
        fs::path sample,corpus;
        for(int i=1;i+1<argc;i+=2) {
            if(std::string_view(argv[i])=="--sample")sample=argv[i+1];
            else if(std::string_view(argv[i])=="--corpus")corpus=argv[i+1];
            else throw std::runtime_error("usage: --sample example.heic --corpus fuzzing/data/corpus");
        }
        Check(!sample.empty()&&!corpus.empty(),"--sample and --corpus are required");
        Sample(sample);Corpus(corpus);
        std::cout<<"HEIF checks passed: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
