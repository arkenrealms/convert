#include "arken_image_codecs_v1/encode.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

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
    const auto prefixDecodes=[&](std::size_t n) {
        const auto r=Decode(Bytes(bytes.begin(),bytes.begin()+std::ptrdiff_t(n)));
        if(r)Check(r.image.rgba8==decoded.image.rgba8,"a truncated file produced a different primary image");
        else Failed(r,"truncated sample");
        return bool(r);
    };
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
    // Primary items coded with a codec this build excludes reject before decoding.
    for(const auto* name:{"avc32.heif","avif32.heif","avif32-mini.heif","vvc32.heif","jpeg32.heif","j2k32.heif","av1-huge-frame-header-oom.heic"}) {
        const auto r=decode(name);Failed(r,name);
        Check(r.issue.code==I::DecodeError::unsupported_content,std::string(name)+" not classified as unsupported");
    }
    // HEVC with a clean aperture (64x64 coded, 32x32 shown) and the low-overhead 'mini' container.
    auto cropped=decode("hevc32.heif");
    Check(bool(cropped)&&cropped.image.width==32&&cropped.image.height==32&&cropped.image.frameCount==1,"hevc32 clap not applied");
    auto mini=decode("hevc32-mini.heif");
    Check(bool(mini)&&mini.image.width==64&&mini.image.height==64&&mini.image.frameCount==1,"hevc32-mini did not decode as 64x64");
    // Minimised iPhone-style layout: a 4032x3024 grid with irot plus an ICC-tagged 320x240 primary
    // whose coded data the fuzzer removed. It must fail as a typed rejection with no pixels.
    const auto phone=decode("clusterfuzz-testcase-minimized-file-fuzzer-5752063708495872.heic");
    Failed(phone,"minimised phone layout");
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

