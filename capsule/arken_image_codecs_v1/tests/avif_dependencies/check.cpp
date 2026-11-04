#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <aom/aom_codec.h>
#include <aom/aom_decoder.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include <aom/aomdx.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#if !defined(__wasm32__)
#include <fstream>
#endif

// Dependency qualification only. Nothing here admits AVIF as an Arken image
// format, defines the product colour/selection policy, or is the eventual
// image-codec API. The same entry points are intended for a later browser
// parity build; that build has not been performed.

namespace {

constexpr std::uint64_t kFnvOffset=14695981039346656037ull;
constexpr std::uint64_t kFnvPrime=1099511628211ull;

std::uint64_t Fnv1a64(const std::uint8_t* data,std::size_t size,std::uint64_t hash=kFnvOffset){
    for(std::size_t i=0;i<size;++i){hash^=data[i];hash*=kFnvPrime;}
    return hash;
}

struct Library{
    bool ok=heif_init(nullptr).code==heif_error_Ok;
    ~Library(){if(ok)heif_deinit();}
};

using Context=std::unique_ptr<heif_context,decltype(&heif_context_free)>;
using Handle=std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)>;
using Image=std::unique_ptr<heif_image,decltype(&heif_image_release)>;
using Encoder=std::unique_ptr<heif_encoder,decltype(&heif_encoder_release)>;
using EncodingOptions=std::unique_ptr<heif_encoding_options,decltype(&heif_encoding_options_free)>;
using DecodingOptions=std::unique_ptr<heif_decoding_options,decltype(&heif_decoding_options_free)>;
using Nclx=std::unique_ptr<heif_color_profile_nclx,decltype(&heif_nclx_color_profile_free)>;

// Bounded qualification limits. They are deliberately no larger than the
// existing ordinary 8 Mi-pixel / 8192-dimension profile.
constexpr std::uint64_t kMaxPixels=8ull*1024*1024;
constexpr int kMaxDimension=8192;
constexpr unsigned kMaxInputBytes=32u*1024*1024;

bool ApplyLimits(heif_context* context){
    auto limits=*heif_context_get_security_limits(context);
    limits.max_image_size_pixels=kMaxPixels;
    limits.max_memory_block_size=64ull*1024*1024;
    limits.max_total_memory=128ull*1024*1024;
    limits.max_number_of_tiles=256;
    limits.max_items=1024;
    limits.max_sequence_frames=1;
    if(heif_context_set_security_limits(context,&limits).code!=heif_error_Ok)return false;
    heif_context_set_max_decoding_threads(context,0);
    return true;
}

struct Decoded{
    unsigned width=0,height=0,lumaBits=0,topLevelImages=0;
    bool alpha=false;
    std::vector<std::uint8_t> rgba;
};

