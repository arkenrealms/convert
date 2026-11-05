#pragma once
#include "arken_image_codecs_v1/encode.hpp"

namespace arken::image::v1::detail {
// Memory-only still AVIF import and export over the one combined libheif instance (libaom 3.15.3,
// generic C, 8-bit only, single thread). Shares container, limit, colour, alpha and transform
// policy with the HEIC wrapper (heif_common.cpp); see AVIF_NOTES.md.
//
// Decode admitted profile (every other variant rejects with a typed DecodeError):
//  - an ISOBMFF file whose ftyp lists the 'avif' brand. HEIC/HEIF files without it, the 'avis'
//    sequence brand, image tracks and the low-overhead 'mif3'/'mini' reduced-header form reject
//    (unsupported_content); bytes without 'ftyp' are wrong_magic.
//  - the primary item, which must be 'av01' or a grid of 'av01' tiles the decoder assembles;
//    Image::frameCount reports the number of top-level images (primary-only extraction is
//    explicit). Thumbnails are never chosen.
//  - 8-bit luma and chroma; an alpha auxiliary must be 'av01'-coded, 8-bit, straight and carry the
//    master's transformative properties. Alpha is preserved in RGBA8.
//  - irot/imir/clap applied by libheif in ipma order; Exif orientation is never consulted.
//  - SDR colour exactly as for HEIC: ICC precedence, else NCLX from colr, else the AV1 sequence
//    header colour config; P3/BT.2020 converted to sRGB; HDR transfers and unadmitted
//    matrices/primaries reject. The identity (GBR) matrix used by lossless AVIF is admitted.
//  - signalled and coded (ispe) extents must satisfy DecodeOptions before decoding.
DecodeResult DecodeAvif(std::span<const std::byte>,const DecodeOptions&);

// Encode policy (EncodeOptions::avifLossless / avifQuality):
//  - lossless (default): identity (GBR) matrix, 4:4:4, full range, AV1 lossless; RGBA is
//    reproduced exactly by DecodeAvif, including RGB under fully transparent pixels.
//  - lossy: avifQuality 1-100 passed to libheif's AV1 quality mapping, BT.601 matrix, 4:2:0,
//    full range. Alpha is always coded losslessly.
//  - alpha: written as an 'av01' alpha auxiliary only when some pixel is not opaque.
//  - output tagged with an NCLX colr box: BT.709 primaries, sRGB transfer, matrix as above, full
//    range. No ICC, Exif or XMP is written. 8-bit only.
//  - deterministic: one encoder thread, fixed speed 6, no timestamps; the caller's
//    maximumOutputBytes bounds the result (output_limit).
EncodeResult EncodeAvif(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba8,
    bool lossless,std::uint32_t quality,std::uint64_t maximumOutputBytes);
}
