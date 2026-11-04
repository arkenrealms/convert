#pragma once
#include "arken_image_codecs_v1/api.hpp"
#include <libheif/heif.h>
#include <cstdint>
#include <memory>

// libheif plumbing shared by the HEIC (heif.cpp) and AVIF (avif.cpp) wrappers over the one
// combined libheif instance. Everything here is codec-neutral except the explicit coded item
// type each wrapper admits; container, limits, colour, alpha and transform policy are identical.
namespace arken::image::v1::detail::heif_shared {
struct Library {
    heif_error status;
    Library() noexcept : status(heif_init(nullptr)) {}
    ~Library() { if(status.code==heif_error_Ok)heif_deinit(); }
    Library(const Library&)=delete;
    Library& operator=(const Library&)=delete;
};
using Context=std::unique_ptr<heif_context,decltype(&heif_context_free)>;
using Handle=std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)>;
using Pixels=std::unique_ptr<heif_image,decltype(&heif_image_release)>;
using Nclx=std::unique_ptr<heif_color_profile_nclx,decltype(&heif_nclx_color_profile_free)>;

constexpr std::uint32_t hevcItem=heif_fourcc('h','v','c','1');
constexpr std::uint32_t av1Item=heif_fourcc('a','v','0','1');

DecodeError Map(const heif_error& error);
// Format-independent checks that run before any brand decision: frame selection, input size and
// the ISOBMFF 'ftyp' signature. Returns DecodeError::none when the bytes may be parsed.
DecodeError Preflight(std::span<const std::byte> bytes,const DecodeOptions& options);
// Parses the container under limits derived from options and decodes its primary still image to
// straight-alpha RGBA8. The primary item, every grid tile and every alpha auxiliary must be coded
// as codedItemType; anything else is unsupported_content. Callers run Preflight and their own
// brand gate first.
DecodeResult DecodeStill(std::span<const std::byte> bytes,const DecodeOptions& options,std::uint32_t codedItemType);
}