// Dependency profile: one top-level primary still image from a file whose
// ftyp lists the 'avif' brand (many real files use major brand 'mif1' with
// 'avif' compatible), 8-bit luma only, decoded through the selected AV1 decoder to tightly
// packed straight-alpha RGBA8. Sequences and higher bit depths are rejected.
int DecodeStill(const std::uint8_t* bytes,unsigned length,Decoded& out){
    out=Decoded{};
    if(!bytes||!length||length>kMaxInputBytes)return 10;
    if(heif_has_compatible_brand(bytes,int(length),"avif")!=1)return 11;
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return 12;
    if(!ApplyLimits(context.get()))return 13;
    if(heif_context_read_from_memory_without_copy(context.get(),bytes,length,nullptr).code!=heif_error_Ok)return 14;
    if(heif_context_has_sequence(context.get()))return 15;
    out.topLevelImages=unsigned(heif_context_get_number_of_top_level_images(context.get()));
    heif_image_handle* rawHandle=nullptr;
    if(heif_context_get_primary_image_handle(context.get(),&rawHandle).code!=heif_error_Ok)return 16;
    Handle handle(rawHandle,heif_image_handle_release);
    const int lumaBits=heif_image_handle_get_luma_bits_per_pixel(rawHandle);
    out.lumaBits=lumaBits>0?unsigned(lumaBits):0;
    if(lumaBits!=8)return 17;
    out.alpha=heif_image_handle_has_alpha_channel(rawHandle)!=0;
    DecodingOptions options(heif_decoding_options_alloc(),heif_decoding_options_free);
    if(!options)return 18;
    options->strict_decoding=1;options->num_codec_threads=1;
    heif_image* rawImage=nullptr;
    if(heif_decode_image(rawHandle,&rawImage,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,options.get()).code!=heif_error_Ok)return 19;
    Image image(rawImage,heif_image_release);
    const int w=heif_image_get_width(rawImage,heif_channel_interleaved),h=heif_image_get_height(rawImage,heif_channel_interleaved);
    int stride=0;const auto* data=heif_image_get_plane_readonly(rawImage,heif_channel_interleaved,&stride);
    if(w<=0||h<=0||w>kMaxDimension||h>kMaxDimension||std::uint64_t(w)*std::uint64_t(h)>kMaxPixels||!data||stride<w*4)return 20;
    if(heif_image_get_bits_per_pixel_range(rawImage,heif_channel_interleaved)!=8)return 21;
    out.width=unsigned(w);out.height=unsigned(h);
    out.rgba.resize(std::size_t(w)*std::size_t(h)*4);
    for(int y=0;y<h;++y)std::memcpy(out.rgba.data()+std::size_t(y)*std::size_t(w)*4,data+std::size_t(y)*std::size_t(stride),std::size_t(w)*4);
    return 0;
}

char lastEncodeError[256]{};

heif_error AppendBytes(heif_context*,const void* data,std::size_t size,void* userdata){
    auto& sink=*static_cast<std::vector<std::uint8_t>*>(userdata);
    const auto* begin=static_cast<const std::uint8_t*>(data);
    sink.insert(sink.end(),begin,begin+size);
    return heif_error{heif_error_Ok,heif_suberror_Unspecified,"Success"};
}

struct EncodeRequest{
    bool lossless=true;
    const char* chroma="444";
    int quality=100;
    int bitDepth=8;
};

