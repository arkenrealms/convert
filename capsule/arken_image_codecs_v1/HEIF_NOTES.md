# HEIC/HEIF decoder wrapper: profile, policies and qualification state

Status: native wrapper (`src/heif.cpp`, `DecodeImage(Format::heif, ...)`) with its native
qualification test (`tests/heif_tests.cpp`, ctest `ArkenImageCodecsV1.Heif`). HEIC/HEIF is
**not** admitted to any product surface: it is absent from `source_formats.hpp`, from
`SupportsStandaloneInput`, from the `conversion_files.v1` schema, from the standalone MCP
commands and from the README. Nothing here is a release, a browser result, or a conversion
command. The dependency milestone it builds on is convert/r6364 (libheif 1.23.7 + libde265 1.1.3,
HEVC decoding only, no dynamic plugins, `-ffp-contract=off`).

The decoder is compiled into `arken_image_codecs_v1` only when `ARKEN_IMAGE_CODECS_V1_HEIF` is ON
(the root `CMakeLists.txt` of this repository sets it ON). The root `lib.manifest.json` resolves
one combined libheif instance privately, `libheif-1.23.7-hevc-av1-v1` (target
`arken::external_heif_v1`): the same archive and patches as the HEVC milestone, the AV1
milestone's root `CMakeLists.txt` replacement (the HEVC replacement plus a pinned-libaom block
guarded by `WITH_AOM_*`), libde265 1.1.3 and libaom 3.15.3 (decoder and encoder). Two libheif
instances cannot share a CMake graph because both materialise the upstream target `heif`.
libde265 and libaom also both define an upstream maintainer target `dist`, so the combined
instance depends on `libde265-1.1.3-v2`: the v1 recipe plus a root `CMakeLists.txt` replacement
that defines `dist` only in a top-level libde265 build (no compiled source changes). The
dependency milestones `tests/heif_dependencies` and `tests/avif_dependencies` keep their
single-codec recipes. Because the shared instance can decode AV1, this wrapper admits items by
codec explicitly (`hvc1`, or a grid whose tiles are all `hvc1`); AVIF belongs to its own wrapper
(`src/avif.cpp`, see AVIF_NOTES.md). The container, limit, colour, alpha and transform logic
described below lives in `src/heif_common.cpp` and is shared verbatim by both wrappers; only the
admitted coded item type differs.
The accepted HEIC fingerprint is unchanged with the AV1-enabled instance.

Any in-process caller of `DecodeImage` can reach the HEIC decoder in this build; the conversion
owners refuse `Format::heif` because no admission predicate lists it.

## 1. Admitted profile

Input is an in-memory byte span plus explicit `DecodeOptions`; nothing reads a path. Output is the
existing tightly packed, top-left, straight-alpha RGBA8 `Image`, or a typed `DecodeError` with no
pixels and zero dimensions.

| Aspect | Admitted | Rejected (error) |
| --- | --- | --- |
| Container | ISOBMFF with `ftyp`, parsed by libheif, including the low-overhead `mini` form | missing `ftyp` (`wrong_magic`); `avif`/`avis` major brand, image sequences/tracks (`unsupported_content`) |
| Selected item | the primary item (`pitm`) only | `frameIndex`/`directionIndex`/`mipIndex` other than 0 (`selection_out_of_range`) |
| Primary item type | `hvc1`, or `grid` whose `dimg` tiles are all `hvc1` (assembled by libheif) | `iden`, `iovl`, `av01`, a grid with any non-`hvc1` tile, `avc1`, `vvc1`, `jpeg`, `j2k1`, `unci` and anything else (`unsupported_content`) |
| Bit depth | 8-bit luma and chroma; 8-bit HEVC-coded alpha auxiliary | anything else (`unsupported_content`) |
| Alpha | straight alpha auxiliary carrying the master's transforms | premultiplied alpha; alpha whose `irot`/`imir`/`clap` differ from the master's (`unsupported_content`) |
| Transforms | `clap`, `irot`, `imir` applied by libheif in `ipma` order | decoded extent different from the signalled post-transform extent (`invalid_dimensions`) |
| Colour | SDR, see section 2.4 | HDR transfers, unadmitted primaries/matrices, non-RGB ICC |
| Collections | primary decoded; `Image::frameCount` = number of top-level images | more than 256 top-level images (`resource_limit`) |

