# Still-image AVIF dependency milestone (avif-deps-1)

Status: **dependency qualification only.** This project proves that a pinned,
CMake-only, assembly-free AV1 decoder/encoder can be built from exact upstream
release archives and can decode and losslessly round-trip ordinary 8-bit still
AVIF images natively on Linux/GCC. It does **not** add AVIF to `api.hpp`,
`source_formats.hpp`, the standalone command schema, the website or any
conversion profile, and it is not a release claim. AVIF stays preview-only in
the source catalogue (handoff section 8, plan-task:d85e256cc4a2ed60).

## Decision: option (a), libheif 1.23.7 configured for AV1 + libaom 3.15.3

| Criterion | (a) `libheif-1.23.7-av1-v1` + `libaom-3.15.3-v1` | (b) libavif 1.4.2 + libaom 3.15.3 |
|---|---|---|
| New pinned archives | 1 (libaom). The libheif archive `f7483e81…2475` is already pinned, cached and reviewed for HEIC. | 2 (libavif, libaom) |
| Source acquisition | libaom: official immutable release object `storage.googleapis.com/aom-releases/libaom-3.15.3.tar.gz` (with upstream `.asc`). | libavif publishes **no release asset** for v1.4.2 (`releases/download/v1.4.2/*` is 404); the only archive is GitHub's generated `codeload` tarball, which is unreachable here (403) and whose bytes are GitHub-generated rather than an upstream release artefact. |
| Container parser | Reuses the HEIF/ISOBMFF parser, security-limits model (`heif_security_limits`), transform handling and memory-only patches already qualified for HEIC. One parser for HEIC and AVIF. | Adds a second ISOBMFF parser with its own limits (`avifDecoder` `imageSizeLimit`, `imageDimensionLimit`, …) and transform/colour code to review. |
| Build system | CMake only (after the libaom Perl removal below). | CMake only (`AVIF_CODEC_AOM=SYSTEM` picks up an existing `aom` target); same libaom Perl issue. |
| Still decode + encode, lossless, RGBA8 | Yes: `heif_encoder_set_lossless`, `chroma=444`, GBR/identity NCLX, alpha as lossless auxiliary image; RGBA8 decode via `heif_decode_image(…interleaved_RGBA)`. Proven by the round-trip test. | Yes (`avifEncoder` `quality=AVIF_QUALITY_LOSSLESS`, `AVIF_MATRIX_COEFFICIENTS_IDENTITY`, `avifRGBImage`). Not built or tested. |
| Licence | libheif LGPL-3.0-or-later (already incurred by HEIC) + libaom BSD-2-Clause + AOM Patent License 1.0. | libavif BSD-2-Clause + libaom BSD-2 + AOM patent licence: more permissive if AVIF were shipped **without** HEIC. |
| Wasm portability | Same libheif memory-only patches as HEIC; libaom generic C. | libavif is portable C; same libaom. |

(a) is chosen because it is the smallest pinned set (one new archive), the only
option whose every archive is an upstream release artefact reachable through
the declared acquisition path, and it reuses the container/limit/transform
code the HEIC wrapper already qualifies. The cost is that AVIF inherits libheif's
LGPL distribution obligations; since HEIC already carries them, no new licence
class is introduced. If AVIF must one day ship without HEIC under a purely
permissive profile, (b) is the fallback; its blocker is an immutable libavif
source archive (or a `tracked-source` vendoring decision), not code.

### Important integration consequence

`libheif-1.23.7-av1-v1` and `libheif-1.23.7-hevc-v1` both materialize the
upstream target `heif`, so **they cannot both be resolved into one CMake graph**
(the cmake-source adapter refuses with `ambient_target_conflict`). The separate
AV1 instance exists so this qualification can be run without touching the
accepted HEIC milestone. For the product, the HEIC owner should replace the two
instances with **one** combined instance (e.g. `libheif-1.23.7-hevc-av1-v1`):
`WITH_LIBDE265=true`, `WITH_AOM_DECODER=true`, `WITH_AOM_ENCODER=true`,
dependencies `["libde265-1.1.3-v1","libaom-3.15.3-v1"]`, plus the HEVC recipe's
`decoder_libde265.cc` replacement. The replacement
`patches/generated/libheif-1.23.7-av1/CMakeLists.txt` was written for that: it
is the HEVC replacement plus the pinned-AOM block, and both blocks are guarded
by their own `WITH_*` options, so the same file serves the combined recipe.

## Pinned identities

