#include "webp.hpp"
#include "reader.hpp"
#include <webp/decode.h>
#include <webp/encode.h>
#include <cstring>
#include <new>

namespace arken::image::v1::detail {
namespace {
bool Is(Bytes b,std::size_t offset,const char* id) {return std::memcmp(b.data()+offset,id,4)==0;}
std::uint32_t U24(Bytes b,std::size_t p) {return U16(b,p)|(std::uint32_t(U8(b,p+2))<<16);}
struct Container {
    DecodeError error=DecodeError::none;
    std::uint32_t width=0,height=0;
    bool extended=false,alpha=false;
};
Container Inspect(Bytes bytes) {
    const auto fail=[](DecodeError e){Container c;c.error=e;return c;};
    if(bytes.size()<12)return fail(DecodeError::truncated);
    if(!Is(bytes,0,"RIFF")||!Is(bytes,8,"WEBP"))return fail(DecodeError::wrong_magic);
    const auto extent=std::uint64_t(U32(bytes,4))+8;
    if(extent>bytes.size())return fail(DecodeError::truncated);
    if(extent!=bytes.size()||(extent&1))return fail(DecodeError::invalid_layout);
    Container result;bool payload=false,alpha=false,xmp=false;unsigned chunks=0;
    std::uint8_t flags=0;
    for(std::size_t p=12;p<bytes.size();) {
        if(++chunks>4096)return fail(DecodeError::resource_limit);
        if(!Range(bytes,p,8))return fail(DecodeError::truncated);
        const auto size=U32(bytes,p+4);const auto body=p+8;
        if(!Range(bytes,body,std::uint64_t(size)+(size&1)))return fail(DecodeError::truncated);
        if((size&1)&&U8(bytes,body+size))return fail(DecodeError::invalid_layout);
        if(Is(bytes,p,"VP8X")) {
            if(p!=12||size!=10)return fail(DecodeError::invalid_layout);
            flags=U8(bytes,body);
            if((flags&0xc1)||U24(bytes,body+1))return fail(DecodeError::invalid_layout);
            if(flags&0x2a)return fail(DecodeError::unsupported_content); // Animation/ICC/Exif.
            result.extended=true;result.alpha=(flags&0x10)!=0;
            result.width=U24(bytes,body+4)+1;result.height=U24(bytes,body+7)+1;
        } else if(Is(bytes,p,"ANIM")||Is(bytes,p,"ANMF")||Is(bytes,p,"ICCP")||Is(bytes,p,"EXIF")) {
            return fail(DecodeError::unsupported_content);
        } else if(Is(bytes,p,"ALPH")) {
            if(!result.extended||!result.alpha||alpha||payload||size==0)return fail(DecodeError::invalid_layout);
            alpha=true;
        } else if(Is(bytes,p,"VP8 ")||Is(bytes,p,"VP8L")) {
            if(payload||!size||(Is(bytes,p,"VP8L")&&alpha))return fail(DecodeError::invalid_layout);
            if(Is(bytes,p,"VP8 ")&&result.extended&&result.alpha!=alpha)return fail(DecodeError::invalid_layout);
            payload=true;
        } else if(Is(bytes,p,"XMP ")) {
            if(!result.extended||!(flags&4)||!payload||xmp)return fail(DecodeError::invalid_layout);
            xmp=true;
        } else if(alpha&&!payload) {
            return fail(DecodeError::invalid_layout); // ALPH must immediately precede VP8.
        }
        p=body+size+(size&1);
    }
    if(!payload||xmp!=bool(flags&4))return fail(DecodeError::invalid_layout);
    return result;
}
DecodeError DecodeStatus(VP8StatusCode status) {
    if(status==VP8_STATUS_OUT_OF_MEMORY)return DecodeError::allocation_failed;
    if(status==VP8_STATUS_NOT_ENOUGH_DATA)return DecodeError::truncated;
    if(status==VP8_STATUS_UNSUPPORTED_FEATURE)return DecodeError::unsupported_content;
    return DecodeError::invalid_layout;
}
struct Picture {
    WebPPicture value{};
    ~Picture(){WebPPictureFree(&value);}
};
struct Output {
    std::vector<std::byte> bytes;
    std::uint64_t limit;
    EncodeError error=EncodeError::none;
};
int Write(const std::uint8_t* data,std::size_t size,const WebPPicture* picture) noexcept {
    auto& output=*static_cast<Output*>(picture->custom_ptr);
    if(output.error!=EncodeError::none)return 0;
    if(size>output.limit-output.bytes.size()){output.error=EncodeError::output_limit;return 0;}
    if(!size)return 1;
    try {
        const auto* first=reinterpret_cast<const std::byte*>(data);
        output.bytes.insert(output.bytes.end(),first,first+size);return 1;
    } catch(const std::bad_alloc&) {output.error=EncodeError::allocation_failed;}
    catch(...) {output.error=EncodeError::encoding_failed;}
    return 0;
}
}
DecodeResult DecodeWebp(Bytes bytes,const DecodeOptions& options) {
    if(options.frameIndex||options.directionIndex||options.mipIndex)return Failure(DecodeError::selection_out_of_range);
    const auto container=Inspect(bytes);
    if(container.error!=DecodeError::none)return Failure(container.error);
    if(container.extended&&!Dimensions(container.width,container.height,options))return Failure(DecodeError::invalid_dimensions);
    WebPDecoderConfig config{};
    if(!WebPInitDecoderConfig(&config))return Failure(DecodeError::unsupported_content);
    const auto* data=reinterpret_cast<const std::uint8_t*>(bytes.data());
    const auto featureStatus=WebPGetFeatures(data,bytes.size(),&config.input);
    if(featureStatus!=VP8_STATUS_OK)return Failure(DecodeStatus(featureStatus));
    if(config.input.has_animation)return Failure(DecodeError::unsupported_content);
    const auto width=std::uint32_t(config.input.width),height=std::uint32_t(config.input.height);
    if(!Dimensions(width,height,options))return Failure(DecodeError::invalid_dimensions);
    if(container.extended&&(container.width!=width||container.height!=height||container.alpha!=bool(config.input.has_alpha)))return Failure(DecodeError::invalid_layout);
    DecodeResult result;result.image.width=width;result.image.height=height;
    result.image.rgba8.resize(std::size_t(width)*height*4);
    config.output.colorspace=MODE_RGBA;config.output.is_external_memory=1;
    config.output.u.RGBA.rgba=result.image.rgba8.data();
    config.output.u.RGBA.stride=int(width*4);config.output.u.RGBA.size=result.image.rgba8.size();
    config.options.use_threads=0;
    const auto status=WebPDecode(data,bytes.size(),&config);
    WebPFreeDecBuffer(&config.output);
    if(status!=VP8_STATUS_OK)return Failure(DecodeStatus(status));
    return result;
}
EncodeResult EncodeWebp(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba,std::uint64_t limit) {
    WebPConfig config{};Picture picture;
    if(!WebPConfigInit(&config)||!WebPPictureInit(&picture.value))return {EncodeError::encoding_failed,false,{}};
    config.lossless=1;config.exact=1;config.method=4;config.quality=75;config.near_lossless=100;config.thread_level=0;
    if(!WebPValidateConfig(&config))return {EncodeError::invalid_options,false,{}};
    picture.value.use_argb=1;picture.value.width=int(width);picture.value.height=int(height);
    Output output{{},limit};picture.value.writer=Write;picture.value.custom_ptr=&output;
    if(!WebPPictureImportRGBA(&picture.value,rgba.data(),int(width*4)))return {EncodeError::allocation_failed,false,{}};
    const auto accepted=WebPEncode(&config,&picture.value);
    if(output.error!=EncodeError::none)return {output.error,false,{}};
    if(!accepted||output.bytes.empty())return {EncodeError::encoding_failed,false,{}};
    return {EncodeError::none,false,std::move(output.bytes)};
}
}
