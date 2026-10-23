#include "reader.hpp"
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
#include <stb_image.h>
#include <skcms_public.h>

namespace arken::image::v1::detail {
namespace {
std::uint32_t be32(Bytes b,std::size_t p){return (std::uint32_t(U8(b,p))<<24)|(std::uint32_t(U8(b,p+1))<<16)|(std::uint32_t(U8(b,p+2))<<8)|U8(b,p+3);}
std::uint32_t crc32(Bytes bytes){
    std::uint32_t value=0xffffffff;
    for(auto byte:bytes){value^=std::to_integer<std::uint8_t>(byte);for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}
    return ~value;
}
struct PngColourProfile {
    std::vector<std::byte> bytes;
    skcms_ICCProfile profile{};
};
DecodeError read_icc(Bytes chunk,PngColourProfile& colour){
    constexpr std::size_t limit=4*1024*1024;
    if(!colour.bytes.empty())return DecodeError::invalid_layout;
    std::size_t name=0;
    while(name<chunk.size()&&name<80&&U8(chunk,name)!=0){
        const auto c=U8(chunk,name);
        if(c<32||(c>126&&c<161)||(c==32&&(name==0||U8(chunk,name-1)==32)))return DecodeError::invalid_layout;
        ++name;
    }
    if(name==0||name>79||!Range(chunk,name,2)||U8(chunk,name)!=0||U8(chunk,name-1)==32||U8(chunk,name+1)!=0)return DecodeError::invalid_layout;
    const auto compressed=chunk.subspan(name+2);
    if(compressed.size()<6)return DecodeError::truncated;
    if(compressed.size()>limit)return DecodeError::resource_limit;
    colour.bytes.resize(limit+2);
    stbi__zbuf inflater{};
    inflater.zbuffer=reinterpret_cast<stbi_uc*>(const_cast<std::byte*>(compressed.data()));
    inflater.zbuffer_end=inflater.zbuffer+compressed.size();
    if(!stbi__do_zlib(&inflater,reinterpret_cast<char*>(colour.bytes.data()),int(limit),0,1))return DecodeError::invalid_layout;
    const auto size=std::size_t(inflater.zout-inflater.zout_start);
    // Require precisely one zlib stream and its Adler trailer, including when
    // the inflater read ahead into that trailer. No trailing/concatenated data.
    if(inflater.hit_zeof_once||inflater.num_bits<0||inflater.zbuffer-inflater.num_bits/8!=inflater.zbuffer_end-4)return DecodeError::invalid_layout;
    std::uint32_t a=1,b=0;
    for(std::size_t i=0;i<size;++i){a=(a+U8(colour.bytes,i))%65521;b=(b+a)%65521;}
    if(((b<<16)|a)!=be32(compressed,compressed.size()-4))return DecodeError::invalid_layout;
    if(size<132||be32(colour.bytes,0)!=size)return DecodeError::invalid_layout;
    colour.bytes.resize(size+2); // skcms documents two readable padding bytes.
    colour.bytes[size]=colour.bytes[size+1]=std::byte{0};
    if(!skcms_Parse(colour.bytes.data(),size,&colour.profile))return DecodeError::invalid_layout;
    if(colour.profile.data_color_space!=skcms_Signature_RGB||skcms_GetInputChannelCount(&colour.profile)!=3)return DecodeError::unsupported_content;
    return DecodeError::none;
}
bool apply_colour(const PngColourProfile& colour,Image& image){
    return colour.bytes.empty()||skcms_Transform(image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,&colour.profile,
        image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),std::size_t(image.width)*image.height);
}
DecodeError png_profile(Bytes bytes,bool allow16,bool scalarData=false,PngColourProfile* colour=nullptr){
    constexpr unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(bytes.size()<8||std::memcmp(bytes.data(),signature,8))return DecodeError::wrong_magic;
    bool header=false,data=false,srgb=false,nonSrgb=false,palette=false;
    for(std::size_t at=8;at<bytes.size();){
        if(!Range(bytes,at,12))return DecodeError::truncated;
        const auto count=be32(bytes,at),kind=be32(bytes,at+4);
        if(!Range(bytes,at+12,count))return DecodeError::truncated;
        if(crc32(bytes.subspan(at+4,std::size_t(count)+4))!=be32(bytes,at+8+count))return DecodeError::invalid_layout;
        if(!header){if(kind!=0x49484452u||count!=13)return DecodeError::invalid_layout;header=true;
            if(U8(bytes,at+16)==16&&!allow16)return DecodeError::unsupported_content;
        }else if(kind==0x49484452u)return DecodeError::invalid_layout;
        if(kind==0x6163544cu||(!scalarData&&kind==0x63494350u))return DecodeError::unsupported_content;
        if(kind==0x504c5445u)palette=true;
        if(kind==0x69434350u&&!scalarData){
            if(!colour||data||palette||srgb)return DecodeError::invalid_layout;
            if(U8(bytes,25)!=2&&U8(bytes,25)!=3&&U8(bytes,25)!=6)return DecodeError::unsupported_content;
            const auto error=read_icc(bytes.subspan(at+8,count),*colour);
            if(error!=DecodeError::none)return error;
        }
        // Scalar import explicitly consumes stored data values, not displayed
        // colour. ICC/gamma/chromaticity metadata must not remap heights.
        if(kind==0x67414d41u){if(count!=4)return DecodeError::invalid_layout;nonSrgb|=be32(bytes,at+8)!=45455;}
        if(kind==0x73524742u){if(count!=1||U8(bytes,at+8)>3||srgb||(colour&&!colour->bytes.empty()))return DecodeError::invalid_layout;srgb=true;}
        if(kind==0x6348524du){
            constexpr std::uint32_t srgb[]={31270,32900,64000,33000,30000,60000,15000,6000};
            if(count!=32)return DecodeError::invalid_layout;
            for(unsigned i=0;i<8;++i)nonSrgb|=be32(bytes,at+8+i*4)!=srgb[i];
        }
        if(kind==0x49444154u)data=true;
        if(kind==0x49454e44u){
            if(count!=0||!data||at+12!=bytes.size())return DecodeError::invalid_layout;
            if(!scalarData&&nonSrgb&&(!colour||colour->bytes.empty()))return DecodeError::unsupported_content;
            return DecodeError::none;
        }
        at+=std::size_t(count)+12;
    }
    return DecodeError::truncated;
}
DecodeError gif_profile(Bytes bytes){
    if(bytes.size()<13)return DecodeError::truncated;
    const bool version87=!std::memcmp(bytes.data(),"GIF87a",6);
    const bool version89=!std::memcmp(bytes.data(),"GIF89a",6);
    if(!version87&&!version89)return DecodeError::wrong_magic;
    const auto width=U16(bytes,6),height=U16(bytes,8);
    if(!width||!height)return DecodeError::invalid_dimensions;
    if(bytes.back()!=std::byte{0x3b})return DecodeError::truncated;
    return DecodeError::none;
}
DecodeError jpeg_profile(Bytes bytes,unsigned& orientation){
    if(bytes.size()<4||U8(bytes,0)!=255||U8(bytes,1)!=216)return DecodeError::wrong_magic;
    if(U8(bytes,bytes.size()-2)!=255||U8(bytes,bytes.size()-1)!=217)return DecodeError::truncated;
    bool seen_orientation=false;
    for(std::size_t at=2;at+1<bytes.size();){
        if(U8(bytes,at++)!=255)return DecodeError::invalid_layout;
        while(at<bytes.size()&&U8(bytes,at)==255)++at;
        if(at==bytes.size())return DecodeError::truncated;
        const auto marker=U8(bytes,at++);if(marker==218)return DecodeError::none;
        if(marker==217)return DecodeError::invalid_layout;
        if(!Range(bytes,at,2))return DecodeError::truncated;
        const auto length=(std::uint32_t(U8(bytes,at))<<8)|U8(bytes,at+1);
        if(length<2||!Range(bytes,at,length))return DecodeError::truncated;
        auto payload=bytes.subspan(at+2,length-2);
        if(marker==226&&payload.size()>=12&&!std::memcmp(payload.data(),"ICC_PROFILE\0",12))return DecodeError::unsupported_content;
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
            for(unsigned i=0;i<entries;++i){const auto p=std::size_t(directory)+2+i*12;
                if(u16(p)!=0x112)continue;
                if(seen_orientation||u16(p+2)!=3||u32(p+4)!=1)return DecodeError::invalid_layout;
                orientation=u16(p+8);seen_orientation=true;
                if(orientation<1||orientation>8)return DecodeError::invalid_layout;
            }
        }
        at+=length;
    }
    return DecodeError::truncated;
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
}
bool InflateScalarStrip(Bytes bytes,std::span<std::byte> output){
    if(bytes.size()<6||bytes.size()>std::size_t(std::numeric_limits<int>::max())
       ||output.size()>std::size_t(std::numeric_limits<int>::max()))return false;
    const auto decoded=stbi_zlib_decode_buffer(reinterpret_cast<char*>(output.data()),int(output.size()),
        reinterpret_cast<const char*>(bytes.data()),int(bytes.size()));
    if(decoded!=int(output.size()))return false;
    // stb's PNG inflater does not check the zlib Adler trailer. Scalar source
    // admission does: damaged terrain must not become a plausible height field.
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
    unsigned orientation=1;DecodeError profile=DecodeError::none;PngColourProfile colour;
    if(format==Format::png)profile=png_profile(bytes,options.allowPng16BitRenderingDerivative,false,&colour);
    else if(format==Format::gif)profile=gif_profile(bytes);
    else if(format==Format::jpeg)profile=jpeg_profile(bytes,orientation);
    else if(format!=Format::bmp || bytes.size()<54||U8(bytes,0)!='B'||U8(bytes,1)!='M')profile=DecodeError::wrong_magic;
    if(profile!=DecodeError::none)return Failure(profile);
    int width=0,height=0,components=0;auto data=reinterpret_cast<const unsigned char*>(bytes.data());
    if(!stbi_info_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components))return Failure(DecodeError::invalid_layout);
    if(width<=0||height<=0||!Dimensions(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),options))return Failure(DecodeError::invalid_dimensions);
    if(format==Format::bmp){
        const auto size=U32(bytes,2),offset=U32(bytes,10),dib=U32(bytes,14),compression=U32(bytes,30);
        const auto bits=U16(bytes,28);
        if(dib<40||!Range(bytes,14,dib)||size>bytes.size()||offset<std::uint64_t(14)+dib||offset>size)return Failure(DecodeError::invalid_layout);
        if(dib>=108&&U32(bytes,70)!=0x73524742u){
            // Writers also emit a zero-filled V4 color block for untagged RGB.
            // Nonzero calibration endpoints/gamma or an ICC profile need an
            // explicit color-management path, not a silent sRGB assumption.
            if(U32(bytes,70)!=0||std::any_of(bytes.begin()+74,bytes.begin()+122,[](std::byte b){return b!=std::byte{0};}))return Failure(DecodeError::unsupported_content);
        }
        if((compression!=0&&compression!=3)||(bits!=1&&bits!=4&&bits!=8&&bits!=16&&bits!=24&&bits!=32))return Failure(DecodeError::unsupported_content);
        const auto row=((std::uint64_t(width)*bits+31)/32)*4;
        if(row*static_cast<unsigned>(height)>size-offset)return Failure(DecodeError::truncated);
    }
    const auto expected_width=width,expected_height=height;
    if(format==Format::png&&U8(bytes,24)==16){
        if(std::uint64_t(width)*height*8>maximum_allocation)return Failure(DecodeError::resource_limit);
        std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components,4),stbi_image_free);
        if(!pixels)return Failure(DecodeError::invalid_layout);
        if(width!=expected_width||height!=expected_height)return Failure(DecodeError::invalid_dimensions);
        DecodeResult result;result.image.width=width;result.image.height=height;result.image.sourceBitsPerChannel=16;
        result.image.rgba8.resize(std::size_t(width)*height*4);
        if(colour.bytes.empty()){
            for(std::size_t i=0;i<result.image.rgba8.size();++i)result.image.rgba8[i]=static_cast<std::uint8_t>((std::uint32_t(pixels.get()[i])+128)/257);
        }else if(!skcms_Transform(pixels.get(),std::endian::native==std::endian::little?skcms_PixelFormat_RGBA_16161616LE:skcms_PixelFormat_RGBA_16161616BE,
            skcms_AlphaFormat_Unpremul,&colour.profile,result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,
            skcms_sRGB_profile(),std::size_t(width)*height))return Failure(DecodeError::unsupported_content);
        return result;
    }
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(data,static_cast<int>(bytes.size()),&width,&height,&components,4),stbi_image_free);
    if(!pixels)return Failure(DecodeError::invalid_layout);
    if(width!=expected_width||height!=expected_height)return Failure(DecodeError::invalid_dimensions);
    DecodeResult result;result.image.width=static_cast<std::uint32_t>(width);result.image.height=static_cast<std::uint32_t>(height);
    result.image.rgba8.assign(pixels.get(),pixels.get()+std::size_t(width)*height*4);
    if(!apply_colour(colour,result.image))return Failure(DecodeError::unsupported_content);
    orient(result.image,orientation);return result;
}
}