| Recipe | Archive | sha256 (downloaded twice, identical) |
|---|---|---|
| `libaom-3.15.3-v1` | `https://storage.googleapis.com/aom-releases/libaom-3.15.3.tar.gz` (6,632,070 bytes, published 2026-10-30, CHANGELOG 2026-10-20) | `12c01dc731ed7b5a999e28120b45bf58a002ba3435a4ba2cfbc1e084a27c9c04` |
| `libheif-1.23.7-av1-v1` | `https://github.com/strukturag/libheif/releases/download/v1.23.7/libheif-1.23.7.tar.gz` (same archive as HEIC) | `f7483e818503ea6224c8863e4a56ac02c7089021a98472c811c12aa19faf2475` |

Licence files: libaom `LICENSE` `4764a286…de7f2`, `PATENTS` `661fb8e5…e61`;
libheif `COPYING` `fa81ce65…4596`. Each replacement directory under
`patches/generated/` carries the exact upstream notices.

The libaom detached signature (`libaom-3.15.3.tar.gz.asc`) exists upstream but
was **not verified** (no AOM signing key available offline); integrity rests on
the pinned sha256.

## libaom configuration and patches

Options (all typed in the recipe): `AOM_TARGET_CPU=generic` (no SIMD, no
assembly, no runtime CPU detection; all `HAVE_<SIMD>=0`), `ENABLE_NASM=OFF`,
`CONFIG_MULTITHREAD=0`, `CONFIG_AV1_HIGHBITDEPTH=0`, `CONFIG_REALTIME_ONLY=0`,
`CONFIG_SIZE_LIMIT=1` with `DECODE_WIDTH_LIMIT=DECODE_HEIGHT_LIMIT=8192`,
`CONFIG_WEBM_IO=0`, `CONFIG_LIBYUV=0`, tuning/analysis/inspection off, apps,
examples, tools, tests, test data, docs and perf tests off, static library,
`AOM_EXTRA_C_FLAGS=AOM_EXTRA_CXX_FLAGS=-ffp-contract=off` (applied to every
libaom object library; the component's `compileOptions` would only reach the
aggregating `aom` target).

**Correction to the task premise:** libaom's CMake build is *not* Perl-free.
Upstream `cmake/aom_configure.cmake` hard-fails without Perl
(`"Perl is required to build libaom."`) and runs `cmake/rtcd.pl` (dispatch
headers) and `cmake/version.pl` (`aom_version.h`); `add_rtcd_build_step` also
re-runs Perl at build time. Four exact-file replacements remove that:

- `cmake/aom_configure.cmake` — no `find_package(Perl)`/`find_package(Git)`.
  The three RTCD headers (`aom_dsp_rtcd.h`, `aom_scale_rtcd.h`, `av1_rtcd.h`)
  are embedded verbatim as the exact output of upstream `rtcd.pl
  --arch=generic` for this configuration, each written only after its sha256
  matches (`1252de00…d5e1`, `16c294be…4f64`, `6dc08b0b…2c03`). Before writing,
  every configuration input `rtcd.pl` reads (`AOM_TARGET_CPU`,
  `AOM_RTCD_FLAGS`, `CONFIG_AV1_DECODER/ENCODER/HIGHBITDEPTH`,
  `CONFIG_AV1_TEMPORAL_DENOISING`, `CONFIG_EXCLUDE_SIMD_MISMATCH`,
  `CONFIG_HIGHWAY`, `CONFIG_INTERNAL_STATS`, `CONFIG_REALTIME_ONLY`,
  `CONFIG_RUNTIME_CPU_DETECT`, `HAVE_AVX2`, `HAVE_SSSE3`) is compared with the
  generated `aom_config.h`; any other configuration fails configure rather than
  compiling against mismatched declarations. Changing those recipe options
  therefore requires regenerating the headers (authoring step below).
- `cmake/aom_optimization.cmake` — `add_rtcd_build_step` no longer adds a Perl
  custom command; it only requires the pinned header to exist.
- `cmake/version.cmake` — CMake-only equivalent of `version.pl` for a release
  tree (reads the newest CHANGELOG release; writes byte-identical
  `aom_version.h`, sha256 `fd32116c…c8a1`, `VERSION_STRING " v3.15.3"`).