// Encodes one RGBA still with the selected AV1 encoder into an in-memory AVIF.
int EncodeStill(const std::uint8_t* rgba,unsigned width,unsigned height,const EncodeRequest& request,std::vector<std::uint8_t>& out){
    out.clear();
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return 30;
    heif_image* rawImage=nullptr;
    const auto chroma=request.bitDepth==8?heif_chroma_interleaved_RGBA:heif_chroma_interleaved_RRGGBBAA_LE;
    if(heif_image_create(int(width),int(height),heif_colorspace_RGB,chroma,&rawImage).code!=heif_error_Ok)return 31;
    Image image(rawImage,heif_image_release);
    if(heif_image_add_plane(rawImage,heif_channel_interleaved,int(width),int(height),request.bitDepth).code!=heif_error_Ok)return 32;
    int stride=0;auto* plane=heif_image_get_plane(rawImage,heif_channel_interleaved,&stride);
    const std::size_t rowBytes=std::size_t(width)*4*(request.bitDepth==8?1:2);
    if(!plane||std::size_t(stride)<rowBytes)return 33;
    for(unsigned y=0;y<height;++y){
        auto* row=plane+std::size_t(y)*std::size_t(stride);
        if(request.bitDepth==8){std::memcpy(row,rgba+std::size_t(y)*width*4,rowBytes);continue;}
        for(std::size_t i=0;i<std::size_t(width)*4;++i){
            const unsigned value=unsigned(rgba[std::size_t(y)*width*4+i])<<2;
            row[i*2]=std::uint8_t(value&0xffu);row[i*2+1]=std::uint8_t(value>>8);
        }
    }
    Nclx nclx(heif_nclx_color_profile_alloc(),heif_nclx_color_profile_free);
    if(!nclx)return 34;
    nclx->color_primaries=heif_color_primaries_ITU_R_BT_709_5;
    nclx->transfer_characteristics=heif_transfer_characteristic_IEC_61966_2_1;
    // Lossless RGB needs the identity (GBR) matrix and full range; lossy
    // output uses the conventional BT.601 full-range matrix.
    nclx->matrix_coefficients=request.lossless?heif_matrix_coefficients_RGB_GBR:heif_matrix_coefficients_ITU_R_BT_601_6;
    nclx->full_range_flag=1;
    if(heif_image_set_nclx_color_profile(rawImage,nclx.get()).code!=heif_error_Ok)return 35;
    heif_encoder* rawEncoder=nullptr;
    if(heif_context_get_encoder_for_format(context.get(),heif_compression_AV1,&rawEncoder).code!=heif_error_Ok)return 36;
    Encoder encoder(rawEncoder,heif_encoder_release);
    if(heif_encoder_set_parameter_integer(rawEncoder,"threads",1).code!=heif_error_Ok)return 37;
    if(heif_encoder_set_parameter_integer(rawEncoder,"speed",6).code!=heif_error_Ok)return 38;
    if(heif_encoder_set_parameter_string(rawEncoder,"chroma",request.chroma).code!=heif_error_Ok)return 39;
    if(request.lossless){
        if(heif_encoder_set_lossless(rawEncoder,1).code!=heif_error_Ok)return 40;
    }else if(heif_encoder_set_lossy_quality(rawEncoder,request.quality).code!=heif_error_Ok)return 40;
    EncodingOptions options(heif_encoding_options_alloc(),heif_encoding_options_free);
    if(!options)return 41;
    options->save_alpha_channel=1;
    options->output_nclx_profile=nclx.get();
    heif_image_handle* rawHandle=nullptr;
    const auto encodedStatus=heif_context_encode_image(context.get(),rawImage,rawEncoder,options.get(),&rawHandle);
    if(encodedStatus.code!=heif_error_Ok){
        std::snprintf(lastEncodeError,sizeof(lastEncodeError),"%d/%d %s",int(encodedStatus.code),int(encodedStatus.subcode),
            encodedStatus.message?encodedStatus.message:"");
        for(char* c=lastEncodeError;*c;++c)if(*c=='"'||*c=='\\'||static_cast<unsigned char>(*c)<0x20)*c='\'';
        return 42;
    }
    Handle handle(rawHandle,heif_image_handle_release);
    heif_writer writer{1,AppendBytes};
    if(heif_context_write(context.get(),&writer,&out).code!=heif_error_Ok)return 43;
    if(out.empty()||out.size()>kMaxInputBytes)return 44;
    if(heif_read_main_brand(out.data(),int(out.size()))!=heif_brand2_avif)return 45;
    return 0;
}

std::vector<std::uint8_t> Pattern(unsigned width,unsigned height){
    std::vector<std::uint8_t> rgba(std::size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        auto* p=rgba.data()+(std::size_t(y)*width+x)*4;
        p[0]=std::uint8_t(x*37u+y*11u);
        p[1]=std::uint8_t((x*5u)^(y*29u));
        p[2]=std::uint8_t(x*y*3u+17u);
        // Includes fully transparent, opaque and intermediate coverage.
        p[3]=x==0?0:(y==0?255:std::uint8_t(x*13u+y*7u));
    }
    return rgba;
}

unsigned decodedWidth=0,decodedHeight=0,decodedBits=0,decodedTopLevel=0;
int decodedAlpha=0;
std::uint64_t decodedFingerprint=0;

} // namespace