Thumbnails, depth maps, gain maps and other auxiliary images are never selected or composited
(the gain-map case is discussed in 2.5).

## 2. Policy decisions and reasons

### 2.1 Image selection

Handoff section 7 requires a sequence or collection not to be silently treated as converted when
only one image was decoded. The wrapper decodes the primary item only and reports
`Image::frameCount` as the number of top-level images. `examples/example.heic` holds two
top-level `hvc1` images, so its decode reports `frameCount == 2`; a consumer must refuse or label
such a conversion as primary-only. Image tracks (`heif_context_has_sequence`) reject outright.
File suffixes play no role; the caller states `Format::heif` and libheif parses the bytes.

Consumer caution: other codecs use `frameCount` for animation frames (GIF converts its first frame
by an explicit `firstFrameOnly` catalogue flag). A future HEIF admission must decide its own
`frameCount > 1` behaviour rather than reuse the GIF wording.

### 2.2 Bounded context and limits mapping

Every libheif security limit that bounds the decode is set per call from `DecodeOptions`
(`maximumPixels` = P):

| libheif limit | Value | At default P = 8 Mi | At standalone P = 1 Mi |
| --- | --- | --- | --- |
| `max_image_size_pixels` | P | 8,388,608 | 1,048,576 |
| `max_number_of_tiles` | 256 (a 48 MP 512x512 grid has 192) | 256 | 256 |
| `max_items` | 256 | 256 | 256 |
| `max_sequence_frames` | 1 | 1 | 1 |
| `max_color_profile_size` | 4 MiB (PNG iCCP bound) | 4 MiB | 4 MiB |
| `max_memory_block_size` | clamp(8P bytes, 16 MiB, 256 MiB) | 64 MiB | 16 MiB |
| `max_total_memory` | clamp(32P bytes, 64 MiB, 1 GiB) | 256 MiB | 64 MiB |

`max_total_memory` covers libheif's own accounted allocations only, not libde265's internal
picture buffers or the returned RGBA vector; no peak-memory measurement exists yet. Tiles decode
on the calling thread (`heif_context_set_max_decoding_threads(ctx, 0)`) and libde265 runs with
one codec thread, matching the single-thread browser path. `strict_decoding` is on.

Before any pixel decoding, both the signalled post-transform size and the coded `ispe` size must
satisfy `maximumPixels` and `maximumDimension`. libheif additionally tightens its pixel limit to
the `ispe` extent plus one coding unit for the codec. After decoding, the plane's own width,
height and stride are used for the copy and must equal the signalled extent. `DecodeImage`
rejects `RasterProfile::largeRaster` for HEIF (`resource_limit`): HEIF has no resident-image
profile.

### 2.3 Orientation

Container transforms (`clap`, `irot`, `imir`) are applied by libheif
(`ignore_transformations = 0`) in `ipma` order. Neither the wrapper nor libheif's decode path
reads Exif orientation (libheif uses orientation codes only in its encoder), so no orientation is
applied twice. Callers must not apply Exif orientation to this output either.

### 2.4 Colour (SDR only)

libheif converts YCbCr to RGB using the input matrix and range (from the `colr` NCLX box, or from
the HEVC VUI when there is no `colr` box). It never converts primaries or transfer for RGB
output. The wrapper passes an explicit output NCLX (BT.709 primaries, sRGB transfer, BT.601
matrix, full range) so libheif's choice does not depend on its NULL-means-sRGB default; only the
matrix/range conversion results from it. Primaries and transfer are then handled as follows:

