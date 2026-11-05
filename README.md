# Arken Convert

Fifty-four MCP tools for image, audio, document, table and markup conversion:
images between common formats; WAV, FLAC and AIFF losslessly; Ogg Vorbis, Ogg
Opus and MP3 as explicitly lossy formats; DOCX, RTF, PPTX,
ODT, ODP and EPUB to text or Markdown; CSV, TSV, JSON, XLSX and ODS to CSV, TSV or
JSON; HTML to Markdown, Markdown to HTML, and SRT to and from WebVTT.
No account, wallet or cloud service required. Files stay on the machine running the server.

## Build

Requires CMake 3.25+, a C++20 compiler and a POSIX host (Linux or macOS); Windows
support is not included. This revision was qualified on Linux with GCC 13; earlier
revisions were also qualified on macOS, and the audio, document, table and markup
additions have not yet been built there.

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

The codec capsules also have standalone qualification projects, configured,
built and tested the same way: `capsule/arken_audio_codecs_v1/tests`,
`capsule/arken_document_extraction_v1/tests` (DOCX/RTF, tables, and
PPTX/ODT/ODP/EPUB) and `capsule/arken_markup_conversion_v1/tests`.

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

- `convert_audio`: any of WAV, FLAC, AIFF, Vorbis, Opus or MP3 to any other
  (`input_format`, `output_format`: `wav`, `flac`, `aiff`, `vorbis`, `opus` or
  `mp3`; AIFF-C is read as `aiff`); the lossy controls below for a lossy output.
- `wav_to_flac`, `flac_to_wav`, `aiff_to_wav`, `aiff_to_flac`, `wav_to_aiff`, `flac_to_aiff`.
- `wav_to_mp3`, `flac_to_mp3` (`mp3_rate_control`, `mp3_bitrate_kbps`,
  `mp3_vbr_quality`), `wav_to_opus` (`opus_bitrate_kbps`), `wav_to_vorbis`
  (`vorbis_quality`), `mp3_to_wav`, `opus_to_wav`, `vorbis_to_wav`.

Documents:

- `convert_document`: DOCX, RTF, PPTX, ODT, ODP or EPUB (`input_format`: `docx`,
  `rtf`, `pptx`, `odt`, `odp`, `epub`) to `txt` or `markdown` (`output_format`);
  `include_notes` for `pptx` and `odp` only.
- `docx_to_txt`, `docx_to_markdown`, `rtf_to_txt`, `rtf_to_markdown`.
- `pptx_to_txt`, `pptx_to_markdown`, `odp_to_txt`, `odp_to_markdown` (each with
  optional `include_notes`), `odt_to_txt`, `odt_to_markdown`, `epub_to_txt`,
  `epub_to_markdown`.

Tables:

- `convert_table`: CSV, TSV, JSON, XLSX or ODS (`input_format`) to CSV, TSV or
  JSON (`output_format`, different from the input); optional `delimiter`,
  `json_shape`, `sheet_index` or `sheet_name` where they apply.
- `xlsx_to_csv`, `ods_to_csv` (`delimiter`, `sheet_index`, `sheet_name`),
  `xlsx_to_json` (`json_shape`, `sheet_index`, `sheet_name`), `csv_to_json`
  (`delimiter`, `json_shape`), `json_to_csv`, `csv_to_tsv` (`delimiter`).

Markup and subtitles:

- `convert_markup`: exactly `html` to `markdown`, `markdown` to `html`, `srt` to
  `vtt` or `vtt` to `srt` (`input_format`, `output_format`); `drop_navigation`
  for HTML input, `full_document` for HTML output.
- `html_to_markdown` (`drop_navigation`), `markdown_to_html` (`full_document`),
  `srt_to_vtt`, `vtt_to_srt`.

Every tool takes a relative `source_path` and a new relative `output_path`. An
option that does not apply to the tool, input or output is an invalid request,
never ignored.

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
  Bytes after the primary image's end marker (an Ultra HDR gain map, an MPF
  secondary image, a camera trailer or padding) are ignored.
