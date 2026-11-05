#include "avif.hpp"
#include "heif_common.hpp"
#include "reader.hpp"
#include <cstring>
#include <new>

namespace arken::image::v1::detail {
namespace {
using namespace heif_shared;
using Encoder=std::unique_ptr<heif_encoder,decltype(&heif_encoder_release)>;
using EncodingOptions=std::unique_ptr<heif_encoding_options,decltype(&heif_encoding_options_free)>;

constexpr int encoderSpeed=6; // libheif's default for libaom; fixed so output never depends on a default.

EncodeResult Failed(EncodeError error){return {error,false,{}};}
EncodeError EncodeFailure(const heif_error& error) {
    return error.code==heif_error_Memory_allocation_error?EncodeError::allocation_failed:EncodeError::encoding_failed;
}
struct Sink {
    std::vector<std::byte> bytes;
    std::uint64_t limit=0;
    EncodeError error=EncodeError::none;
};
heif_error Write(heif_context*,const void* data,std::size_t size,void* userdata) {
    auto& sink=*static_cast<Sink*>(userdata);
    if(sink.error==EncodeError::none&&std::uint64_t(size)>sink.limit-sink.bytes.size())sink.error=EncodeError::output_limit;
    if(sink.error==EncodeError::none)try {
        const auto* first=static_cast<const std::byte*>(data);
        sink.bytes.insert(sink.bytes.end(),first,first+size);
    }catch(const std::bad_alloc&){sink.error=EncodeError::allocation_failed;}
    if(sink.error!=EncodeError::none)return {heif_error_Encoding_error,heif_suberror_Cannot_write_output_data,"output rejected"};
    return {heif_error_Ok,heif_suberror_Unspecified,"Success"};
}
bool Set(heif_error status){return status.code==heif_error_Ok;}
}

DecodeResult DecodeAvif(Bytes bytes,const DecodeOptions& options) {
    if(const auto error=heif_shared::Preflight(bytes,options);error!=DecodeError::none)return Failure(error);
    const auto* data=reinterpret_cast<const std::uint8_t*>(bytes.data());
    const auto size=static_cast<int>(bytes.size());
    // The ftyp must list 'avif'. HEIC files, 'avis' sequences and the reduced-header 'mif3'
    // ('mini') form, which carries no compatible-brand list, are not this wrapper's profile.
    const auto brand=heif_read_main_brand(data,size);
    if(brand==heif_brand2_avis||brand==heif_fourcc('m','i','f','3')||heif_has_compatible_brand(data,size,"avif")!=1)
        return Failure(DecodeError::unsupported_content);
    // Admitted primary items: a coded AV1 image or a grid of AV1 tiles the decoder assembles.
    return heif_shared::DecodeStill(bytes,options,av1Item);
}

EncodeResult EncodeAvif(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba,
    bool lossless,std::uint32_t quality,std::uint64_t maximumOutputBytes) {
    bool alpha=false;for(std::size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255){alpha=true;break;}
    Library library;
    if(library.status.code!=heif_error_Ok)return Failed(EncodeFailure(library.status));
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return Failed(EncodeError::allocation_failed);

    heif_image* rawImage=nullptr;
    if(const auto status=heif_image_create(int(width),int(height),heif_colorspace_RGB,
        alpha?heif_chroma_interleaved_RGBA:heif_chroma_interleaved_RGB,&rawImage);status.code!=heif_error_Ok)return Failed(EncodeFailure(status));
    Pixels image(rawImage,heif_image_release);
    if(const auto status=heif_image_add_plane(rawImage,heif_channel_interleaved,int(width),int(height),8);status.code!=heif_error_Ok)
        return Failed(EncodeFailure(status));
    std::size_t stride=0;
    auto* plane=heif_image_get_plane2(rawImage,heif_channel_interleaved,&stride);
    const std::size_t channels=alpha?4:3,rowBytes=std::size_t(width)*channels;
    if(!plane||stride<rowBytes)return Failed(EncodeError::encoding_failed);
    for(std::size_t y=0;y<height;++y) {
        const auto* source=rgba.data()+y*std::size_t(width)*4;
        auto* target=plane+y*stride;
        if(alpha){std::memcpy(target,source,rowBytes);continue;}
        for(std::size_t x=0;x<width;++x)std::memcpy(target+x*3,source+x*4,3);
    }

    // sRGB tagging. Lossless needs the identity (GBR) matrix so no YCbCr rounding occurs.
    Nclx nclx(heif_nclx_color_profile_alloc(),heif_nclx_color_profile_free);
    if(!nclx)return Failed(EncodeError::allocation_failed);
    nclx->color_primaries=heif_color_primaries_ITU_R_BT_709_5;
    nclx->transfer_characteristics=heif_transfer_characteristic_IEC_61966_2_1;
    nclx->matrix_coefficients=lossless?heif_matrix_coefficients_RGB_GBR:heif_matrix_coefficients_ITU_R_BT_601_6;
    nclx->full_range_flag=1;
    if(const auto status=heif_image_set_nclx_color_profile(rawImage,nclx.get());status.code!=heif_error_Ok)return Failed(EncodeFailure(status));

    heif_encoder* rawEncoder=nullptr;
    if(const auto status=heif_context_get_encoder_for_format(context.get(),heif_compression_AV1,&rawEncoder);status.code!=heif_error_Ok)
        return Failed(EncodeFailure(status));
    Encoder encoder(rawEncoder,heif_encoder_release);
    bool configured=Set(heif_encoder_set_parameter_integer(rawEncoder,"threads",1))
        &&Set(heif_encoder_set_parameter_integer(rawEncoder,"speed",encoderSpeed))
        &&Set(heif_encoder_set_parameter_string(rawEncoder,"chroma",lossless?"444":"420"));
    if(lossless)configured=configured&&Set(heif_encoder_set_lossless(rawEncoder,1));
    else configured=configured&&Set(heif_encoder_set_lossy_quality(rawEncoder,int(quality)))
        // Alpha is coverage, not picture content: it is always coded losslessly.
        &&Set(heif_encoder_set_parameter_boolean(rawEncoder,"lossless-alpha",1))
        &&Set(heif_encoder_set_parameter_integer(rawEncoder,"alpha-min-q",0))
        &&Set(heif_encoder_set_parameter_integer(rawEncoder,"alpha-max-q",0));
    if(!configured)return Failed(EncodeError::encoding_failed);

    EncodingOptions encoding(heif_encoding_options_alloc(),heif_encoding_options_free);
    if(!encoding)return Failed(EncodeError::allocation_failed);
    encoding->save_alpha_channel=alpha?1:0;
    encoding->output_nclx_profile=nclx.get();
    heif_image_handle* rawHandle=nullptr;
    if(const auto status=heif_context_encode_image(context.get(),rawImage,rawEncoder,encoding.get(),&rawHandle);status.code!=heif_error_Ok)
        return Failed(EncodeFailure(status));
    Handle handle(rawHandle,heif_image_handle_release);
    image.reset();

    Sink sink;sink.limit=maximumOutputBytes;
    heif_writer writer{1,Write};
    const auto written=heif_context_write(context.get(),&writer,&sink);
    if(sink.error!=EncodeError::none)return Failed(sink.error);
    if(written.code!=heif_error_Ok||sink.bytes.empty())return Failed(EncodeFailure(written));
    return {EncodeError::none,false,std::move(sink.bytes)};
}
}