extern "C" int arken_avif_dependency_check(){
    if(std::strcmp(heif_get_version(),"1.23.7"))return 1;
    if(std::strcmp(aom_codec_version_str(),"v3.15.3")||aom_codec_version()!=((3<<16)|(15<<8)|3))return 1;
    Library library;
    if(!library.ok)return 2;
    // Only AV1 decode and encode are admitted; every other codec is absent.
    if(!heif_have_decoder_for_format(heif_compression_AV1)||!heif_have_encoder_for_format(heif_compression_AV1))return 3;
    constexpr heif_compression_format others[]={heif_compression_HEVC,heif_compression_AVC,heif_compression_JPEG,
        heif_compression_VVC,heif_compression_EVC,heif_compression_JPEG2000,heif_compression_uncompressed,
        heif_compression_HTJ2K};
    for(const auto format:others)if(heif_have_decoder_for_format(format)||heif_have_encoder_for_format(format))return 4;
    // libheif 1.23.7 unconditionally registers its internal region-mask
    // encoder ("mask"); it has no decoder and no option to remove it. It is
    // the only permitted non-AV1 entry and is never selected for images here.
    if(heif_have_decoder_for_format(heif_compression_mask))return 4;
    const heif_decoder_descriptor* decoders[4]{};
    const heif_encoder_descriptor* encoders[4]{};
    if(heif_get_decoder_descriptors(heif_compression_undefined,decoders,4)!=1||std::strcmp(heif_decoder_descriptor_get_id_name(decoders[0]),"aom"))return 5;
    if(heif_get_encoder_descriptors(heif_compression_undefined,nullptr,encoders,4)!=2)return 5;
    bool aomEncoder=false,maskEncoder=false;
    for(const auto* descriptor:encoders){
        if(!descriptor)continue;
        const char* id=heif_encoder_descriptor_get_id_name(descriptor);
        aomEncoder|=!std::strcmp(id,"aom");maskEncoder|=!std::strcmp(id,"mask");
    }
    if(!aomEncoder||!maskEncoder)return 5;
    if(heif_get_encoder_descriptors(heif_compression_AV1,nullptr,encoders,4)!=1||std::strcmp(heif_encoder_descriptor_get_id_name(encoders[0]),"aom"))return 5;
    // 8-bit-only codec build: neither direction advertises high bit depth.
    if((aom_codec_get_caps(aom_codec_av1_dx())&AOM_CODEC_CAP_HIGHBITDEPTH)||(aom_codec_get_caps(aom_codec_av1_cx())&AOM_CODEC_CAP_HIGHBITDEPTH))return 6;
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return 7;
    auto limits=*heif_context_get_security_limits(context.get());
    limits.max_image_size_pixels=1024*1024;
    limits.max_memory_block_size=16*1024*1024;
    limits.max_total_memory=32*1024*1024;
    limits.max_number_of_tiles=64;
    limits.max_items=128;
    limits.max_sequence_frames=1;
    if(heif_context_set_security_limits(context.get(),&limits).code!=heif_error_Ok)return 8;
    const auto* applied=heif_context_get_security_limits(context.get());
    if(applied->max_image_size_pixels!=1024*1024||applied->max_total_memory!=32*1024*1024||applied->max_sequence_frames!=1)return 8;
    constexpr unsigned char invalid[]={0,0,0,8,'f','t','y','p'};
    if(heif_context_read_from_memory_without_copy(context.get(),invalid,sizeof(invalid),nullptr).code==heif_error_Ok)return 9;
    return 0;
}

extern "C" unsigned arken_avif_dependency_width(){return decodedWidth;}
extern "C" unsigned arken_avif_dependency_height(){return decodedHeight;}
extern "C" std::uint64_t arken_avif_dependency_pixels(){return decodedFingerprint;}

// Dependency parity only: accepted product colour/selection policy is separate.
extern "C" int arken_avif_dependency_decode(const unsigned char* bytes,unsigned length){
    decodedWidth=decodedHeight=decodedBits=decodedTopLevel=0;decodedAlpha=0;decodedFingerprint=0;
    Library library;
    if(!library.ok)return 2;
    Decoded decoded;
    const int result=DecodeStill(bytes,length,decoded);
    decodedBits=decoded.lumaBits;decodedTopLevel=decoded.topLevelImages;decodedAlpha=decoded.alpha;
    if(result)return result;
    decodedWidth=decoded.width;decodedHeight=decoded.height;
    decodedFingerprint=Fnv1a64(decoded.rgba.data(),decoded.rgba.size());
    return 0;
}

namespace {
std::size_t losslessBytes=0,lossyBytes=0;
std::uint64_t sourceFingerprint=0;
int highBitDepthResult=0;
}

