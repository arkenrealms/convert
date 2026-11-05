#include "heif.hpp"
#include "heif_common.hpp"
#include "reader.hpp"

namespace arken::image::v1::detail {
DecodeResult DecodeHeif(Bytes bytes,const DecodeOptions& options) {
    if(const auto error=heif_shared::Preflight(bytes,options);error!=DecodeError::none)return Failure(error);
    const auto brand=heif_read_main_brand(reinterpret_cast<const std::uint8_t*>(bytes.data()),static_cast<int>(bytes.size()));
    if(brand==heif_brand2_avif||brand==heif_brand2_avis)return Failure(DecodeError::unsupported_content); // AVIF is not this wrapper's format.
    // Admitted primary items: a coded HEVC image or a grid of HEVC tiles the decoder assembles.
    return heif_shared::DecodeStill(bytes,options,heif_shared::hevcItem);
}
}