1. **ICC takes precedence.** An `rICC`/`prof` profile must be at most 4 MiB, at least 132 bytes,
   have a header size equal to its length (the PNG iCCP owner's rule), parse in skcms, and be an
   RGB profile with three input channels; otherwise `invalid_layout` / `unsupported_content`.
   It is converted to sRGB with skcms (`skcms_Transform`, straight alpha). A profile skcms
   reports as approximately equal to sRGB is not transformed, which avoids 8-bit rounding churn
   (the PNG owner always transforms; this difference is deliberate).
2. **NCLX** (from `colr`, else from the HEVC VUI): transfer must be BT.709, BT.601, BT.2020
   10/12-bit SDR, sRGB or unspecified; PQ, HLG, linear, logarithmic and every other transfer
   reject (`unsupported_content`), also when an ICC profile is present. Matrices must be H.273
   0-2, 4-9, 12 or 16; constant-luminance matrices (10, 13), which libheif would decode as
   non-constant, and 11, 14, 15, 17 and reserved values reject.
3. Without ICC: BT.709 or unspecified primaries are presented as sRGB (BT.709/BT.601/BT.2020 SDR
   transfers are treated as the sRGB curve, the conventional display-referred presentation).
   Display P3 (SMPTE EG 432-1) and BT.2020 primaries are converted to sRGB through skcms with a
   profile built from those primaries, a D65 white point and the sRGB transfer function. Other
   primaries (BT.601 525/625, DCI-P3 with its DCI white, and the rest) reject.
4. **Untagged.** With no `colr` box the VUI is the only colour signalling libheif uses, and it is
   visible only on a decoded picture. The wrapper therefore decodes one tile (the whole image when
   it is not a grid) in native YCbCr with libheif's NCLX passthrough and applies rule 2/3 to the
   VUI values. Only when the VUI also has no colour description does the image follow libheif's
   documented default, sRGB. Cost: such files decode one tile twice. `examples/example.heic` is in
   this case (VUI 2/2/2, limited range), which is also the accepted dependency sample's own
   disposition, so its fingerprint is unchanged.
5. With an ICC profile but no `colr` NCLX, the VUI is not probed; libheif still uses the VUI
   matrix/range, but a constant-luminance VUI matrix in that combination is not detected.

### 2.5 HDR and gain maps

There is no HDR policy. Every high-bit-depth image rejects at the 8-bit gate before decoding, and
HDR transfers reject in rule 2, so no highlight is clipped and no tone curve is invented.

Gain maps (decision confirmed by the coordinator): the wrapper ignores the gain-map auxiliary
(for example Apple's `urn:com:apple:photo:2020:aux:hdrgainmap`) and returns the stored SDR base
image. That is neither clipping nor tone mapping, and the base rendition is what the format
defines for SDR consumers. This disposition is not yet exercised on a real file. libheif
1.23.7 has no `tmap` (ISO 21496-1) image item; a `tmap` primary is not `hvc1`/`grid` and rejects.

### 2.6 Arithmetic

The `-ffp-contract=off` libheif profile and the full HEVC in-loop filters (deblocking and SAO)
from the dependency milestone are unchanged. No comparison tolerance was added to the accepted
fingerprint.

## 3. What is qualified on this host (Linux, GCC 13, native only)

`ArkenImageCodecsV1.Heif` asserts, against the pinned libheif source tree:

- **Accepted sample parity.** `examples/example.heic` decodes to 1280x854, 8-bit, opaque, with
  `frameCount == 2` and RGBA FNV-1a-64 fingerprint `9563ad71245621e0`, the value the dependency
  milestone observed on macOS, browser Wasm and this host. Repeat decodes are identical; a PNG
  round trip through the existing encoder is pixel-exact.
- **Limits.** A 1,048,576-pixel limit and a 1000-pixel dimension limit reject the sample with
  `invalid_dimensions`; `maximumSourceBytes`, non-zero frame selection and the large-raster
  profile reject.
- **Truncation and corruption.** The shortest prefix that still decodes the complete primary is
  334,661 of 718,114 bytes; every prefix tested either decodes exactly the full primary or
  rejects with no pixels, and the tested cuts below that prefix (1, 64 and 1024 bytes short, half
  and an eighth) reject. Single-bit flips at every third offset of the first 640 bytes never leak
  pixels. PNG bytes reject as `wrong_magic`; `examples/example.avif` rejects as
  `unsupported_content` although the shared instance has an AV1 decoder.
- **Fixtures from the pinned trees.** `colors-no-alpha`, `colors-with-alpha` (alpha kept, colour
  equal to the opaque fixture), primaries chosen over 64x64 thumbnails (72x72), `hevc32.heif`
  (clap 64->32), `hevc32-mini.heif` and `lightning_mini.heif` (`mini` container),
  `clap_cropped.heic` (256->64), `conformance_window_padding.heic` (clap then irot, 1x1).
  `avc32`, `avif32`, `avif32-mini`, `vvc32`, `jpeg32`, `j2k32`, uncompressed and generically
  compressed items reject as `unsupported_content`. A minimised iPhone-style layout (4032x3024
  grid with `irot`, ICC-tagged 320x240 primary whose coded data does not decode) rejects with no
  pixels.
  Every corpus file is handled without a crash or leaked pixels.
- **Orientation (asymmetric, spec-derived expectations).** No pinned fixture has a decodable
  rotation or mirror, so variants of `rainbow-451x461.heic` (distinct saturated corners) are made
  in memory by overwriting whole property boxes in place (sizes unchanged). The expected result
  is computed by the test from the upright decode using ISO/IEC 23008-12 semantics: `irot` turns
  anti-clockwise by angle x 90 degrees; `imir` axis 0 exchanges top and bottom, axis 1 exchanges
  left and right; properties apply in `ipma` order. Checked: the clap selects the top-left
  451x461 of the 452x462 coded picture exactly; `irot` 0/90/180/270 and `imir` 0/1 after the
  clap; `irot`+`imir` and `imir`+`irot` on the coded picture; output extents swap for 90/270. Each
  decode must match its expected orientation and be far (> 64 levels) from every other visibly
  different dihedral orientation. On the even-sized coded picture the match is exact; after the
  odd-sized clap the maximum difference is 2 levels because libheif transforms the 4:2:0 planes
  before chroma upsampling. Alpha rotates with colour exactly when both items carry `irot`; an
  alpha item without the master's `irot` rejects (libheif would otherwise composite an upright
  alpha under rotated colour).
- **Declared versus coded size.** An `ispe` of 4000x4000 behind the 451x461 clap rejects before
  decoding (`invalid_dimensions`); `ispe` 300x300 for a 452x462 coded picture rejects at
  libheif's tightened limit (`resource_limit`); `ispe` 512x512 rejects at libheif's decoded-size
  check (`invalid_dimensions`).
- **Colour.** The fixture's ICC (GIMP built-in sRGB) is not transformed and equals the decode
  with the `colr` box removed (VUI 1/13/6 full range, read by the bitstream probe). The same
  pixels declared Display P3 through edited ICC colorants and through NCLX primaries 12 agree
  within 1 level (asserted: at most 2), and move a P3 (255,64,20) to sRGB (255,28,0), the direction colour science
  predicts; BT.2020 primaries change the image; BT.709 transfer equals sRGB presentation. PQ,
  HLG, linear, logarithmic, BT.601-625 primaries, DCI-P3 primaries, BT.2020 constant luminance and
  ICtCp reject. ICC plus a P3 NCLX equals the ICC-only decode (ICC precedence); ICC plus a PQ NCLX
  rejects; a GRAY ICC and an ICC whose header size disagrees reject.

## 4. What is NOT qualified

- **A real phone photo.** No real phone HEIC (multi-tile grid, `irot`, Display P3 ICC, Exif,
  thumbnail, possibly gain map) has been decoded, and no output has been compared with an
  independent reference application. Grid assembly is untested on any decodable fixture.
- **Independent orientation evidence.** The orientation checks compare libheif against
  expectations derived from the specification wording by this test, using edited fixtures; no
  independent decoder or trusted application has viewed those files, and no real asymmetric phone
  export with `irot`/`imir` has been compared. The `imir` axis meaning used (axis 0 exchanges top
  and bottom) is the current ISO/IEC 23008-12 wording as understood here and is also what libheif
  implements; files from writers that read the earlier, more ambiguous wording differently are
  not assessed.
- **Exif orientation.** No fixture carries an Exif item; "never applied" rests on code
  inspection.
- **VUI-only wide gamut or HDR.** The bitstream probe is exercised with BT.709 and unspecified VUI
  values only; no fixture has a P3, BT.2020 or PQ VUI without a `colr` box.
- **10-bit SDR.** All high-bit-depth input rejects. No rounding or representation policy for
  10-bit SDR to RGBA8 has been chosen or tested.
- **HDR and gain-map rejection on a real file.** Rejection of PQ/HLG is tested only on edited
  NCLX boxes; the gain-map disposition (SDR primary, map ignored) is untested on a real file.
- **12 MP admission.** A 4032x3024 photo has 12,192,768 pixels. It exceeds the standalone
  profile (1,048,576 pixels, 4096 per dimension), the default `DecodeOptions` ceiling
  (8,388,608) and is refused under `largeRaster`. Even the 1280x854 accepted sample
  (1,093,120 pixels) exceeds the standalone limit, which the test asserts. A phone-photo profile
  needs its own admitted pixel budget and measured peak memory; enlarging every decoder's default
  or the hosted budget is not authorised.
- **Memory and time.** No peak-memory or timing measurement exists for this wrapper.
- **Browser.** The wrapper has not been compiled to Wasm or run in a browser; only the dependency
  check has (r6364).
- **LGPL distribution.** libheif and libde265 are LGPL-3.0-or-later; the combined instance also
  links libaom (BSD-2-Clause with the AOM patent licence, see its recipe) into the binary, whose
  notices must ship too (recipes record `COPYING`
  digests and patches). Statically linking them into the standalone binary creates source/relink
  obligations (notices, exact upstream sources, the pinned patches and build configuration, and
  the materials to relink against a modified library) that are not yet met. Nothing here
  establishes legal clearance.

## 5. Remaining steps to admit HEIC to the standalone MCP later

Each step is separate evidence; none follows from this wrapper's test.

1. Close the section 4 gaps that the chosen surface needs: at least a real phone grid with
   `irot` and ICC compared against an independent reference; an explicit decision on
   `frameCount > 1` (refuse, or convert the primary and say so in the result); and an admitted
   pixel budget for phone-sized images with measured memory, or an explicit statement that the
   standalone profile admits only images within 1,048,576 pixels.
2. `capsule/arken_file_conversion_v1/api.hpp`: add `image::Format::heif` to
   `SupportsStandaloneInput` only (HEIC export is out of scope, so not to any output predicate,
   not to hosted `SupportsInput`, not to the browser profile), with the profile wording in its
   comment, and extend `tests/image_tests.cpp` with HEIC-to-PNG/JPEG pairs, the JPEG alpha
   policy (explicit white matte), limit and rejection cases.
3. `capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`: add
   `value heif @value_id(14)` to `enum input_kind` (matching `Format::heif = 14`).
4. `capsule/arken_file_conversion_commands_v1/src/commands.cpp`: add `heic_to_png` and
   `heic_to_jpeg` commands whose descriptions state the profile (primary still image only,
   8-bit SDR, transforms applied, collections reported, HDR rejected, limits), plus their stdio
   test coverage in `tests/commands.cpp`.
5. `capsule/arken_image_codecs_v1/source_formats.hpp`: add `heic`/`heif` entries only when a
   surface that uses filename eligibility is admitted.
6. `README.md`: document the new commands, the profile and its limits, and the LGPL notice and
   source offer that the distribution arrangement requires.
7. Complete the LGPL deliverables (section 4) before any public binary is distributed.