// Encode -> decode round trips. Lossless must reproduce RGBA exactly;
// lossy only has to decode at the same geometry. A 10-bit request must be
// refused by this 8-bit-only build rather than silently reduced.
extern "C" int arken_avif_dependency_roundtrip(){
    losslessBytes=lossyBytes=0;sourceFingerprint=0;highBitDepthResult=0;
    Library library;
    if(!library.ok)return 2;
    constexpr unsigned width=61,height=37;
    const auto source=Pattern(width,height);
    sourceFingerprint=Fnv1a64(source.data(),source.size());
    std::vector<std::uint8_t> encoded;
    if(const int result=EncodeStill(source.data(),width,height,EncodeRequest{},encoded))return result;
    losslessBytes=encoded.size();
    Decoded decoded;
    if(const int result=DecodeStill(encoded.data(),unsigned(encoded.size()),decoded))return 50+result;
    if(decoded.width!=width||decoded.height!=height||!decoded.alpha||decoded.topLevelImages!=1)return 51;
    if(decoded.rgba!=source)return 52;
    EncodeRequest lossy;lossy.lossless=false;lossy.chroma="420";lossy.quality=60;
    if(const int result=EncodeStill(source.data(),width,height,lossy,encoded))return 100+result;
    lossyBytes=encoded.size();
    if(const int result=DecodeStill(encoded.data(),unsigned(encoded.size()),decoded))return 150+result;
    if(decoded.width!=width||decoded.height!=height||!decoded.alpha)return 61;
    EncodeRequest deep;deep.bitDepth=10;
    lastEncodeError[0]=0;
    highBitDepthResult=EncodeStill(source.data(),width,height,deep,encoded);
    // The refusal must come from the AV1 encoder (encoder plugin error), not
    // from an earlier image-construction step.
    char expected[32];
    std::snprintf(expected,sizeof(expected),"%d/%d ",int(heif_error_Encoder_plugin_error),int(heif_suberror_Encoder_initialization));
    if(highBitDepthResult!=42||std::strncmp(lastEncodeError,expected,std::strlen(expected)))return 62;
    return 0;
}

#if !defined(__wasm32__)
int main(int argc,char** argv){
    for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--version")){
        std::puts("arken-avif-dependency-check avif-deps-1 (libheif 1.23.7, libaom 3.15.3)");return 0;
    }
    if(argc==3&&!std::strcmp(argv[1],"--decode")){
        std::ifstream input(argv[2],std::ios::binary|std::ios::ate);
        if(!input||input.tellg()<=0||input.tellg()>std::streamoff(kMaxInputBytes))return 65;
        std::vector<unsigned char> bytes(std::size_t(input.tellg()));input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));if(!input)return 66;
        const auto result=arken_avif_dependency_decode(bytes.data(),unsigned(bytes.size()));
        std::printf("{\"status\":\"%s\",\"code\":%d,\"width\":%u,\"height\":%u,\"lumaBits\":%u,\"alpha\":%s,\"topLevelImages\":%u,\"rgbaFnv1a64\":\"%016llx\"}\n",
            result?"failed":"passed",result,decodedWidth,decodedHeight,decodedBits,decodedAlpha?"true":"false",decodedTopLevel,
            static_cast<unsigned long long>(decodedFingerprint));
        return result;
    }
    if(argc==2&&!std::strcmp(argv[1],"--roundtrip")){
        const auto result=arken_avif_dependency_roundtrip();
        std::printf("{\"status\":\"%s\",\"code\":%d,\"width\":61,\"height\":37,\"sourceRgbaFnv1a64\":\"%016llx\",\"losslessBytes\":%zu,\"lossyBytes\":%zu,\"tenBitEncodeRefusal\":%d,\"tenBitError\":\"%s\"}\n",
            result?"failed":"passed",result,static_cast<unsigned long long>(sourceFingerprint),losslessBytes,lossyBytes,highBitDepthResult,lastEncodeError);
        return result;
    }
    if(argc!=1){std::fputs("Unexpected argument\n",stderr);return 64;}
    const auto result=arken_avif_dependency_check();
    std::printf("AVIF dependency admission: %s (%d) aom build: %s\n",result?"failed":"passed",result,aom_codec_build_config());
    return result;
}
#endif