// --- Property-edited variants of the libheif test images -------------------------------------
// The pinned trees have no decodable HEVC fixture with a visible rotation (only a 1x1 one), a
// mirror or a wide-gamut tag, so the
// tests derive them from rainbow-451x461.heic (asymmetric colours, GIMP sRGB ICC, clap) and
// with-alpha-512x512.heic by overwriting whole property boxes in place. Box sizes never change,
// so iloc offsets stay valid. Expectations come from ISO/IEC 23008-12 semantics applied by this
// test to the unmodified decode, not from libheif: irot rotates anti-clockwise by angle*90
// degrees; imir axis 0 exchanges top and bottom, axis 1 exchanges left and right; transformative
// properties apply in ipma order.
struct Box {std::size_t at=0,size=0;std::string type;};
std::uint32_t Be32(const Bytes& b,std::size_t p) {
    return (std::uint32_t(b[p])<<24)|(std::uint32_t(b[p+1])<<16)|(std::uint32_t(b[p+2])<<8)|std::uint32_t(b[p+3]);
}
void Put32(Bytes& b,std::size_t p,std::uint32_t v){for(int i=0;i<4;++i)b[p+std::size_t(i)]=std::byte(v>>(24-8*i));}
std::vector<Box> Children(const Bytes& b,std::size_t from,std::size_t to) {
    std::vector<Box> boxes;
    for(std::size_t p=from;p<to;) {
        Check(p+8<=to,"box header overruns its parent");
        const Box box{p,Be32(b,p),std::string(reinterpret_cast<const char*>(b.data())+p+4,4)};
        Check(box.size>=8&&box.size<=to-p,"fixture box size invalid");
        boxes.push_back(box);p+=box.size;
    }
    return boxes;
}
Box Child(const Bytes& b,const Box& parent,const char* type,std::size_t skip=0) {
    for(const auto& box:Children(b,parent.at+8+skip,parent.at+parent.size))if(box.type==type)return box;
    throw std::runtime_error(std::string("fixture lacks box ")+type);
}
struct Fixture {
    Bytes bytes;
    std::vector<Box> properties; // ipco children; ipma indices are 1-based into this list.
    Box ipma;
    explicit Fixture(Bytes b):bytes(std::move(b)) {
        Box meta;for(const auto& box:Children(bytes,0,bytes.size()))if(box.type=="meta")meta=box;
        Check(meta.size!=0,"fixture lacks meta");
        const auto iprp=Child(bytes,meta,"iprp",4);
        properties=Children(bytes,Child(bytes,iprp,"ipco").at+8,Child(bytes,iprp,"ipco").at+Child(bytes,iprp,"ipco").size);
        ipma=Child(bytes,iprp,"ipma");
        Check(Be32(bytes,ipma.at+8)==0,"fixture ipma is not version 0 with 1-byte associations");
    }
    std::size_t Index(const char* type) const {
        for(std::size_t i=0;i<properties.size();++i)if(properties[i].type==type)return i+1;
        throw std::runtime_error(std::string("fixture lacks property ")+type);
    }
    // Overwrite property 'index' with a box of 'type' and 'payload', zero-padded to the old size.
    Fixture& Replace(std::size_t index,const char* type,std::initializer_list<std::uint8_t> payload) {
        const auto& box=properties.at(index-1);Check(8+payload.size()<=box.size,"replacement property does not fit");
        std::memcpy(bytes.data()+box.at+4,type,4);
        std::size_t p=box.at+8;for(const auto v:payload)bytes[p++]=std::byte(v);
        std::fill(bytes.begin()+std::ptrdiff_t(p),bytes.begin()+std::ptrdiff_t(box.at+box.size),std::byte{0});
        return *this;
    }
    // Overwrite the association list of 'item' with the same number of 1-byte entries.
    Fixture& Associate(std::uint16_t item,std::initializer_list<std::uint8_t> entries) {
        std::size_t p=ipma.at+16;
        for(std::uint32_t n=Be32(bytes,ipma.at+12);n--;) {
            const auto id=std::uint16_t((std::uint32_t(bytes[p])<<8)|std::uint32_t(bytes[p+1]));const auto count=std::size_t(bytes[p+2]);
            if(id==item){Check(count==entries.size(),"association count differs");std::size_t q=p+3;for(const auto v:entries)bytes[q++]=std::byte(v);return *this;}
            p+=3+count;
        }
        throw std::runtime_error("fixture lacks the item association");
    }
};
Bytes NclxPayload(std::uint16_t primaries,std::uint16_t transfer,std::uint16_t matrix) {
    return {std::byte{'n'},std::byte{'c'},std::byte{'l'},std::byte{'x'},std::byte(primaries>>8),std::byte(primaries),
        std::byte(transfer>>8),std::byte(transfer),std::byte(matrix>>8),std::byte(matrix),std::byte{0x80}};
}
Fixture& ReplaceWithNclx(Fixture& f,std::size_t index,std::uint16_t primaries,std::uint16_t transfer,std::uint16_t matrix) {
    f.Replace(index,"colr",{});
    const auto payload=NclxPayload(primaries,transfer,matrix);
    std::copy(payload.begin(),payload.end(),f.bytes.begin()+std::ptrdiff_t(f.properties.at(index-1).at+8));
    return f;
}

