# Arken Convert

Twenty-three MCP tools for image, audio and document conversion: images between
common formats, WAV and FLAC losslessly, and DOCX or RTF to text or Markdown.
No account, wallet or cloud service required. Files stay on the machine running the server.

## Build

Requires CMake 3.25+, a C++20 compiler and a POSIX host (Linux or macOS); Windows
support is not included. This revision was qualified on Linux with GCC 13; earlier
revisions were also qualified on macOS, and the audio and document additions have
not yet been built there.

The first configure downloads the pinned third-party archives, verifies their
digests and caches them in `~/Library/Caches/Arken/external-packages/v1` on macOS,
otherwise `$XDG_CACHE_HOME/arken/external-packages/v1` or
`~/.cache/arken/external-packages/v1` (override with
`-DARKEN_EXTERNAL_PACKAGE_CACHE_ROOT=...`). It needs no locale setup:
archives are listed and extracted with a UTF-8 locale when the host has none.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## Connect

Select one existing physical folder in the client configuration. The server can
read files and create outputs there; it cannot change that folder through a tool call.

```json
{
  "mcpServers": {
    "convert": {
      "command": "/absolute/path/to/convert/build/bin/arken-convert-commands",
      "args": ["--stdio", "--root", "/absolute/path/to/files"]
    }
  }
}
```

Discover with `tools/list`, then call a tool with relative paths. For example:

```json
{"name":"png_to_jpeg","arguments":{"source_path":"photo.png","output_path":"photo.jpg","jpeg_quality":95,"alpha_policy":"flatten_white"}}
```

## Tools

Images:

- `convert_image`: PNG, JPEG, BMP, TGA, GIF, ICO/CUR, TIFF, WebP or supported DDS
  to PNG, JPEG, BMP, TGA, ICO, TIFF or WebP (`input_format`, `output_format`).
- `png_to_jpeg`, `jpeg_to_png`, `bmp_to_png`, `bmp_to_jpeg`, `tga_to_png`, `dds_to_png`.
- `webp_to_png`, `webp_to_jpeg`, `png_to_webp`, `gif_to_png`, `tiff_to_png`,
  `png_to_tiff`, `ico_to_png`, `png_to_ico`.

Audio:

- `convert_audio`: WAV or FLAC to WAV or FLAC (`input_format`, `output_format`).
- `wav_to_flac`, `flac_to_wav`.

Documents:

- `convert_document`: DOCX or RTF (`input_format`) to `txt` or `markdown` (`output_format`).
- `docx_to_txt`, `docx_to_markdown`, `rtf_to_txt`, `rtf_to_markdown`.

Every tool takes a relative `source_path` and a new relative `output_path`.

## Image profile

Each call converts one image. The result's `source_frames` says how many the source
held, so a partial conversion is never silent:

- GIF: the first frame. An animated GIF never yields a sequence.
- WebP: lossy and lossless stills, and the first frame of an animation composited
  on a transparent canvas. An embedded ICC profile is applied; Exif and XMP are ignored.
- TIFF: the first page. Classic and BigTIFF, strips or tiles, chunky or planar;
  none, LZW, Deflate, PackBits and new-style JPEG compression; orientation applied.
  Gray, palette, RGB, CMYK and YCbCr, with ICC profiles applied. CCITT,
  old-style JPEG, floating-point and Lab TIFF are rejected.
- ICO/CUR: the largest rendition (largest area, then deepest colour). AND masks
  become transparency; cursor hotspots are ignored.
- JPEG: Exif orientation and an embedded ICC profile are applied; Adobe CMYK/YCCK
  is converted to RGB. Arithmetic-coded, lossless and 12-bit JPEG are rejected.
- PNG: sRGB, iCCP, cHRM and gAMA colour information is applied.
- BMP: OS/2 and Windows V1–V5 headers, 1–32 bit pixels, bitfields, RLE4/RLE8;
  calibrated and embedded colour spaces are applied, linked profiles are rejected.
- TGA and supported DDS textures.

16-bit PNG and TIFF are rounded to 8 bits per channel; `source_bits_per_channel`
reports 16. Metadata is not copied to any output.

Options are accepted only for their own output; anything else is an invalid request:

- JPEG: `jpeg_quality` (1–100, default 95) and `alpha_policy` (`preserve` or
  `flatten_white`). Transparency is never discarded without `flatten_white`.
- WebP: lossless by default, with exact RGBA including the colour of transparent
  pixels. `webp_lossless: false` writes lossy VP8 at `webp_quality` (1–100,
  default 90) with exact alpha. `webp_quality` requires `webp_lossless: false`.
- TIFF: one RGBA image, uncompressed by default; `tiff_deflate: true` uses Deflate
  with a horizontal predictor.
- ICO: one PNG-backed rendition of at most 256×256, never resized.

Limits: 8 MiB input and output, 1,048,576 pixels and 4096 pixels per dimension.

## Audio profile

WAV and FLAC conversion is lossless. Integer PCM samples (4–32 bit), sample rate,
channel count, channel order and any WAVE_FORMAT_EXTENSIBLE channel mask are
preserved exactly. FLAC frame CRCs and the stream MD5 are checked on input. FLAC
output uses compression level 5, is verified while encoding and is deterministic
for a given build.

Rejected, never approximated: IEEE float, A-law, mu-law and ADPCM WAV, RF64,
BW64, Wave64 and Ogg FLAC. Tags, pictures, cue sheets, seek tables and WAV LIST
chunks are not carried; `skipped_metadata` counts them and `comments_dropped`
reports dropped FLAC comments. 12- and 20-bit audio is written as
WAVE_FORMAT_EXTENSIBLE with channel mask 0 when no mask was declared.