- `CMakeLists.txt` — two isolation fixes needed because cmake-source uses
  `add_subdirectory`:
  1. libaom's flag helpers `FORCE` its `-std=c11/c++17`, warnings, `-O3` and
     `-ffp-contract=off` into the global `CMAKE_<LANG>_FLAGS*` **cache**, which
     leaked into libheif and the first-party checker (observed in the first
     configure). The replacement snapshots those cache entries after
     `project()`, restores them at the end of the file and keeps libaom's
     values only as directory-scope variables; libaom's own flag bookkeeping
     caches are cleared so a reconfigure derives identical flags (verified:
     `flags.make` unchanged after reconfigure; checker flags are just
     `-O3 -DNDEBUG -std=gnu++20 -Wall -Wextra -Wpedantic -Werror -ffp-contract=off`).
     Caveat: a consumer that sets `CMAKE_<LANG>_FLAGS*` as *normal* variables
     would shadow libaom's values inside libaom; the convert root does not.
  2. `aom` had only an `INSTALL_INTERFACE` include directory. Consumers now
     get a `BUILD_INTERFACE` directory containing only the upstream
     `AOM_INSTALL_INCS` public headers (`aom/*.h`), not the source root.

Authoring (only needed when libaom options change; Perl is used at authoring
time only, like the HEIC reference diffs): configure pristine libaom 3.15.3
standalone with the recipe options, take `config/{aom_dsp,aom_scale,av1}_rtcd.h`,
and regenerate the replacement files with the same structure (original
`aom_configure.cmake` lines 1–491 unchanged, then the pinned block). Update the
recipe's replacement sha256 values.

## libheif AV1 instance

Same archive, options and `-ffp-contract=off` /
`__EMSCRIPTEN_STANDALONE_WASM__` component profile as the HEVC recipe, except
`WITH_LIBDE265=false`, `WITH_AOM_DECODER=true`, `WITH_AOM_ENCODER=true`
(built-in, `*_PLUGIN=false`, `ENABLE_PLUGIN_LOADING=false`). Every other codec
(x265, kvazaar, uvg266, vvdec/vvenc, x264, OpenH264, dav1d, SVT, rav1e,
JPEG, OpenJPEG, FFmpeg, OpenJPH, uncompressed, WebCodecs), sharpyuv, header
compression, multithreading, parallel tile decoding, experimental features,
examples, tools, docs, fuzzers and tests are off. Patches: the new top-level
`CMakeLists.txt` replacement (pinned `arken::external_aom_v1` instead of
`find_package(AOM)`), and the HEVC milestone's `libheif/CMakeLists.txt`,
`box.cc`, `box.h`, `file.cc` replacements reused unchanged by path and digest.

## Qualification checks (`ctest`)

| Test | What it proves |
|---|---|
| `AvifDependencies.Version` | `--version` wins over other arguments; prints `avif-deps-1`. |
| `AvifDependencies.Admission` | libheif `1.23.7`, libaom `v3.15.3` (`aom_codec_version()==0x030F03`); AV1 decoder **and** encoder present; no decoder/encoder for HEVC, AVC, JPEG, VVC, EVC, JPEG 2000, uncompressed, HTJ2K; exactly one decoder descriptor (`aom`); exactly two encoder descriptors, `aom` and libheif's internal region-`mask` encoder (registered unconditionally by `plugin_registry.cc`, no option removes it, no mask decoder exists), and only `aom` for AV1; neither libaom interface advertises `AOM_CODEC_CAP_HIGHBITDEPTH`; bounded security limits are applied and read back; a truncated `ftyp` is rejected. |
| `AvifDependencies.Sample` | Decodes the upstream sample `examples/example.avif` from the pinned libheif tree (declared sha256 `54a0dc31…1469`, 113,604 bytes, ftyp `avif`/`mif1`) to RGBA8 under bounded limits, strict decoding, one codec thread; fingerprint pinned. |
| `AvifDependencies.SampleTransform` | Decodes `tests/data/clap_cropped_irot_imir.avif` (`15d85477…bf9d`), which carries `clap`+`irot`+`imir`; fingerprint pinned. This exercises the transform path only as dependency evidence; there is no independent orientation reference. |
| `AvifDependencies.RejectMini` | `tests/data/simple_osm_tile_alpha.avif` uses the low-overhead `mif3`/`mini` reduced-header form (ftyp major `mif3`, minor `avif`, no compatible brands). It is refused (code 11). It was the first alpha-sample candidate; alpha is covered by the round trip instead. |
| `AvifDependencies.RoundTrip` | 61×37 RGBA pattern with transparent, opaque and partial alpha: lossless encode (`chroma=444`, GBR matrix, full range, alpha lossless, `threads=1`, `speed=6`) → decode is **byte-identical RGBA**; lossy (q60, 4:2:0, BT.601) encode decodes at the same geometry with alpha; a 10-bit encode request is **refused by libaom's encoder initialisation** (`heif_error_Encoder_plugin_error`/`heif_suberror_Encoder_initialization`), not silently reduced. |
| `AvifDependencies.RejectHeic` | The pinned HEVC `examples/example.heic` is refused (no `avif` compatible brand, code 11) before any decoder runs. |