struct Raster {
    std::uint32_t w=0,h=0;std::vector<std::uint8_t> p;
    std::uint8_t* At(std::uint32_t x,std::uint32_t y){return p.data()+(std::size_t(y)*w+x)*4;}
    const std::uint8_t* At(std::uint32_t x,std::uint32_t y) const {return p.data()+(std::size_t(y)*w+x)*4;}
    bool operator==(const Raster&) const=default;
};
Raster Of(const I::DecodeResult& r,const std::string& what) {
    Check(bool(r),what+" did not decode: "+std::string(I::ErrorMessage(r.issue.code)));
    Check(r.image.rgba8.size()==std::size_t(r.image.width)*r.image.height*4,what+": extent mismatch");
    return {r.image.width,r.image.height,r.image.rgba8};
}
Raster RotateAnticlockwise(const Raster& s,unsigned quarterTurns) {
    Raster r=s;
    for(unsigned t=0;t<quarterTurns%4;++t) {
        Raster d{r.h,r.w,std::vector<std::uint8_t>(r.p.size())};
        for(std::uint32_t y=0;y<r.h;++y)for(std::uint32_t x=0;x<r.w;++x)std::memcpy(d.At(y,r.w-1-x),r.At(x,y),4);
        r=std::move(d);
    }
    return r;
}
Raster Mirror(const Raster& s,unsigned axis) {
    Raster d{s.w,s.h,std::vector<std::uint8_t>(s.p.size())};
    for(std::uint32_t y=0;y<s.h;++y)for(std::uint32_t x=0;x<s.w;++x)
        std::memcpy(axis==0?d.At(x,s.h-1-y):d.At(s.w-1-x,y),s.At(x,y),4);
    return d;
}
Raster Crop(const Raster& s,std::uint32_t w,std::uint32_t h) {
    Raster d{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4)};
    for(std::uint32_t y=0;y<h;++y)std::memcpy(d.At(0,y),s.At(0,y),std::size_t(w)*4);
    return d;
}
int MaxDifference(const Raster& a,const Raster& b) {
    Check(a.w==b.w&&a.h==b.h,"compared rasters differ in size");
    int m=0;for(std::size_t i=0;i<a.p.size();++i)m=std::max(m,std::abs(int(a.p[i])-int(b.p[i])));
    return m;
}
// libheif applies clap/irot/imir to the coded 4:2:0 planes before chroma upsampling. On the
// even-sized coded image the result is an exact pixel permutation; after the odd-sized clap the
// chroma phase changes, and the decode differs from the transformed upright decode by at most 2
// levels (observed on this host). The orientation itself must be unambiguous: every
// other dihedral orientation of the reference that differs visibly from the expected one must
// also differ visibly from the decode.
constexpr int upsamplingTolerance=2;
void Oriented(const Raster& got,const Raster& expected,const Raster& reference,const std::string& what) {
    Check(got.w==expected.w&&got.h==expected.h,what+": wrong output extent");
    const auto difference=MaxDifference(got,expected);
    Check(difference<=upsamplingTolerance,what+": differs from the expected orientation by "+std::to_string(difference));
    std::cout<<what<<": "<<got.w<<"x"<<got.h<<", max difference from expected "<<difference<<"\n";
    unsigned distinct=0;
    for(unsigned mirrored=0;mirrored<2;++mirrored)for(unsigned turns=0;turns<4;++turns) {
        const auto candidate=RotateAnticlockwise(mirrored?Mirror(reference,0):reference,turns);
        if(candidate.w!=got.w||candidate.h!=got.h||MaxDifference(candidate,expected)<=64)continue;
        ++distinct;Check(MaxDifference(got,candidate)>64,what+": also matches a different orientation");
    }
    Check(distinct>=3,what+": reference is too symmetric to prove an orientation");
}
I::DecodeError Rejection(const Bytes& b,const std::string& what) {
    const auto r=Decode(b);Failed(r,what);return r.issue.code;
}