Results report `sample_rate`, `channels`, `bits_per_sample` and `frames`.

Limits: 64 MiB input and output, 33,554,432 samples (frames × channels), 1–8
channels, up to 384 kHz, 8 MiB per metadata block. That is about 6 min 20 s of
16-bit 44.1 kHz stereo. Conversion is resident, not streaming: one call holds the
source, the samples as 32-bit integers and the output (about 256 MiB at the limits).

## Document profile

DOCX and RTF are extracted, not rendered. Only the main story is read: paragraphs,
headings, lists, tables and hyperlinks. Headers, footers, footnotes, endnotes,
comments, text boxes, images, charts and equations are excluded. Tracked changes
appear accepted. Fields keep their stored result; only HYPERLINK is interpreted.
Bold and italic come from direct formatting only. Encrypted, macro-enabled and
malformed files are rejected; nothing is fetched or executed.

RTF: Unicode escapes and code pages 1250–1254 and 1257 are decoded; text in other
code pages (including DBCS) is rejected. Lists become bullets; nested tables are
flattened.

TXT is UTF-8 with LF line ends; table rows are tab-separated and link URLs follow
the link text in parentheses. Markdown uses ATX headings, nested lists, pipe tables
when every row has the same number of cells, and links only for http, https and
mailto. Literal Markdown characters are escaped; no HTML is emitted.

Results report `paragraphs`, `headings`, `list_items`, `tables` and `table_rows`.

Limits: 32 MiB input, 16 MiB per expanded DOCX part, 32 MiB output, 16 MiB of
extracted text. Extraction is resident: the expanded main part, its XML tree and
the text model coexist (about 8–10 times the expanded part).

## Files and results

Paths must stay inside the selected folder. Symlinks, parent traversal and
nonregular inputs are rejected. Output parents must already exist. Existing files
are never overwritten, including on repeat calls, and sources are never modified.

Results include the relative output path, media type, byte count and SHA-256,
plus the family's fields above. Rejections name the codec owner's reason in the
error message. A rare finalization failure after publication explicitly reports
that the output exists: inspect its path and digest before retrying.

The stdio server uses MCP 2025-11-25. `--catalog` prints its generated catalogue;
`--version` exits without opening a folder. Hosted upload/download, payments,
Cloudflare, revenue and aggregate services are separate monorepo integrations.

Waygate preserves selected source history. Its native refresh derives dependencies
from the build and independently qualifies updates before applying them. Third-party
notices remain with their owners; this package grants no new first-party licence.

## HEIC and AVIF development prerequisites

HEIC/HEIF and AVIF are compiled for development qualification only. They are not
admitted: no tool, format value or conversion profile accepts them.

The root build compiles the memory-only HEIC decoder wrapper and the still-AVIF
decode/encode wrapper (`ARKEN_IMAGE_CODECS_V1_HEIF`) against one combined libheif
1.23.7 instance with libde265 1.1.3 (HEVC) and libaom 3.15.3 (AV1), and runs their
native qualifications (`ArkenImageCodecsV1.Heif`, `ArkenImageCodecsV1.Avif`). They
are reachable only in-process through the codec library. The single-codec
dependency qualifications remain separate projects:

```sh
cmake -S capsule/arken_image_codecs_v1/tests/heif_dependencies -B build-heif -DCMAKE_BUILD_TYPE=Release
cmake --build build-heif --target arken-heif-dependency-check --parallel 2
ctest --test-dir build-heif --output-on-failure
cmake -S capsule/arken_image_codecs_v1/tests/avif_dependencies -B build-avif -DCMAKE_BUILD_TYPE=Release
cmake --build build-avif --parallel 2
ctest --test-dir build-avif --output-on-failure
```

Recipes admit macOS, Linux and the separately qualified browser target. Archive
pins, decoder selection, normative deblocking/SAO and memory-only browser patches
are preserved. Portable exact-file replacements live under
`sdk/arken_sdk_dev/external_packages/recipes/patches/generated/`;
the small original diffs remain as authoring references. Each replacement source
directory includes the exact upstream `COPYING` notices. Actual platform and
product-format qualification must precede advertising HEIC or AVIF support.

## Third-party notices

Pinned sources are acquired from exact upstream archives by the SDK package graph
(`sdk/arken_sdk_dev/external_packages/recipes/`), which verifies each archive digest
and its declared licence files. Their licences apply to their code:

- stb_image / stb_image_write 2.30 (MIT): PNG, JPEG and GIF decoding; PNG, JPEG,
  BMP and TGA encoding; zlib for TIFF Deflate and DOCX members.
- skcms (BSD-3-Clause): ICC colour conversion.
- libwebp 1.6.0 (BSD-3-Clause): WebP decode and encode.
- libFLAC 1.5.0 (BSD-3-Clause; library only, no GPL programs or LGPL helpers).
- nlohmann/json 3.12.0 (MIT), vendored in `sdk/arken_sdk_dev/third_party/`.
- Development qualification only, not admitted: libheif 1.23.7
  (LGPL-3.0-or-later), libde265 1.1.3 (LGPL-3.0-or-later) and libaom 3.15.3
  (BSD-2-Clause with the AOM Patent License 1.0). They are linked into the root
  build's server binary for HEIC/AVIF qualification. Distributing that binary
  carries the LGPL obligations (notices, corresponding source, relinking); this
  repository does not claim they are settled.
