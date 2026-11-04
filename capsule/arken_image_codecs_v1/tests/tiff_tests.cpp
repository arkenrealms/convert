#include "arken_image_codecs_v1/encode.hpp"
#include "../test_support/tiff_fixtures.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace image=arken::image::v1;
namespace tf=tiff_fixture;
namespace fs=std::filesystem;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class T>void Write(const fs::path& path,const std::vector<T>& bytes){
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()*sizeof(T));Require(bool(f),"Fixture write failed");
}
tf::Bytes Read(const fs::path& path){
    const auto size=fs::file_size(path);Require(size<=64ull*1024*1024,"Fixture too large");tf::Bytes b(size);
    std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),b.size());Require(bool(f),"Fixture read failed");return b;
}
void Reject(const tf::Bytes& b){Require(!image::DecodeImage(image::Format::tiff,b),"Invalid TIFF accepted");}
using image::DecodeError;
image::DecodeResult Decode(const tf::Bytes& b,const image::DecodeOptions& o={}){return image::DecodeImage(image::Format::tiff,b,o);}
void Expect(const tf::Bytes& b,DecodeError error,const char* why,const image::DecodeOptions& o={}){
    const auto d=Decode(b,o);
    if(d||d.issue.code!=error){std::cerr<<why<<": got "<<int(d.issue.code)<<" expected "<<int(error)<<'\n';throw std::runtime_error(why);}
}
std::vector<std::uint8_t> Expected(const tf::Spec& s,int alpha=-1){
    std::vector<std::uint8_t> out;
    for(std::size_t p=0;p<std::size_t(s.width)*s.height;++p){
        for(unsigned c=0;c<3;++c)out.push_back(std::uint8_t(s.samples[p*s.spp+c]));
        out.push_back(alpha<0?255:std::uint8_t(s.samples[p*s.spp+unsigned(alpha)]));
    }
    return out;
}
tf::Spec Rgb(unsigned w,unsigned h,unsigned spp=3,unsigned bits=8,unsigned seed=0){
    tf::Spec s;s.width=w;s.height=h;s.spp=spp;s.bits=bits;s.samples=tf::Pattern(w,h,spp,bits,seed);return s;
}
void Same(const image::DecodeResult& d,unsigned w,unsigned h,const std::vector<std::uint8_t>& rgba,const char* why,unsigned tolerance=0){
    if(!d){std::cerr<<why<<": decode failed "<<int(d.issue.code)<<'\n';throw std::runtime_error(why);}
    Require(d.image.width==w&&d.image.height==h&&d.image.rgba8.size()==rgba.size(),why);
    for(std::size_t i=0;i<rgba.size();++i)if(unsigned(std::abs(int(d.image.rgba8[i])-int(rgba[i])))>tolerance){
        std::cerr<<why<<": sample "<<i<<" got "<<int(d.image.rgba8[i])<<" expected "<<int(rgba[i])<<'\n';throw std::runtime_error(why);}
}
tf::Tag Short(std::vector<std::uint64_t> v){return {3,std::move(v),{}};}
tf::Tag Undefined(tf::Bytes raw){return {7,{},std::move(raw)};}
// Independent BT.601 YCbCr -> RGB with TIFF ReferenceBlackWhite scaling.
std::uint8_t Clamp(double v){return std::uint8_t(v<=0?0:v>=255?255:std::floor(v+0.5));}
std::array<std::uint8_t,3> FromYcbcr(double y,double cb,double cr,const double* ref){
    y=(y-ref[0])*255/(ref[1]-ref[0]);cb=(cb-ref[2])*127/(ref[3]-ref[2]);cr=(cr-ref[4])*127/(ref[5]-ref[4]);
    const double r=y+1.402*cr,b=y+1.772*cb,g=(y-0.114*b-0.299*r)/0.587;
    return {Clamp(r),Clamp(g),Clamp(b)};
}
unsigned Luma(unsigned x,unsigned y){return 40+x*20+y*7;}
unsigned Blue(unsigned bx,unsigned by){return 90+bx*20+by*5;}
unsigned Red(unsigned bx,unsigned by){return 170-by*20-bx*3;}
tf::Bytes YcbcrTiff(unsigned w,unsigned h,unsigned sh,unsigned sv,unsigned rows,unsigned compression,
    std::vector<std::uint8_t>& expected,const std::vector<std::uint64_t>& reference={}){
    tf::Page page;
    for(unsigned y0=0;y0<h;y0+=rows){
        tf::Bytes raw;const unsigned n=std::min(rows,h-y0);
        for(unsigned by=0;by<(n+sv-1)/sv;++by)for(unsigned bx=0;bx<(w+sh-1)/sh;++bx){
            for(unsigned j=0;j<sv;++j)for(unsigned i=0;i<sh;++i)
                raw.push_back(std::byte(Luma(std::min(bx*sh+i,w-1),std::min(y0+by*sv+j,h-1))));
            raw.push_back(std::byte(Blue(bx,y0/sv+by)));raw.push_back(std::byte(Red(bx,y0/sv+by)));
        }
        page.chunks.push_back(tf::Compress(raw,compression,((w+sh-1)/sh)*(sh*sv+2)));
    }
    page.tags={{256,{4,{w},{}}},{257,{4,{h},{}}},{258,Short({8,8,8})},{259,Short({compression})},{262,Short({6})},
        {277,Short({3})},{278,{4,{rows},{}}},{530,Short({sh,sv})}};
    double ref[6]={0,255,128,255,128,255};
    if(!reference.empty()){page.tags[532]={5,reference,{}};for(unsigned i=0;i<6;++i)ref[i]=double(reference[i*2])/double(reference[i*2+1]);}
    expected.clear();
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const auto rgb=FromYcbcr(Luma(x,y),Blue(x/sh,y/sv),Red(x/sh,y/sv),ref);
        expected.insert(expected.end(),{rgb[0],rgb[1],rgb[2],255});
    }
    return tf::Build({page});
}
std::pair<tf::Bytes,tf::Bytes> SplitJpegTables(const tf::Bytes& jpeg){
    tf::Bytes tables{std::byte{255},std::byte{216}},stream{std::byte{255},std::byte{216}};
    for(std::size_t at=2;at+4<=jpeg.size();){
        const auto marker=std::to_integer<unsigned>(jpeg[at+1]);
        if(marker==0xda){stream.insert(stream.end(),jpeg.begin()+std::ptrdiff_t(at),jpeg.end());break;}
        const auto length=(std::to_integer<unsigned>(jpeg[at+2])<<8)|std::to_integer<unsigned>(jpeg[at+3]);
        auto& target=marker==0xdb||marker==0xc4?tables:stream;
        target.insert(target.end(),jpeg.begin()+std::ptrdiff_t(at),jpeg.begin()+std::ptrdiff_t(at+2+length));at+=2+length;
    }
    tables.push_back(std::byte{255});tables.push_back(std::byte{217});
    return {tables,stream};
}
// Relabels a three-component JPEG's frame and scan component ids as 'R','G','B'.
tf::Bytes RgbCoded(tf::Bytes jpeg){
    for(std::size_t at=2;at+4<=jpeg.size();){
        const auto marker=std::to_integer<unsigned>(jpeg[at+1]);
        if(marker==0xd8){at+=2;continue;}
        const auto length=(std::to_integer<unsigned>(jpeg[at+2])<<8)|std::to_integer<unsigned>(jpeg[at+3]);
        if(marker==0xc0)for(unsigned c=0;c<3;++c)jpeg[at+4+6+c*3]=std::byte("RGB"[c]);
        if(marker==0xda){for(unsigned c=0;c<3;++c)jpeg[at+4+1+c*2]=std::byte("RGB"[c]);break;}
        at+=2+length;
    }
    return jpeg;
}
std::vector<std::uint8_t> Gradient(unsigned w,unsigned h,unsigned seed){
    std::vector<std::uint8_t> v(std::size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto p=(std::size_t(y)*w+x)*4;
        v[p]=std::uint8_t(x*9+seed);v[p+1]=std::uint8_t(y*13+seed*3);v[p+2]=std::uint8_t((x+y)*5);v[p+3]=255;}
    return v;
}
void Sweep(const tf::Bytes& seed,const char* name){
    for(std::size_t size=0;size<seed.size();++size)
        if(Decode(tf::Bytes(seed.begin(),seed.begin()+std::ptrdiff_t(size)))){std::cerr<<name<<" prefix "<<size<<'\n';throw std::runtime_error("Truncated TIFF accepted");}
    image::DecodeOptions bounded;bounded.maximumPixels=4096;bounded.maximumDimension=128;bounded.allowPng16BitRenderingDerivative=true;
    for(std::size_t p=0;p<seed.size();++p)for(unsigned bit:{1u,128u}){
        auto mutated=seed;mutated[p]^=std::byte(bit);
        const auto result=Decode(mutated,bounded);
        if(result)Require(result.image.rgba8.size()==std::size_t(result.image.width)*result.image.height*4,"Mutated TIFF result extent differs");
    }
}
void Tests(){
    unsigned checked=0;
    for(bool little:{true,false})for(unsigned compression:{1u,32773u,8u,32946u})
        for(auto shape:{std::pair{3u,2u},{4u,2u},{1u,0u},{1u,1u},{2u,1u}}){
            const auto f=tf::Make(shape.first,shape.second,little,compression,compression==8?2:1);
            const auto d=image::DecodeImage(image::Format::tiff,f.bytes);
            Require(bool(d)&&d.image.width==7&&d.image.height==3&&d.image.rgba8==f.rgba,"TIFF sample/alpha/strip/predictor mismatch");++checked;
        }
    // Byte order x classic/BigTIFF x compression x predictor x strips/tiles x chunky/planar.
    std::vector<tf::Bytes> seeds;
    for(bool little:{true,false})for(bool big:{false,true})for(unsigned compression:{1u,5u,8u,32773u})
        for(unsigned predictor:{1u,2u})for(bool tiled:{false,true})for(unsigned planar:{1u,2u}){
            auto s=Rgb(20,18,4,8,compression+predictor);s.little=little;s.compression=compression;s.predictor=predictor;s.planar=planar;
            s.rows=5;if(tiled){s.tileW=16;s.tileH=16;}
            s.extra[338]=Short({2});
            const auto b=tf::Encode(s,big);
            Same(Decode(b),20,18,Expected(s,3),"TIFF layout/codec matrix mismatch");++checked;
            if(!little&&big&&tiled&&planar==2&&predictor==2&&(compression==5||compression==8))seeds.push_back(b);
        }
    // 16-bit samples: rendering derivative only by explicit consent.
    image::DecodeOptions allow16;allow16.allowPng16BitRenderingDerivative=true;
    for(bool little:{true,false})for(unsigned compression:{1u,5u,8u})for(unsigned predictor:{1u,2u}){
        auto s=Rgb(9,4,3,16,7);s.little=little;s.compression=compression;s.predictor=predictor;
        const auto b=tf::Encode(s);
        Expect(b,DecodeError::unsupported_content,"16-bit TIFF silently rounded");
        std::vector<std::uint8_t> expected;
        for(std::size_t p=0;p<36;++p){for(unsigned c=0;c<3;++c)expected.push_back(std::uint8_t((s.samples[p*3+c]+128)/257));expected.push_back(255);}
        const auto d=Decode(b,allow16);Same(d,9,4,expected,"16-bit TIFF derivative mismatch");
        Require(d.image.sourceBitsPerChannel==16,"16-bit TIFF source depth not reported");++checked;
    }
    // Gray 1/2/4/8/16-bit, WhiteIsZero and BlackIsZero, odd width row padding.
    for(unsigned bits:{1u,2u,4u,8u,16u})for(unsigned photo:{0u,1u})for(unsigned compression:{1u,5u,32773u}){
        auto s=Rgb(11,3,1,bits,bits);s.photometric=photo;s.compression=compression;
        const auto max=(1u<<bits)-1;std::vector<std::uint8_t> expected;
        for(auto v:s.samples){auto g=photo==0?max-v:v;const auto e=std::uint8_t(bits==16?(g+128)/257:g*255/max);expected.insert(expected.end(),{e,e,e,255});}
        const auto b=tf::Encode(s);
        if(bits==16)Expect(b,DecodeError::unsupported_content,"16-bit gray height data silently rounded");
        Same(Decode(b,allow16),11,3,expected,"Gray TIFF depth/polarity mismatch");++checked;
    }
    // FillOrder 2 (LSB-first bytes) for raw sub-byte and LZW/Deflate data.
    for(unsigned compression:{1u,5u,8u}){
        auto s=Rgb(13,3,1,1,3);s.photometric=1;s.fill=2;s.compression=compression;
        std::vector<std::uint8_t> expected;for(auto v:s.samples){const auto e=std::uint8_t(v*255);expected.insert(expected.end(),{e,e,e,255});}
        Same(Decode(tf::Encode(s)),13,3,expected,"FillOrder 2 TIFF mismatch");++checked;
    }
    // Palette with 16-bit ColorMap entries scaled to 8 bits.
    for(unsigned bits:{1u,2u,4u,8u}){
        auto s=Rgb(6,5,1,bits,bits+1);s.photometric=3;const unsigned n=1u<<bits;
        std::vector<std::uint64_t> map(3*n);for(unsigned i=0;i<3*n;++i)map[i]=(i*7919u+1234u)%65536u;
        s.extra[320]=Short(map);std::vector<std::uint8_t> expected;
        for(auto v:s.samples){for(unsigned c=0;c<3;++c)expected.push_back(std::uint8_t((map[c*n+v]+128)/257));expected.push_back(255);}
        Same(Decode(tf::Encode(s)),6,5,expected,"Palette TIFF mismatch");++checked;
        auto missing=s;missing.extra.erase(320);Expect(tf::Encode(missing),DecodeError::invalid_palette,"Palette without ColorMap accepted");
        auto shortMap=s;map.pop_back();shortMap.extra[320]=Short(map);Expect(tf::Encode(shortMap),DecodeError::invalid_palette,"Short ColorMap accepted");
    }
    // LZW stress: table growth through 12 bits, Clear codes and KwKwK runs.
    {
        auto noise=Rgb(96,64,3,8,99);noise.compression=5;noise.rows=64;
        const auto b=tf::Encode(noise);Same(Decode(b),96,64,Expected(noise),"LZW table growth mismatch");
        auto flat=Rgb(64,64,1,8);flat.photometric=1;flat.compression=5;flat.rows=64;for(auto& v:flat.samples)v=77;
        std::vector<std::uint8_t> expected(64*64*4);for(std::size_t i=0;i<expected.size();++i)expected[i]=i%4==3?255:77;
        Same(Decode(tf::Encode(flat)),64,64,expected,"LZW KwKwK mismatch");
        auto page=tf::MakePage(flat);
        page.chunks[0]=tf::Lzw(tf::Bytes(64*64+1,std::byte{77}));Expect(tf::Build({page}),DecodeError::invalid_layout,"LZW overrun accepted");
        page.chunks[0]=tf::Lzw(tf::Bytes(64*64-1,std::byte{77}));Expect(tf::Build({page}),DecodeError::truncated,"Short LZW accepted");
        page.chunks[0]={std::byte{0},std::byte{1},std::byte{2},std::byte{3}};Expect(tf::Build({page}),DecodeError::unsupported_compression,"Old-style LZW accepted");
        checked+=2;
    }
    // Orientation 1-8 against an asymmetric 3x2 image.
    for(unsigned orientation=1;orientation<=8;++orientation){
        auto s=Rgb(3,2,3,8,5);s.extra[274]=Short({orientation});
        const bool transpose=orientation>=5;const unsigned ow=transpose?2:3,oh=transpose?3:2;
        std::vector<std::uint8_t> expected(ow*oh*4);
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x){
            unsigned dx=x,dy=y;
            switch(orientation){case 2:dx=2-x;break;case 3:dx=2-x;dy=1-y;break;case 4:dy=1-y;break;
                case 5:dx=y;dy=x;break;case 6:dx=1-y;dy=x;break;case 7:dx=1-y;dy=2-x;break;case 8:dx=y;dy=2-x;break;default:break;}
            for(unsigned c=0;c<3;++c)expected[(dy*ow+dx)*4+c]=std::uint8_t(s.samples[(y*3+x)*3+c]);
            expected[(dy*ow+dx)*4+3]=255;
        }
        Same(Decode(tf::Encode(s)),ow,oh,expected,"TIFF orientation mismatch");++checked;
    }
    // Multi-page: page count, frame selection, classic and BigTIFF chains.
    for(bool big:{false,true})for(bool little:{true,false}){
        auto a=Rgb(7,5,3,8,1),g=Rgb(3,2,1,8,2),p=Rgb(4,4,1,4,3);
        a.little=g.little=p.little=little;g.photometric=1;p.photometric=3;
        std::vector<std::uint64_t> map(48);for(unsigned i=0;i<48;++i)map[i]=i*1300;p.extra[320]=Short(map);
        const auto b=tf::Build({tf::MakePage(a),tf::MakePage(g),tf::MakePage(p)},little,big);
        image::DecodeOptions o;
        for(unsigned page=0;page<3;++page){o.frameIndex=page;const auto d=Decode(b,o);
            Require(bool(d)&&d.image.frameCount==3,"TIFF page count wrong");
            Require(d.image.width==(page==0?7u:page==1?3u:4u),"TIFF page selection wrong");}
        Same(Decode(b),7,5,Expected(a),"First TIFF page mismatch");
        o.frameIndex=1;std::vector<std::uint8_t> gray;for(auto v:g.samples)gray.insert(gray.end(),{std::uint8_t(v),std::uint8_t(v),std::uint8_t(v),255});
        Same(Decode(b,o),3,2,gray,"Second TIFF page mismatch");
        o.frameIndex=3;Expect(b,DecodeError::selection_out_of_range,"Missing TIFF page accepted",o);
        if(big&&!little)seeds.push_back(b);
        checked+=3;
    }
    // CMYK: inversion formula without a profile, skcms A2B with one.
    {
        auto s=Rgb(5,4,5,8,4);s.photometric=5;s.extra[338]=Short({2});
        std::vector<std::uint8_t> expected;
        for(std::size_t i=0;i<20;++i){const auto* c=&s.samples[i*5];
            for(unsigned k=0;k<3;++k)expected.push_back(std::uint8_t(((255-c[k])*(255-c[3])+127)/255));
            expected.push_back(std::uint8_t(c[4]));}
        Same(Decode(tf::Encode(s)),5,4,expected,"CMYK inversion mismatch");
        auto s16=Rgb(5,4,4,16,4);s16.photometric=5;expected.clear();
        for(std::size_t i=0;i<20;++i){const auto* c=&s16.samples[i*4];
            for(unsigned k=0;k<3;++k)expected.push_back(std::uint8_t(((std::uint64_t(65535-c[k])*(65535-c[3])+32767)/65535+128)/257));
            expected.push_back(255);}
        Expect(tf::Encode(s16),DecodeError::unsupported_content,"16-bit CMYK silently rounded");
        Same(Decode(tf::Encode(s16),allow16),5,4,expected,"16-bit CMYK mismatch");
        tf::Spec ink;ink.width=3;ink.height=1;ink.spp=4;ink.photometric=5;ink.rows=1;
        ink.samples={255,0,0,0, 0,0,0,255, 40,200,90,128};ink.extra[34675]=Undefined(tf::KOnlyCmykProfile());
        const auto d=Decode(tf::Encode(ink));
        Require(bool(d),"CMYK ICC TIFF rejected");
        for(unsigned c=0;c<3;++c)Require(d.image.rgba8[c]>=250&&d.image.rgba8[4+c]<=5,"CMYK profile not applied (pure cyan must be white under a K-only profile)");
        Require(d.image.rgba8[8]==d.image.rgba8[9]&&d.image.rgba8[9]==d.image.rgba8[10]&&d.image.rgba8[8]>90&&d.image.rgba8[8]<150,"CMYK profile midpoint wrong");
        auto assoc=s;assoc.extra[338]=Short({1});Expect(tf::Encode(assoc),DecodeError::unsupported_content,"Premultiplied CMYK accepted");
        auto inks=s;inks.extra[332]=Short({2});Expect(tf::Encode(inks),DecodeError::unsupported_content,"Non-CMYK ink set accepted");
        seeds.push_back(tf::Encode(ink));checked+=3;
    }
    // Uncompressed YCbCr 1x1, 2x1, 2x2 with chroma replication, BT.601, ReferenceBlackWhite.
    for(auto sub:{std::pair{1u,1u},{2u,1u},{2u,2u}})for(unsigned compression:{1u,5u,32773u}){
        std::vector<std::uint8_t> expected;
        Same(Decode(YcbcrTiff(7,5,sub.first,sub.second,sub.second==2?2:3,compression,expected)),7,5,expected,"YCbCr TIFF mismatch",1);
        Same(Decode(YcbcrTiff(7,5,sub.first,sub.second,4,compression,expected,{16,1,235,1,128,1,240,1,128,1,240,1})),7,5,expected,"YCbCr ReferenceBlackWhite mismatch",1);
        checked+=2;
    }
    {
        std::vector<std::uint8_t> expected;seeds.push_back(YcbcrTiff(7,5,2,2,2,1,expected));
        Expect(YcbcrTiff(7,5,2,2,3,1,expected),DecodeError::invalid_layout,"YCbCr strip splitting a chroma block accepted");
        auto s=Rgb(4,4,3,8);s.photometric=6;s.extra[530]=Short({4,2});Expect(tf::Encode(s),DecodeError::unsupported_content,"YCbCr 4x2 accepted");
        s.extra[530]=Short({1,2});Expect(tf::Encode(s),DecodeError::unsupported_content,"YCbCr 1x2 accepted");
        s.extra[530]=Short({1,1});s.planar=2;Expect(tf::Encode(s),DecodeError::unsupported_content,"Planar YCbCr accepted");
    }
    // New-style JPEG: inline tables, JPEGTables, multiple strips and tiles.
    {
        const auto encode=[](unsigned w,unsigned h,const std::vector<std::uint8_t>& rgba){
            const auto j=image::EncodeImage(image::Format::jpeg,w,h,rgba);Require(bool(j),"JPEG fixture encoding failed");return j.bytes;};
        const auto reference=[](const tf::Bytes& j){const auto d=image::DecodeImage(image::Format::jpeg,j);Require(bool(d),"JPEG reference decode failed");return d.image.rgba8;};
        const auto top=Gradient(24,8,1),bottom=Gradient(24,8,2);
        const auto jtop=encode(24,8,top),jbottom=encode(24,8,bottom);
        auto expected=reference(jtop);const auto lower=reference(jbottom);expected.insert(expected.end(),lower.begin(),lower.end());
        for(bool shared:{false,true}){
            tf::Page page;page.tags={{256,{4,{24},{}}},{257,{4,{16},{}}},{258,Short({8,8,8})},{259,Short({7})},{262,Short({6})},
                {277,Short({3})},{278,{4,{8},{}}},{530,Short({2,2})}};
            if(shared){const auto a=SplitJpegTables(jtop),b=SplitJpegTables(jbottom);
                Require(a.first==b.first,"JPEG fixture tables differ");page.tags[347]=Undefined(a.first);page.chunks={a.second,b.second};}
            else page.chunks={jtop,jbottom};
            const auto b=tf::Build({page});Same(Decode(b),24,16,expected,shared?"JPEGTables TIFF mismatch":"Inline JPEG TIFF mismatch");
            if(shared)seeds.push_back(b);
            auto rgb=page;rgb.tags[262]=Short({2});Expect(tf::Build({rgb}),DecodeError::unsupported_content,"YCbCr-coded JPEG labelled RGB accepted");
            // An RGB-coded stream (component ids R,G,B: no colour transform) under Photometric RGB.
            for(auto& chunk:rgb.chunks)chunk=RgbCoded(chunk);
            auto rgb_expected=reference(RgbCoded(jtop));const auto rgb_lower=reference(RgbCoded(jbottom));
            rgb_expected.insert(rgb_expected.end(),rgb_lower.begin(),rgb_lower.end());
            Require(rgb_expected!=expected,"RGB-coded JPEG fixture did not change the colour transform");
            Same(Decode(tf::Build({rgb})),24,16,rgb_expected,"RGB-coded JPEG TIFF mismatch");
            rgb.tags[262]=Short({6});Expect(tf::Build({rgb}),DecodeError::unsupported_content,"RGB-coded JPEG labelled YCbCr accepted");
            auto gray=page;gray.tags[262]=Short({1});gray.tags[277]=Short({1});gray.tags[258]=Short({8});
            Expect(tf::Build({gray}),DecodeError::unsupported_content,"JPEG component mismatch accepted");
            auto fill=page;fill.tags[266]=Short({2});Expect(tf::Build({fill}),DecodeError::unsupported_content,"FillOrder 2 JPEG accepted");
            if(shared){auto broken=page;auto t=broken.tags[347].raw;t.pop_back();broken.tags[347]=Undefined(t);
                Expect(tf::Build({broken}),DecodeError::invalid_layout,"Malformed JPEGTables accepted");}
            checked++;
        }
        // Two 16x16 tiles; the right tile is padded and cropped.
        auto left=Gradient(16,16,3),right=Gradient(16,16,4);
        const auto jl=encode(16,16,left),jr=encode(16,16,right);const auto rl=reference(jl),rr=reference(jr);
        std::vector<std::uint8_t> tiled(24*16*4);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<24;++x)for(unsigned c=0;c<4;++c)
            tiled[(y*24+x)*4+c]=x<16?rl[(y*16+x)*4+c]:rr[(y*16+x-16)*4+c];
        tf::Page page;page.tiled=true;page.chunks={jl,jr};
        page.tags={{256,{4,{24},{}}},{257,{4,{16},{}}},{258,Short({8,8,8})},{259,Short({7})},{262,Short({6})},
            {277,Short({3})},{322,Short({16})},{323,Short({16})}};
        Same(Decode(tf::Build({page})),24,16,tiled,"Tiled JPEG TIFF mismatch");
        page.tags[259]=Short({6});Expect(tf::Build({page}),DecodeError::unsupported_compression,"Old-style JPEG accepted");
        checked++;
    }
    // ExtraSamples: associated alpha un-premultiplied, unspecified dropped.
    {
        auto s=Rgb(6,4,4,8,8);
        for(std::size_t i=0;i<24;++i){auto* p=&s.samples[i*4];if(i==3)p[3]=0;for(unsigned c=0;c<3;++c)p[c]=p[c]*p[3]/255;}
        s.extra[338]=Short({1});std::vector<std::uint8_t> expected;
        for(std::size_t i=0;i<24;++i){const auto* p=&s.samples[i*4];
            for(unsigned c=0;c<3;++c)expected.push_back(std::uint8_t(p[3]?std::min(255u,(p[c]*255+p[3]/2)/p[3]):0));
            expected.push_back(std::uint8_t(p[3]));}
        Same(Decode(tf::Encode(s)),6,4,expected,"Associated alpha mismatch");
        s.extra[338]=Short({0});Same(Decode(tf::Encode(s)),6,4,Expected(s),"Unspecified extra sample not dropped");
        auto two=Rgb(6,4,5,8,9);two.extra[338]=Short({0,2});Same(Decode(tf::Encode(two)),6,4,Expected(two,4),"Second extra sample alpha mismatch");
        two.extra[338]=Short({3,2});Expect(tf::Encode(two),DecodeError::unsupported_content,"Unknown ExtraSamples value accepted");
        two.extra[338]=Short({2});Expect(tf::Encode(two),DecodeError::invalid_layout,"ExtraSamples count mismatch accepted");
        auto gray=Rgb(5,2,2,8,1);gray.photometric=1;gray.extra[338]=Short({1});
        for(std::size_t i=0;i<10;++i)gray.samples[i*2]=gray.samples[i*2]*gray.samples[i*2+1]/255;
        expected.clear();
        for(std::size_t i=0;i<10;++i){const auto v=gray.samples[i*2],a=gray.samples[i*2+1];
            const auto e=std::uint8_t(a?std::min(255u,(v*255+a/2)/a):0);expected.insert(expected.end(),{e,e,e,std::uint8_t(a)});}
        Same(Decode(tf::Encode(gray)),5,2,expected,"Associated gray alpha mismatch");
        auto wide=Rgb(3,2,4,16,2);wide.extra[338]=Short({1});
        for(std::size_t i=0;i<6;++i)for(unsigned c=0;c<3;++c)wide.samples[i*4+c]=std::uint32_t(std::uint64_t(wide.samples[i*4+c])*wide.samples[i*4+3]/65535);
        expected.clear();
        for(std::size_t i=0;i<6;++i){const auto* p=&wide.samples[i*4];
            for(unsigned c=0;c<3;++c){const auto u=p[3]?std::min<std::uint64_t>(65535,(std::uint64_t(p[c])*65535+p[3]/2)/p[3]):0;expected.push_back(std::uint8_t((u+128)/257));}
            expected.push_back(std::uint8_t((p[3]+128)/257));}
        Same(Decode(tf::Encode(wide),allow16),3,2,expected,"16-bit associated alpha mismatch");
        checked+=5;
    }
    // ICC profiles (tag 34675) converted to sRGB through skcms.
    {
        auto s=Rgb(8,2,3,8,6);s.extra[34675]=Undefined(tf::LinearRgbProfile());
        std::vector<std::uint8_t> expected;
        for(std::size_t i=0;i<16;++i){for(unsigned c=0;c<3;++c)expected.push_back(Clamp(255*tf::SrgbEncode(s.samples[i*3+c]/255.0)));expected.push_back(255);}
        Same(Decode(tf::Encode(s)),8,2,expected,"RGB ICC TIFF mismatch",2);
        seeds.push_back(tf::Encode(s));
        auto g=Rgb(8,2,1,8,6);g.photometric=1;g.extra[34675]=Undefined(tf::LinearGrayProfile());
        expected.clear();for(auto v:g.samples){const auto e=Clamp(255*tf::SrgbEncode(v/255.0));expected.insert(expected.end(),{e,e,e,255});}
        Same(Decode(tf::Encode(g)),8,2,expected,"Gray ICC TIFF mismatch",2);
        auto w=Rgb(4,2,3,16,6);w.extra[34675]=Undefined(tf::LinearRgbProfile());
        expected.clear();
        for(std::size_t i=0;i<8;++i){for(unsigned c=0;c<3;++c)expected.push_back(Clamp(255*tf::SrgbEncode(w.samples[i*3+c]/65535.0)));expected.push_back(255);}
        Same(Decode(tf::Encode(w),allow16),4,2,expected,"16-bit ICC TIFF mismatch",2);
        auto mismatch=g;mismatch.extra[34675]=Undefined(tf::LinearRgbProfile());Expect(tf::Encode(mismatch),DecodeError::unsupported_content,"RGB profile on gray accepted");
        auto cmyk=s;cmyk.extra[34675]=Undefined(tf::KOnlyCmykProfile());Expect(tf::Encode(cmyk),DecodeError::unsupported_content,"CMYK profile on RGB accepted");
        auto corrupt=s;auto profile=tf::LinearRgbProfile();tf::Be(profile,0,1000);corrupt.extra[34675]=Undefined(profile);
        Expect(tf::Encode(corrupt),DecodeError::invalid_layout,"ICC size mismatch accepted");
        auto huge=s;profile.assign(4*1024*1024+1,std::byte{0});huge.extra[34675]=Undefined(profile);
        Expect(tf::Encode(huge),DecodeError::resource_limit,"Oversized ICC accepted");
        checked+=3;
    }
    // Explicit rejections.
    {
        const auto base=Rgb(6,4);
        const auto variant=[&](auto change){auto s=base;change(s);return tf::Encode(s);};
        for(unsigned compression:{2u,3u,4u,6u,34712u,34887u})
            Expect(variant([&](tf::Spec& s){s.compression=compression;}),DecodeError::unsupported_compression,"Unsupported TIFF compression accepted");
        Expect(variant([](tf::Spec& s){s.predictor=3;}),DecodeError::unsupported_content,"Floating-point predictor accepted");
        Expect(variant([](tf::Spec& s){s.extra[339]=Short({3,3,3});}),DecodeError::unsupported_content,"IEEE float samples accepted");
        Expect(variant([](tf::Spec& s){s.extra[339]=Short({2});}),DecodeError::unsupported_content,"Signed samples accepted");
        Expect(variant([](tf::Spec& s){s.photometric=8;}),DecodeError::unsupported_content,"CIELab accepted");
        Expect(variant([](tf::Spec& s){s.photometric=4;}),DecodeError::unsupported_content,"Transparency mask accepted");
        Expect(variant([](tf::Spec& s){s.extra[274]=Short({9});}),DecodeError::invalid_layout,"Orientation 9 accepted");
        Expect(variant([](tf::Spec& s){s.extra[266]=Short({3});}),DecodeError::invalid_layout,"FillOrder 3 accepted");
        Expect(variant([](tf::Spec& s){s.bits=12;s.samples=tf::Pattern(6,4,3,12);}),DecodeError::unsupported_content,"12-bit samples accepted");
        Expect(variant([](tf::Spec& s){s.bits=4;s.samples=tf::Pattern(6,4,3,4);}),DecodeError::unsupported_content,"4-bit RGB accepted");
        Expect(variant([](tf::Spec& s){s.photometric=3;s.spp=1;s.bits=16;s.samples=tf::Pattern(6,4,1,16);}),DecodeError::unsupported_content,"16-bit palette accepted");
        Expect(variant([](tf::Spec& s){s.tileW=20;s.tileH=16;}),DecodeError::invalid_layout,"Tile width not a multiple of 16 accepted");
        image::DecodeOptions small;small.maximumPixels=200;
        Expect(variant([](tf::Spec& s){s.tileW=16;s.tileH=16;}),DecodeError::resource_limit,"Tile larger than pixel budget accepted",small);
        Expect(variant([](tf::Spec& s){s.predictor=2;s.photometric=1;s.spp=1;s.bits=4;s.samples=tf::Pattern(6,4,1,4);}),DecodeError::unsupported_content,"Sub-byte predictor accepted");
        Expect(variant([](tf::Spec& s){s.extra[338]=Short({2});}),DecodeError::invalid_layout,"ExtraSamples without an extra sample accepted");
        Expect(variant([](tf::Spec& s){s.spp=9;s.samples=tf::Pattern(6,4,9,8);}),DecodeError::unsupported_content,"Nine samples per pixel accepted");
        auto both=tf::MakePage(base);both.tags[322]=Short({16});both.tags[323]=Short({16});both.tags[324]={4,{8},{}};both.tags[325]={4,{1},{}};
        Expect(tf::Build({both}),DecodeError::invalid_layout,"Strip and tile layout together accepted");
        auto extra=tf::MakePage(base);extra.chunks.push_back(extra.chunks.back());Expect(tf::Build({extra}),DecodeError::invalid_layout,"Strip count mismatch accepted");
        auto b=tf::Encode(base);image::DecodeOptions o;o.directionIndex=1;Expect(b,DecodeError::selection_out_of_range,"TIFF direction selection accepted",o);
        o={};o.mipIndex=1;Expect(b,DecodeError::selection_out_of_range,"TIFF mip selection accepted",o);
        auto big=tf::Encode(base,true);tf::Put(big,4,4,2);Expect(big,DecodeError::unsupported_content,"BigTIFF offset size 4 accepted");
        auto version=b;tf::Put(version,2,44,2);Expect(version,DecodeError::unsupported_content,"TIFF version 44 accepted");
        auto loop=tf::Build({tf::MakePage(base),tf::MakePage(base)});
        const auto first=tf::Get(loop,4);const auto entries=tf::Get(loop,first,2);
        const auto second=tf::Get(loop,first+2+entries*12);const auto entries2=tf::Get(loop,second,2);
        tf::Put(loop,second+2+entries2*12,first,4);Expect(loop,DecodeError::invalid_layout,"TIFF IFD loop accepted");
    }
    const auto f=tf::Make();
    auto w=image::EncodeImage(image::Format::tiff,7,3,f.rgba);
    Require(bool(w)&&!w.alphaFlattened&&w.bytes.size()==218+f.rgba.size(),"TIFF write layout wrong");
    Require(image::EncodeImage(image::Format::tiff,7,3,f.rgba).bytes==w.bytes,"TIFF encoding nondeterministic");
    auto d=image::DecodeImage(image::Format::tiff,w.bytes);Require(bool(d)&&d.image.rgba8==f.rgba,"TIFF writer lost RGBA");
    image::EncodeOptions out;out.maximumOutputBytes=w.bytes.size()-1;
    Require(image::EncodeImage(image::Format::tiff,7,3,f.rgba,out).error==image::EncodeError::output_limit,"TIFF header escaped output budget");
    out.maximumOutputBytes=w.bytes.size();Require(bool(image::EncodeImage(image::Format::tiff,7,3,f.rgba,out)),"Exact TIFF budget rejected");
    out.resourceProfile=image::RasterProfile::largeRaster;Require(!image::EncodeImage(image::Format::tiff,7,3,f.rgba,out),"TIFF broadened large profile");
    // Deflate + predictor output: lossless, deterministic, multi-strip, budgeted.
    for(auto size:{std::pair{7u,3u},{300u,200u},{1u,1u}}){
        auto rgba=size.first==7?f.rgba:Gradient(size.first,size.second,5);
        if(size.first==300)for(std::size_t i=3;i<rgba.size();i+=4)rgba[i]=std::uint8_t(i/7);
        image::EncodeOptions deflate;deflate.tiffDeflate=true;
        const auto z=image::EncodeImage(image::Format::tiff,size.first,size.second,rgba,deflate);
        Require(bool(z)&&image::EncodeImage(image::Format::tiff,size.first,size.second,rgba,deflate).bytes==z.bytes,"Deflate TIFF nondeterministic");
        Same(Decode(z.bytes),size.first,size.second,rgba,"Deflate TIFF round trip mismatch");
        if(size.first==300){
            Require(z.bytes.size()*4<rgba.size(),"Deflate TIFF did not compress a gradient");
            Require(tf::Get(z.bytes,tf::Field(z.bytes,273)+4)>1,"Deflate TIFF wrote one oversized strip");
            seeds.push_back(z.bytes);
        }
        Require(tf::Get(z.bytes,tf::Field(z.bytes,259)+8,2)==8&&tf::Get(z.bytes,tf::Field(z.bytes,317)+8,2)==2,"Deflate TIFF tags wrong");
        deflate.maximumOutputBytes=z.bytes.size()-1;
        Require(image::EncodeImage(image::Format::tiff,size.first,size.second,rgba,deflate).error==image::EncodeError::output_limit,"Deflate TIFF escaped output budget");
        deflate.maximumOutputBytes=z.bytes.size();Require(bool(image::EncodeImage(image::Format::tiff,size.first,size.second,rgba,deflate)),"Exact Deflate TIFF budget rejected");
        ++checked;
    }
    image::DecodeOptions in;in.maximumPixels=20;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF pixel limit ignored");
    in={};in.maximumSourceBytes=f.bytes.size()-1;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF byte limit ignored");
    in={};in.frameIndex=1;Expect(f.bytes,DecodeError::selection_out_of_range,"TIFF frame selection ignored",in);
    in={};in.resourceProfile=image::RasterProfile::largeRaster;Require(!image::DecodeImage(image::Format::tiff,f.bytes,in),"TIFF decode broadened large profile");
    for(auto [tag,value]:{std::pair{256u,9000u},{284u,3u},{259u,2u},{278u,0u},{262u,8u},{338u,4u}}){
        auto b=f.bytes;tf::Scalar(b,tag,value);Reject(b);
    }
    for(std::size_t size=0;size<f.bytes.size();++size)Reject(tf::Bytes(f.bytes.begin(),f.bytes.begin()+size));
    auto b=f.bytes;tf::Put(b,22,256,2);Reject(b); // Duplicate directory tag.
    b=f.bytes;tf::Put(b,8+2+tf::Get(b,8,2)*12,8,4);Reject(b); // Next IFD loops to itself.
    b=f.bytes;tf::Put(b,2,43,2);Reject(b); // Classic directory labelled BigTIFF.
    b=f.bytes;tf::Put(b,tf::Get(b,tf::Field(b,258)+8),16,2);Reject(b);
    b=f.bytes;tf::Put(b,tf::Get(b,tf::Field(b,273)+8),0,4);Reject(b); // Header overlap.
    b=f.bytes;const auto offsets=tf::Get(b,tf::Field(b,273)+8);tf::Put(b,offsets+4,tf::Get(b,offsets),4);Reject(b);
    b=f.bytes;tf::Put(b,tf::Field(b,273)+4,8193,4);Reject(b);
    auto pack=tf::Make(4,2,true,32773);b=pack.bytes;
    const auto first=tf::Get(b,tf::Get(b,tf::Field(b,273)+8));b[first+1]=std::byte{127};Reject(b); // Row overflow.
    auto compressed=tf::Make(4,2,true,8,2);b=compressed.bytes;b.back()^=std::byte{1};Reject(b);
    // A replicate packet for every grayscale row, with explicit no-ops.
    auto repeat=tf::Make(1,1,true,32773,1,1);b=repeat.bytes;
    const auto positions=tf::Get(b,tf::Field(b,273)+8),sizes=tf::Get(b,tf::Field(b,279)+8);
    for(unsigned row=0;row<3;++row){const auto p=tf::Get(b,positions+row*4);b[p]=std::byte{250};b[p+1]=std::byte(30+row);tf::Put(b,sizes+row*4,2,4);}
    d=image::DecodeImage(image::Format::tiff,b);Require(bool(d),"PackBits replicate failed");
    for(unsigned row=0;row<3;++row)for(unsigned x=0;x<7;++x)Require(d.image.rgba8[(row*7+x)*4]==30+row,"PackBits repeat pixels differ");
    auto truncated_repeat=b;tf::Put(truncated_repeat,sizes,1,4);Reject(truncated_repeat);
    auto leftover=pack.bytes;tf::Put(leftover,tf::Field(leftover,278)+8,3,4);Reject(leftover);
    image::DecodeOptions bounded;bounded.maximumPixels=1024;bounded.maximumDimension=64;
    const tf::Bytes* legacy[]{&f.bytes,&pack.bytes,&compressed.bytes};
    for(const auto* seed:legacy)for(std::size_t p=0;p<seed->size();++p)
        for(unsigned bit:{1u,128u}){auto mutated=*seed;mutated[p]^=std::byte(bit);
            const auto result=image::DecodeImage(image::Format::tiff,mutated,bounded);
            if(result)Require(result.image.rgba8.size()==std::size_t(result.image.width)*result.image.height*4,"Mutated TIFF result extent differs");}
    // Truncation and corruption sweeps across the broadened feature set.
    for(const auto& seed:seeds)Sweep(seed,"TIFF seed");
    std::cout<<"TIFF native profile passed: "<<checked<<" pixel variants, "<<seeds.size()<<" swept seeds, writer, malformed input and resource checks\n";
}
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version"){std::cout<<"arken-tiff-codec-tests-r6354\n";return 0;}
    try{
        if(argc==3&&std::string_view(argv[1])=="--browser-fixtures"){
            const fs::path root=argv[2];Require(root.is_absolute()&&!fs::exists(root),"Use a new absolute fixture directory");
            fs::create_directories(root.parent_path());Require(fs::create_directory(root),"Fixture destination already exists");
            constexpr unsigned width=320,height=192;std::vector<std::uint8_t> rgba(width*height*4);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto p=(y*width+x)*4;
                rgba[p]=std::uint8_t(x*3);rgba[p+1]=std::uint8_t(y*5);rgba[p+2]=std::uint8_t(x+y);
                rgba[p+3]=std::uint8_t((x/32+y/32)%3==0?0:(x/32+y/32)%3==1?127:255);}
            const auto png=image::EncodeImage(image::Format::png,width,height,rgba);
            const auto tiff=image::EncodeImage(image::Format::tiff,width,height,rgba);
            Require(bool(png)&&bool(tiff),"Browser reference encoding failed");
            Write(root/"color-grid.png",png.bytes);Write(root/"color-grid.tiff",tiff.bytes);Write(root/"reference.rgba",rgba);
            std::cout<<"TIFF browser references emitted\n";return 0;
        }
        if(argc==3&&std::string_view(argv[1])=="--emit"){
            const fs::path root=argv[2];Require(root.is_absolute()&&!fs::exists(root),"Use a new absolute fixture directory");
            fs::create_directories(root.parent_path());Require(fs::create_directory(root),"Fixture destination already exists");
            const auto f=tf::Make();const auto rgba=image::EncodeImage(image::Format::tiff,7,3,f.rgba);
            Require(bool(rgba),"Reference encoding failed");Write(root/"native-rgba.tiff",rgba.bytes);Write(root/"reference.rgba",f.rgba);
            for(auto [name,little,compression]:{std::tuple{"rgb-be.tiff",false,1u},std::tuple{"rgba-packbits.tiff",true,32773u},std::tuple{"rgba-deflate.tiff",true,8u}}){
                const auto fixture=tf::Make(name==std::string_view("rgb-be.tiff")?3:4,2,little,compression,compression==8?2:1);
                Write(root/name,fixture.bytes);Write(root/(std::string(name)+".rgba"),fixture.rgba);
            }
            std::cout<<"TIFF references emitted\n";return 0;
        }
        if(argc==4&&std::string_view(argv[1])=="--check"){
            const auto d=image::DecodeImage(image::Format::tiff,Read(argv[2]));Require(bool(d),image::ErrorMessage(d.issue.code).data());
            const auto raw=Read(argv[3]);Require(raw.size()==d.image.rgba8.size()&&std::equal(raw.begin(),raw.end(),d.image.rgba8.begin(),[](auto a,auto b){return std::to_integer<unsigned>(a)==b;}),"Independent TIFF sample mismatch");
            std::cout<<"Independent TIFF exact samples passed: "<<d.image.width<<"x"<<d.image.height<<"\n";return 0;
        }
        Require(argc==1,"Unknown TIFF test argument");Tests();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
