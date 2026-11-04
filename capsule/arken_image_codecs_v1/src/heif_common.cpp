#include "heif_common.hpp"
#include "reader.hpp"
#include <libheif/heif_items.h>
#include <libheif/heif_properties.h>
#include <libheif/heif_sequences.h>
#include <skcms_public.h>
#include <cstring>
#include <string_view>

namespace arken::image::v1::detail::heif_shared {
namespace {
using Options=std::unique_ptr<heif_decoding_options,decltype(&heif_decoding_options_free)>;

constexpr std::size_t maximumProfileBytes=4u*1024*1024; // Same bound as the PNG iCCP owner.
constexpr std::uint32_t maximumItems=256, maximumTiles=256; // A 48 MP grid of 512x512 tiles is 192.

// Colour disposition decided before decoding; applied to the decoded straight-alpha RGBA8.
struct Colour {
    bool transform=false;
    skcms_ICCProfile profile{};
    std::vector<std::byte> icc; // skcms keeps pointers into the parsed bytes.
};

bool SdrTransfer(std::uint16_t transfer) noexcept {
    switch(transfer) {
        case heif_transfer_characteristic_ITU_R_BT_709_5:case heif_transfer_characteristic_unspecified:
        case heif_transfer_characteristic_ITU_R_BT_601_6:case heif_transfer_characteristic_IEC_61966_2_1:
        case heif_transfer_characteristic_ITU_R_BT_2020_2_10bit:case heif_transfer_characteristic_ITU_R_BT_2020_2_12bit:
            return true;
        default:return false; // PQ, HLG, linear, logarithmic, SMPTE 428 and the rest need a deliberate policy.
    }
}
bool AdmittedMatrix(std::uint16_t matrix) noexcept {
    // Linear Kr/Kb matrices (H.273 0-2, 4-9, 12) plus YCgCo-Re (16), which libheif implements.
    // Constant-luminance matrices (10, 13) would be decoded by libheif as if non-constant (its own
    // TODO), and 11, 14, 15, 17 and reserved values have no conversion; all reject explicitly.
    return matrix<=heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance?matrix!=3
        :matrix==heif_matrix_coefficients_chromaticity_derived_non_constant_luminance||matrix==16;
}
bool WideGamutPrimaries(std::uint16_t primaries,skcms_Matrix3x3& toXYZD50) noexcept {
    // D65 white point for both. Display P3: SMPTE EG 432-1. BT.2020: ITU-R BT.2020-2.
    if(primaries==heif_color_primaries_SMPTE_EG_432_1)
        return skcms_PrimariesToXYZD50(0.680f,0.320f,0.265f,0.690f,0.150f,0.060f,0.3127f,0.3290f,&toXYZD50);
    if(primaries==heif_color_primaries_ITU_R_BT_2020_2_and_2100_0)
        return skcms_PrimariesToXYZD50(0.708f,0.292f,0.170f,0.797f,0.131f,0.046f,0.3127f,0.3290f,&toXYZD50);
    return false;
}
// Without any colr box, libheif takes primaries, transfer, matrix and range from the coded
// stream (HEVC VUI or the AV1 sequence header colour config) and from nothing else. That
// signalling is only visible on a decoded picture, so one tile (the whole image when it is not a
// grid) is decoded in its native YCbCr form with libheif's NCLX passthrough. No colour
// description in the stream leaves the image untagged.
DecodeError BitstreamNclx(const heif_image_handle* handle,Nclx& nclx) {
    Options probe(heif_decoding_options_alloc(),heif_decoding_options_free);
    if(!probe)return DecodeError::allocation_failed;
    probe->ignore_transformations=1;
    probe->strict_decoding=1;
    probe->num_codec_threads=1;
    probe->output_image_nclx_profile_passthrough=1;
    heif_image* raw=nullptr;
    const auto status=heif_image_handle_decode_image_tile(handle,&raw,heif_colorspace_undefined,heif_chroma_undefined,probe.get(),0,0);
    Pixels tile(raw,heif_image_release);
    if(status.code!=heif_error_Ok||!tile)return Map(status);
    heif_color_profile_nclx* signalled=nullptr;
    const auto read=heif_image_get_nclx_color_profile(tile.get(),&signalled);
    nclx.reset(signalled);
    if(read.code!=heif_error_Ok&&read.code!=heif_error_Color_profile_does_not_exist)return Map(read);
    return DecodeError::none;
}
DecodeError DecideColour(const heif_image_handle* handle,Colour& colour) {
    const auto type=heif_image_handle_get_color_profile_type(handle);
    const bool icc=type==heif_color_profile_type_rICC||type==heif_color_profile_type_prof;
    heif_color_profile_nclx* raw=nullptr;
    const auto nclxStatus=heif_image_handle_get_nclx_color_profile(handle,&raw);
    Nclx nclx(raw,heif_nclx_color_profile_free);
    if(nclxStatus.code!=heif_error_Ok&&nclxStatus.code!=heif_error_Color_profile_does_not_exist)return Map(nclxStatus);
    if(!nclx&&!icc)if(const auto error=BitstreamNclx(handle,nclx);error!=DecodeError::none)return error;
    // Any HDR transfer signalled in NCLX rejects, even when an ICC profile would take precedence.
    if(nclx&&(!SdrTransfer(nclx->transfer_characteristics)||!AdmittedMatrix(nclx->matrix_coefficients)))return DecodeError::unsupported_content;
    if(icc) {
        const auto size=heif_image_handle_get_raw_color_profile_size(handle);
        if(size>maximumProfileBytes)return DecodeError::resource_limit;
        if(size<132)return DecodeError::invalid_layout;
        colour.icc.resize(size+2); // skcms documents two readable padding bytes.
        const auto status=heif_image_handle_get_raw_color_profile(handle,colour.icc.data());
        if(status.code!=heif_error_Ok)return Map(status);
        colour.icc[size]=colour.icc[size+1]=std::byte{0};
        const auto declared=(std::uint32_t(std::to_integer<std::uint8_t>(colour.icc[0]))<<24)|(std::uint32_t(std::to_integer<std::uint8_t>(colour.icc[1]))<<16)
            |(std::uint32_t(std::to_integer<std::uint8_t>(colour.icc[2]))<<8)|std::to_integer<std::uint8_t>(colour.icc[3]);
        if(declared!=size)return DecodeError::invalid_layout; // Same header rule as the PNG iCCP owner.
        if(!skcms_Parse(colour.icc.data(),size,&colour.profile))return DecodeError::invalid_layout;
        if(colour.profile.data_color_space!=skcms_Signature_RGB||skcms_GetInputChannelCount(&colour.profile)!=3)
            return DecodeError::unsupported_content;
        // The ICC profile owns primaries and transfer; the YCbCr matrix still comes from NCLX/VUI.
        // A profile skcms considers equal to sRGB is not re-transformed (no 8-bit rounding churn).
        colour.transform=!skcms_ApproximatelyEqualProfiles(&colour.profile,skcms_sRGB_profile());
        return DecodeError::none;
    }
    if(!nclx)return DecodeError::none; // Untagged: libheif's documented default interpretation, sRGB.
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
// The shared libheif instance decodes both HEVC and AV1, so each wrapper names its codec
// explicitly: an item is admitted when it is 'coded', or a 'grid' whose every 'dimg' tile is.
bool CodedAs(const heif_context* context,heif_item_id item,std::uint32_t coded) {
    const auto type=heif_item_get_item_type(context,item);
    if(type==coded)return true;
    if(type!=heif_fourcc('g','r','i','d'))return false;
    bool tiles=false;
    for(int index=0;index<int(maximumItems);++index) {
        std::uint32_t reference=0;heif_item_id* targets=nullptr;
        const auto count=heif_context_get_item_references(context,item,index,&reference,&targets);
        bool admitted=true;
        for(std::size_t i=0;i<count;++i)admitted=admitted&&heif_item_get_item_type(context,targets[i])==coded;
        heif_release_item_references(context,&targets);
        if(!count)return tiles;
        if(reference!=heif_fourcc('d','i','m','g'))continue;
        if(!admitted)return false;
        tiles=true;
    }
    return false;
}
// The item's transformative properties (irot, imir, clap) in application order, with values.
std::vector<int> Transforms(const heif_context* context,const heif_image_handle* handle) {
    const auto item=heif_image_handle_get_item_id(handle);
    const auto count=heif_item_get_transformation_properties(context,item,nullptr,0);
    std::vector<heif_property_id> ids(std::size_t(std::clamp(count,0,64)));
    const auto listed=heif_item_get_transformation_properties(context,item,ids.data(),int(ids.size()));
    const auto width=heif_image_handle_get_ispe_width(handle),height=heif_image_handle_get_ispe_height(handle);
    std::vector<int> signature{count};
    for(int i=0;i<listed;++i) {
        const auto id=ids[std::size_t(i)];
        const auto type=heif_item_get_property_type(context,item,id);
        signature.push_back(int(type));
        if(type==heif_item_property_type_transform_rotation)signature.push_back(heif_item_get_property_transform_rotation_ccw(context,item,id));
        else if(type==heif_item_property_type_transform_mirror)signature.push_back(int(heif_item_get_property_transform_mirror(context,item,id)));
        else if(type==heif_item_property_type_transform_crop) {
            int left=0,top=0,right=0,bottom=0;
            heif_item_get_property_transform_crop_borders(context,item,id,width,height,&left,&top,&right,&bottom);
            signature.insert(signature.end(),{left,top,right,bottom});
        }
    }
    return signature;
}
// libheif applies each item's own transformative properties, so an alpha auxiliary whose irot,
// imir or clap differ from the master's would silently be composited in another orientation
// (observed with an edited fixture). Alpha must carry the master's transforms, be coded with the
// wrapper's codec and be 8-bit: requesting RGBA8 would otherwise reduce a deeper alpha plane.
DecodeError Alpha(const heif_context* context,const heif_image_handle* handle,std::uint32_t coded) {
    if(!heif_image_handle_has_alpha_channel(handle))return DecodeError::none;
    const auto count=heif_image_handle_get_number_of_auxiliary_images(handle,0);
    if(count<0||std::uint32_t(count)>maximumItems)return DecodeError::resource_limit;
    std::vector<heif_item_id> ids(std::size_t(count)+1);
    const auto listed=heif_image_handle_get_list_of_auxiliary_image_IDs(handle,0,ids.data(),count);
    for(int i=0;i<listed;++i) {
        heif_image_handle* raw=nullptr;
        if(const auto status=heif_image_handle_get_auxiliary_image_handle(handle,ids[std::size_t(i)],&raw);status.code!=heif_error_Ok)return Map(status);
        Handle auxiliary(raw,heif_image_handle_release);
        const char* type=nullptr;
        if(const auto status=heif_image_handle_get_auxiliary_type(auxiliary.get(),&type);status.code!=heif_error_Ok)return Map(status);
        const std::string_view name=type?type:"";
        const bool alpha=name=="urn:mpeg:hevc:2015:auxid:1"||name=="urn:mpeg:mpegB:cicp:systems:auxiliary:alpha";
        heif_image_handle_release_auxiliary_type(auxiliary.get(),&type);
        if(!alpha)continue;
        if(!CodedAs(context,heif_image_handle_get_item_id(auxiliary.get()),coded))return DecodeError::unsupported_content;
        if(heif_image_handle_get_luma_bits_per_pixel(auxiliary.get())!=8)return DecodeError::unsupported_content;
        if(Transforms(context,auxiliary.get())!=Transforms(context,handle))return DecodeError::unsupported_content;
    }
    return DecodeError::none;
}
}

DecodeError Map(const heif_error& error) {
    switch(error.subcode) {
        case heif_suberror_Security_limit_exceeded:return DecodeError::resource_limit;
        case heif_suberror_End_of_data:return DecodeError::truncated;
        case heif_suberror_Unsupported_codec:case heif_suberror_Unsupported_image_type:
        case heif_suberror_Unsupported_color_conversion:case heif_suberror_Unsupported_data_version:
        case heif_suberror_Unsupported_item_construction_method:case heif_suberror_Unknown_color_profile_type:
        case heif_suberror_Unknown_NCLX_color_primaries:case heif_suberror_Unknown_NCLX_transfer_characteristics:
        case heif_suberror_Unknown_NCLX_matrix_coefficients:case heif_suberror_Unsupported_bit_depth:
        case heif_suberror_No_matching_decoder_installed:return DecodeError::unsupported_content;
        case heif_suberror_Invalid_image_size:case heif_suberror_Invalid_clean_aperture:
        case heif_suberror_Invalid_grid_data:return DecodeError::invalid_dimensions;
        default:break;
    }
    switch(error.code) {
        case heif_error_Unsupported_filetype:return DecodeError::wrong_magic;
        case heif_error_Unsupported_feature:case heif_error_Plugin_loading_error:return DecodeError::unsupported_content;
        case heif_error_Memory_allocation_error:return DecodeError::allocation_failed;
        case heif_error_Color_profile_does_not_exist:return DecodeError::unsupported_content;
        default:return DecodeError::invalid_layout;
    }
}

DecodeError Preflight(Bytes bytes,const DecodeOptions& options) {
    if(options.frameIndex||options.directionIndex||options.mipIndex)return DecodeError::selection_out_of_range;
    if(bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))return DecodeError::input_too_large;
    // Only the ISOBMFF signature is checked here; libheif's coarse file-type check reports the
    // common 'mif1' major brand as undecided, so item and codec admission is left to its parser.
    if(bytes.size()<12)return DecodeError::truncated;
    if(std::memcmp(bytes.data()+4,"ftyp",4)!=0)return DecodeError::wrong_magic;
    return DecodeError::none;
}

DecodeResult DecodeStill(Bytes bytes,const DecodeOptions& options,std::uint32_t codedItemType) {
    const auto* data=reinterpret_cast<const std::uint8_t*>(bytes.data());
    Library library;
    if(library.status.code!=heif_error_Ok)return Failure(DecodeError::allocation_failed);
    Context context(heif_context_alloc(),heif_context_free);
    if(!context)return Failure(DecodeError::allocation_failed);
    // Every libheif limit that bounds this decode is derived from the caller's DecodeOptions.
    auto limits=*heif_context_get_security_limits(context.get());
    const std::uint64_t planeBytes=options.maximumPixels*4;
    limits.max_image_size_pixels=options.maximumPixels;
    limits.max_number_of_tiles=maximumTiles;
    limits.max_items=maximumItems;
    limits.max_sequence_frames=1;
    limits.max_color_profile_size=maximumProfileBytes;
    limits.max_memory_block_size=std::clamp<std::uint64_t>(planeBytes*2,16ull*1024*1024,256ull*1024*1024);
    limits.max_total_memory=std::clamp<std::uint64_t>(planeBytes*8,64ull*1024*1024,1024ull*1024*1024);
    if(heif_context_set_security_limits(context.get(),&limits).code!=heif_error_Ok)return Failure(DecodeError::resource_limit);
    heif_context_set_max_decoding_threads(context.get(),0); // Tiles decode on the calling thread.
    auto status=heif_context_read_from_memory_without_copy(context.get(),data,bytes.size(),nullptr);
    if(status.code!=heif_error_Ok)return Failure(Map(status));
    if(heif_context_has_sequence(context.get()))return Failure(DecodeError::unsupported_content);
    const auto topLevel=heif_context_get_number_of_top_level_images(context.get());
    if(topLevel<1)return Failure(DecodeError::invalid_layout);
    if(std::uint32_t(topLevel)>maximumItems)return Failure(DecodeError::resource_limit);

    heif_image_handle* rawHandle=nullptr;
    status=heif_context_get_primary_image_handle(context.get(),&rawHandle);
    if(status.code!=heif_error_Ok)return Failure(Map(status));
    Handle handle(rawHandle,heif_image_handle_release);
    // Admitted primary items: a coded image of the wrapper's codec or a grid of such tiles.
    if(!CodedAs(context.get(),heif_image_handle_get_item_id(handle.get()),codedItemType))return Failure(DecodeError::unsupported_content);
    const auto luma=heif_image_handle_get_luma_bits_per_pixel(handle.get()),chroma=heif_image_handle_get_chroma_bits_per_pixel(handle.get());
    if(luma!=8||chroma!=8)return Failure(DecodeError::unsupported_content); // No high-bit-depth or HDR policy yet.
    if(heif_image_handle_is_premultiplied_alpha(handle.get()))return Failure(DecodeError::unsupported_content);
    // Signalled (post-transform) and coded (ispe) extents are both checked before any decoding.
    const auto width=heif_image_handle_get_width(handle.get()),height=heif_image_handle_get_height(handle.get());
    const auto codedWidth=heif_image_handle_get_ispe_width(handle.get()),codedHeight=heif_image_handle_get_ispe_height(handle.get());
    if(width<=0||height<=0||codedWidth<=0||codedHeight<=0)return Failure(DecodeError::invalid_dimensions);
    if(!Dimensions(std::uint32_t(width),std::uint32_t(height),options)
        ||!Dimensions(std::uint32_t(codedWidth),std::uint32_t(codedHeight),options))return Failure(DecodeError::invalid_dimensions);
    if(const auto error=Alpha(context.get(),handle.get(),codedItemType);error!=DecodeError::none)return Failure(error);
    Colour colour;
    if(const auto error=DecideColour(handle.get(),colour);error!=DecodeError::none)return Failure(error);

    Options decoding(heif_decoding_options_alloc(),heif_decoding_options_free);
    Nclx output(heif_nclx_color_profile_alloc(),heif_nclx_color_profile_free);
    if(!decoding||!output)return Failure(DecodeError::allocation_failed);
    decoding->ignore_transformations=0;
    decoding->convert_hdr_to_8bit=0;
    decoding->strict_decoding=1;
    decoding->num_codec_threads=1;
    // Explicit output tagging. For RGB output libheif converts only the YCbCr matrix and range
    // (taken from the input NCLX/VUI); it never converts primaries or transfer, which are handled
    // by the colour decision above.
    output->color_primaries=heif_color_primaries_ITU_R_BT_709_5;
    output->transfer_characteristics=heif_transfer_characteristic_IEC_61966_2_1;
    output->matrix_coefficients=heif_matrix_coefficients_ITU_R_BT_601_6;
    output->full_range_flag=1;
    decoding->output_image_nclx_profile=output.get();
    heif_image* rawImage=nullptr;
    status=heif_decode_image(handle.get(),&rawImage,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,decoding.get());
    Pixels image(rawImage,heif_image_release);
    if(status.code!=heif_error_Ok||!image)return Failure(Map(status));
    // The copy below uses only the decoded plane's own extent and stride, never a predecode size.
    const auto decodedWidth=heif_image_get_width(image.get(),heif_channel_interleaved);
    const auto decodedHeight=heif_image_get_height(image.get(),heif_channel_interleaved);
    if(decodedWidth!=width||decodedHeight!=height)return Failure(DecodeError::invalid_dimensions);
    if(heif_image_get_bits_per_pixel(image.get(),heif_channel_interleaved)!=32)return Failure(DecodeError::unsupported_content);
    std::size_t stride=0;
    const auto* plane=heif_image_get_plane_readonly2(image.get(),heif_channel_interleaved,&stride);
    const auto rowBytes=std::size_t(decodedWidth)*4;
    if(!plane||stride<rowBytes)return Failure(DecodeError::invalid_layout);

    DecodeResult result;
    result.image.width=std::uint32_t(decodedWidth);result.image.height=std::uint32_t(decodedHeight);
    // Primary-only extraction is explicit: a collection reports every top-level image it holds.
    result.image.frameCount=std::uint32_t(topLevel);
    result.image.rgba8.resize(rowBytes*std::size_t(decodedHeight));
    for(std::size_t y=0;y<std::size_t(decodedHeight);++y)
        std::memcpy(result.image.rgba8.data()+y*rowBytes,plane+y*stride,rowBytes);
    image.reset();
    if(colour.transform&&!skcms_Transform(result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,&colour.profile,
        result.image.rgba8.data(),skcms_PixelFormat_RGBA_8888,skcms_AlphaFormat_Unpremul,skcms_sRGB_profile(),
        std::size_t(decodedWidth)*std::size_t(decodedHeight)))return Failure(DecodeError::unsupported_content);
    return result;
}
}