The brand gate is "ftyp lists `avif`" (`heif_has_compatible_brand`), not "major
brand is `avif`", because ordinary files may use major brand `mif1`.

Sample candidates considered (all inside pinned trees): libheif
`examples/example.avif` (113,604 bytes, real photo; chosen),
`tests/data/simple_osm_tile_alpha.avif` and `simple_osm_tile_meta.avif`
(both `mini` reduced-header, rejected), `clap_cropped.avif` (decodes, 64×64),
`clap_cropped_irot_imir.avif` (transform cases, 1.4 KB), and the
`clap_oversized_ispe_*`, `mini_size_zero.avif`, `meta_size_zero.avif` malformed
cases (useful for the wrapper's negative tests). libaom ships no AVIF files.
libavif's `tests/data` (94 files incl. 10/12-bit, animated, gain-map) is not in
the pinned set; it can be used as independent interoperability input later.

## Observed results

Linux x86-64, Ubuntu 24.04, GCC 13.3, CMake 3.28, Release, `-j2`:

```
1/7 Test #1: AvifDependencies.Version ...........   Passed    0.00 sec
2/7 Test #2: AvifDependencies.Admission .........   Passed    0.00 sec
3/7 Test #3: AvifDependencies.Sample ............   Passed    0.06 sec
4/7 Test #4: AvifDependencies.SampleTransform ...   Passed    0.01 sec
5/7 Test #5: AvifDependencies.RejectMini ........   Passed    0.00 sec
6/7 Test #6: AvifDependencies.RoundTrip .........   Passed    0.03 sec
7/7 Test #7: AvifDependencies.RejectHeic ........   Passed    0.00 sec
100% tests passed, 0 tests failed out of 7
```

- `example.avif`: 800×533, 8-bit, no alpha, 1 top-level image,
  RGBA FNV-1a-64 **`fa6ee36f0fba9dfd`**.
- `clap_cropped_irot_imir.avif`: 64×64, RGBA FNV-1a-64 `9a67e30862ebb142`.
- Round trip: source RGBA FNV-1a-64 `2e42719fa2224914`; lossless AVIF 8,788
  bytes (61×37×4 = 9,028 raw), lossy q60 2,017 bytes. Encoded bytes are not
  pinned (encoder output identity across platforms is not claimed).
- Build: libaom + libheif from clean at `-j2` took about 3.6 minutes wall on
  the shared 4-core host. `libaom.a` 5.1 MB, `libheif.a` 8.3 MB (unstripped
  static archives); the linked checker is 6.75 MB (5.94 MB text).
- Per-image cost is small for these samples (0.06 s for the 800×533 decode in
  pure C, single thread). Peak RSS was not measured (no `time -v` available).
  Encode speed at larger sizes is unmeasured; pure-C libaom encoding of a
  12 MP image will be far slower than SIMD builds and needs its own budget.
- First configure wrote no Perl or Git probe (`aom_configure` reports
  `Detected CPU: generic`); a reconfigure produces identical libaom
  `flags.make`.

## What is deliberately NOT enabled or decided

- **Animation / image sequences** (`avis`, `heif_context_has_sequence`): rejected
  by the checker; `max_sequence_frames=1`. Needs separate qualification.
- **Bit depth:** libaom is built with `CONFIG_AV1_HIGHBITDEPTH=0`. 10/12-bit
  AVIF cannot be decoded or encoded by this build; the checker additionally
  rejects any `luma_bits != 8` before decoding, and the round trip proves a
  10-bit encode is refused. No 10-bit *decode* sample was exercised (none
  exists in the pinned trees), so the decoder-side refusal for 10-bit streams
  is asserted only through the capability bit and the pre-decode bit-depth
  gate. Enabling high bit depth later changes the RTCD
  headers (regenerate) and needs an explicit rounding/representation policy.
- **HDR** (PQ/HLG transfer, gain maps, `clli`/`mdcv`): not handled. The
  wrapper must reject non-SDR transfer like `heif.cpp`'s `SdrTransfer`.
- **ICC / NCLX colour management:** the checker uses libheif's default
  YCbCr→RGB conversion and does not apply ICC or wide-gamut primaries; its
  fingerprints are dependency evidence, not an accepted colour result.
- **Exif / XMP / ICC on output:** the encoder path writes only an NCLX `colr`
  box; Exif, XMP and ICC are not carried. Lossless means exact RGBA samples,
  not metadata preservation.
- **Orientation:** libheif applies `irot`/`imir`/`clap` on decode (as for
  HEIC); no asymmetric-orientation qualification was done for AVIF.
- **Grids, multi-layer (`a1lx`/`lsel`), progressive, 4:2:2/4:0:0 input**: not
  qualified beyond what the two samples exercise.
- **Threads:** off at every layer (`CONFIG_MULTITHREAD=0`,
  `ENABLE_MULTITHREADING_SUPPORT=false`, `threads=1`, one decoding thread).
- **Browser/Wasm:** not built. The CMake project refuses a wasm32 toolchain
  until an AVIF browser runtime (import surface, memory-only file denials, as
  in `ArkenHeifBrowserRuntime.cmake`) is qualified. Native/Wasm pixel parity is
  unproven.
- **macOS / Clang:** not built here; the recipe declares macOS support by
  analogy with the HEIC recipe only.

## Remaining steps to wire `src/avif.cpp` (following `heif.cpp` on `claude/heic-wip`)

1. **Recipe consolidation (HEIC owner):** replace `libheif-1.23.7-hevc-v1` in the
   consuming manifests (root `lib.manifest.json`, capsule build) with one
   combined `libheif-1.23.7-hevc-av1-v1` instance as described above, keep
   `libaom-3.15.3-v1` as its dependency, and gate it the way
   `ARKEN_IMAGE_CODECS_V1_HEIF` is gated (e.g. `ARKEN_IMAGE_CODECS_V1_AVIF`
   requiring the combined target). Two libheif instances cannot coexist.
2. **Decoder:** add `src/avif.cpp/.hpp` with `detail::DecodeAvif(Bytes,const
   DecodeOptions&)`, or generalise `DecodeHeif` behind a brand parameter. Reuse
   from `heif.cpp`: `Library` RAII, `Map(heif_error)` → `DecodeError`,
   security limits derived from `DecodeOptions::maximumPixels`,
   `heif_context_has_sequence` rejection, primary-only selection with
   `frameCount=topLevel`, luma/chroma `>8` rejection, premultiplied-alpha
   rejection, `ispe` and post-transform dimension checks before decoding,
   `DecideColour` (SDR transfer gate, ICC via skcms, P3/BT.2020 → sRGB),
   explicit `output_image_nclx_profile`, decoded-extent check, and stride-safe
   copy into `Image::rgba8`. AVIF-specific: admit `avif` (and only still
   `avif`; reject `avis`), and remove the `heif.cpp` line that rejects AVIF
   brands in the HEIC path only when the AV1 decoder is compiled in. Decide
   whether AVIF identity/orientation semantics match HEIC (handoff §8: do not
   assume) using independently produced, visibly oriented samples.
3. **Format enum + dispatch:** `Format::avif` in `api.hpp`, a
   `case Format::avif` in `DecodeImage` (`codec.cpp`) under the compile-time
   gate, and the source-format decision in `source_formats.hpp` (still
   preview-only until the user journey is qualified).
4. **Encoder:** `EncodeImage` support in `encode.hpp` with an explicit output
   policy before any control is exposed: default lossy quality, lossless mode
   (`heif_encoder_set_lossless`, `chroma=444`, GBR/identity full-range NCLX,
   alpha lossless — the exact configuration the round-trip test proves),
   alpha on/off, chroma subsampling, 8-bit only, `threads=1`, a fixed `speed`
   value, and an NCLX sRGB tag. State that metadata is not carried.
5. **Tests:** port the checker's round-trip, sample fingerprints and
   negative cases (HEIC-by-brand, truncated, `clap_oversized_ispe_*`,
   `*_size_zero.avif`, limits) into `tests/` like `heif_tests.cpp`; add
   independent interoperability (an outside encoder's AVIF in, Arken's AVIF out
   opened by an outside decoder) and the transparent-PNG / opaque-JPEG paths.
6. **Browser:** an AVIF browser runtime mirroring
   `cmake/ArkenHeifBrowserRuntime.cmake` (the libheif memory-only patches are
   already in this recipe), import-surface check, and native/Wasm fingerprint
   parity for `example.avif` before any browser admission.
7. **Distribution:** LGPL deliverables for libheif (already required by HEIC)
   plus libaom's BSD-2 notice and AOM Patent License 1.0 text in the notices.
