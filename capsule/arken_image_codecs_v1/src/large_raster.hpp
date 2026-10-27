#pragma once
#include "arken_image_codecs_v1/api.hpp"

namespace arken::image::v1::detail {
// Called only after the shared raster owner has admitted format, metadata and
// dimensions. This loader has an immutable allocation policy, not global state.
DecodeResult DecodeLargeRasterPixels(std::span<const std::byte>,std::uint32_t width,std::uint32_t height);
}
