#include "heif.hpp"
#include "reader.hpp"
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <skcms_public.h>
#include <cstring>
#include <memory>

namespace arken::image::v1::detail {
namespace {
struct Library {
    heif_error status;
    Library() noexcept : status(heif_init(nullptr)) {}
    ~Library() { if(status.code==heif_error_Ok)heif_deinit(); }
    Library(const Library&)=delete;
    Library& operator=(const Library&)=delete;
};
using Context=std::unique_ptr<heif_context,decltype(&heif_context_free)>;
using Handle=std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)>;
using Options=std::unique_ptr<heif_decoding_options,decltype(&heif_decoding_options_free)>;
using Pixels=std::unique_ptr<heif_image,decltype(&heif_image_release)>;
using Nclx=std::unique_ptr<heif_color_profile_nclx,decltype(&heif_nclx_color_profile_free)>;

constexpr std::size_t maximumProfileBytes=4u*1024*1024; // Same bound as the PNG iCCP owner.

DecodeError Map(const heif_error& error) {
    switch(error.subcode) {
        case heif_suberror_Security_limit_exceeded:return DecodeError::resource_limit;
        case heif_suberror_End_of_data:return DecodeError::truncated;
        case heif_suberror_Unsupported_codec:case heif_suberror_Unsupported_image_type:
        case heif_suberror_Unsupported_color_conversion:case heif_suberror_Unsupported_data_version:
        case heif_suberror_Unsupported_item_construction_method:case heif_suberror_Unknown_color_profile_type:
        case heif_suberror_Unknown_NCLX_color_primaries:case heif_suberror_Unknown_NCLX_transfer_characteristics:
        case heif_suberror_Unknown_NCLX_matrix_coefficients:return DecodeError::unsupported_content;
        case heif_suberror_Invalid_image_size:case heif_suberror_Invalid_clean_aperture:
        case heif_suberror_Invalid_grid_data:return DecodeError::invalid_dimensions;
        default:break;
    }
    switch(error.code) {
        case heif_error_Unsupported_filetype:return DecodeError::wrong_magic;
        case heif_error_Unsupported_feature:return DecodeError::unsupported_content;
        case heif_error_Memory_allocation_error:return DecodeError::allocation_failed;
        case heif_error_Color_profile_does_not_exist:return DecodeError::unsupported_content;
        default:return DecodeError::invalid_layout;
    }
}

// Colour disposition decided before decoding; applied to the decoded straight-alpha RGBA8.
struct Colour {
    bool transform=false;
    skcms_ICCProfile profile{};
    std::vector<std::byte> icc; // skcms keeps pointers into the parsed bytes.
};

bool SdrTransfer(heif_transfer_characteristics transfer) noexcept {
    switch(transfer) {
        case heif_transfer_characteristic_ITU_R_BT_709_5:case heif_transfer_characteristic_unspecified:
        case heif_transfer_characteristic_ITU_R_BT_601_6:case heif_transfer_characteristic_IEC_61966_2_1:
        case heif_transfer_characteristic_ITU_R_BT_2020_2_10bit:case heif_transfer_characteristic_ITU_R_BT_2020_2_12bit:
            return true;
        default:return false; // PQ, HLG, linear, logarithmic, SMPTE 428 and the rest need a deliberate policy.
    }
}
bool WideGamutPrimaries(heif_color_primaries primaries,skcms_Matrix3x3& toXYZD50) noexcept {
    // D65 white point for both. Display P3: SMPTE EG 432-1. BT.2020: ITU-R BT.2020-2.
    if(primaries==heif_color_primaries_SMPTE_EG_432_1)
        return skcms_PrimariesToXYZD50(0.680f,0.320f,0.265f,0.690f,0.150f,0.060f,0.3127f,0.3290f,&toXYZD50);
    if(primaries==heif_color_primaries_ITU_R_BT_2020_2_and_2100_0)
        return skcms_PrimariesToXYZD50(0.708f,0.292f,0.170f,0.797f,0.131f,0.046f,0.3127f,0.3290f,&toXYZD50);
    return false;
}
DecodeError DecideColour(const heif_image_handle* handle,Colour& colour) {
    const auto type=heif_image_handle_get_color_profile_type(handle);
    heif_color_profile_nclx* raw=nullptr;
    const auto nclxStatus=heif_image_handle_get_nclx_color_profile(handle,&raw);
    Nclx nclx(raw,heif_nclx_color_profile_free);
    if(nclxStatus.code!=heif_error_Ok&&nclxStatus.code!=heif_error_Color_profile_does_not_exist)return Map(nclxStatus);
    if(nclx&&!SdrTransfer(nclx->transfer_characteristics))return DecodeError::unsupported_content;
    if(type==heif_color_profile_type_rICC||type==heif_color_profile_type_prof) {
        const auto size=heif_image_handle_get_raw_color_profile_size(handle);
        if(size>maximumProfileBytes)return DecodeError::resource_limit;
        if(size<132)return DecodeError::invalid_layout;
        colour.icc.resize(size+2); // skcms documents two readable padding bytes.
        const auto status=heif_image_handle_get_raw_color_profile(handle,colour.icc.data());
        if(status.code!=heif_error_Ok)return Map(status);
        colour.icc[size]=colour.icc[size+1]=std::byte{0};
        if(!skcms_Parse(colour.icc.data(),size,&colour.profile))return DecodeError::invalid_layout;
        if(colour.profile.data_color_space!=skcms_Signature_RGB||skcms_GetInputChannelCount(&colour.profile)!=3)
            return DecodeError::unsupported_content;
        // The ICC profile owns primaries and transfer; NCLX still drives the YCbCr matrix below.
        colour.transform=!skcms_ApproximatelyEqualProfiles(&colour.profile,skcms_sRGB_profile());
        return DecodeError::none;
    }
    if(!nclx)return DecodeError::none; // Untagged: decoder default interpretation (sRGB).
    switch(nclx->color_primaries) {
        case heif_color_primaries_ITU_R_BT_709_5:case heif_color_primaries_unspecified:return DecodeError::none;
        case heif_color_primaries_SMPTE_EG_432_1:case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0: {
            skcms_Matrix3x3 toXYZD50{};
            if(!WideGamutPrimaries(nclx->color_primaries,toXYZD50))return DecodeError::unsupported_content;
            skcms_Init(&colour.profile);
            skcms_SetTransferFunction(&colour.profile,skcms_sRGB_TransferFunction());
            skcms_SetXYZD50(&colour.profile,&toXYZD50);
            colour.transform=true;
            return DecodeError::none;
        }
        default:return DecodeError::unsupported_content;
    }
}
}