- PNG: sRGB, iCCP, cHRM and gAMA colour information is applied. APNG: the default
  image when it is also the first frame (`source_frames` is the animation's frame
  count); an APNG whose default image is not part of the animation is rejected.
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

WAV, FLAC and AIFF conversion is lossless. Integer PCM samples (4–32 bit), sample rate,
channel count, channel order and any WAVE_FORMAT_EXTENSIBLE channel mask are
preserved exactly, except that AIFF has no channel-mask field (see AIFF below). FLAC frame CRCs and the stream MD5 are checked on input. FLAC
output uses compression level 5, is verified while encoding and is deterministic
for a given build.

Rejected, never approximated: IEEE float, A-law, mu-law and ADPCM WAV, RF64,
BW64, Wave64 and Ogg FLAC. Tags, pictures, cue sheets, seek tables and WAV LIST
chunks are not carried; `skipped_metadata` counts them and `comments_dropped`
reports dropped FLAC comments. 12- and 20-bit audio is written as
WAVE_FORMAT_EXTENSIBLE with channel mask 0 when no mask was declared.

AIFF: big-endian AIFF and AIFF-C with `NONE`, `twos`, `sowt` or `raw ` integer
samples are read; AIFF (COMM and SSND chunks only) is written. The 80-bit sample
rate must be an exact positive whole number. Float (`fl32`, `fl64`), mu-law, A-law,
IMA4 and other compressed AIFF-C are rejected, never approximated. MARK, INST,
COMT, NAME, AUTH, ANNO, APPL, ID3 and other chunks are not carried and count in
`skipped_metadata`. AIFF declares no channel mask: a mask from a WAV or FLAC
source is not written and `channel_mask_dropped` is true; channel order is kept,
and AIFF read into WAV or FLAC carries no mask.

### Lossy formats: Vorbis, Opus and MP3

"OGG" is a container, not a codec, so no tool offers a bare Ogg choice: `vorbis`
is Vorbis I in Ogg (`.ogg`, `audio/ogg; codecs=vorbis`), `opus` is Opus in Ogg
per RFC 7845 (`.opus`, `audio/ogg; codecs=opus`) and `mp3` is an MPEG-1/2/2.5
Layer III stream (`audio/mpeg`).

Quality contract:

- Lossy output is an approximation and is never reversible. `lossy_output` labels
  every conversion into Vorbis, Opus or MP3, `lossy_source` every conversion from
  one, and `generation_loss` every lossy-to-lossy conversion: a second generation
  of coding loss on top of the first. Quality is never restored, and WAV or FLAC
  made from an MP3 is still the MP3's audio.
- Controls, each accepted only for its own output (otherwise an invalid request):

  | Output | Control | Default |
  | --- | --- | --- |
  | Vorbis (libvorbis 1.3.7) | `vorbis_quality` 0–100 = libvorbis VBR quality −0.1…1.0 | 50 (0.45) |
  | Opus (libopus 1.5.2) | `opus_bitrate_kbps` 6–510, unconstrained VBR, 20 ms frames | 128 |
  | MP3 (LAME 3.100) | `mp3_rate_control` `cbr` with `mp3_bitrate_kbps`, or `vbr` with `mp3_vbr_quality` 0 (best)–9 (LAME `-V`) | CBR 192 kbps; VBR quality 2 |

  `mp3_bitrate_kbps` must be a Layer III bitrate of the sample rate's MPEG version
  (32–320 kbps at 32, 44.1 and 48 kHz; 8–160 kbps at 8–24 kHz) and the frame must
  be large enough to hold the gapless LAME tag. The minimum CBR bitrates are
  therefore 64 kbps at 48 kHz, 56 (mono) or 64 (stereo) at 44.1 kHz, 40 or 48 at
  32 kHz, 64 at 24 kHz, 56 at 22.05 kHz, 40 at 16 kHz, 32 at 12 and 11.025 kHz and
  24 at 8 kHz. A lower bitrate is rejected before encoding with a typed error
  (`bitrate_too_low_for_gapless_tag`) whose message names the minimum for the
  source's sample rate and channel count; the gapless tag is never silently
  dropped. VBR works at every quality and rate.
