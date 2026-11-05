# Still-AVIF codec wrapper: profile, policies and qualification state

Status: native wrapper (`src/avif.cpp`: `DecodeImage(Format::avif, ...)` and
`EncodeImage(Format::avif, ...)`) with its native qualification test
(`tests/avif_tests.cpp`, ctest `ArkenImageCodecsV1.Avif`). AVIF is **not** admitted to any product
surface: it is absent from `source_formats.hpp` (where it stays preview-only), from
`SupportsStandaloneInput`/output, the `conversion_files.v1` schema, the standalone MCP commands
and the README. Nothing here is a release, a browser result or a conversion command.

Build: compiled only with `ARKEN_IMAGE_CODECS_V1_HEIF` (same define, `ARKEN_IMAGE_HEIF_DECODER`, as
HEIC), over the one combined libheif instance `libheif-1.23.7-hevc-av1-v1`
(`arken::external_heif_v1`), whose AV1 side is libaom 3.15.3 (`libaom-3.15.3-v1`: generic C,
no SIMD/assembly, single thread, 8-bit only, decode size limit 8192, `-ffp-contract=off`, no
Perl at build time). Without the define, `Format::avif` decodes as `unsupported_content` and
encodes as `unsupported_format`. The dependency milestone is `tests/avif_dependencies`
(avif-deps-1); its decoded fingerprints are reproduced exactly by this wrapper.

## 1. Shared implementation with HEIC

`src/heif_common.hpp/.cpp` hold everything both wrappers share, moved verbatim from the HEIC
wrapper: libheif library/context RAII, `heif_error` -> `DecodeError` mapping, security limits
derived from `DecodeOptions`, `Preflight` (frame selection, size, `ftyp` signature), the colour
decision (ICC precedence, NCLX policy, bitstream colour fallback via a passthrough tile probe),
alpha/transform consistency, primary selection with `frameCount`, coded/signalled extent checks
before decoding, and the decoded-extent/stride-safe copy. The only parameter is the coded item
type each wrapper admits: HEIC `hvc1`, AVIF `av01` (primary item, every `dimg` grid tile and
every alpha auxiliary). The HEIC test (`ArkenImageCodecsV1.Heif`, fingerprint
`9563ad71245621e0`) passes unchanged after the refactor.

## 2. Decode profile

| Aspect | Admitted | Rejected (error) |
| --- | --- | --- |
| Container | ISOBMFF whose `ftyp` lists the `avif` brand (major brand may be `avif` or `mif1`) | no `ftyp` (`wrong_magic`); no `avif` compatible brand, i.e. HEIC/HEIF files (`unsupported_content`); `avis` major brand, image tracks (`unsupported_content`); the low-overhead `mif3`/`mini` reduced-header form (`unsupported_content`) |
| Primary item | `av01`, or `grid` whose `dimg` tiles are all `av01` | `hvc1` and every other codec, mixed grids (`unsupported_content`) |
| Selection | primary only, `frameCount` = number of top-level images; thumbnails never chosen | `frameIndex`/`directionIndex`/`mipIndex` != 0 (`selection_out_of_range`) |
| Bit depth | 8-bit luma and chroma; 8-bit `av01` alpha auxiliary | anything else (`unsupported_content`); libaom is also built without high-bit-depth support |
| Alpha | straight alpha auxiliary with the master's transforms | premultiplied alpha, mismatched transforms |
| Transforms | `clap`, `irot`, `imir` applied by libheif in `ipma` order; Exif orientation never read | decoded extent different from the signalled extent (`invalid_dimensions`) |
| Colour | SDR exactly as HEIC (HEIF_NOTES.md 2.4): ICC first, else NCLX from `colr`, else the AV1 sequence header colour config; identity (GBR) matrix admitted | HDR transfers, unadmitted primaries/matrices, non-RGB ICC |
| Limits | same derivation as HEIC from `DecodeOptions`; `largeRaster` profile rejected (`resource_limit`) | |

`mini` differs deliberately from HEIC, which admits it: the reduced-header AVIF form was not
requested for this profile, its only pinned AVIF fixtures are `simple_osm_tile_*`/`mini_size_zero`,
and it carries no compatible-brand list to gate on.

## 3. Encode policy

`EncodeOptions` gains `bool avifLossless=true` and `std::uint32_t avifQuality=60` (1-100; any
other value is `invalid_options` for every format, like `jpegQuality`).

- **Lossless (default):** identity (GBR) matrix, 4:4:4, full range, AV1 lossless
  (`heif_encoder_set_lossless`). Exact RGBA is reproduced by `DecodeImage(Format::avif)`,
  including RGB values under fully transparent pixels.