void Orientation(const fs::path& data) {
    const Fixture rainbow(Read(data/"rainbow-451x461.heic"));
    const auto clap=rainbow.Index("clap"),pixi=rainbow.Index("pixi"),hvcC=rainbow.Index("hvcC"),colr=rainbow.Index("colr"),ispe=rainbow.Index("ispe");
    Check(hvcC==1&&colr==2&&ispe==3&&clap==4&&pixi==5,"rainbow property layout changed");
    const auto shown=Of(Decode(rainbow.bytes),"rainbow");
    Check(shown.w==451&&shown.h==461,"rainbow clean aperture not applied");
    // The four corners are distinct saturated colours, so any rotation or mirror is visible.
    Check(std::memcmp(shown.At(0,0),shown.At(450,0),3)!=0&&std::memcmp(shown.At(0,0),shown.At(0,460),3)!=0
        &&std::memcmp(shown.At(450,460),shown.At(450,0),3)!=0,"rainbow corners are not asymmetric");
    // clap -> irot 0 (identity) exposes the full 452x462 coded image; the clap box (width 451,
    // height 461, offsets -1/2) selects its top-left 451x461 region.
    auto full=Fixture(rainbow);full.Replace(clap,"irot",{0});
    const auto coded=Of(Decode(full.bytes),"rainbow without clap");
    Check(coded.w==452&&coded.h==462,"identity irot changed the coded extent");
    Check(Crop(coded,451,461)==shown,"clap did not select the top-left 451x461 region exactly");
    for(unsigned angle=0;angle<4;++angle) { // clap, then irot (pixi slot)
        auto f=Fixture(rainbow);f.Replace(pixi,"irot",{std::uint8_t(angle)}).Associate(1,{0x81,0x02,0x03,0x84,0x85});
        const auto r=Decode(f.bytes);const auto got=Of(r,"rainbow irot "+std::to_string(angle));
        Oriented(got,RotateAnticlockwise(shown,angle),shown,"clap then irot "+std::to_string(angle*90));
        Check(r.image.frameCount==1,"rotated fixture frame count");
    }
    for(unsigned axis=0;axis<2;++axis) { // clap, then imir (pixi slot)
        auto f=Fixture(rainbow);f.Replace(pixi,"imir",{std::uint8_t(axis)}).Associate(1,{0x81,0x02,0x03,0x84,0x85});
        Oriented(Of(Decode(f.bytes),"rainbow imir"),Mirror(shown,axis),shown,"clap then imir axis "+std::to_string(axis));
    }
    // Combinations on the uncropped image: the property order in ipma is the application order.
    auto rotateThenMirror=Fixture(rainbow);rotateThenMirror.Replace(clap,"irot",{1}).Replace(pixi,"imir",{0}).Associate(1,{0x81,0x02,0x03,0x84,0x85});
    Oriented(Of(Decode(rotateThenMirror.bytes),"irot+imir"),Mirror(RotateAnticlockwise(coded,1),0),coded,"irot 90 then imir axis 0");
    auto mirrorThenRotate=Fixture(rainbow);mirrorThenRotate.Replace(clap,"irot",{1}).Replace(pixi,"imir",{0}).Associate(1,{0x81,0x02,0x03,0x85,0x84});
    Oriented(Of(Decode(mirrorThenRotate.bytes),"imir+irot"),RotateAnticlockwise(Mirror(coded,0),1),coded,"imir axis 0 then irot 90");
    // No fixture here carries an Exif item, so "Exif orientation is never applied" is a code-level
    // property (neither the wrapper nor libheif's decode path reads Exif), not tested here.

    // Declared extents that disagree with the coded stream never reach an allocation or copy.
    auto huge=Fixture(rainbow);huge.Replace(ispe,"ispe",{0,0,0,0, 0,0,0x0f,0xa0, 0,0,0x0f,0xa0}); // 4000x4000 behind a 451x461 clap
    Check(Rejection(huge.bytes,"oversized ispe")==I::DecodeError::invalid_dimensions,"coded ispe extent above the pixel limit not rejected before decoding");
    auto small=Fixture(rainbow);small.Replace(clap,"irot",{0}).Replace(ispe,"ispe",{0,0,0,0, 0,0,0x01,0x2c, 0,0,0x01,0x2c}); // 300x300 vs 452x462 coded
    const auto smaller=Rejection(small.bytes,"undersized ispe");
    auto larger=Fixture(rainbow);larger.Replace(clap,"irot",{0}).Replace(ispe,"ispe",{0,0,0,0, 0,0,0x02,0x00, 0,0,0x02,0x00}); // 512x512 vs 452x462 coded
    const auto bigger=Rejection(larger.bytes,"ispe larger than the coded picture");
    // libheif tightens its pixel limit to the ispe extent (plus one coding unit) for the codec, so
    // a coded picture larger than ispe hits that limit; one smaller than ispe fails its size check.
    Check(smaller==I::DecodeError::resource_limit&&bigger==I::DecodeError::invalid_dimensions,"ispe/coded disagreement classification changed");

    // Alpha follows the primary's transforms.
    const Fixture alpha(Read(data/"with-alpha-512x512.heic"));
    const auto alphaPixi=alpha.Index("pixi");
    const auto upright=Of(Decode(alpha.bytes),"with-alpha");
    Check(upright.w==512&&upright.h==512,"with-alpha extent");
    bool clear=false,opaque=false;for(std::size_t i=3;i<upright.p.size();i+=4){clear=clear||upright.p[i]==0;opaque=opaque||upright.p[i]==255;}
    Check(clear&&opaque,"with-alpha lost its alpha plane");
    // Master and alpha auxiliary both carry irot 90: colour and alpha turn together, exactly.
    auto turned=Fixture(alpha);turned.Replace(alphaPixi,"irot",{1}).Associate(1,{0x81,0x02,0x03,0x84}).Associate(2,{0x85,0x03,0x84,0x86});
    const auto rotatedAlpha=Of(Decode(turned.bytes),"with-alpha irot 90");
    Check(rotatedAlpha==RotateAnticlockwise(upright,1),"alpha and colour not rotated together");
    // Only the master rotated: libheif would composite an upright alpha under rotated colour.
    auto split=Fixture(alpha);split.Replace(alphaPixi,"irot",{1}).Associate(1,{0x81,0x02,0x03,0x84}).Associate(2,{0x85,0x03,0x00,0x86});
    Check(Rejection(split.bytes,"alpha without the master's irot")==I::DecodeError::unsupported_content,"alpha transformed differently from its master was accepted");
}