- No resampling. Opus encodes only 8, 12, 16, 24 or 48 kHz sources (44.1 kHz is
  rejected with `resampling_required`); MP3 only the standard rates 8, 11.025, 12,
  16, 22.05, 24, 32, 44.1 and 48 kHz; Vorbis keeps the source rate.
- Encoding takes 1 or 2 channels (more is rejected, never downmixed). Decoding
  accepts Vorbis and Opus (mapping family 0 and 1) with up to 8 channels, written
  in WAV order with the standard channel mask.
- Decoding yields 16-bit PCM. Encoder delay and padding are removed from container
  data and reported (`encoder_delay_trimmed`, `trimmed_leading_frames`,
  `trimmed_trailing_frames`): Vorbis granule positions, Opus pre-skip and final
  granule, and for MP3 only a LAME/Info (or FFmpeg Lavc) gapless tag. An MP3
  without one is decoded untrimmed and `encoder_delay_unknown` is true: the output
  keeps the encoder and decoder delay and the final padding. Opus always decodes at
  48 kHz; `original_sample_rate` reports the input rate the stream declares, and
  nothing is resampled back to it.
- Duration a decoder reports for our output: exactly the source length for Vorbis;
  exactly the source length at 48 kHz for Opus (frames × 48000 / source rate); for
  MP3 exactly the source length in decoders that honour the LAME tag (FFmpeg, and
  `mp3_to_wav`), otherwise about 1105 extra leading samples plus trailing padding.
- Metadata and album art are not carried in either direction. Encoders write no
  tags (only the libvorbis or libopus vendor string the format requires); decoders
  count and skip Vorbis comments and OpusTags entries including
  `METADATA_BLOCK_PICTURE` (`comments_dropped_count`), ID3v2, ID3v1 and APEv2 tags
  including pictures (`tags_skipped`) and other multiplexed Ogg streams
  (`streams_skipped`).
- Rejected: chained Ogg files, Opus mapping families other than 0 and 1, MP3 free
  format and Layer I/II, sample-rate or channel-mode changes inside an MP3, corrupt
  pages or frames and trailing bytes.
- Output is deterministic for a given build; identical bytes across compilers,
  CPUs or platforms are not promised. Quality was qualified by waveform SNR floors,
  frame-exact lengths and FFmpeg 6.1.1 as an independent decoder
  (`capsule/arken_audio_codecs_v1/NOTES.md`); no listening test or perceptual
  quality level is claimed.

