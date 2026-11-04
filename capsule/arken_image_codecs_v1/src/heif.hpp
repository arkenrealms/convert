#pragma once
#include "arken_image_codecs_v1/api.hpp"

namespace arken::image::v1::detail {
// Memory-only HEIC/HEIF still-image import over the pinned libheif + libde265 decoder.
// Admitted profile (every other variant rejects with a typed DecodeError; see HEIF_NOTES.md):
//  - the container's primary item, which must be a coded HEVC image ('hvc1') or a grid the
//    decoder assembles from its tiles. Sequences (image tracks) are rejected. A collection of
//    several top-level images decodes only its primary item and says so: Image::frameCount
//    carries the number of top-level images, so a consumer can refuse or label a partial
//    conversion instead of mistaking the primary for the whole file. Thumbnails are never chosen.
//  - 8-bit luma and chroma; an alpha auxiliary must be 8-bit, straight (non-premultiplied) and
//    carry the same transformative properties as its master. Alpha is preserved in RGBA8.
//  - container transforms (irot, imir, clap) are applied by the decoder in ipma order; Exif
//    orientation is never consulted, so it is never applied twice. The decoded extent must equal
//    the signalled post-transform extent or the image is rejected.
//  - SDR colour only. An RGB ICC profile (rICC/prof) takes precedence for primaries and transfer
//    and is converted to sRGB through skcms unless skcms finds it equal to sRGB. Otherwise the
//    NCLX from the colr box, or without any colr box the HEVC VUI, decides: BT.709/unspecified
//    primaries are presented as sRGB; Display P3 and BT.2020 primaries are converted to sRGB with
//    the sRGB transfer curve; other primaries reject. Transfers other than BT.709/BT.601/BT.2020
//    SDR, sRGB and unspecified (PQ, HLG, linear, logarithmic, ...) reject, also beside an ICC
//    profile. Constant-luminance, ICtCp and reserved matrices reject. With neither a colr box nor
//    a VUI colour description the image follows libheif's documented default, sRGB.
//  - the signalled (post-transform) and coded (ispe) sizes must both satisfy DecodeOptions limits
//    before any decoding, and libheif's security limits are derived from the same options.
DecodeResult DecodeHeif(std::span<const std::byte>,const DecodeOptions&);
}