// Display P3 colorants as an ICC v2/v4 rXYZ/gXYZ/bXYZ triple (D50-adapted, s15Fixed16).
void PatchColorants(Fixture& f,std::size_t colrIndex) {
    const auto icc=f.properties.at(colrIndex-1).at+12;
    Check(std::memcmp(f.bytes.data()+icc-4,"prof",4)==0,"colr property is not an ICC profile");
    const double p3[3][3]={{0.515121,0.241196,-0.001053},{0.291977,0.692245,0.041885},{0.157104,0.066574,0.784073}};
    const char* tags[3]={"rXYZ","gXYZ","bXYZ"};
    const auto count=Be32(f.bytes,icc+128);unsigned patched=0;
    for(std::uint32_t i=0;i<count;++i) {
        const auto entry=icc+132+std::size_t(i)*12;
        for(int c=0;c<3;++c)if(std::memcmp(f.bytes.data()+entry,tags[c],4)==0) {
            const auto offset=icc+Be32(f.bytes,entry+4);
            for(int k=0;k<3;++k)Put32(f.bytes,offset+8+std::size_t(k)*4,std::uint32_t(std::int32_t(std::lround(p3[c][k]*65536.0))));
            ++patched;
        }
    }
    Check(patched==3,"ICC colorant tags not found");
}
void Colour(const fs::path& data) {
    const Fixture rainbow(Read(data/"rainbow-451x461.heic"));
    const auto colr=rainbow.Index("colr"),clap=rainbow.Index("clap");
    const auto tagged=Of(Decode(rainbow.bytes),"rainbow");
    // The fixture's ICC is GIMP's built-in sRGB: skcms treats it as sRGB and no transform runs.
    // Removing the colr box leaves only the HEVC VUI (BT.709 primaries, sRGB transfer, BT.601
    // matrix, full range), which the bitstream probe reads and treats as sRGB as well.
    auto untagged=Fixture(rainbow);untagged.Replace(colr,"free",{});
    Check(Of(Decode(untagged.bytes),"rainbow without colr")==tagged,"sRGB ICC and VUI-only decodes differ");
    // A sample corner: (255,64,20) in the coded image.
    const auto* corner=tagged.At(0,460);
    Check(corner[0]==255&&corner[1]==64&&corner[2]==20,"rainbow reference corner changed");
    // Same pixels declared Display P3, once through ICC colorants and once through NCLX primaries.
    auto p3icc=Fixture(rainbow);PatchColorants(p3icc,colr);
    const auto fromIcc=Of(Decode(p3icc.bytes),"P3 ICC");
    auto p3nclx=Fixture(rainbow);ReplaceWithNclx(p3nclx,colr,12,13,6);
    const auto fromNclx=Of(Decode(p3nclx.bytes),"P3 NCLX");
    // P3 red with little green is outside sRGB: converting P3 -> sRGB keeps R saturated and must
    // lower G and B (the inverse direction would raise them).
    const auto* converted=fromIcc.At(0,460);
    Check(converted[0]==255&&converted[1]<48&&converted[2]<8,"P3 ICC not converted towards sRGB");
    std::cout<<"Display P3 (255,64,20) -> sRGB ("<<int(converted[0])<<","<<int(converted[1])<<","<<int(converted[2])<<"); ICC vs NCLX max difference "
        <<MaxDifference(fromIcc,fromNclx)<<"\n";
    Check(MaxDifference(fromIcc,fromNclx)<=2,"ICC and NCLX Display P3 paths disagree by more than 2 levels");
    Check(MaxDifference(fromIcc,tagged)>32,"P3 conversion had no visible effect");
    auto bt2020=Fixture(rainbow);ReplaceWithNclx(bt2020,colr,9,1,6);
    Check(MaxDifference(Of(Decode(bt2020.bytes),"BT.2020 NCLX"),tagged)>32,"BT.2020 primaries not converted");
    auto bt709=Fixture(rainbow);ReplaceWithNclx(bt709,colr,1,1,6);
    Check(Of(Decode(bt709.bytes),"BT.709 NCLX")==tagged,"BT.709 transfer not presented as sRGB");
    // HDR transfers, unadmitted primaries and constant-luminance matrices reject.
    for(const auto& [primaries,transfer,matrix,what]:std::initializer_list<std::tuple<int,int,int,const char*>>{
        {9,16,9,"PQ"},{9,18,9,"HLG"},{1,8,6,"linear"},{1,9,6,"logarithmic"},{5,6,6,"BT.601 625 primaries"},{11,13,6,"DCI-P3 primaries"},
        {9,14,10,"BT.2020 constant luminance"},{1,13,14,"ICtCp"}}) {
        auto f=Fixture(rainbow);ReplaceWithNclx(f,colr,std::uint16_t(primaries),std::uint16_t(transfer),std::uint16_t(matrix));
        Check(Rejection(f.bytes,what)==I::DecodeError::unsupported_content,std::string(what)+" not rejected as unsupported");
    }
    // ICC and NCLX together (NCLX in the clap slot): the ICC owns primaries and transfer, so a P3
    // NCLX beside the sRGB ICC changes nothing, while an HDR transfer in NCLX still rejects.
    auto full=Fixture(rainbow);full.Replace(clap,"irot",{0});
    const auto coded=Of(Decode(full.bytes),"rainbow without clap");
    auto both=Fixture(rainbow);ReplaceWithNclx(both,clap,12,13,6).Associate(1,{0x81,0x02,0x03,0x04,0x05});
    Check(Of(Decode(both.bytes),"ICC+NCLX")==coded,"NCLX overrode the ICC profile");
    auto bothPq=Fixture(rainbow);ReplaceWithNclx(bothPq,clap,9,16,9).Associate(1,{0x81,0x02,0x03,0x04,0x05});
    Check(Rejection(bothPq.bytes,"ICC+PQ NCLX")==I::DecodeError::unsupported_content,"PQ beside ICC not rejected");
    // A non-RGB or malformed ICC rejects rather than decoding untransformed.
    auto gray=Fixture(rainbow);std::memcpy(gray.bytes.data()+gray.properties.at(colr-1).at+12+16,"GRAY",4);
    Check(Rejection(gray.bytes,"GRAY ICC")==I::DecodeError::unsupported_content,"gray ICC not rejected as unsupported");
    auto lying=Fixture(rainbow);Put32(lying.bytes,lying.properties.at(colr-1).at+12,600);
    Check(Rejection(lying.bytes,"ICC size mismatch")==I::DecodeError::invalid_layout,"ICC header size mismatch accepted");
}

