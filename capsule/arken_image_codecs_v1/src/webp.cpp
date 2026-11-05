#include "webp.hpp"
#include "reader.hpp"
#include <webp/decode.h>
#include <webp/encode.h>
#include <skcms_public.h>
#include <cstring>
#include <new>

// WebP decode policy (RIFF container parsed here; libwebp only sees one
// ALPH+VP8 or VP8L image payload at a time):
// - Still lossy (VP8, optionally preceded by ALPH) and lossless (VP8L) images.
// - ICCP: the profile (at most 4 MiB, RGB, 3 input channels) is parsed with
//   skcms and the decoded straight-alpha RGBA8 is converted to sRGB, exactly
//   as the PNG iCCP path does. Alpha is never altered. Non-RGB profiles reject.
//   ICCP must precede the image data/ANIM and appear at most once.
// - EXIF and XMP are ignored (at most one of each): metadata is not carried
//   into outputs and Exif orientation is NOT applied (WebP has no normative
//   orientation rule and browsers ignore it as well).
// - VP8X metadata flags are hints, as in libwebp's demuxer and browsers: an
//   ICC/EXIF/XMP flag without its chunk is ignored, and an ICCP chunk without
//   the flag is still applied. An animation flag on a file holding a plain
//   VP8/VP8L image and no ANIM/ANMF decodes as that still image.
// - Animation (ANIM/ANMF, requiring the animation flag): the first frame is
//   composited onto a transparent black (0,0,0,0) canvas of the VP8X size,
//   honouring its offset and blending method, as WebPAnimDecoder and browsers
//   do (the ANIM background colour is only a hint and is not used). The result
//   is returned as the still image; Image::frameCount reports the number of
//   ANMF frames. Every frame is checked structurally and its bitstream header
//   must match its rectangle, but only the first is decoded. An animation
//   without frames has no presentable image and is unsupported content.
//   Contradictory structure rejects: ANMF without ANIM, duplicate ANIM, or a
//   top-level image alongside ANIM/ANMF. frameIndex must be zero.
// - Unknown chunks are skipped wherever the RIFF structure stays valid, except
//   between ALPH and its VP8 chunk, where libwebp requires adjacency.
// - Limits: the canvas obeys maximumDimension/maximumPixels; frames must lie
//   inside the canvas; at most 4096 frames and 16384 chunks in total (frame
//   subchunks included). Failures never return partial pixels.
namespace arken::image::v1::detail {
namespace {
constexpr std::uint8_t animationFlag=0x02,alphaFlag=0x10; // ICC/EXIF/XMP flags are hints only.
constexpr unsigned maximumChunks=16384,maximumFrames=4096;
constexpr std::size_t maximumIccBytes=4*1024*1024;
bool Is(Bytes b,std::size_t offset,const char* id) {return std::memcmp(b.data()+offset,id,4)==0;}
std::uint32_t U24(Bytes b,std::size_t p) {return U16(b,p)|(std::uint32_t(U8(b,p+2))<<16);}
// Visits each chunk header in [begin,end) after validating its extent and padding.
template<class Visit> DecodeError Walk(Bytes b,std::size_t begin,std::size_t end,unsigned& chunks,Visit&& visit) {
    for(auto p=begin;p<end;) {
        if(++chunks>maximumChunks)return DecodeError::resource_limit;
        if(end-p<8)return DecodeError::truncated;
        const auto size=U32(b,p+4);const auto body=p+8;
        const auto padded=std::uint64_t(size)+(size&1);
        if(padded>end-body)return DecodeError::truncated;
        if((size&1)&&U8(b,body+size))return DecodeError::invalid_layout;
        if(const auto error=visit(p,size,body);error!=DecodeError::none)return error;
        p=body+std::size_t(padded);
    }
    return DecodeError::none;
}
// One image payload: optional ALPH immediately followed by VP8, or VP8L.
struct Payload {
    bool alpha=false,present=false;
    std::size_t begin=0,end=0;
    bool Image(Bytes b,std::size_t p) const {return Is(b,p,"ALPH")||Is(b,p,"VP8 ")||Is(b,p,"VP8L");}
    DecodeError Visit(Bytes b,std::size_t p,std::uint32_t size) {
        if(alpha&&!present&&!Is(b,p,"VP8 "))return DecodeError::invalid_layout;
        if(!Image(b,p))return DecodeError::none;
        if(Is(b,p,"ALPH")) {
            if(alpha||present||!size)return DecodeError::invalid_layout;
            alpha=true;begin=p;return DecodeError::none;
        }
        if(present||!size||(Is(b,p,"VP8L")&&alpha))return DecodeError::invalid_layout;
        if(!alpha)begin=p;
        end=p+8+size;present=true;
        return DecodeError::none;
    }
};
struct Frame {
    std::size_t begin=0,end=0;
    std::uint32_t x=0,y=0,width=0,height=0;
    bool blend=true;
};
struct Container {
    DecodeError error=DecodeError::none;
    std::uint32_t width=0,height=0,frames=0;
    bool extended=false,alpha=false,animated=false,icc=false;
    Frame image; // The still payload, or the first animation frame.
    Bytes profile;
};
DecodeError DecodeStatus(VP8StatusCode status) {
    if(status==VP8_STATUS_OUT_OF_MEMORY)return DecodeError::allocation_failed;
    if(status==VP8_STATUS_NOT_ENOUGH_DATA)return DecodeError::truncated;
    if(status==VP8_STATUS_UNSUPPORTED_FEATURE)return DecodeError::unsupported_content;
    return DecodeError::invalid_layout;
}
const std::uint8_t* Data(Bytes b,std::size_t p) {return reinterpret_cast<const std::uint8_t*>(b.data())+p;}
DecodeError ParseFrame(Bytes b,std::size_t body,std::uint32_t size,const Container& canvas,unsigned& chunks,Frame& frame) {
    if(size<16)return DecodeError::invalid_layout;
    frame.x=U24(b,body)*2;frame.y=U24(b,body+3)*2;
    frame.width=U24(b,body+6)+1;frame.height=U24(b,body+9)+1;
    // Byte 15: reserved bits ignored, bit 1 = do not blend, bit 0 = dispose.
    frame.blend=(U8(b,body+15)&2)==0;
    if(std::uint64_t(frame.x)+frame.width>canvas.width||std::uint64_t(frame.y)+frame.height>canvas.height)
        return DecodeError::invalid_layout;
    Payload payload;
    const auto error=Walk(b,body+16,body+size,chunks,[&](std::size_t p,std::uint32_t chunk,std::size_t) {
        return payload.Visit(b,p,chunk);
    });
    if(error!=DecodeError::none)return error;
    if(!payload.present)return DecodeError::invalid_layout;
    frame.begin=payload.begin;frame.end=payload.end;
    WebPBitstreamFeatures features{};
    const auto status=WebPGetFeatures(Data(b,frame.begin),frame.end-frame.begin,&features);
    if(status!=VP8_STATUS_OK)return DecodeStatus(status);
    if(std::uint32_t(features.width)!=frame.width||std::uint32_t(features.height)!=frame.height||features.has_animation)
        return DecodeError::invalid_layout;
    return DecodeError::none;
}
Container Inspect(Bytes bytes) {
    const auto fail=[](DecodeError e){Container c;c.error=e;return c;};
    if(bytes.size()<12)return fail(DecodeError::truncated);
    if(!Is(bytes,0,"RIFF")||!Is(bytes,8,"WEBP"))return fail(DecodeError::wrong_magic);
    const auto extent=std::uint64_t(U32(bytes,4))+8;
    if(extent>bytes.size())return fail(DecodeError::truncated);
    if(extent!=bytes.size()||(extent&1))return fail(DecodeError::invalid_layout);
    Container result;Payload still;
    bool icc=false,exif=false,xmp=false;unsigned chunks=0;std::uint8_t flags=0;
    const auto error=Walk(bytes,12,bytes.size(),chunks,[&](std::size_t p,std::uint32_t size,std::size_t body) {
        const bool seen=still.present||result.frames;
        if(Is(bytes,p,"VP8X")) {
            if(p!=12||size!=10)return DecodeError::invalid_layout;
            flags=U8(bytes,body);
            if((flags&0xc1)||U24(bytes,body+1))return DecodeError::invalid_layout;
            result.extended=true;result.alpha=(flags&alphaFlag)!=0;
            result.width=U24(bytes,body+4)+1;result.height=U24(bytes,body+7)+1;
            return DecodeError::none;
        }
        if(still.alpha&&!still.present&&!Is(bytes,p,"VP8 "))return DecodeError::invalid_layout;
        if(Is(bytes,p,"ICCP")) {
            if(icc||seen||result.animated)return DecodeError::invalid_layout; // Flag not required.
            if(size>maximumIccBytes)return DecodeError::resource_limit;
            icc=true;result.icc=true;result.profile=bytes.subspan(body,size);
        } else if(Is(bytes,p,"ANIM")) {
            if(!(flags&animationFlag)||result.animated||still.present||size!=6)return DecodeError::invalid_layout;
            result.animated=true; // Background colour (a hint) and loop count are not used.
        } else if(Is(bytes,p,"ANMF")) {
            if(!result.animated)return DecodeError::invalid_layout;
            if(result.frames==maximumFrames)return DecodeError::resource_limit;
            Frame frame;
            if(const auto e=ParseFrame(bytes,body,size,result,chunks,frame);e!=DecodeError::none)return e;
            if(!result.frames++)result.image=frame;
        } else if(Is(bytes,p,"EXIF")) {
            if(exif)return DecodeError::invalid_layout; // Flag not required.
            exif=true; // Ignored: never carried into outputs, orientation not applied.
        } else if(Is(bytes,p,"XMP ")) {
            if(xmp)return DecodeError::invalid_layout; // Flag not required.
            xmp=true; // Ignored: never carried into outputs.
        } else if(still.Image(bytes,p)) {
            if(result.animated)return DecodeError::invalid_layout;
            if(Is(bytes,p,"ALPH")&&(!result.extended||!result.alpha))return DecodeError::invalid_layout;
            if(const auto e=still.Visit(bytes,p,size);e!=DecodeError::none)return e;
            if(Is(bytes,p,"VP8 ")&&result.extended&&result.alpha!=still.alpha)return DecodeError::invalid_layout;
        } // Unknown chunks are skipped.
        return DecodeError::none;
    });
    if(error!=DecodeError::none)return fail(error);
    // An animation (or an animation flag with no image at all) without frames
    // has nothing presentable. A plain image under an animation flag is a still.
    if(result.animated||(!still.present&&(flags&animationFlag))) {
        if(!result.frames)return fail(DecodeError::unsupported_content);
    } else {
        if(!still.present)return fail(DecodeError::invalid_layout);
        result.image.begin=still.begin;result.image.end=still.end;
    }
    return result;
}
struct Profile {
    std::vector<std::byte> bytes;
    skcms_ICCProfile profile{};
};
DecodeError ReadIcc(Bytes chunk,Profile& colour) {
    const auto size=chunk.size();
    if(size<132||U8(chunk,0)!=(size>>24&255)||U8(chunk,1)!=(size>>16&255)||U8(chunk,2)!=(size>>8&255)||U8(chunk,3)!=(size&255))
        return DecodeError::invalid_layout;
    colour.bytes.assign(chunk.begin(),chunk.end());
    colour.bytes.resize(size+2); // skcms documents two readable padding bytes.
    if(!skcms_Parse(colour.bytes.data(),size,&colour.profile))return DecodeError::invalid_layout;
    if(colour.profile.data_color_space!=skcms_Signature_RGB||skcms_GetInputChannelCount(&colour.profile)!=3)
        return DecodeError::unsupported_content;
    return DecodeError::none;
}
DecodeError DecodePayload(Bytes bytes,const Frame& frame,std::vector<std::uint8_t>& rgba) {
    WebPDecoderConfig config{};
    if(!WebPInitDecoderConfig(&config))return DecodeError::unsupported_content;
    rgba.resize(std::size_t(frame.width)*frame.height*4);
    config.output.colorspace=MODE_RGBA;config.output.is_external_memory=1;
    config.output.u.RGBA.rgba=rgba.data();
    config.output.u.RGBA.stride=int(frame.width*4);config.output.u.RGBA.size=rgba.size();
    config.options.use_threads=0;
    const auto status=WebPDecode(Data(bytes,frame.begin),frame.end-frame.begin,&config);
    WebPFreeDecBuffer(&config.output);
    return status==VP8_STATUS_OK?DecodeError::none:DecodeStatus(status);
}
// Straight-alpha "over" from the WebP container specification, rounded.
void Composite(const Frame& frame,const std::vector<std::uint8_t>& source,Image& canvas) {
    for(std::uint32_t y=0;y<frame.height;++y)for(std::uint32_t x=0;x<frame.width;++x) {
        const auto* s=source.data()+(std::size_t(y)*frame.width+x)*4;
        auto* d=canvas.rgba8.data()+((std::size_t(frame.y)+y)*canvas.width+frame.x+x)*4;
        if(!frame.blend||s[3]==255) {
            std::memcpy(d,s,4);
            continue;
        }
        const std::uint32_t destination=std::uint32_t(d[3])*(255u-s[3]);
        const std::uint32_t alpha=std::uint32_t(s[3])*255u+destination;
        if(!alpha) {
            std::memset(d,0,4);
            continue;
        }
        for(unsigned c=0;c<3;++c)d[c]=std::uint8_t((std::uint32_t(s[c])*s[3]*255u+std::uint32_t(d[c])*destination+alpha/2)/alpha);
        d[3]=std::uint8_t((alpha+127)/255);
    }
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
    Profile colour;
    if(container.icc) {
        if(const auto error=ReadIcc(container.profile,colour);error!=DecodeError::none)return Failure(error);
    }
    auto frame=container.image;
    WebPBitstreamFeatures features{};
    const auto featureStatus=WebPGetFeatures(Data(bytes,frame.begin),frame.end-frame.begin,&features);
    if(featureStatus!=VP8_STATUS_OK)return Failure(DecodeStatus(featureStatus));
    if(features.has_animation)return Failure(DecodeError::invalid_layout);
    const auto width=std::uint32_t(features.width),height=std::uint32_t(features.height);
    if(!Dimensions(width,height,options))return Failure(DecodeError::invalid_dimensions);
    DecodeResult result;
    if(container.animated) {
        // Frame rectangles were validated against the canvas by Inspect.
        std::vector<std::uint8_t> pixels;
        if(const auto error=DecodePayload(bytes,frame,pixels);error!=DecodeError::none)return Failure(error);
        result.image.width=container.width;result.image.height=container.height;
        result.image.frameCount=container.frames;
        result.image.rgba8.resize(std::size_t(container.width)*container.height*4);
        // resize() zero-fills: the canvas starts as transparent black (0,0,0,0).
        Composite(frame,pixels,result.image);
    } else {
        if(container.extended&&(container.width!=width||container.height!=height||container.alpha!=bool(features.has_alpha)))
            return Failure(DecodeError::invalid_layout);
        frame.width=width;frame.height=height;
        result.image.width=width;result.image.height=height;
        if(const auto error=DecodePayload(bytes,frame,result.image.rgba8);error!=DecodeError::none)return Failure(error);
    }
    if(!colour.bytes.empty()&&!skcms_Transform(result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,
        &colour.profile,result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),
        std::size_t(result.image.width)*result.image.height))return Failure(DecodeError::unsupported_content);
    return result;
}
// Lossless (default): exact RGBA including transparent RGB. Lossy: VP8 at
// webpQuality with method 4, exact=0 (transparent RGB may change) and a
// losslessly coded ALPH plane (alpha_quality 100). Single-threaded, fixed
// configuration, so output bytes are deterministic for identical inputs.
EncodeResult EncodeWebp(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba,const EncodeOptions& options) {
    if(options.webpQuality<1||options.webpQuality>100)return {EncodeError::invalid_options,false,{}};
    WebPConfig config{};Picture picture;
    if(!WebPConfigInit(&config)||!WebPPictureInit(&picture.value))return {EncodeError::encoding_failed,false,{}};
    config.method=4;config.thread_level=0;
    if(options.webpLossless) {
        config.lossless=1;config.exact=1;config.quality=75;config.near_lossless=100;
    } else {
        config.lossless=0;config.exact=0;config.quality=float(options.webpQuality);
        config.alpha_compression=1;config.alpha_quality=100;
    }
    if(!WebPValidateConfig(&config))return {EncodeError::invalid_options,false,{}};
    picture.value.use_argb=1;picture.value.width=int(width);picture.value.height=int(height);
    Output output{{},options.maximumOutputBytes};picture.value.writer=Write;picture.value.custom_ptr=&output;
    if(!WebPPictureImportRGBA(&picture.value,rgba.data(),int(width*4)))return {EncodeError::allocation_failed,false,{}};
    const auto accepted=WebPEncode(&config,&picture.value);
    if(output.error!=EncodeError::none)return {output.error,false,{}};
    if(!accepted||output.bytes.empty())return {EncodeError::encoding_failed,false,{}};
    return {EncodeError::none,false,std::move(output.bytes)};
}
}