- **Lossy:** `avifQuality` goes to `heif_encoder_set_lossy_quality` (libheif's AV1 quality
  mapping to a libaom cq-level, which depends on the tune libheif selects), BT.601 matrix, **4:2:0** chroma (chosen as the
  conventional AVIF photo default and the smaller output; 4:4:4 remains available only through
  lossless today), full range, libheif's automatic tune for the pinned libaom.
  **Alpha is always coded losslessly** (`lossless-alpha`, alpha q 0): alpha is coverage and the
  test proves it exact.
- **Alpha** is written as an `av01` alpha auxiliary only when some pixel is not opaque.
- **Tagging:** NCLX `colr` with primaries 1 (BT.709), transfer 13 (sRGB), matrix 0 (lossless) or
  6 (lossy), full range. No ICC, Exif or XMP is written (no metadata is carried).
- **Determinism and bounds:** one encoder thread, fixed speed 6; identical input gives identical
  bytes (tested). `maximumOutputBytes` is enforced on the written file (`output_limit`, no partial
  bytes); pixel and dimension ceilings are the bounded profile's (8 Mi pixels, 8192), and
  `largeRaster` / JPEG-only alpha policies are `invalid_options`.

## 4. Qualified on this host (Linux x86-64, GCC 13.3, native only)

`ArkenImageCodecsV1.Avif` (1,206 checks, about 5 s):

- `examples/example.avif`: 800x533, `frameCount` 1, opaque, RGBA FNV-1a-64
  **`fa6ee36f0fba9dfd`** (milestone parity); repeated decode identical; PNG round trip exact;
  pixel, dimension, frame-selection, source-byte and `largeRaster` limits typed.
- `tests/data/clap_cropped_irot_imir.avif`: 64x64, **`9a67e30862ebb142`** (milestone parity);
  differs from `clap_cropped.avif` (transforms visible).
- Truncation: the only decodable prefix is the whole file (113,604 bytes); every shorter cut
  tried rejects with no pixels; no prefix yields a different image. Header corruption sweep
  (bytes 4-640) and payload corruption sweep never leak a partial or mis-sized image.
- Rejections: PNG bytes `wrong_magic`; `example.heic` as AVIF and `example.avif` as HEIF
  `unsupported_content`; the three `mini` fixtures and corpus `avif32-mini.heif`
  `unsupported_content`; HEVC/AVC corpus files `unsupported_content`; `clap_oversized_ispe_*`
  and `meta_size_zero.avif` typed failures; the whole libheif fuzzing corpus without leaks.
  Corpus `avif32.heif` (ftyp avif/mif1/miaf) decodes, 32x32 opaque.
- Encoder: lossless byte-exact round trips at 1x1, 2x3, 17x9, 61x37, 64x64, 255x3, 3x255 and
  300x200 with opaque noise, every alpha level (RGB under alpha 0), a binary mask and a fully
  transparent image; output rejected by the HEIC wrapper. Lossy on a 160x120 smooth image with
  noisy alpha: q20 33.5 dB, q60 37.4 dB, q90 38.1 dB RGB PSNR, alpha exact, size and PSNR
  increasing with quality (thresholds 26/32 dB). Opaque input writes no alpha. Quality 0/101/1000
  rejected, 1 and 100 accepted; exact output limit accepted, one byte less `output_limit`.

## 5. NOT qualified

- **Independent interoperability:** no outside encoder's AVIF beyond the libheif test files, and
  no outside decoder (browser, libavif, dav1d) has opened this encoder's output.
- **Alpha decode fixtures:** the pinned trees contain no non-`mini` AVIF with alpha; alpha decode
  is proven only on this encoder's own output.
- **Grids:** no AV1 grid fixture exists in the pinned trees; grid admission is the shared HEIC
  code path, untested for AV1.
- **10/12-bit, HDR, gain maps, `clli`/`mdcv`:** rejected, not converted.
- **Colour on real AVIF:** ICC/P3/BT.2020 handling is shared code qualified on HEIC fixtures,
  not on AVIF files; no AVIF with ICC or wide-gamut NCLX was decoded.
- **Real-world files:** phone/camera/browser-exported AVIF, large images (12 MP decode or encode
  time and memory in pure-C libaom), multi-layer (`a1lx`/`lsel`) and progressive AVIF.
- **Browser/Wasm:** not built; native/Wasm parity unproven.
- **macOS/Clang:** not built here.
- **Distribution:** libheif LGPL obligations (as for HEIC) plus libaom BSD-2 and the AOM Patent
  License 1.0 notices.
- **Product:** no catalogue, schema, command, UI, cancellation or persistence work.