void Data(const fs::path& data) {
    Check(fs::is_directory(data),"libheif tests/data directory missing");
    const auto decode=[&](const char* name){return Decode(Read(data/name));};
    auto c=decode("clap_cropped.heic");
    Check(bool(c)&&c.image.width==64&&c.image.height==64&&c.image.frameCount==1,"clap_cropped is not 64x64 from its 256x256 coded image");
    auto w=decode("conformance_window_padding.heic"); // clap then irot on a 2x2 coded picture
    Check(bool(w)&&w.image.width==1&&w.image.height==1,"conformance_window_padding is not 1x1");
    auto l=decode("lightning_mini.heif");
    Check(bool(l)&&l.image.width==256&&l.image.height==256&&l.image.frameCount==1,"lightning_mini did not decode as 256x256");
    Failed(decode("pclr_zero_columns.heic"),"pclr_zero_columns");
    // Uncompressed (ISO 23001-17) and generically compressed items are not in the admitted profile.
    for(const auto* name:{"uncompressed_comp_RGB.heif","rgb_generic_compressed_zlib.heif","rgb_generic_compressed_brotli.heif"}) {
        const auto r=decode(name);Failed(r,name);Check(r.issue.code==I::DecodeError::unsupported_content,std::string(name)+" not unsupported");
    }
    Orientation(data);
    Colour(data);
}
}
int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-heif-codec-tests r6365-dev\n";return 0;}
    try {
        fs::path sample,corpus,data;
        for(int i=1;i+1<argc;i+=2) {
            if(std::string_view(argv[i])=="--sample")sample=argv[i+1];
            else if(std::string_view(argv[i])=="--corpus")corpus=argv[i+1];
            else if(std::string_view(argv[i])=="--data")data=argv[i+1];
            else throw std::runtime_error("usage: --sample example.heic --corpus fuzzing/data/corpus --data tests/data");
        }
        Check(!sample.empty()&&!corpus.empty()&&!data.empty(),"--sample, --corpus and --data are required");
        Sample(sample);Corpus(corpus);Data(data);
        std::cout<<"HEIF checks passed: "<<checks<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
