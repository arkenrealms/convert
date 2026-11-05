#include "reader.hpp"
#include "large_raster.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
// Bound every inflater/decoder allocation, including malformed compressed data
// that expands beyond its declared image. There is no mutable allocator context.
// Explicit offline PNG map imports admit 32M RGBA pixels plus one filter
// byte per row. Default callers retain their smaller decoded-pixel budget.
constexpr std::size_t maximum_allocation=128u*1024u*1024u+8192u;
void* raster_allocate(std::size_t size){return size<=maximum_allocation?std::malloc(size):nullptr;}
void* raster_reallocate(void* pointer,std::size_t size){return size<=maximum_allocation?std::realloc(pointer,size):nullptr;}
}
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(size) raster_allocate(size)
#define STBI_REALLOC(pointer,size) raster_reallocate(pointer,size)
#define STBI_FREE(pointer) std::free(pointer)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) && !defined(__clang__)
// GCC reports a false maybe-uninitialized read of stbi__context fields when it inlines
// stbi__refill_buffer into memory-only loads; the context is fully assigned by stbi__start_mem.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <stb_image.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <skcms_public.h>

#include <array>

namespace arken::image::v1::detail {
namespace {
constexpr std::size_t maximum_profile_bytes=4*1024*1024;
std::uint32_t be32(Bytes b,std::size_t p){return (std::uint32_t(U8(b,p))<<24)|(std::uint32_t(U8(b,p+1))<<16)|(std::uint32_t(U8(b,p+2))<<8)|U8(b,p+3);}
std::uint32_t crc32(Bytes bytes){
    std::uint32_t value=0xffffffff;
    for(auto byte:bytes){value^=std::to_integer<std::uint8_t>(byte);for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}
    return ~value;
}
// One source colour description for PNG, JPEG and BMP: either an embedded ICC
// profile (bytes own the storage skcms points into) or a profile synthesised
// from PNG cHRM/gAMA or BMP V4/V5 endpoints. Inactive means the pixels are sRGB.
// Non-copyable and non-movable: skcms_ICCProfile holds pointers into bytes.
struct ColourProfile {
    std::vector<std::byte> bytes;
    skcms_ICCProfile profile{};
    bool active=false;
    ColourProfile()=default;
    ColourProfile(const ColourProfile&)=delete;
    ColourProfile& operator=(const ColourProfile&)=delete;
};
// colour.bytes[0,size) holds exactly one ICC profile.
DecodeError parse_icc(ColourProfile& colour,std::size_t size){
    if(size<132||be32(colour.bytes,0)!=size)return DecodeError::invalid_layout;
    colour.bytes.resize(size+2); // skcms documents two readable padding bytes.
    colour.bytes[size]=colour.bytes[size+1]=std::byte{0};
    if(!skcms_Parse(colour.bytes.data(),size,&colour.profile))return DecodeError::invalid_layout;
    colour.active=true;
    return DecodeError::none;
}
DecodeError load_icc(Bytes profile,ColourProfile& colour){
    if(colour.active)return DecodeError::invalid_layout;
    if(profile.size()>maximum_profile_bytes)return DecodeError::resource_limit;
    colour.bytes.assign(profile.begin(),profile.end());
    return parse_icc(colour,profile.size());
}
bool rgb_profile(const skcms_ICCProfile& profile){
    return profile.data_color_space==skcms_Signature_RGB&&skcms_GetInputChannelCount(&profile)==3;
}
void synthetic_profile(ColourProfile& colour,const skcms_Matrix3x3& toXYZD50,const std::array<skcms_TransferFunction,3>& curves){
    skcms_Init(&colour.profile);
    colour.profile.has_trc=true;
    for(unsigned i=0;i<3;++i){colour.profile.trc[i].table_entries=0;colour.profile.trc[i].parametric=curves[i];}
    skcms_SetXYZD50(&colour.profile,&toXYZD50);
    colour.active=true;
}
skcms_TransferFunction power_curve(double exponent){
    return {static_cast<float>(exponent),1.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
}
DecodeError read_icc(Bytes chunk,ColourProfile& colour){
    if(colour.active)return DecodeError::invalid_layout;
    std::size_t name=0;
    while(name<chunk.size()&&name<80&&U8(chunk,name)!=0){
        const auto c=U8(chunk,name);
        if(c<32||(c>126&&c<161)||(c==32&&(name==0||U8(chunk,name-1)==32)))return DecodeError::invalid_layout;
        ++name;
    }
    if(name==0||name>79||!Range(chunk,name,2)||U8(chunk,name)!=0||U8(chunk,name-1)==32||U8(chunk,name+1)!=0)return DecodeError::invalid_layout;
    const auto compressed=chunk.subspan(name+2);
    if(compressed.size()<6)return DecodeError::truncated;
    if(compressed.size()>maximum_profile_bytes)return DecodeError::resource_limit;
    colour.bytes.resize(maximum_profile_bytes+2);
    stbi__zbuf inflater{};
    inflater.zbuffer=reinterpret_cast<stbi_uc*>(const_cast<std::byte*>(compressed.data()));
    inflater.zbuffer_end=inflater.zbuffer+compressed.size();
    if(!stbi__do_zlib(&inflater,reinterpret_cast<char*>(colour.bytes.data()),int(maximum_profile_bytes),0,1))return DecodeError::invalid_layout;
    const auto size=std::size_t(inflater.zout-inflater.zout_start);
    // Require precisely one zlib stream and its Adler trailer, including when
    // the inflater read ahead into that trailer. No trailing/concatenated data.
    if(inflater.hit_zeof_once||inflater.num_bits<0||inflater.zbuffer-inflater.num_bits/8!=inflater.zbuffer_end-4)return DecodeError::invalid_layout;
    std::uint32_t a=1,b=0;
    for(std::size_t i=0;i<size;++i){a=(a+U8(colour.bytes,i))%65521;b=(b+a)%65521;}
    if(((b<<16)|a)!=be32(compressed,compressed.size()-4))return DecodeError::invalid_layout;
    const auto parsed=parse_icc(colour,size);
    if(parsed!=DecodeError::none)return parsed;
    if(!rgb_profile(colour.profile))return DecodeError::unsupported_content;
    return DecodeError::none;
}
bool apply_colour(const ColourProfile& colour,Image& image){
    return !colour.active||skcms_Transform(image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,&colour.profile,
        image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),std::size_t(image.width)*image.height);
}
// PNG colour precedence (PNG 2nd edition, 11.3.3): an sRGB chunk wins; else
// iCCP; else cHRM and/or gAMA describe the encoding. A lone gAMA keeps the sRGB
// primaries; a lone cHRM keeps the sRGB transfer curve. Values within writer
// rounding of the sRGB set (gAMA 45000..46000, each chromaticity within 0.001)
// are sRGB and leave the pixels untouched.
DecodeError png_chunk_colour(bool hasGamma,std::uint32_t gamma,bool hasChromaticities,
    const std::array<std::uint32_t,8>& chromaticities,ColourProfile& colour){
    constexpr std::array<std::uint32_t,8> srgb{31270,32900,64000,33000,30000,60000,15000,6000};
    const bool srgbGamma=!hasGamma||(gamma>=45000&&gamma<=46000);
    bool srgbPrimaries=true;
    if(hasChromaticities)for(unsigned i=0;i<8;++i)
        srgbPrimaries=srgbPrimaries&&(chromaticities[i]>srgb[i]?chromaticities[i]-srgb[i]:srgb[i]-chromaticities[i])<=100;
    if(srgbGamma&&srgbPrimaries)return DecodeError::none;
    const auto& c=hasChromaticities?chromaticities:srgb;
    for(unsigned i=0;i<8;++i)if(c[i]>100000||(i%2&&!c[i]))return DecodeError::invalid_layout;
    const auto v=[&](unsigned i){return static_cast<float>(c[i]/100000.0);};
    skcms_Matrix3x3 toXYZD50;
    if(!skcms_PrimariesToXYZD50(v(2),v(3),v(4),v(5),v(6),v(7),v(0),v(1),&toXYZD50))return DecodeError::invalid_layout;
    // gAMA stores the encoding exponent; decoding raises samples to 1/gamma.
    const auto curve=hasGamma?power_curve(100000.0/gamma):*skcms_sRGB_TransferFunction();
    synthetic_profile(colour,toXYZD50,{curve,curve,curve});
    return DecodeError::none;
}
// APNG (when `frames` is given): the default image is decoded only when it is
// also the first animation frame (an fcTL precedes the first IDAT). APNG 1.0
// requires that frame to cover the whole canvas at offset 0,0, so with either
// blend op over a transparent canvas it equals the IDAT image exactly; fcTL
// and fdAT are ancillary chunks the decoder skips. `frames` reports acTL's
// frame count. A default image outside the animation remains unsupported.
DecodeError png_profile(Bytes bytes,bool allow16,bool scalarData=false,ColourProfile* colour=nullptr,std::uint32_t* frames=nullptr){
    constexpr unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(bytes.size()<8||std::memcmp(bytes.data(),signature,8))return DecodeError::wrong_magic;
    bool header=false,data=false,srgb=false,palette=false,hasGamma=false,hasChromaticities=false;
    bool animated=false,firstFrameControl=false;
    std::uint32_t gamma=0;std::array<std::uint32_t,8> chromaticities{};
    for(std::size_t at=8;at<bytes.size();){
        if(!Range(bytes,at,12))return DecodeError::truncated;
        const auto count=be32(bytes,at),kind=be32(bytes,at+4);
        if(!Range(bytes,at+12,count))return DecodeError::truncated;
        if(crc32(bytes.subspan(at+4,std::size_t(count)+4))!=be32(bytes,at+8+count))return DecodeError::invalid_layout;
        if(!header){
            if(kind!=0x49484452u||count!=13)return DecodeError::invalid_layout;
            header=true;
            if(U8(bytes,at+16)==16&&!allow16)return DecodeError::unsupported_content;
        }else if(kind==0x49484452u)return DecodeError::invalid_layout;
        // cICP (coding-independent code points) remains unsupported.
        if(!scalarData&&kind==0x63494350u)return DecodeError::unsupported_content;
        if(kind==0x6163544cu){ // acTL
            if(!frames||scalarData)return DecodeError::unsupported_content;
            if(count!=8||animated||data)return DecodeError::invalid_layout;
            const auto declared=be32(bytes,at+8);
            if(!declared)return DecodeError::invalid_layout;
            *frames=declared;animated=true;
        }
        if(kind==0x6663544cu&&animated&&!data&&!firstFrameControl){ // fcTL before IDAT
            if(count!=26)return DecodeError::invalid_layout;
            if(be32(bytes,at+12)!=be32(bytes,16)||be32(bytes,at+16)!=be32(bytes,20)||be32(bytes,at+20)||be32(bytes,at+24))
                return DecodeError::invalid_layout;
            firstFrameControl=true;
        }
        if(kind==0x49444154u&&animated&&!firstFrameControl)return DecodeError::unsupported_content;
        if(kind==0x504c5445u)palette=true;
        if(kind==0x69434350u&&!scalarData){
            if(!colour||data||palette||srgb)return DecodeError::invalid_layout;
            if(U8(bytes,25)!=2&&U8(bytes,25)!=3&&U8(bytes,25)!=6)return DecodeError::unsupported_content;
            const auto error=read_icc(bytes.subspan(at+8,count),*colour);
            if(error!=DecodeError::none)return error;
        }
        // Scalar import explicitly consumes stored data values, not displayed
        // colour. ICC/gamma/chromaticity metadata must not remap heights.
        if(kind==0x67414d41u){
            if(count!=4||hasGamma)return DecodeError::invalid_layout;
            gamma=be32(bytes,at+8);hasGamma=true;
            if(!gamma)return DecodeError::invalid_layout;
        }
        if(kind==0x73524742u){if(count!=1||U8(bytes,at+8)>3||srgb||(colour&&colour->active))return DecodeError::invalid_layout;srgb=true;}
        if(kind==0x6348524du){
            if(count!=32||hasChromaticities)return DecodeError::invalid_layout;
            for(unsigned i=0;i<8;++i)chromaticities[i]=be32(bytes,at+8+i*4);
            hasChromaticities=true;
        }
        if(kind==0x49444154u)data=true;
        if(kind==0x49454e44u){
            if(count!=0||!data||at+12!=bytes.size())return DecodeError::invalid_layout;
            if(!scalarData&&colour&&!colour->active&&!srgb&&(hasGamma||hasChromaticities))
                return png_chunk_colour(hasGamma,gamma,hasChromaticities,chromaticities,*colour);
            return DecodeError::none;
        }
        at+=std::size_t(count)+12;
    }
    return DecodeError::truncated;
}
// A pixel-count limit alone does not bound IDAT expansion: a PNG can declare
// one pixel and inflate megabytes of ignored trailing scanlines. Validate the
// exact admitted filtered extent before the decoder can grow an inflate buffer.
DecodeError png_inflate_shape(Bytes bytes,std::uint32_t width,std::uint32_t height,const DecodeOptions& options){
    if(bytes.size()<33||U8(bytes,26)||U8(bytes,27)||U8(bytes,28)>1)return DecodeError::invalid_layout;
    const auto depth=U8(bytes,24),type=U8(bytes,25);
    const unsigned channels=type==0||type==3?1:type==2?3:type==4?2:type==6?4:0;
    if(!channels||!(depth==1||depth==2||depth==4||depth==8||depth==16))return DecodeError::invalid_layout;
    constexpr unsigned x0[]={0,4,0,2,0,1,0},y0[]={0,0,4,0,2,0,1};
    constexpr unsigned dx[]={8,8,4,4,2,2,1},dy[]={8,8,8,4,4,2,2};
    const bool interlaced=U8(bytes,28)!=0;
    std::uint64_t extent=0;
    for(unsigned pass=0;pass<(interlaced?7u:1u);++pass){
        const auto w=interlaced?(width>x0[pass]?(width-x0[pass]+dx[pass]-1)/dx[pass]:0):width;
        const auto h=interlaced?(height>y0[pass]?(height-y0[pass]+dy[pass]-1)/dy[pass]:0):height;
        if(w&&h)extent+=(((std::uint64_t(w)*channels*depth+7)/8)+1)*h;
    }
    const auto limit=options.resourceProfile==RasterProfile::largeRaster?maximumLargeRasterAllocation:maximum_allocation;
    if(!extent||extent>limit||extent>std::uint64_t(std::numeric_limits<int>::max()))return DecodeError::resource_limit;
    std::vector<std::byte> compressed;
    for(std::size_t at=8;at<bytes.size();){
        if(!Range(bytes,at,12))return DecodeError::truncated;
        const auto count=be32(bytes,at),kind=be32(bytes,at+4);
        if(!Range(bytes,at+12,count))return DecodeError::truncated;
        if(kind==0x49444154u)compressed.insert(compressed.end(),bytes.begin()+at+8,bytes.begin()+at+8+count);
        at+=std::size_t(count)+12;
    }
    if(compressed.size()<6||compressed.size()>std::size_t(std::numeric_limits<int>::max()))return DecodeError::invalid_layout;
    std::vector<char> filtered(static_cast<std::size_t>(extent));
    const auto decoded=stbi_zlib_decode_buffer(filtered.data(),static_cast<int>(extent),
        reinterpret_cast<const char*>(compressed.data()),static_cast<int>(compressed.size()));
    return decoded==static_cast<int>(extent)?DecodeError::none:DecodeError::invalid_layout;
}
// Walks the complete GIF block structure (bounded sub-block chains, colour
// tables, extensions and image descriptors) and counts frames. Only the first
// frame is decoded; frameCount reports how many image descriptors exist.
DecodeError gif_profile(Bytes bytes,std::uint32_t& frames){
    if(bytes.size()<6)return DecodeError::truncated;
    const bool version87=!std::memcmp(bytes.data(),"GIF87a",6);
    const bool version89=!std::memcmp(bytes.data(),"GIF89a",6);
    if(!version87&&!version89)return DecodeError::wrong_magic;
    if(bytes.size()<13)return DecodeError::truncated;
    const auto width=U16(bytes,6),height=U16(bytes,8);
    if(!width||!height)return DecodeError::invalid_dimensions;
    std::uint64_t at=13;
    if(U8(bytes,10)&0x80u)at+=3u<<((U8(bytes,10)&7u)+1u);
    const auto blocks=[&]{
        for(;;){
            if(at>=bytes.size())return DecodeError::truncated;
            const auto size=U8(bytes,at++);
            if(!size)return DecodeError::none;
            if(!Range(bytes,at,size))return DecodeError::truncated;
            at+=size;
        }
    };
    frames=0;
    for(;;){
        if(at>=bytes.size())return DecodeError::truncated;
        const auto introducer=U8(bytes,at++);
        if(introducer==0x3b){
            if(!frames)return DecodeError::invalid_layout;
            return at==bytes.size()?DecodeError::none:DecodeError::trailing_bytes;
        }
        if(introducer==0x21){
            if(at>=bytes.size())return DecodeError::truncated;
            ++at; // extension label; every extension body is a sub-block chain.
        }else if(introducer==0x2c){
            if(!Range(bytes,at,9))return DecodeError::truncated;
            const std::uint32_t x=U16(bytes,at),y=U16(bytes,at+2),w=U16(bytes,at+4),h=U16(bytes,at+6),flags=U8(bytes,at+8);
            // The decoded first frame must lie on the logical screen (stb rejects
            // it otherwise); later frames are only walked, never decoded, and
            // browsers clip them, so only their block structure is enforced.
            if(!frames&&(!w||!h||x+w>width||y+h>height))return DecodeError::invalid_layout;
            at+=9;
            if(flags&0x80u)at+=3u<<((flags&7u)+1u);
            if(at>=bytes.size())return DecodeError::truncated;
            const auto codeSize=U8(bytes,at++);
            if(codeSize<1||codeSize>11)return DecodeError::invalid_layout;
            ++frames;
        }else return DecodeError::invalid_layout;
        const auto error=blocks();
        if(error!=DecodeError::none)return error;
    }
}
struct JpegInfo {
    unsigned orientation=1,components=0;
    int adobeTransform=-1;
    unsigned iccCount=0;
    std::uint64_t iccBytes=0;
    std::vector<Bytes> icc;
    std::vector<bool> iccSeen;
    std::size_t end=0; // one past the primary image's EOI marker
};
// Walks from the first SOS to the primary image's EOI: entropy-coded data
// (where FF is followed by a 00 stuffing byte, an RSTn marker or FF fill) and
// any further marker segments (progressive DHT/SOS, DNL, COM, APPn). Returns
// the offset one past EOI, or 0 when the stream ends first (truncated) or an
// SOI appears inside the image. Bytes after EOI belong to no part of the
// primary image: an Ultra HDR gain map, an MPF secondary image, a camera
// trailer or padding. They are ignored and never reach the decoder.
std::size_t jpeg_end(Bytes bytes,std::size_t at){
    while(at+1<bytes.size()){
        if(U8(bytes,at)!=255){++at;continue;}
        const auto marker=U8(bytes,at+1);
        if(marker==255){++at;continue;}
        if(marker==0||marker==1||(marker>=0xd0&&marker<=0xd7)){at+=2;continue;}
        if(marker==217)return at+2;
        if(marker==216)return 0;
        if(!Range(bytes,at+2,2))return 0;
        const auto length=(std::uint32_t(U8(bytes,at+2))<<8)|U8(bytes,at+3);
        if(length<2||!Range(bytes,at+2,length))return 0;
        at+=2+length;
    }
    return 0;
}
DecodeError jpeg_profile(Bytes bytes,JpegInfo& info){
    if(bytes.size()<4||U8(bytes,0)!=255||U8(bytes,1)!=216)return DecodeError::wrong_magic;
    bool seen_orientation=false;
    for(std::size_t at=2;at+1<bytes.size();){
        if(U8(bytes,at++)!=255)return DecodeError::invalid_layout;
        while(at<bytes.size()&&U8(bytes,at)==255)++at;
        if(at==bytes.size())return DecodeError::truncated;
        const auto marker=U8(bytes,at++);
        if(marker==218){
            if(!info.components)return DecodeError::invalid_layout;
            for(unsigned i=0;i<info.iccCount;++i)if(!info.iccSeen[i])return DecodeError::invalid_layout;
            info.end=jpeg_end(bytes,at-2);
            return info.end?DecodeError::none:DecodeError::truncated;
        }
        if(marker==217)return DecodeError::invalid_layout;
        if(!Range(bytes,at,2))return DecodeError::truncated;
        const auto length=(std::uint32_t(U8(bytes,at))<<8)|U8(bytes,at+1);
        if(length<2||!Range(bytes,at,length))return DecodeError::truncated;
        auto payload=bytes.subspan(at+2,length-2);
        // DAC defines arithmetic-coding conditioning; SOF3/5-7 are lossless or
        // hierarchical and SOF9-11/13-15 arithmetic. None are decoded.
        if(marker==0xcc)return DecodeError::unsupported_compression;
        if(marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8){
            if(marker!=0xc0&&marker!=0xc1&&marker!=0xc2)return DecodeError::unsupported_compression;
            if(info.components)return DecodeError::invalid_layout;
            if(payload.size()<6)return DecodeError::invalid_layout;
            if(U8(payload,0)!=8)return DecodeError::unsupported_content; // 12-bit samples
            info.components=U8(payload,5);
            if(info.components!=1&&info.components!=3&&info.components!=4)return DecodeError::unsupported_content;
        }
        if(marker==226&&payload.size()>=12&&!std::memcmp(payload.data(),"ICC_PROFILE\0",12)){
            // ICC.1 B.4: chunks carry a 1-based sequence number and the chunk
            // count; every chunk must agree on the count and appear once.
            if(payload.size()<14)return DecodeError::invalid_layout;
            const unsigned sequence=U8(payload,12),count=U8(payload,13);
            if(!count||!sequence||sequence>count||(info.iccCount&&count!=info.iccCount))return DecodeError::invalid_layout;
            if(!info.iccCount){info.iccCount=count;info.icc.assign(count,{});info.iccSeen.assign(count,false);}
            if(info.iccSeen[sequence-1])return DecodeError::invalid_layout;
            info.iccSeen[sequence-1]=true;info.icc[sequence-1]=payload.subspan(14);
            info.iccBytes+=payload.size()-14;
            if(info.iccBytes>maximum_profile_bytes)return DecodeError::resource_limit;
        }
        // Recognised exactly as stb_image does (payload >= 12, "Adobe\0", last
        // segment wins) so the admitted interpretation is the decoded one.
        if(marker==0xee&&payload.size()>=12&&!std::memcmp(payload.data(),"Adobe\0",6))info.adobeTransform=U8(payload,11);
        if(marker==225&&payload.size()>=6&&!std::memcmp(payload.data(),"Exif\0\0",6)){
            auto tiff=payload.subspan(6);if(tiff.size()<8)return DecodeError::invalid_layout;
            const bool little=U8(tiff,0)==73&&U8(tiff,1)==73,big=U8(tiff,0)==77&&U8(tiff,1)==77;
            if(!little&&!big)return DecodeError::invalid_layout;
            const auto u16=[&](std::size_t p){return little?std::uint32_t(U16(tiff,p)):((std::uint32_t(U8(tiff,p))<<8)|U8(tiff,p+1));};
            const auto u32=[&](std::size_t p){return little?U32(tiff,p):be32(tiff,p);};
            if(u16(2)!=42)return DecodeError::invalid_layout;
            const auto directory=u32(4);if(directory<8||!Range(tiff,directory,2))return DecodeError::invalid_layout;
            const auto entries=u16(directory);if(entries>256)return DecodeError::resource_limit;
            if(!Range(tiff,std::uint64_t(directory)+2,std::uint64_t(entries)*12+4))return DecodeError::truncated;
            for(unsigned i=0;i<entries;++i){
                const auto p=std::size_t(directory)+2+i*12;
                if(u16(p)!=0x112)continue;
                // Exif 2.3 types Orientation as SHORT; some cameras and editors
                // write it as LONG (4) or SLONG (9). The single value is read at
                // its own width; any other type or count stays ambiguous.
                const auto type=u16(p+2);
                if(seen_orientation||(type!=3&&type!=4&&type!=9)||u32(p+4)!=1)return DecodeError::invalid_layout;
                info.orientation=type==3?u16(p+8):u32(p+8);seen_orientation=true;
                if(info.orientation<1||info.orientation>8)return DecodeError::invalid_layout;
            }
        }
        at+=length;
    }
    return DecodeError::truncated;
}
// Four-component JPEG is only meaningful with Adobe's transform flag: 0 is
// (inverted, Adobe convention) CMYK and 2 is YCCK. stb treats any other
// four-component stream as YCbCr and drops the fourth plane, so reject it.
// An embedded profile must describe the decoded components: Gray for one,
// RGB for three, and a CMYK profile with an A2B table skcms can evaluate for four.
DecodeError jpeg_colour(const JpegInfo& info,ColourProfile& colour,bool& cmyk){
    cmyk=false;
    if(info.components==4&&info.adobeTransform!=0&&info.adobeTransform!=2)return DecodeError::unsupported_content;
    if(!info.iccCount)return DecodeError::none;
    colour.bytes.clear();colour.bytes.reserve(static_cast<std::size_t>(info.iccBytes)+2);
    for(const auto chunk:info.icc)colour.bytes.insert(colour.bytes.end(),chunk.begin(),chunk.end());
    const auto parsed=parse_icc(colour,static_cast<std::size_t>(info.iccBytes));
    if(parsed!=DecodeError::none)return parsed;
    const auto& profile=colour.profile;
    const auto channels=skcms_GetInputChannelCount(&profile);
    if(info.components==1&&(profile.data_color_space!=skcms_Signature_Gray||channels!=1))return DecodeError::unsupported_content;
    if(info.components==3&&!rgb_profile(profile))return DecodeError::unsupported_content;
    if(info.components==4){
        if(profile.data_color_space!=skcms_Signature_CMYK||channels!=4||!profile.has_A2B)return DecodeError::unsupported_content;
        cmyk=true;
    }
    return DecodeError::none;
}
// Decodes a four-component Adobe JPEG to its stored (inverted) CMYK planes,
// upsampled exactly as stb's own colour conversion would. YCCK is first
// converted to RGB and inverted, which is what stb does before applying K.
bool jpeg_cmyk(Bytes bytes,std::uint32_t width,std::uint32_t height,std::vector<std::uint8_t>& cmyk){
    stbi__context context;
    stbi__start_mem(&context,reinterpret_cast<const stbi_uc*>(bytes.data()),static_cast<int>(bytes.size()));
    auto decoder=std::make_unique<stbi__jpeg>();
    decoder->s=&context;
    stbi__setup_jpeg(decoder.get());
    context.img_n=0;
    struct Cleanup {stbi__jpeg* jpeg;~Cleanup(){stbi__cleanup_jpeg(jpeg);}} cleanup{decoder.get()};
    auto& j=*decoder;
    if(!stbi__decode_jpeg_image(&j))return false;
    if(j.s->img_n!=4||j.s->img_x!=width||j.s->img_y!=height||(j.app14_color_transform!=0&&j.app14_color_transform!=2))return false;
    stbi__resample resample[4];
    for(int k=0;k<4;++k){
        j.img_comp[k].linebuf=static_cast<stbi_uc*>(stbi__malloc(j.s->img_x+3));
        if(!j.img_comp[k].linebuf)return false;
        auto& r=resample[k];
        r.hs=j.img_h_max/j.img_comp[k].h;r.vs=j.img_v_max/j.img_comp[k].v;
        r.ystep=r.vs>>1;r.w_lores=(int(j.s->img_x)+r.hs-1)/r.hs;r.ypos=0;
        r.line0=r.line1=j.img_comp[k].data;
        if(r.hs==1&&r.vs==1)r.resample=resample_row_1;
        else if(r.hs==1&&r.vs==2)r.resample=stbi__resample_row_v_2;
        else if(r.hs==2&&r.vs==1)r.resample=stbi__resample_row_h_2;
        else if(r.hs==2&&r.vs==2)r.resample=j.resample_row_hv_2_kernel;
        else r.resample=stbi__resample_row_generic;
    }
    cmyk.assign(std::size_t(width)*height*4,0);
    std::vector<stbi_uc> rgb(std::size_t(width)*4);
    for(std::uint32_t y=0;y<height;++y){
        stbi_uc* planes[4];
        for(int k=0;k<4;++k){
            auto& r=resample[k];
            const bool bottom=r.ystep>=(r.vs>>1);
            planes[k]=r.resample(j.img_comp[k].linebuf,bottom?r.line1:r.line0,bottom?r.line0:r.line1,r.w_lores,r.hs);
            if(++r.ystep>=r.vs){
                r.ystep=0;r.line0=r.line1;
                if(++r.ypos<j.img_comp[k].y)r.line1+=j.img_comp[k].w2;
            }
        }
        auto* row=cmyk.data()+std::size_t(y)*width*4;
        if(j.app14_color_transform==2){
            j.YCbCr_to_RGB_kernel(rgb.data(),planes[0],planes[1],planes[2],int(width),4);
            for(std::uint32_t x=0;x<width;++x){
                for(unsigned c=0;c<3;++c)row[x*4+c]=static_cast<std::uint8_t>(255-rgb[x*4+c]);
                row[x*4+3]=planes[3][x];
            }
        }else{
            for(std::uint32_t x=0;x<width;++x)for(unsigned c=0;c<4;++c)row[x*4+c]=planes[c][x];
        }
    }
    return true;
}
void orient(Image& image,unsigned orientation){
    if(orientation==1)return;
    const auto w=image.width,h=image.height;const bool transpose=orientation>=5;
    const auto out_w=transpose?h:w,out_h=transpose?w:h;
    std::vector<std::uint8_t> output(image.rgba8.size());
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){
        std::uint32_t dx=x,dy=y;
        switch(orientation){case 2:dx=w-1-x;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dy=h-1-y;break;
            case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;case 8:dx=y;dy=w-1-x;break;default:break;}
        std::memcpy(output.data()+(std::size_t(dy)*out_w+dx)*4,image.rgba8.data()+(std::size_t(y)*w+x)*4,4);
    }
    image.rgba8=std::move(output);image.width=out_w;image.height=out_h;
}
// ---- BMP: Arken-owned decoder. stb_image's BMP reader lacks RLE, misreads
// BITMAPV3 (56-byte) masks and discards V4/V5 colour spaces, so BMP never
// reaches stb. Supported: OS/2 BITMAPCOREHEADER (12) and BITMAPINFOHEADER/V2/
// V3/V4/V5 (40/52/56/108/124); 1/4/8-bit palettes, RLE8/RLE4, 16/32-bit
// BI_RGB/BI_BITFIELDS/BI_ALPHABITFIELDS masks, 24-bit, bottom-up and top-down.
constexpr std::uint32_t bmp_srgb=0x73524742u,bmp_windows=0x57696e20u,bmp_linked=0x4c494e4bu,bmp_embedded=0x4d424544u;
bool contiguous_mask(std::uint32_t mask){
    if(!mask)return true;
    const auto shifted=mask>>std::countr_zero(mask);
    return (shifted&(shifted+1))==0;
}
std::uint8_t mask_sample(std::uint32_t value,std::uint32_t mask){
    const auto shift=static_cast<unsigned>(std::countr_zero(mask));
    const std::uint64_t maximum=mask>>shift,sample=(value&mask)>>shift;
    return static_cast<std::uint8_t>((sample*510+maximum)/(2*maximum));
}
// V4/V5 colour space. sRGB and LCS_WINDOWS_COLOR_SPACE need no transform; an
// all-zero calibrated block is how writers mark untagged RGB. Calibrated
// endpoints (CIEXYZ, 2.30 fixed) and per-channel tone curves (16.16 fixed
// exponents) become an skcms matrix/power profile; an embedded profile is parsed
// by skcms. Linked (external file) profiles are never followed.
DecodeError bmp_colour(Bytes bytes,std::uint32_t dib,ColourProfile& colour,std::uint64_t& offset){
    if(dib<108)return DecodeError::none;
    const auto space=U32(bytes,70);
    offset=70;
    if(space==bmp_srgb||space==bmp_windows)return DecodeError::none;
    if(space==0){
        if(std::all_of(bytes.begin()+74,bytes.begin()+122,[](std::byte b){return b==std::byte{0};}))return DecodeError::none;
        double xyz[3][3];
        for(unsigned p=0;p<3;++p)for(unsigned c=0;c<3;++c)xyz[p][c]=I32(bytes,74+p*12+c*4)/1073741824.0;
        float chromaticity[8];
        double white[3]={0,0,0};
        for(unsigned p=0;p<3;++p){
            const auto sum=xyz[p][0]+xyz[p][1]+xyz[p][2];
            offset=74+p*12;
            if(!(sum>0)||!(xyz[p][1]>0))return DecodeError::invalid_layout;
            chromaticity[p*2]=static_cast<float>(xyz[p][0]/sum);chromaticity[p*2+1]=static_cast<float>(xyz[p][1]/sum);
            for(unsigned c=0;c<3;++c)white[c]+=xyz[p][c];
        }
        const auto whiteSum=white[0]+white[1]+white[2];
        chromaticity[6]=static_cast<float>(white[0]/whiteSum);chromaticity[7]=static_cast<float>(white[1]/whiteSum);
        skcms_Matrix3x3 toXYZD50;
        if(!skcms_PrimariesToXYZD50(chromaticity[0],chromaticity[1],chromaticity[2],chromaticity[3],
            chromaticity[4],chromaticity[5],chromaticity[6],chromaticity[7],&toXYZD50))return DecodeError::invalid_layout;
        std::array<skcms_TransferFunction,3> curves;
        for(unsigned c=0;c<3;++c){
            const auto gamma=U32(bytes,110+c*4);
            offset=110+c*4;
            // Endpoints without a tone response leave the encoding undefined.
            if(!gamma)return DecodeError::unsupported_content;
            curves[c]=power_curve(gamma/65536.0);
        }
        synthetic_profile(colour,toXYZD50,curves);
        return DecodeError::none;
    }
    if(space==bmp_embedded){
        if(dib<124)return DecodeError::invalid_layout;
        // bV5ProfileData is relative to the start of the BITMAPV5HEADER.
        const std::uint64_t at=std::uint64_t(14)+U32(bytes,126),size=U32(bytes,130);
        offset=126;
        if(at<std::uint64_t(14)+dib)return DecodeError::invalid_layout;
        if(size>maximum_profile_bytes)return DecodeError::resource_limit;
        if(!Range(bytes,at,size))return DecodeError::truncated;
        const auto parsed=load_icc(bytes.subspan(static_cast<std::size_t>(at),size),colour);
        if(parsed!=DecodeError::none)return parsed;
        return rgb_profile(colour.profile)?DecodeError::none:DecodeError::unsupported_content;
    }
    return DecodeError::unsupported_content; // PROFILE_LINKED and unknown spaces.
}
DecodeResult DecodeBmp(Bytes bytes,const DecodeOptions& options){
    if(bytes.size()<2||U8(bytes,0)!='B'||U8(bytes,1)!='M')return Failure(DecodeError::wrong_magic);
    if(bytes.size()<18)return Failure(DecodeError::truncated);
    const auto fileSize=U32(bytes,2),offset=U32(bytes,10),dib=U32(bytes,14);
    if(dib==16||dib==64)return Failure(DecodeError::unsupported_content,14); // OS/2 2.x
    if(dib!=12&&dib!=40&&dib!=52&&dib!=56&&dib!=108&&dib!=124)return Failure(DecodeError::invalid_layout,14);
    if(!Range(bytes,14,dib))return Failure(DecodeError::truncated,14);
    if(fileSize>bytes.size())return Failure(DecodeError::truncated,2);
    if(offset<std::uint64_t(14)+dib||offset>fileSize)return Failure(DecodeError::invalid_layout,10);
    const bool core=dib==12;
    std::int64_t width=0,height=0;std::uint32_t planes=0,bits=0,compression=0,used=0;
    if(core){width=U16(bytes,18);height=U16(bytes,20);planes=U16(bytes,22);bits=U16(bytes,24);}
    else{width=I32(bytes,18);height=I32(bytes,22);planes=U16(bytes,26);bits=U16(bytes,28);compression=U32(bytes,30);used=U32(bytes,46);}
    if(planes!=1)return Failure(DecodeError::invalid_layout,core?22:26);
    const bool topDown=height<0;
    if(topDown)height=-height;
    if(width<=0||height<=0||width>8192||height>8192
        ||!Dimensions(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),options))
        return Failure(DecodeError::invalid_dimensions,18);
    const auto w=static_cast<std::uint32_t>(width),h=static_cast<std::uint32_t>(height);
    const std::uint64_t bitsAt=core?24:28;
    if(bits!=1&&bits!=4&&bits!=8&&bits!=16&&bits!=24&&bits!=32)return Failure(DecodeError::unsupported_content,bitsAt);
    if(core&&(bits==16||bits==32))return Failure(DecodeError::unsupported_content,bitsAt);
    if(compression==4||compression==5||compression>6)return Failure(DecodeError::unsupported_compression,30);
    const bool rle=compression==1||compression==2,fields=compression==3||compression==6;
    if((compression==1&&bits!=8)||(compression==2&&bits!=4)||(fields&&bits!=16&&bits!=32))return Failure(DecodeError::invalid_layout,30);
    // RLE bitmaps are bottom-up by definition.
    if(rle&&topDown)return Failure(DecodeError::invalid_layout,22);
    std::uint32_t mr=0,mg=0,mb=0,ma=0;
    if(bits==16&&!compression){mr=0x7c00;mg=0x3e0;mb=0x1f;}
    else if(bits==32&&!compression){mr=0xff0000;mg=0xff00;mb=0xff;ma=0xff000000u;}
    else if(fields){
        if(dib>=52){mr=U32(bytes,54);mg=U32(bytes,58);mb=U32(bytes,62);ma=dib>=56?U32(bytes,66):0;}
        else{
            // BITMAPINFOHEADER: three (or four for BI_ALPHABITFIELDS) DWORD masks follow.
            const unsigned count=compression==6?4:3;
            if(std::uint64_t(54)+4*count>offset)return Failure(DecodeError::invalid_layout,54);
            mr=U32(bytes,54);mg=U32(bytes,58);mb=U32(bytes,62);ma=count==4?U32(bytes,66):0;
        }
        if(!mr||!mg||!mb||!contiguous_mask(mr)||!contiguous_mask(mg)||!contiguous_mask(mb)||!contiguous_mask(ma)
            ||(mr&mg)||(mr&mb)||(mg&mb)||((mr|mg|mb)&ma)||(bits==16&&(mr|mg|mb|ma)>0xffffu))
            return Failure(DecodeError::invalid_layout,54);
    }
    std::uint32_t colors=0;
    std::array<std::array<std::uint8_t,3>,256> palette{};
    if(bits<=8){
        const unsigned entry=core?3:4;
        const std::uint32_t maximum=1u<<bits;
        const auto available=(std::uint64_t(offset)-14-dib)/entry;
        if(core)colors=static_cast<std::uint32_t>(std::min<std::uint64_t>(available,maximum));
        else{
            if(used>maximum)return Failure(DecodeError::invalid_palette,46);
            colors=used?used:maximum;
        }
        if(!colors||available<colors)return Failure(DecodeError::invalid_palette,core?14:46);
        for(std::uint32_t i=0;i<colors;++i){
            const auto p=std::size_t(14)+dib+i*entry;
            palette[i]={U8(bytes,p+2),U8(bytes,p+1),U8(bytes,p)};
        }
    }
    ColourProfile colour;std::uint64_t colourAt=0;
    if(const auto error=bmp_colour(bytes,dib,colour,colourAt);error!=DecodeError::none)return Failure(error,colourAt);
    DecodeResult result;auto& image=result.image;
    image.width=w;image.height=h;image.rgba8.assign(std::size_t(w)*h*4,0);
    const auto put=[&](std::uint32_t x,std::uint32_t y,const std::array<std::uint8_t,3>& rgb){
        auto* pixel=image.rgba8.data()+(std::size_t(y)*w+x)*4;
        pixel[0]=rgb[0];pixel[1]=rgb[1];pixel[2]=rgb[2];pixel[3]=255;
    };
    if(rle){
        // Bottom-up RLE8/RLE4 with EOL, EOF, delta and word-aligned absolute
        // runs. Pixels never painted (skipped by delta/EOL/EOF) are transparent
        // black, matching browsers. Every escape is bounds-checked; every loop
        // iteration consumes input, so decoding is linear in the source size.
        const bool four=compression==2;
        std::uint64_t at=offset;std::uint32_t x=0,y=0;
        const auto source=bytes.first(fileSize);
        for(;;){
            if(!Range(source,at,2))return Failure(DecodeError::truncated,at);
            const auto count=U8(bytes,at),value=U8(bytes,at+1);
            const auto escape=at;at+=2;
            if(count){
                if(y>=h||x+count>w)return Failure(DecodeError::invalid_run,escape);
                for(unsigned i=0;i<count;++i){
                    const auto sample=four?((i&1)?value&15u:value>>4):value;
                    if(sample>=colors)return Failure(DecodeError::invalid_palette,escape);
                    put(x++,h-1-y,palette[sample]);
                }
            }else if(value==0){x=0;if(++y>h)return Failure(DecodeError::invalid_run,escape);}
            else if(value==1)break;
            else if(value==2){
                if(!Range(source,at,2))return Failure(DecodeError::truncated,at);
                x+=U8(bytes,at);y+=U8(bytes,at+1);at+=2;
                if(x>w||y>h)return Failure(DecodeError::invalid_run,escape);
            }else{
                const std::uint64_t stored=four?(value+1u)/2u:value,padded=(stored+1)&~std::uint64_t(1);
                if(!Range(source,at,padded))return Failure(DecodeError::truncated,at);
                if(y>=h||x+value>w)return Failure(DecodeError::invalid_run,escape);
                for(unsigned i=0;i<value;++i){
                    const auto byte=U8(bytes,at+(four?i/2:i));
                    const auto sample=four?((i&1)?byte&15u:byte>>4):byte;
                    if(sample>=colors)return Failure(DecodeError::invalid_palette,at+(four?i/2:i));
                    put(x++,h-1-y,palette[sample]);
                }
                at+=padded;
            }
        }
    }else{
        const std::uint64_t stride=((std::uint64_t(w)*bits+31)/32)*4;
        if(stride*h>std::uint64_t(fileSize)-offset)return Failure(DecodeError::truncated,offset);
        bool alphaSeen=false;
        for(std::uint32_t y=0;y<h;++y){
            const auto row=offset+std::uint64_t(topDown?y:h-1-y)*stride;
            for(std::uint32_t x=0;x<w;++x){
                if(bits<=8){
                    const auto at=row+(std::uint64_t(x)*bits)/8;
                    const auto shift=8-bits-(x*bits)%8;
                    const auto sample=(U8(bytes,at)>>shift)&((1u<<bits)-1);
                    if(sample>=colors)return Failure(DecodeError::invalid_palette,at);
                    put(x,y,palette[sample]);
                }else if(bits==24){
                    const auto at=row+std::uint64_t(x)*3;
                    put(x,y,{U8(bytes,at+2),U8(bytes,at+1),U8(bytes,at)});
                }else{
                    const auto value=bits==16?std::uint32_t(U16(bytes,row+std::uint64_t(x)*2)):U32(bytes,row+std::uint64_t(x)*4);
                    auto* pixel=image.rgba8.data()+(std::size_t(y)*w+x)*4;
                    pixel[0]=mask_sample(value,mr);pixel[1]=mask_sample(value,mg);pixel[2]=mask_sample(value,mb);
                    pixel[3]=ma?mask_sample(value,ma):255;
                    alphaSeen=alphaSeen||pixel[3]!=0;
                }
            }
        }
        // Alpha (stb_image's baseline, kept for compatibility): an alpha mask
        // declared by BI_BITFIELDS/BI_ALPHABITFIELDS in a V3+ header is always
        // honoured. 32-bit BI_RGB declares no alpha, but its fourth byte is
        // read as alpha unless every pixel's byte is zero; writers that leave
        // that byte zero mean opaque, so all-zero decodes as opaque.
        if(ma&&!compression&&!alphaSeen)for(std::size_t i=3;i<image.rgba8.size();i+=4)image.rgba8[i]=255;
    }
    if(!apply_colour(colour,image))return Failure(DecodeError::unsupported_content,colourAt);
    return result;
}
}
bool InflateTiffStrip(Bytes bytes,std::span<std::byte> output){
    if(bytes.size()<6||bytes.size()>std::size_t(std::numeric_limits<int>::max())
       ||output.size()>std::size_t(std::numeric_limits<int>::max()))return false;
    const auto decoded=stbi_zlib_decode_buffer(reinterpret_cast<char*>(output.data()),int(output.size()),
        reinterpret_cast<const char*>(bytes.data()),int(bytes.size()));
    if(decoded!=int(output.size()))return false;
    // stb's PNG inflater does not check the zlib Adler trailer. TIFF strip
    // admission does, for both exact scalar data and color-image samples.
    std::uint32_t a=1,b=0;
    for(auto value:output){a=(a+std::to_integer<std::uint8_t>(value))%65521;b=(b+a)%65521;}
    return ((b<<16)|a)==be32(bytes,bytes.size()-4);
}
ScalarDecodeResult DecodeScalarPng16(Bytes bytes,const DecodeOptions& options){
    const auto fail=[](DecodeError e){ScalarDecodeResult r;r.issue={e,0};return r;};
    const auto profile=png_profile(bytes,true,true);if(profile!=DecodeError::none)return fail(profile);
    if(bytes.size()<33||U8(bytes,24)!=16||U8(bytes,25)!=0)return fail(DecodeError::unsupported_content);
    const auto w=be32(bytes,16),h=be32(bytes,20);
    if(!Dimensions(w,h,options))return fail(DecodeError::invalid_dimensions);
    const auto shape=png_inflate_shape(bytes,w,h,options);if(shape!=DecodeError::none)return fail(shape);
    int width=0,height=0,components=0;
    std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(
        reinterpret_cast<const unsigned char*>(bytes.data()),int(bytes.size()),&width,&height,&components,1),stbi_image_free);
    if(!pixels)return fail(DecodeError::invalid_layout);
    if(width!=int(w)||height!=int(h)||components!=1)return fail(DecodeError::invalid_dimensions);
    ScalarDecodeResult result;result.width=w;result.height=h;
    result.samples.assign(pixels.get(),pixels.get()+std::size_t(w)*h);return result;
}
DecodeResult DecodeRaster(Format format,Bytes bytes,const DecodeOptions& options){
    if(options.frameIndex||options.directionIndex||options.mipIndex)return Failure(DecodeError::selection_out_of_range);
    if(bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))return Failure(DecodeError::input_too_large);
    if(format==Format::bmp)return DecodeBmp(bytes,options);
    JpegInfo jpeg;std::uint32_t frames=1;DecodeError profile=DecodeError::none;ColourProfile colour;bool cmyk=false;
    if(format==Format::png)profile=png_profile(bytes,options.allowPng16BitRenderingDerivative,false,&colour,&frames);
    else if(format==Format::gif)profile=gif_profile(bytes,frames);
    else if(format==Format::jpeg){
        profile=jpeg_profile(bytes,jpeg);
        if(profile==DecodeError::none)profile=jpeg_colour(jpeg,colour,cmyk);
        if(profile==DecodeError::none)bytes=bytes.first(jpeg.end); // trailing data never reaches stb
    }else profile=DecodeError::wrong_magic;
    if(profile!=DecodeError::none)return Failure(profile);
    int width=0,height=0,components=0;auto data=reinterpret_cast<const unsigned char*>(bytes.data());
    if(!stbi_info_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components))return Failure(DecodeError::invalid_layout);
    if(width<=0||height<=0||!Dimensions(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),options))return Failure(DecodeError::invalid_dimensions);
    if(format==Format::png){const auto shape=png_inflate_shape(bytes,width,height,options);if(shape!=DecodeError::none)return Failure(shape);}
    const auto expected_width=width,expected_height=height;
    if(format==Format::png&&U8(bytes,24)==16){
        if(std::uint64_t(width)*height*8>maximum_allocation)return Failure(DecodeError::resource_limit);
        std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components,4),stbi_image_free);
        if(!pixels)return Failure(DecodeError::invalid_layout);
        if(width!=expected_width||height!=expected_height)return Failure(DecodeError::invalid_dimensions);
        DecodeResult result;result.image.width=width;result.image.height=height;result.image.sourceBitsPerChannel=16;result.image.frameCount=frames;
        result.image.rgba8.resize(std::size_t(width)*height*4);
        if(!colour.active){
            for(std::size_t i=0;i<result.image.rgba8.size();++i)result.image.rgba8[i]=static_cast<std::uint8_t>((std::uint32_t(pixels.get()[i])+128)/257);
        }else if(!skcms_Transform(pixels.get(),std::endian::native==std::endian::little?skcms_PixelFormat_RGBA_16161616LE:skcms_PixelFormat_RGBA_16161616BE,
            skcms_AlphaFormat_Unpremul,&colour.profile,result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,
            skcms_sRGB_profile(),std::size_t(width)*height))return Failure(DecodeError::unsupported_content);
        return result;
    }
    DecodeResult result;
    if(cmyk){
        std::vector<std::uint8_t> planes;
        if(!jpeg_cmyk(bytes,static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),planes))return Failure(DecodeError::invalid_layout);
        result.image.width=static_cast<std::uint32_t>(width);result.image.height=static_cast<std::uint32_t>(height);
        result.image.rgba8.resize(planes.size());
        if(!skcms_Transform(planes.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,&colour.profile,
            result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),planes.size()/4))
            return Failure(DecodeError::unsupported_content);
        for(std::size_t i=3;i<result.image.rgba8.size();i+=4)result.image.rgba8[i]=255;
        orient(result.image,jpeg.orientation);
        return result;
    }
    if(options.resourceProfile==RasterProfile::largeRaster){
        result=DecodeLargeRasterPixels(bytes,width,height);
        if(!result)return result;
    }else{
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components,4),stbi_image_free);
        if(!pixels)return Failure(DecodeError::invalid_layout);
        if(width!=expected_width||height!=expected_height)return Failure(DecodeError::invalid_dimensions);
        result.image.width=static_cast<std::uint32_t>(width);result.image.height=static_cast<std::uint32_t>(height);
        result.image.rgba8.assign(pixels.get(),pixels.get()+std::size_t(width)*height*4);
    }
    // Exif orientation first, then the pixel-local colour transform to sRGB.
    orient(result.image,jpeg.orientation);
    if(!apply_colour(colour,result.image))return Failure(DecodeError::unsupported_content);
    result.image.frameCount=frames;
    return result;
}
}