Results report `sample_rate`, `channels`, `bits_per_sample`, `frames`,
`skipped_metadata`, `comments_dropped` and `channel_mask_dropped`, and
`lossy_source`, `lossy_output`, `generation_loss`, `encoder_delay_trimmed`,
`encoder_delay_unknown`, `trimmed_leading_frames`, `trimmed_trailing_frames`,
`comments_dropped_count`, `tags_skipped`, `streams_skipped`,
`original_sample_rate` and `silenced_frames` (MP3 leading frames whose bit
reservoir lies before a cut file's start, emitted as silence). For a lossy output
the sample fields describe the PCM that was encoded.

Limits: 64 MiB input and output, 33,554,432 samples (frames × channels), 1–8
channels, up to 384 kHz, 8 MiB per metadata block. That is about 6 min 20 s of
16-bit 44.1 kHz stereo. For lossy sources the sample budget, not the file size, is
the binding limit: lossy decoding is bounded to about 6 min 20 s of 44.1 kHz stereo
or 5 min 49 s of 48 kHz stereo (every Opus file decodes at 48 kHz), checked against
the container's declared length before decoding; a longer file is rejected, never
decoded in part, although a 64 MiB MP3 can hold hours. Conversion is resident, not
streaming: one call holds the source, the samples as 32-bit integers and the output
(about 256 MiB at the limits).

## Document profile

Documents are extracted, not rendered: no layout, positions, fonts, colours or
page structure, and no reading-order analysis beyond document order. All formats
feed one structural model and the same TXT and Markdown writers.

### DOCX and RTF

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

### Presentations and OpenDocument

PPTX: slides in presentation order, each introduced by a `Slide N` line (`# Slide
N` in Markdown) and its title (`## ` in Markdown), then body text with bullet and
numbering levels, tables and http/https/mailto links. Shapes are read in
shape-tree order. Text inherited from layouts and masters, images, charts,
SmartArt, OLE objects, media, comments and date, footer and slide-number
placeholders are excluded; hidden slides are not filtered. Encrypted and
macro-enabled files are rejected.

ODP: pages in order with the same `Slide N` structure: title, text boxes, shape
text, lists and tables. Images, charts, embedded objects and page-number, date and
footer fields are excluded.

Speaker notes (PPTX and ODP) are off by default and counted in
`excluded_notes`; `include_notes: true` appends each slide's notes as a trailing
`Notes:` paragraph (`**Notes:**` in Markdown) and counts them in `notes_included`.

ODT: the body only. Headings, paragraphs, nested lists, tables (repeated rows and
columns expanded) and links. Tracked changes appear accepted; deletions are
counted. A list whose style is not known (no style definition reachable) becomes a
bullet list. Footnotes, endnotes, comments, headers and footers (master pages in
`styles.xml`), frames, drawings and images are excluded. Password-protected files
and OpenDocument files of another kind (for example a spreadsheet given as ODT)
are rejected.

### EPUB

EPUB 2 and 3: the well-formed XHTML spine items in spine order. Headings,
paragraphs, nested lists, tables, block quotes, preformatted code (its language
taken from a `language-x` or `lang-x` class), horizontal rules and
http/https/mailto links are kept; relative links into the book become text. A
chapter without its own `h1`–`h6` is titled from the table of contents. Images,
SVG, MathML, media, scripts, styles, navigation documents and `nav` elements,
non-linear spine items and non-XHTML spine items are excluded and counted; CSS is
not applied (`display:none` is not honoured, `hidden` is). DRM-protected books
(any `META-INF/encryption.xml` entry other than font obfuscation, or
`META-INF/rights.xml`) and spine items that are HTML but not well-formed XML are
rejected.

In TXT a block quote is a plain paragraph, a horizontal rule is one extra blank
line and code is verbatim. In Markdown a block quote is `> `, a horizontal rule is
`---` and code is fenced with backticks longer than any backtick run inside it;
the language is written only when it matches `[A-Za-z0-9_+.#-]{1,32}`. A list item
with several paragraphs is still one item whose paragraphs are joined by a line
break.

### Results and limits

Results report `paragraphs`, `headings`, `list_items`, `tables`, `table_rows`,
`block_quotes`, `code_blocks`, `thematic_breaks`, `slides`, `chapters`,
`notes_included`, `toc_headings`, and what was deliberately left out:
`excluded_images`, `excluded_graphics`, `excluded_notes`, `excluded_annotations`,
`excluded_deletions`, `excluded_placeholders`, `excluded_scripts`,
`excluded_navigation`, `excluded_non_linear` and `excluded_unsupported_items`
(element or item counts; zero for DOCX and RTF, whose extractor keeps no exclusion
report).

Limits: 32 MiB input, 16 MiB per expanded package part (and 64 MiB expanded across
all parts read), 32 MiB output, 16 MiB of extracted text, 4096 slides or spine
items, nesting depth 128, 1,000,000 table cells including ODF repeats. Extraction
is resident: one expanded part, its XML tree and the text model coexist (about
8–10 times the expanded part, measured for DOCX; PPTX, ODT, ODP and EPUB use the
same parser one part at a time and were not measured separately).

## Spreadsheets and tables

One sheet is converted per call: the first visible sheet unless `sheet_index`
(zero-based, in workbook order, hidden sheets included) or `sheet_name` (exact,
case-sensitive) selects another. `sheet_names` lists every sheet and
`selected_sheet` names the one converted. Chart, dialog and macro sheets cannot be
selected.

Values are those stored in the file. **Formulas are never evaluated**: XLSX `<v>`
and ODS `office:value` hold the result last cached by the producing application;
`formulas` counts formula cells and `formulas_without_value` those without a cached
value (written empty). Dates and times become ISO 8601. Merged ranges are reported
in `merged_ranges`, not expanded: the covered cells are empty. Number formats,
currency and percent display, fonts, colours, column widths, hidden rows and
columns, comments, charts, images, pivot tables, data validation, defined names and
hyperlinks are not carried; rich text is flattened. Rows shorter than the widest
row are padded and counted in `ragged_rows`. ODS empty repeated rows and cells after
the data are trimmed, never materialized, and counted in `trimmed_repeated_rows`
and `trimmed_repeated_cells` (saturating at 4,294,967,295).

Rejected: encrypted and macro-enabled workbooks, XLSB, legacy XLS and other OLE
files, flat ODS (`.fods`), non-UTF-8 CSV/TSV, unterminated quotes, and JSON that is
not an array of scalar arrays or of flat objects (including duplicate keys and
nesting deeper than 64).

CSV and TSV follow RFC 4180 and are written as UTF-8 with LF line ends; fields
holding the delimiter, a quote or a line break are quoted. `delimiter` (`comma`,
`semicolon` or `tab`) applies to a CSV input and a CSV output. CSV fields that
follow the JSON number grammar, and `true`/`false`, become typed cells.

JSON output is an array of row arrays, or with `json_shape: objects` one object per
row keyed by the first row; empty or duplicate keys are renamed (`renamed_keys`).
Numbers are written as JSON numbers only when the value round-trips exactly through
binary64; booleans as `true`/`false`; everything else as strings. JSON input of
objects takes its header row from the union of keys in first-seen order.

Results report `rows`, `columns`, `sheet_count`, `sheet_names`, `selected_sheet`,
`ragged_rows`, `merged_ranges`, `formulas`, `formulas_without_value`,
`trimmed_repeated_rows`, `trimmed_repeated_cells`, `renamed_keys` and
`byte_order_mark` (the source began with a UTF-8 BOM).

Limits: 32 MiB input, 16 MiB per expanded XLSX/ODS part, 1,048,576 rows, 16,384
columns, 2,000,000 cells (checked before allocation, so a single cell at
`XFD1048576` is rejected rather than allocated), 1 MiB per cell, 64 MiB of cell
text, 2,000,000 shared strings, 4096 sheets, 64 MiB output. Conversion is
resident and not streaming: the XLSX/ODS path costs about 15–17 bytes of memory per
byte of worksheet XML (the XML node tree dominates), so a 16 MiB worksheet part
needs about 280 MB; a CSV at the 2,000,000-cell cap measured about 280 MB peak.

## Markup profile

HTML to Markdown (CommonMark with GFM tables and strikethrough): headings,
paragraphs, lists, block quotes, fenced code (language from a `language-` class),
code spans, emphasis, strikethrough, rectangular tables, definition lists,
http/https/mailto links and http/https images are kept. Other links (relative,
fragment, `javascript:`, `data:`, ...) become text (`links_as_text`); other images
become their alt text (`images_as_text`) or are dropped without one
(`images_dropped`); tables with spans or block content become paragraphs
(`tables_as_paragraphs`). Scripts, styles, `noscript` and templates, iframes,
objects, embeds, SVG, MathML, canvas, audio, video and hidden elements are dropped
(`dropped_elements`); `drop_navigation: true` also drops `nav`, `aside`, `header`
and `footer`. Comments are skipped (`comments_skipped`). CSS is not evaluated
beyond inline `display:none`/`visibility:hidden`, relative links are not resolved,
and no raw HTML is ever emitted. Only UTF-8 is accepted: HTML declaring another
charset, or starting with a UTF-16 BOM, is rejected. The tree builder is a
simplified HTML5 builder, so pathological misnesting can differ from a browser.

Markdown to HTML renders a CommonMark subset: ATX and setext headings, paragraphs,
block quotes, lists, indented and fenced code, thematic breaks, emphasis, code
spans, inline links and images, autolinks and hard breaks, plus GFM tables and
strikethrough. Reference links, footnotes, task lists and bare-URL autolinks are
not supported. Raw HTML is never passed through: it is shown as text
(`html_escaped`). URL policy: link destinations must be `http`, `https`, `mailto`
or relative, image sources `http`, `https` or relative; scheme-relative URLs and
every other scheme are refused and rendered as text (`unsafe_links`,
`unsafe_images`). Output is an HTML fragment, or with `full_document: true` a
complete UTF-8 document whose title is the first heading. Results also report
`links`, `images` and `unknown_entities`.

## Subtitles

SRT and WebVTT convert through one cue model: start and end times and text with
`b`, `i` and `u`. SRT to WebVTT drops SRT coordinates (`dropped_cue_settings`),
strips `font` and other tags and `{\...}` override codes (`stripped_tags`) and
tolerates missing index lines (`missing_indices`). WebVTT to SRT drops cue settings
(position, alignment), cue identifiers that differ from the SRT number
(`dropped_identifiers`), header text and NOTE, STYLE and REGION blocks
(`dropped_blocks`), and voice, class, language, ruby and timestamp tags
(`stripped_tags`); cues are numbered from 1. SRT has no escaping, so WebVTT text
such as `&lt;b&gt;` becomes a literal `<b>`. Only UTF-8 is accepted. A missing
WEBVTT header, a malformed timestamp or block, a cue that ends before it starts and
a cue that starts before the previous one are rejected, and the message names the
input line. Results report `cues` and the counts above.

Limits for markup and subtitles: 8 MiB input and 8 MiB output for every pair; a
larger result is rejected, never cut. Hostile Markdown made only of syntax
characters converts at about 0.5 s per MiB, and deeply nested HTML lists around
8 MiB of text take about 3 s.

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
  BMP and TGA encoding; zlib for TIFF Deflate and DOCX, XLSX, ODS, PPTX, ODT, ODP
  and EPUB members.
- skcms (BSD-3-Clause): ICC colour conversion.
- libwebp 1.6.0 (BSD-3-Clause): WebP decode and encode.
- libFLAC 1.5.0 (BSD-3-Clause; library only, no GPL programs or LGPL helpers).
- libogg 1.3.6, libvorbis 1.3.7 (libvorbis and libvorbisenc) and Opus 1.5.2
  (libopus, float build) (BSD-3-Clause): Ogg Vorbis and Ogg Opus decode and encode.
- minimp3 (CC0-1.0), vendored in `sdk/arken_sdk_dev/third_party/minimp3` from
  upstream commit `ea99364`: MP3 decoding.
- LAME 3.100 (libmp3lame only, LGPL-2.0-or-later): MP3 encoding. It is statically
  linked into the server binary. Distributing that binary carries the LGPL
  obligations (LAME's notices, its corresponding source and the ability to relink
  with a modified LAME, for example from this repository's sources or object
  files); this repository does not claim they are settled. MP3 patents have
  expired.
- nlohmann/json 3.12.0 (MIT), vendored in `sdk/arken_sdk_dev/third_party/`.
- Development qualification only, not admitted: libheif 1.23.7
  (LGPL-3.0-or-later), libde265 1.1.3 (LGPL-3.0-or-later) and libaom 3.15.3
  (BSD-2-Clause with the AOM Patent License 1.0). They are linked into the root
  build's server binary for HEIC/AVIF qualification. Distributing that binary
  carries the LGPL obligations (notices, corresponding source, relinking); this
  repository does not claim they are settled.