DecodeResult DecodeHeif(Bytes bytes,const DecodeOptions& options) {
    if(options.frameIndex||options.directionIndex||options.mipIndex)return Failure(DecodeError::selection_out_of_range);
    if(bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))return Failure(DecodeError::input_too_large);
    const auto* data=reinterpret_cast<const std::uint8_t*>(bytes.data());
    // Only the ISOBMFF signature is checked here; libheif's coarse file-type check reports the
    // common 'mif1' major brand as undecided, so item and codec admission is left to its parser.
    if(bytes.size()<12)return Failure(DecodeError::truncated);
    if(std::memcmp(data+4,"ftyp",4)!=0)return Failure(DecodeError::wrong_magic);
    const auto brand=heif_read_main_brand(data,static_cast<int>(bytes.size()));
    if(brand==heif_brand2_avif||brand==heif_brand2_avis)return Failure(DecodeError::unsupported_content); // AV1 is not enabled.

    Library library;
    if(library.status.code!=heif_error_Ok)return Failure(DecodeError::allocation_failed);
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return Failure(DecodeError::allocation_failed);
    auto limits=*heif_context_get_security_limits(context.get());
    const std::uint64_t planeBytes=options.maximumPixels*4;
    limits.max_image_size_pixels=options.maximumPixels;
    limits.max_number_of_tiles=256;
    limits.max_items=256;
    limits.max_sequence_frames=1;
    limits.max_color_profile_size=maximumProfileBytes;
    limits.max_memory_block_size=std::clamp<std::uint64_t>(planeBytes*2,16ull*1024*1024,256ull*1024*1024);
    limits.max_total_memory=std::clamp<std::uint64_t>(planeBytes*8,64ull*1024*1024,1024ull*1024*1024);
    if(heif_context_set_security_limits(context.get(),&limits).code!=heif_error_Ok)return Failure(DecodeError::resource_limit);
    heif_context_set_max_decoding_threads(context.get(),0);
    auto status=heif_context_read_from_memory_without_copy(context.get(),data,bytes.size(),nullptr);
    if(status.code!=heif_error_Ok)return Failure(Map(status));
    if(heif_context_has_sequence(context.get()))return Failure(DecodeError::unsupported_content);
    const auto topLevel=heif_context_get_number_of_top_level_images(context.get());
    if(topLevel<1)return Failure(DecodeError::invalid_layout);
    if(topLevel>256)return Failure(DecodeError::resource_limit);

    heif_image_handle* rawHandle=nullptr;
    status=heif_context_get_primary_image_handle(context.get(),&rawHandle);
    if(status.code!=heif_error_Ok)return Failure(Map(status));
    Handle handle(rawHandle,heif_image_handle_release);
    const auto luma=heif_image_handle_get_luma_bits_per_pixel(handle.get()),chroma=heif_image_handle_get_chroma_bits_per_pixel(handle.get());
    if(luma>8||chroma>8)return Failure(DecodeError::unsupported_content); // No high-bit-depth or HDR policy yet.
    if(heif_image_handle_is_premultiplied_alpha(handle.get()))return Failure(DecodeError::unsupported_content);
    const auto width=heif_image_handle_get_width(handle.get()),height=heif_image_handle_get_height(handle.get());
    const auto codedWidth=heif_image_handle_get_ispe_width(handle.get()),codedHeight=heif_image_handle_get_ispe_height(handle.get());
    if(width<=0||height<=0||codedWidth<=0||codedHeight<=0)return Failure(DecodeError::invalid_dimensions);
    if(!Dimensions(std::uint32_t(width),std::uint32_t(height),options)
        ||!Dimensions(std::uint32_t(codedWidth),std::uint32_t(codedHeight),options))return Failure(DecodeError::invalid_dimensions);
    Colour colour;
    if(const auto error=DecideColour(handle.get(),colour);error!=DecodeError::none)return Failure(error);

    Options decoding(heif_decoding_options_alloc(),heif_decoding_options_free);
    Nclx output(heif_nclx_color_profile_alloc(),heif_nclx_color_profile_free);
    if(!decoding||!output)return Failure(DecodeError::allocation_failed);
    decoding->ignore_transformations=0;
    decoding->convert_hdr_to_8bit=0;
    decoding->strict_decoding=1;
    decoding->num_codec_threads=1;
    // Explicit output tagging: the decoder converts only the YCbCr matrix and range to this
    // target; primaries and transfer are handled by the colour decision above.
    output->color_primaries=heif_color_primaries_ITU_R_BT_709_5;
    output->transfer_characteristics=heif_transfer_characteristic_IEC_61966_2_1;
    output->matrix_coefficients=heif_matrix_coefficients_ITU_R_BT_601_6;
    output->full_range_flag=1;
    decoding->output_image_nclx_profile=output.get();
    heif_image* rawImage=nullptr;
    status=heif_decode_image(handle.get(),&rawImage,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,decoding.get());
    Pixels image(rawImage,heif_image_release);
    if(status.code!=heif_error_Ok||!image)return Failure(Map(status));
    const auto decodedWidth=heif_image_get_width(image.get(),heif_channel_interleaved);
    const auto decodedHeight=heif_image_get_height(image.get(),heif_channel_interleaved);
    if(decodedWidth!=width||decodedHeight!=height)return Failure(DecodeError::invalid_dimensions);
    if(heif_image_get_bits_per_pixel(image.get(),heif_channel_interleaved)!=32)return Failure(DecodeError::unsupported_content);
    std::size_t stride=0;
    const auto* plane=heif_image_get_plane_readonly2(image.get(),heif_channel_interleaved,&stride);
    const auto rowBytes=std::size_t(width)*4;
    if(!plane||stride<rowBytes)return Failure(DecodeError::invalid_layout);

    DecodeResult result;
    result.image.width=std::uint32_t(width);result.image.height=std::uint32_t(height);
    // Primary-only extraction is explicit: a collection reports every top-level image it holds.
    result.image.frameCount=std::uint32_t(topLevel);
    result.image.rgba8.resize(rowBytes*std::size_t(height));
    for(std::size_t y=0;y<std::size_t(height);++y)
        std::memcpy(result.image.rgba8.data()+y*rowBytes,plane+y*stride,rowBytes);
    image.reset();
    if(colour.transform&&!skcms_Transform(result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,&colour.profile,
        result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),
        std::size_t(width)*std::size_t(height)))return Failure(DecodeError::unsupported_content);
    return result;
}
}
