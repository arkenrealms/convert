#pragma once
#include "arken_image_codecs_v1/api.hpp"

namespace arken::image::v1::detail {
// Memory-only HEIC/HEIF still-image import over the pinned libheif + libde265 decoder.
// Admitted profile (every other variant rejects with a typed DecodeError):
//  - the container's primary HEVC still image; derived grids and alpha auxiliaries are assembled
//    by the owning decoder. Sequences (image tracks) are rejected. A collection of several
//    top-level images decodes only its primary item and says so: Image::frameCount carries the
//    number of top-level images, so a consumer can refuse or label a partial conversion instead
//    of mistaking the primary for the whole file.
//  - 8-bit luma and chroma, straight (non-premultiplied) alpha preserved in RGBA8.
//  - container transforms (irot, imir, clap) are applied by the decoder; Exif orientation is
//    not consulted, so it is never applied twice. The decoded extent must equal the signalled
//    post-transform extent or the image is rejected.
//  - SDR colour only. An RGB ICC profile (rICC/prof) takes precedence and is converted to sRGB
//    through the existing skcms owner; otherwise NCLX primaries BT.709 are presented as sRGB and
//    Display P3 / BT.2020 primaries are converted to sRGB with the sRGB transfer curve. Transfer
//    characteristics PQ, HLG, linear and logarithmic reject. An untagged image follows the
//    decoder's declared default interpretation, sRGB, which is the accepted dependency parity
//    sample's own disposition.
//  - the declared pre- and post-transform sizes must both satisfy DecodeOptions limits before any
//    decoding, and libheif's security limits are derived from the same options.
DecodeResult DecodeHeif(std::span<const std::byte>,const DecodeOptions&);
}
