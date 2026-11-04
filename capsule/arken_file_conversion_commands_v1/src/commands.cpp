#include "arken_file_conversion_commands_v1/api.hpp"
#include "arken_file_conversion_v1/api.hpp"
#include "arken_file_conversion_v1/audio.hpp"
#include "arken_file_conversion_v1/documents.hpp"
#include "arken_file_conversion_v1/markup.hpp"
#include "arken_file_conversion_v1/tables.hpp"
#include "arken/files/workspace/v1/native_files.hpp"
#include "arken/digest/v1/digest.hpp"
#include "arken_schema_generated/command-json.hpp"
#include <array>
#include <limits>

namespace arken::file_conversion::commands::v1 {
namespace mcp=arken::mcp::v1;
namespace J=arken::schema::cpp::v1::tool_json;
namespace S=arken::schema::generated::conversion_files::v1;
namespace C=arken::file_conversion::v1;
namespace I=arken::image::v1;
namespace A=arken::audio::v1;
namespace D=arken::document::v1;
namespace T=arken::document::v1::tabular;
namespace M=arken::markup::v1;
using Json=mcp::Json;
namespace {
// Each family is admitted by its own conversion-owner profile with its own limits; the
// descriptions below state what that profile actually does, including what it leaves out.
struct Pair {std::uint32_t id;I::Format input,output;const char* description;};
constexpr Pair pairs[]{
 {3,I::Format::png,I::Format::jpeg,"Convert PNG to JPEG. sRGB, iCCP and cHRM/gAMA colour information is applied; 16-bit PNG is rounded to 8 bits per channel. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {4,I::Format::jpeg,I::Format::png,"Convert JPEG to PNG. Exif orientation and an embedded ICC profile are applied; Adobe CMYK/YCCK is converted to RGB. Arithmetic-coded, lossless and 12-bit JPEG are rejected. Detail already lost to JPEG compression is not restored. Uses relative source_path and a new output_path."},
 {5,I::Format::bmp,I::Format::png,"Convert BMP to PNG. Reads OS/2 and Windows V1-V5 headers, 1-32 bit pixels, bitfields and RLE4/RLE8; calibrated and embedded colour spaces are applied, linked profiles are rejected. Alpha is preserved. Uses relative source_path and a new output_path."},
 {6,I::Format::bmp,I::Format::jpeg,"Convert BMP to JPEG. Reads OS/2 and Windows V1-V5 headers, 1-32 bit pixels, bitfields and RLE4/RLE8. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {7,I::Format::tga,I::Format::png,"Convert TGA to PNG. Preserves admitted transparency. Uses relative source_path and a new output_path."},
 {8,I::Format::dds,I::Format::png,"Convert a supported DDS texture to PNG for inspection and editing. Unsupported DDS profiles are rejected. Uses relative source_path and a new output_path."},
 {9,I::Format::webp,I::Format::png,"Convert WebP to PNG. Accepts lossy and lossless stills. An animation converts its first frame only; source_frames reports the frame count. An embedded ICC profile is applied; Exif and XMP are ignored. Transparency is preserved. Uses relative source_path and a new output_path."},
 {10,I::Format::webp,I::Format::jpeg,"Convert WebP to JPEG. An animation converts its first frame only; source_frames reports the frame count. An embedded ICC profile is applied; Exif and XMP are ignored. jpeg_quality is 1-100 (default 95). Transparency requires alpha_policy=flatten_white. Uses relative source_path and a new output_path."},
 {11,I::Format::png,I::Format::webp,"Convert PNG to WebP. Lossless by default: exact RGBA, including the colour of transparent pixels. webp_lossless=false writes lossy VP8 at webp_quality (1-100, default 90) with exact alpha. Metadata is not written. Uses relative source_path and a new output_path."},
 {12,I::Format::gif,I::Format::png,"Convert the first frame of a GIF to PNG. An animated GIF yields one frame, never a sequence; source_frames reports how many frames the file holds. Uses relative source_path and a new output_path."},
 {13,I::Format::tiff,I::Format::png,"Convert TIFF to PNG using the first page; source_frames reports the page count. Classic and BigTIFF; strips or tiles; chunky or planar; none, LZW, Deflate, PackBits or JPEG compression; orientation applied. Gray, palette, RGB, CMYK and YCbCr with ICC profiles applied; 16-bit samples are rounded to 8 bits. CCITT, old-style JPEG, floating-point and Lab TIFF are rejected. Uses relative source_path and a new output_path."},
 {14,I::Format::png,I::Format::tiff,"Convert PNG to an RGBA TIFF. Uncompressed by default; tiff_deflate=true writes Deflate with a horizontal predictor. Transparency is preserved. Uses relative source_path and a new output_path."},
 {15,I::Format::ico,I::Format::png,"Convert a Windows ICO or CUR to PNG using its largest rendition (largest area, then deepest colour); source_frames reports the rendition count. AND masks become transparency; cursor hotspots are ignored. Uses relative source_path and a new output_path."},
 {16,I::Format::png,I::Format::ico,"Convert a PNG of at most 256x256 pixels to a single-image ICO. Larger images are rejected, never resized. Uses relative source_path and a new output_path."}
};
constexpr const char* imageDescription=
 "Convert one image. Inputs: PNG, JPEG, BMP, TGA, GIF, ICO/CUR, TIFF, WebP and supported DDS. Outputs: PNG, JPEG, BMP, TGA, ICO, TIFF and WebP. "
 "One image per call: the first GIF or WebP frame, the first TIFF page or the largest ICO/CUR rendition; source_frames reports how many the source holds. "
 "Embedded ICC profiles are applied; Exif and XMP are not copied. 16-bit PNG and TIFF are rounded to 8 bits; source_bits_per_channel reports 16. "
 "JPEG output: jpeg_quality 1-100 (default 95); transparency requires alpha_policy=flatten_white. "
 "WebP output: lossless by default; webp_lossless=false with webp_quality 1-100 (default 90) for lossy. TIFF output: uncompressed, or tiff_deflate=true. "
 "Options for another output are rejected. Limits: 8 MiB input and output, 1,048,576 pixels, 4096 per dimension, 256 per dimension for ICO output. Existing files are never overwritten.";

// Audio ids 17-19 and 45-48; the compressed-audio track takes the next free ids.
struct AudioPair {std::uint32_t id;A::Container input,output;const char* description;};
constexpr AudioPair audioPairs[]{
 {18,A::Container::wav,A::Container::flac,"Convert WAV to FLAC losslessly. Integer PCM samples (4-32 bit), sample rate, channel order and any channel mask are preserved exactly. Float, A-law, mu-law, ADPCM and RF64/Wave64 WAV are rejected, never approximated. LIST and other extra chunks are not carried; skipped_metadata counts them. FLAC level 5, verified while encoding, deterministic. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {19,A::Container::flac,A::Container::wav,"Convert FLAC to WAV losslessly. Integer samples, sample rate, channel order and any WAVEFORMATEXTENSIBLE_CHANNEL_MASK are preserved exactly; frame CRCs and the stream MD5 are checked. Tags, pictures, cue sheets and seek tables are not carried; skipped_metadata counts them. Ogg FLAC is rejected. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {45,A::Container::aiff,A::Container::wav,"Convert AIFF or AIFF-C to WAV losslessly. Reads big-endian AIFF and AIFF-C with NONE, twos, sowt or raw integer PCM (4-32 bit); samples, sample rate, channel count and channel order are preserved exactly. AIFF has no channel mask, so none is written. Float (fl32/fl64), mu-law, A-law, IMA4 and other compressed AIFF-C, and sample rates that are not whole numbers, are rejected, never approximated. MARK, INST, COMT, NAME, AUTH, ANNO, APPL, ID3 and other chunks are not carried; skipped_metadata counts them. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {46,A::Container::aiff,A::Container::flac,"Convert AIFF or AIFF-C to FLAC losslessly. Reads big-endian AIFF and AIFF-C with NONE, twos, sowt or raw integer PCM (4-32 bit); samples, sample rate, channel count and channel order are preserved exactly. AIFF has no channel mask, so none is written. Float, mu-law, A-law, IMA4 and other compressed AIFF-C, and sample rates that are not whole numbers, are rejected. Chunks other than COMM and SSND are not carried; skipped_metadata counts them. FLAC level 5, verified while encoding, deterministic. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {47,A::Container::wav,A::Container::aiff,"Convert WAV to AIFF losslessly: big-endian AIFF with COMM and SSND chunks only. Integer PCM samples (4-32 bit), sample rate and channel order are preserved exactly. AIFF has no channel-mask field: a channel mask declared by the WAV is not written and channel_mask_dropped reports it. Float, A-law, mu-law, ADPCM and RF64/Wave64 WAV are rejected. LIST and other extra chunks are not carried; skipped_metadata counts them. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."},
 {48,A::Container::flac,A::Container::aiff,"Convert FLAC to AIFF losslessly: big-endian AIFF with COMM and SSND chunks only. Integer samples, sample rate and channel order are preserved exactly; frame CRCs and the stream MD5 are checked. AIFF has no channel-mask field: a WAVEFORMATEXTENSIBLE_CHANNEL_MASK comment is not written and channel_mask_dropped reports it. Tags, pictures, cue sheets and seek tables are not carried; skipped_metadata counts them. Ogg FLAC is rejected. Limits: 64 MiB input and output. Uses relative source_path and a new output_path."}
};
constexpr const char* audioDescription=
 "Convert audio between WAV, FLAC and AIFF (input_format and output_format: wav, flac or aiff; AIFF-C is read as aiff). Lossless: integer PCM samples, sample rate, channel count and channel order are preserved exactly, "
 "and any channel mask too except when writing AIFF, which has no mask field (channel_mask_dropped reports it). "
 "Admits 4-32 bit integer PCM, 1-8 channels, up to 384 kHz. Float, A-law, mu-law and ADPCM WAV, float and compressed AIFF-C, non-integer AIFF sample rates, RF64/Wave64 and Ogg FLAC are rejected, never approximated. "
 "Tags, pictures, cue sheets, LIST chunks and AIFF chunks other than COMM/SSND are not carried; skipped_metadata counts them and comments_dropped reports dropped FLAC comments. "
 "Limits: 64 MiB input and output, 33,554,432 samples (frames x channels). Existing files are never overwritten.";

// Document ids 20-24 and 32-39. `notes`: the request carries include_notes (PPTX/ODP).
struct DocumentPair {std::uint32_t id;C::DocumentInput input;D::OutputFormat output;bool notes;const char* description;};
constexpr DocumentPair documentPairs[]{
 {21,C::DocumentInput::docx,D::OutputFormat::text,false,"Extract the main story of a Word .docx to UTF-8 plain text with LF line ends: paragraphs, headings, lists, tab-separated table rows and link URLs in parentheses. Headers, footers, footnotes, endnotes, comments, text boxes and images are excluded; tracked changes appear accepted. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {22,C::DocumentInput::docx,D::OutputFormat::markdown,false,"Extract the main story of a Word .docx to Markdown: ATX headings, nested lists, pipe tables when rectangular, bold and italic from direct formatting, links for http, https and mailto. Literal Markdown characters are escaped and no HTML is emitted. Headers, footers, notes, comments, text boxes and images are excluded. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {23,C::DocumentInput::rtf,D::OutputFormat::text,false,"Extract RTF text to UTF-8 plain text with LF line ends. Unicode escapes and code pages 1250-1254 and 1257 are decoded; text in other code pages is rejected. Headers, footers, footnotes, comments, pictures and objects are dropped; lists become bullets; nested tables are flattened. Uses relative source_path and a new output_path."},
 {24,C::DocumentInput::rtf,D::OutputFormat::markdown,false,"Extract RTF text to Markdown: headings, bulleted lists, pipe tables when rectangular, bold, italic and HYPERLINK fields. Literal Markdown characters are escaped and no HTML is emitted. Headers, footers, footnotes, comments, pictures and objects are dropped. Uses relative source_path and a new output_path."},
 {32,C::DocumentInput::pptx,D::OutputFormat::text,true,"Extract slide text from a PowerPoint .pptx to UTF-8 plain text with LF line ends: a 'Slide N' line per slide in presentation order, then the slide title, body text with bullet and numbering levels, tab-separated table rows and http/https/mailto link URLs in parentheses. Shapes are read in shape-tree order; there is no layout or reading-order analysis. Text inherited from layouts and masters, images, charts, SmartArt, OLE objects, media, comments and date/footer/slide-number placeholders are excluded (counted in the excluded_* fields); hidden slides are not filtered. Speaker notes are excluded unless include_notes=true, which appends a 'Notes:' paragraph per slide. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {33,C::DocumentInput::pptx,D::OutputFormat::markdown,true,"Extract slide text from a PowerPoint .pptx to Markdown: '# Slide N' per slide in presentation order, '## ' slide title, nested bulleted and numbered lists, pipe tables when rectangular, bold and italic, links for http, https and mailto. Literal Markdown characters are escaped and no HTML is emitted. Shapes are read in shape-tree order. Text inherited from layouts and masters, images, charts, SmartArt, objects, media, comments and date/footer/slide-number placeholders are excluded (counted). Speaker notes are excluded unless include_notes=true. Encrypted and macro-enabled files are rejected. Uses relative source_path and a new output_path."},
 {34,C::DocumentInput::odt,D::OutputFormat::text,false,"Extract the body of an OpenDocument text .odt to UTF-8 plain text with LF line ends: headings, paragraphs, nested lists, tab-separated table rows (repeated rows and columns expanded) and link URLs in parentheses. Tracked changes appear accepted (deletions counted). A list whose style is not known becomes a bullet list. Footnotes, endnotes, comments, headers, footers (styles.xml master pages), frames, drawings and images are excluded (counted where seen). Password-protected files and other OpenDocument kinds are rejected. Uses relative source_path and a new output_path."},
 {35,C::DocumentInput::odt,D::OutputFormat::markdown,false,"Extract the body of an OpenDocument text .odt to Markdown: ATX headings, nested lists, pipe tables when rectangular (repeats expanded), bold and italic from styles, links for http, https and mailto. Literal Markdown characters are escaped and no HTML is emitted. Tracked changes appear accepted. A list whose style is not known becomes a bullet list. Footnotes, endnotes, comments, headers, footers, frames, drawings and images are excluded. Password-protected files and other OpenDocument kinds are rejected. Uses relative source_path and a new output_path."},
 {36,C::DocumentInput::odp,D::OutputFormat::text,true,"Extract slide text from an OpenDocument presentation .odp to UTF-8 plain text with LF line ends: a 'Slide N' line per page in order, the title, text boxes, shape text, lists and tab-separated table rows. Images, charts, embedded objects and page-number/date/footer fields are excluded (counted). Speaker notes are excluded unless include_notes=true, which appends a 'Notes:' paragraph per slide. Password-protected files and other OpenDocument kinds are rejected. Uses relative source_path and a new output_path."},
 {37,C::DocumentInput::odp,D::OutputFormat::markdown,true,"Extract slide text from an OpenDocument presentation .odp to Markdown: '# Slide N' per page, '## ' title, text boxes, shape text, nested lists, pipe tables when rectangular and http/https/mailto links. Literal Markdown characters are escaped and no HTML is emitted. Images, charts, objects and page-number/date/footer fields are excluded (counted); speaker notes unless include_notes=true. Password-protected files are rejected. Uses relative source_path and a new output_path."},
 {38,C::DocumentInput::epub,D::OutputFormat::text,false,"Extract the reading-order text of an EPUB 2 or 3 book to UTF-8 plain text with LF line ends: well-formed XHTML spine items in order with headings, paragraphs, lists, tables, block quotes, preformatted text and http/https/mailto link URLs; a chapter without its own heading is titled from the table of contents; a horizontal rule is an extra blank line. Images, SVG, MathML, media, scripts, styles, navigation documents, non-linear and non-XHTML spine items are excluded (counted); CSS is not applied. DRM-protected books (encryption other than font obfuscation, or rights.xml) and content that is not well-formed XHTML are rejected. Uses relative source_path and a new output_path."},
 {39,C::DocumentInput::epub,D::OutputFormat::markdown,false,"Extract the reading-order text of an EPUB 2 or 3 book to Markdown: XHTML spine items in order with ATX headings, nested lists, pipe tables when rectangular, '> ' block quotes, fenced code blocks (language from a language- class), '---' thematic breaks, bold, italic and http/https/mailto links; a chapter without its own heading is titled from the table of contents. Literal Markdown characters are escaped and no HTML is emitted. Images, SVG, MathML, media, scripts, styles, navigation documents, non-linear and non-XHTML spine items are excluded (counted). DRM-protected books and content that is not well-formed XHTML are rejected. Uses relative source_path and a new output_path."}
};
constexpr const char* documentDescription=
 "Extract text from a document to UTF-8 plain text or Markdown (output_format: txt or markdown). input_format: docx (main story), rtf, pptx or odp (slide text), odt (body) or epub (XHTML spine). "
 "Paragraphs, headings, lists, tables and http/https/mailto links are kept; EPUB also keeps block quotes, code blocks and thematic breaks. "
 "Headers, footers, footnotes, endnotes, comments, text boxes, images, charts and objects are excluded; speaker notes too unless include_notes=true (pptx and odp only). "
 "Tracked changes appear accepted; fields keep their stored result. Encrypted, DRM-protected, macro-enabled and malformed files are rejected. "
 "The result reports block counts, slides, chapters and excluded-content counts. "
 "Limits: 32 MiB input, 16 MiB per expanded package part, 4096 slides or spine items, 1,000,000 table cells, 32 MiB output. Existing files are never overwritten.";

// Table ids 25-31.
struct TablePair {std::uint32_t id;T::TableFormat input,output;const char* description;};
constexpr TablePair tablePairs[]{
 {26,T::TableFormat::xlsx,T::TableFormat::csv,"Convert one worksheet of an Excel .xlsx to UTF-8 CSV (RFC 4180 quoting, LF line ends; delimiter comma by default, or semicolon or tab). The first visible worksheet unless sheet_index (zero-based, hidden sheets included) or sheet_name (exact) selects another; sheet_names lists them all. Values are the cached results stored in the file: formulas are never evaluated (formulas and formulas_without_value count them). Dates become ISO 8601. Merged ranges are reported, not expanded. Number formats, styles, comments, charts, images and hyperlinks are not carried. Short rows are padded (ragged_rows). Encrypted, macro-enabled, XLSB and legacy XLS files are rejected. Limits: 32 MiB input, 16 MiB per expanded part, 1,048,576 rows, 16,384 columns, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."},
 {27,T::TableFormat::xlsx,T::TableFormat::json,"Convert one worksheet of an Excel .xlsx to JSON: an array of row arrays by default, or json_shape=objects for one object per row keyed by the first row (empty or duplicate keys are renamed and counted in renamed_keys). Numbers are JSON numbers only when the cached value round-trips exactly; booleans are true/false; everything else is a string. The first visible worksheet unless sheet_index or sheet_name selects another. Formulas are never evaluated: cached values only. Dates become ISO 8601; merged ranges are reported, not expanded; formatting is not carried. Encrypted, macro-enabled, XLSB and XLS files are rejected. Limits: 32 MiB input, 16 MiB per expanded part, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."},
 {28,T::TableFormat::ods,T::TableFormat::csv,"Convert one table of an OpenDocument spreadsheet .ods to UTF-8 CSV (RFC 4180 quoting, LF; delimiter comma, semicolon or tab). The first visible table unless sheet_index or sheet_name selects another. Values are the cached office:value results: formulas are never evaluated. Dates and times become ISO 8601. Merged ranges are reported (covered cells are empty), not expanded. Empty repeated rows and cells after the data are trimmed and counted, never materialized. Formatting, annotations and charts are not carried. Encrypted and flat (.fods) files are rejected. Limits: 32 MiB input, 16 MiB per expanded part, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."},
 {29,T::TableFormat::csv,T::TableFormat::json,"Convert UTF-8 CSV (RFC 4180; delimiter comma by default, or semicolon or tab) to JSON: an array of row arrays, or json_shape=objects for one object per row keyed by the first (header) row. Fields that follow the JSON number grammar and round-trip exactly become numbers, true/false become booleans, everything else is a string. Short rows are padded (ragged_rows). Other encodings and unterminated quotes are rejected. Limits: 32 MiB input, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."},
 {30,T::TableFormat::json,T::TableFormat::csv,"Convert JSON to UTF-8 CSV (RFC 4180 quoting, LF; delimiter comma by default, or semicolon or tab). The JSON must be an array of scalar arrays, or an array of flat objects whose header row is the union of keys in first-seen order. Nested arrays or objects, scalars and any other shape are rejected as non-table JSON, as are duplicate keys and nesting deeper than 64. Limits: 32 MiB input, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."},
 {31,T::TableFormat::csv,T::TableFormat::tsv,"Convert UTF-8 CSV (RFC 4180; delimiter comma by default, or semicolon or tab) to TSV with LF line ends. Fields containing a tab, a quote or a line break are quoted as in CSV. Short rows are padded (ragged_rows). Other encodings and unterminated quotes are rejected. Limits: 32 MiB input, 2,000,000 cells, 64 MiB output. Uses relative source_path and a new output_path."}
};
constexpr const char* tableDescription=
 "Convert a table between formats. input_format: csv, tsv, json, xlsx or ods; output_format: csv, tsv or json (different from the input). "
 "One sheet per call: the first visible sheet unless sheet_index (zero-based) or sheet_name selects another (xlsx and ods only). "
 "delimiter (comma, semicolon or tab) applies to a CSV input and a CSV output; json_shape (arrays or objects keyed by the first row) to JSON output. Options for another format are rejected. "
 "Formulas are never evaluated: cached values only. Dates become ISO 8601; merged ranges are reported, not expanded; formatting, comments, charts and images are not carried. "
 "Encrypted, macro-enabled, XLSB, XLS and flat ODS files, non-UTF-8 text and non-table JSON are rejected. "
 "Limits: 32 MiB input, 16 MiB per expanded part, 1,048,576 rows, 16,384 columns, 2,000,000 cells, 1 MiB per cell, 64 MiB output. Existing files are never overwritten.";

// Markup ids 40-44.
struct MarkupPair {std::uint32_t id;M::Format input,output;const char* description;};
constexpr MarkupPair markupPairs[]{
 {41,M::Format::html,M::Format::markdown,"Convert a UTF-8 HTML document or fragment to Markdown (CommonMark with GFM tables and strikethrough). Headings, paragraphs, lists, block quotes, code, emphasis, rectangular tables, http/https/mailto links and http/https images are kept; other links and images become text and non-rectangular tables become paragraphs (counted). Scripts, styles, templates, iframes, objects, SVG, MathML, media and hidden elements are dropped (dropped_elements); drop_navigation=true also drops nav, aside, header and footer. CSS is not evaluated, relative links are not resolved and no raw HTML is emitted. HTML declaring a charset other than UTF-8, or UTF-16 input, is rejected. Limits: 8 MiB input, 8 MiB output. Uses relative source_path and a new output_path."},
 {42,M::Format::markdown,M::Format::html,"Render Markdown to sanitized HTML: a CommonMark subset (headings, paragraphs, lists, block quotes, code, emphasis, inline links and images, autolinks) plus GFM tables and strikethrough; reference links are not supported. Raw HTML is never passed through: it is shown as text (html_escaped). Links must be http, https, mailto or relative and images http, https or relative; other schemes such as javascript: and data: become text (unsafe_links, unsafe_images). An HTML fragment by default; full_document=true writes a complete UTF-8 document titled by the first heading. Limits: 8 MiB input, 8 MiB output. Uses relative source_path and a new output_path."},
 {43,M::Format::srt,M::Format::vtt,"Convert SRT subtitles to WebVTT. Cue times and text are kept; b, i and u tags are kept; font tags, other tags and {\\...} override codes are stripped (stripped_tags); SRT coordinates are dropped (dropped_cue_settings). Missing index lines are tolerated (missing_indices). UTF-8 only. A malformed timestamp or block, a cue ending before it starts or starting before the previous cue is rejected with its line number. Limits: 8 MiB input, 8 MiB output. Uses relative source_path and a new output_path."},
 {44,M::Format::vtt,M::Format::srt,"Convert WebVTT subtitles to SRT. Cue times and text are kept and cues are numbered from 1; b, i and u tags are kept. Cue settings (position, alignment), cue identifiers, header text, NOTE, STYLE and REGION blocks, voice names, classes, ruby and timestamp tags are dropped and counted. SRT has no escaping, so text such as <b> written as &lt;b&gt; in WebVTT becomes literal. UTF-8 only; a missing WEBVTT header or a malformed cue is rejected with its line number. Limits: 8 MiB input, 8 MiB output. Uses relative source_path and a new output_path."}
};
constexpr const char* markupDescription=
 "Convert text markup. Admitted pairs only: html to markdown, markdown to html, srt to vtt and vtt to srt (input_format and output_format). "
 "drop_navigation applies to HTML input and full_document to HTML output; options for another pair are rejected. "
 "HTML to Markdown drops scripts, styles, embedded and hidden content and keeps only http/https/mailto links; Markdown to HTML is a CommonMark subset with GFM tables, never passes raw HTML through and refuses unsafe link schemes; "
 "subtitle conversion keeps cue times, text and b/i/u, and drops cue settings, identifiers, styles, regions and notes. Each drop is counted in the result. "
 "UTF-8 only. Limits: 8 MiB input and output. Existing files are never overwritten.";

enum class Family {image,audio,document,table,markup,unknown};
Family family(std::uint32_t command) {
 if(command==2)return Family::image;
 for(const auto& p:pairs)if(p.id==command)return Family::image;
 if(command==17)return Family::audio;
 for(const auto& p:audioPairs)if(p.id==command)return Family::audio;
 if(command==20)return Family::document;
 for(const auto& p:documentPairs)if(p.id==command)return Family::document;
 if(command==25)return Family::table;
 for(const auto& p:tablePairs)if(p.id==command)return Family::table;
 if(command==40)return Family::markup;
 for(const auto& p:markupPairs)if(p.id==command)return Family::markup;
 return Family::unknown;
}
std::vector<mcp::ToolPresentation> presentations() {
 std::vector<mcp::ToolPresentation> result{{2,false,false,false,imageDescription}};
 for(const auto& p:pairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({17,false,false,false,audioDescription});
 for(const auto& p:audioPairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({20,false,false,false,documentDescription});
 for(const auto& p:documentPairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({25,false,false,false,tableDescription});
 for(const auto& p:tablePairs)result.push_back({p.id,false,false,false,p.description});
 result.push_back({40,false,false,false,markupDescription});
 for(const auto& p:markupPairs)result.push_back({p.id,false,false,false,p.description});
 return result;
}
const mcp::Server& server() {
 static const auto value=[] {
  mcp::ServerDescription d{"Arken Convert","r6262",
   "Convert images, audio (WAV, FLAC, AIFF), documents (DOCX, RTF, PPTX, ODT, ODP, EPUB to text or Markdown), tables (CSV, TSV, JSON, XLSX, ODS to CSV, TSV or JSON) "
   "and markup (HTML and Markdown, SRT and WebVTT) inside the launch-selected folder. Paths are relative; parents must exist. "
   "Output files are created exclusively, never overwritten; sources are never modified. Limits differ by family: 8 MiB images, 64 MiB audio, 32 MiB documents and tables, 8 MiB markup. "
   "Results state what a conversion did not carry: source_frames for multi-frame images, skipped_metadata and channel_mask_dropped for audio, excluded_* counts for documents, "
   "formulas and merged ranges for tables, dropped and degraded counts for markup. "
   "Discovery is free of filesystem effects. No upload, account, network or payment is required.",
   S::commandCatalog,presentations()};
  d.toolNameStyle=mcp::ToolNameStyle::familyLocal;return mcp::Server(std::move(d));
 }();return value;
}
template<class Request> void paths(const Request& v) {
 J::require(!v.source_path.value.empty()&&v.source_path.value.size()<=4096
  &&!v.output_path.value.empty()&&v.output_path.value.size()<=4096,"path bounds");
 J::require(v.source_path!=v.output_path,"output must be a new path");
}
struct ImageInput {std::string source,output;C::ImageOptions options;};
template<class Request> ImageInput imageInput(const Request& v,I::Format from,I::Format to) {
 paths(v);
 C::ImageOptions options{from,to};
 if constexpr(requires {v.jpeg_quality;v.alpha_policy;}) {
  J::require(to==I::Format::jpeg||(!v.jpeg_quality&&!v.alpha_policy),"JPEG options require JPEG output");
  options.jpegQuality=v.jpeg_quality?v.jpeg_quality->value:95u;
  options.alphaPolicy=v.alpha_policy?static_cast<I::AlphaPolicy>(static_cast<std::uint32_t>(*v.alpha_policy)-1u):I::AlphaPolicy::preserve;
 }
 if constexpr(requires {v.webp_lossless;v.webp_quality;v.tiff_deflate;}) {
  // Presence matters: an option for another output, or a quality without lossy mode,
  // is rejected rather than ignored.
  J::require(to==I::Format::webp||(!v.webp_lossless&&!v.webp_quality),"WebP options require WebP output");
  J::require(!v.webp_quality||(v.webp_lossless&&!v.webp_lossless->value),"webp_quality requires webp_lossless=false");
  J::require(to==I::Format::tiff||!v.tiff_deflate,"tiff_deflate requires TIFF output");
  options.webpLossless=v.webp_lossless?v.webp_lossless->value:true;
  options.webpQuality=v.webp_quality?v.webp_quality->value:90u;
  options.tiffDeflate=v.tiff_deflate?v.tiff_deflate->value:false;
 }
 return {v.source_path.value,v.output_path.value,options};
}
std::string media(I::Format format) {
 switch(format){case I::Format::png:return "image/png";case I::Format::jpeg:return "image/jpeg";
  case I::Format::bmp:return "image/bmp";case I::Format::tga:return "image/x-tga";
  case I::Format::ico:return "image/vnd.microsoft.icon";case I::Format::tiff:return "image/tiff";
  case I::Format::webp:return "image/webp";default:throw std::invalid_argument("output format");}
}
std::string media(A::Container format) {
 switch(format){case A::Container::wav:return "audio/wav";case A::Container::flac:return "audio/flac";
  case A::Container::aiff:return "audio/aiff";default:throw std::invalid_argument("output format");}
}
std::string media(T::TableFormat format) {
 switch(format){case T::TableFormat::csv:return "text/csv; charset=utf-8";
  case T::TableFormat::tsv:return "text/tab-separated-values; charset=utf-8";
  case T::TableFormat::json:return "application/json";default:throw std::invalid_argument("output format");}
}
Json error(const Json& id,std::string_view code,std::string_view message) {
 return mcp::toolReply(id,{{"code",code},{"message",message}},true);
}
// The codec owner names the exact rejection (animation, metadata, dimensions, matte) so a
// client can act on it without guessing which limit the shared profile message refers to.
std::string conversionMessage(const C::ImageResult& converted) {
 std::string message{C::ErrorMessage(converted.error)};
 if(converted.error==C::Error::decode_failed&&converted.decodeError!=I::DecodeError::none)
  message.append(": ").append(I::ErrorMessage(converted.decodeError));
 if(converted.error==C::Error::encode_failed&&converted.encodeError!=I::EncodeError::none)
  message.append(": ").append(I::EncodeErrorMessage(converted.encodeError));
 return message;
}
std::string conversionMessage(const C::AudioResult& converted) {
 std::string message{C::AudioErrorMessage(converted.error)};
 if(converted.codecError!=A::Error::none)message.append(": ").append(A::ErrorMessage(converted.codecError));
 return message;
}
std::string conversionMessage(const C::DocumentResult& converted) {
 std::string message{C::DocumentErrorMessage(converted.error)};
 if(converted.documentError!=D::Error::none)message.append(": ").append(D::ErrorMessage(converted.documentError));
 if(converted.textDocumentError!=D::TextDocumentError::none)message.append(": ").append(D::ErrorMessage(converted.textDocumentError));
 return message;
}
std::string conversionMessage(const C::TableResult& converted) {
 std::string message{C::TableErrorMessage(converted.error)};
 if(converted.tableError!=T::Error::none)message.append(": ").append(T::ErrorMessage(converted.tableError));
 return message;
}
std::string conversionMessage(const C::MarkupResult& converted) {
 std::string message{C::MarkupErrorMessage(converted.error)};
 if(converted.markupError!=M::Error::none)message.append(": ").append(M::ErrorMessage(converted.markupError));
 if(converted.line)message.append(" (line ").append(std::to_string(converted.line)).append(")");
 return message;
}
std::string digest(std::span<const std::byte> bytes) {
 const auto value=arken::digest::v1::sha256(bytes);std::array<char,71> hash{};
 J::require(value&&arken::digest::v1::format_sha256(*value,hash),"output digest");
 return {hash.data(),hash.size()};
}
// Report counts are bounded by the profile (cells, nodes, expanded bytes); exceeding u32
// would be an owner defect, reported before anything is written.
std::uint32_t count(std::uint64_t value) {
 J::require(value<=std::numeric_limits<std::uint32_t>::max(),"report count");return static_cast<std::uint32_t>(value);
}
// ODS repeat attributes can name billions of empty cells that are never materialized;
// their count saturates rather than fail a valid conversion.
std::uint32_t saturated(std::uint64_t value) {
 return static_cast<std::uint32_t>(std::min<std::uint64_t>(value,std::numeric_limits<std::uint32_t>::max()));
}
char delimiter(S::delimiter_kind value) {
 switch(value){case S::delimiter_kind::comma:return ',';case S::delimiter_kind::semicolon:return ';';case S::delimiter_kind::tab:return '\t';}
 throw std::invalid_argument("delimiter");
}
}
struct Session::Impl {
 std::unique_ptr<arken::files::workspace::v1::NativeFiles> files;
 // Writes the output exclusively and replies with `result`; every family shares this.
 template<class Result> Json publish(const Json& id,const std::string& output,std::span<const std::byte> bytes,
                                     Result result,bool& published) {
  result.output_path.value=output;result.output_bytes.value=static_cast<std::uint32_t>(bytes.size());
  result.sha256.value=digest(bytes);
  const auto written=files->create(output,bytes);published=written.published;result.durable.value=written.durable;
  if(!written) {
   if(written.published){auto detail=J::write(result);detail["code"]="outputPublished";
    detail["message"]="Output was published but finalization failed. Inspect this path and digest before retrying.";
    detail["published"]=true;return mcp::toolReply(id,detail,true);}
   return error(id,written.error,"Output was not created. Choose a new relative path with an existing parent; existing files are never overwritten.");
  }
  return mcp::toolReply(id,J::write(result),false);
 }
 Json image(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  ImageInput value;
  if(command==2){const auto v=J::read<S::conversion_request>(arguments);
   value=imageInput(v,static_cast<I::Format>(v.input_format),static_cast<I::Format>(v.output_format));}
  else for(const auto& p:pairs)if(p.id==command) {
   const bool options=p.output==I::Format::jpeg||p.output==I::Format::webp||p.output==I::Format::tiff;
   value=options?imageInput(J::read<S::pair_request>(arguments),p.input,p.output)
    :imageInput(J::read<S::file_request>(arguments),p.input,p.output);
  }
  const auto source=files->read(value.source,C::maximumSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 8 MiB.");
  const auto converted=C::ConvertStandaloneImage(source.bytes,value.options);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::conversion_result result;result.media_type.value=media(value.options.output);
  result.width.value=converted.width;result.height.value=converted.height;
  result.alpha_flattened.value=converted.alphaFlattened;
  result.source_frames.value=converted.sourceFrames;result.source_bits_per_channel.value=converted.sourceBitsPerChannel;
  return publish(id,value.output,converted.bytes,std::move(result),published);
 }
 Json audio(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;A::Container from=A::Container::wav,to=A::Container::flac;
  if(command==17){const auto v=J::read<S::audio_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   // audio_kind values equal the codec's Container values.
   from=static_cast<A::Container>(v.input_format);to=static_cast<A::Container>(v.output_format);}
  else for(const auto& p:audioPairs)if(p.id==command){const auto v=J::read<S::file_request>(arguments);paths(v);
   sourcePath=v.source_path.value;outputPath=v.output_path.value;from=p.input;to=p.output;}
  const auto source=files->read(sourcePath,C::maximumAudioSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 64 MiB.");
  const auto converted=C::ConvertStandaloneAudio(from,to,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::audio_result result;result.media_type.value=media(to);
  result.sample_rate.value=converted.sampleRate;result.channels.value=converted.channels;
  result.bits_per_sample.value=converted.bitsPerSample;
  // The profile's sample budget (2^25 frames x channels) keeps frames within u32.
  result.frames.value=count(converted.frames);
  result.skipped_metadata.value=converted.skippedMetadata;result.comments_dropped.value=converted.ignoredComments;
  result.channel_mask_dropped.value=converted.channelMaskDropped;
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
 Json document(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;C::DocumentOptions options;
  if(command==20){const auto v=J::read<S::document_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   // document_input_kind values equal C::DocumentInput; document_output_kind equals D::OutputFormat.
   options.input=static_cast<C::DocumentInput>(v.input_format);options.output=static_cast<D::OutputFormat>(v.output_format);
   J::require(!v.include_notes||options.input==C::DocumentInput::pptx||options.input==C::DocumentInput::odp,"include_notes requires pptx or odp input");
   options.includeNotes=v.include_notes&&v.include_notes->value;}
  else for(const auto& p:documentPairs)if(p.id==command){
   options.input=p.input;options.output=p.output;
   if(p.notes){const auto v=J::read<S::presentation_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
    options.includeNotes=v.include_notes&&v.include_notes->value;}
   else{const auto v=J::read<S::file_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;}
  }
  const auto source=files->read(sourcePath,C::maximumDocumentSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 32 MiB.");
  const auto converted=C::ConvertStandaloneDocument(options,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::document_result result;result.media_type.value=std::string(D::MediaType(options.output));
  result.paragraphs.value=converted.paragraphs;result.headings.value=converted.headings;
  result.list_items.value=converted.listItems;result.tables.value=converted.tables;result.table_rows.value=converted.tableRows;
  result.block_quotes.value=converted.blockQuotes;result.code_blocks.value=converted.codeBlocks;result.thematic_breaks.value=converted.thematicBreaks;
  const auto& r=converted.report;const auto& x=r.excluded;
  result.slides.value=r.slides;result.chapters.value=r.chapters;result.notes_included.value=r.notesIncluded;result.toc_headings.value=r.tocHeadings;
  result.excluded_images.value=count(x.images);result.excluded_graphics.value=count(x.graphics);result.excluded_notes.value=count(x.notes);
  result.excluded_annotations.value=count(x.annotations);result.excluded_deletions.value=count(x.deletions);
  result.excluded_placeholders.value=count(x.placeholders);result.excluded_scripts.value=count(x.scripts);
  result.excluded_navigation.value=count(x.navigation);result.excluded_non_linear.value=count(x.nonLinear);
  result.excluded_unsupported_items.value=count(x.unsupportedItems);
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
 template<class Request> static void sheet(const Request& v,C::TableOptions& options) {
  J::require(!(v.sheet_index&&v.sheet_name),"sheet_index and sheet_name are exclusive");
  if(v.sheet_index)options.sheet={T::SheetSelection::index,v.sheet_index->value,{}};
  if(v.sheet_name){J::require(!v.sheet_name->value.empty()&&v.sheet_name->value.size()<=1024,"sheet_name bounds");
   options.sheet={T::SheetSelection::name,0,v.sheet_name->value};}
 }
 Json table(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;C::TableOptions options;
  const auto common=[&](const auto& v){paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   if constexpr(requires {v.delimiter;})if(v.delimiter)options.delimiter=delimiter(*v.delimiter);
   if constexpr(requires {v.json_shape;})if(v.json_shape)options.json=*v.json_shape==S::json_shape_kind::objects?T::JsonShape::objects:T::JsonShape::arrays;
   if constexpr(requires {v.sheet_index;v.sheet_name;})sheet(v,options);
  };
  if(command==25){const auto v=J::read<S::table_request>(arguments);
   // table_input_kind/table_output_kind values equal T::TableFormat.
   options.input=static_cast<T::TableFormat>(v.input_format);options.output=static_cast<T::TableFormat>(v.output_format);
   J::require(options.input!=options.output,"output_format must differ from input_format");
   J::require(!v.delimiter||options.input==T::TableFormat::csv||options.output==T::TableFormat::csv,"delimiter requires CSV input or output");
   J::require(!v.json_shape||options.output==T::TableFormat::json,"json_shape requires JSON output");
   J::require((!v.sheet_index&&!v.sheet_name)||options.input==T::TableFormat::xlsx||options.input==T::TableFormat::ods,"sheet selection requires xlsx or ods input");
   common(v);}
  else for(const auto& p:tablePairs)if(p.id==command){
   options.input=p.input;options.output=p.output;
   if(command==26||command==28)common(J::read<S::sheet_csv_request>(arguments));
   else if(command==27)common(J::read<S::sheet_json_request>(arguments));
   else if(command==29)common(J::read<S::csv_json_request>(arguments));
   else common(J::read<S::delimited_request>(arguments));
  }
  const auto source=files->read(sourcePath,C::maximumTableSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 32 MiB.");
  const auto converted=C::ConvertStandaloneTable(options,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::table_result result;result.media_type.value=media(options.output);
  const auto& r=converted.report;
  result.rows.value=count(r.rows);result.columns.value=count(r.columns);
  result.sheet_count.value=count(r.sheets.size());
  for(const auto& s:r.sheets){S::text name;name.value=s.name;result.sheet_names.push_back(std::move(name));}
  result.selected_sheet.value=r.selectedSheet;result.ragged_rows.value=count(r.raggedRows);
  result.merged_ranges.value=count(r.mergedRanges.size());result.formulas.value=count(r.formulas);
  result.formulas_without_value.value=count(r.formulasWithoutValue);
  result.trimmed_repeated_rows.value=saturated(r.trimmedRepeatedRows);result.trimmed_repeated_cells.value=saturated(r.trimmedRepeatedCells);
  result.renamed_keys.value=count(converted.renamedKeys);result.byte_order_mark.value=r.byteOrderMark;
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
 Json markup(const Json& id,std::uint32_t command,const Json& arguments,bool& published) {
  std::string sourcePath,outputPath;C::MarkupOptions options;
  if(command==40){const auto v=J::read<S::markup_request>(arguments);paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;
   // markup_input_kind/markup_output_kind values equal M::Format.
   options.input=static_cast<M::Format>(v.input_format);options.output=static_cast<M::Format>(v.output_format);
   J::require(C::MarkupPairAdmitted(options.input,options.output),"admitted pairs: html-markdown, markdown-html, srt-vtt, vtt-srt");
   J::require(!v.drop_navigation||options.input==M::Format::html,"drop_navigation requires HTML input");
   J::require(!v.full_document||options.output==M::Format::html,"full_document requires HTML output");
   options.dropNavigation=v.drop_navigation&&v.drop_navigation->value;options.fullDocument=v.full_document&&v.full_document->value;}
  else for(const auto& p:markupPairs)if(p.id==command){
   options.input=p.input;options.output=p.output;
   const auto take=[&](const auto& v){paths(v);sourcePath=v.source_path.value;outputPath=v.output_path.value;};
   if(command==41){const auto v=J::read<S::html_markdown_request>(arguments);take(v);options.dropNavigation=v.drop_navigation&&v.drop_navigation->value;}
   else if(command==42){const auto v=J::read<S::markdown_html_request>(arguments);take(v);options.fullDocument=v.full_document&&v.full_document->value;}
   else take(J::read<S::file_request>(arguments));
  }
  const auto source=files->read(sourcePath,C::maximumMarkupSourceBytes);
  if(!source)return error(id,source.error,"Input must be a nonempty regular file inside the selected folder, no larger than 8 MiB.");
  const auto converted=C::ConvertStandaloneMarkup(options,source.bytes);
  if(!converted)return error(id,"conversionFailed",conversionMessage(converted));
  S::markup_result result;result.media_type.value=std::string(M::MediaType(options.output));
  const auto& h=converted.html;const auto& m=converted.markdown;const auto& s=converted.subtitles;
  result.dropped_elements.value=count(h.scriptStyleDropped+h.embeddedDropped+h.hiddenDropped+h.navigationDropped);
  result.links_as_text.value=count(h.linksAsText);result.images_as_text.value=count(h.imagesAsText);
  result.images_dropped.value=count(h.imagesDropped);result.tables_as_paragraphs.value=count(h.tablesAsParagraphs);
  result.comments_skipped.value=count(h.commentsSkipped);
  result.links.value=count(m.links);result.images.value=count(m.images);result.unsafe_links.value=count(m.unsafeLinks);
  result.unsafe_images.value=count(m.unsafeImages);result.html_escaped.value=count(m.htmlEscaped);
  result.unknown_entities.value=count(h.unknownEntities+m.unknownEntities);
  result.cues.value=count(s.cues);result.missing_indices.value=count(s.missingIndices);
  result.dropped_identifiers.value=count(s.identifiersDropped);result.dropped_cue_settings.value=count(s.settingsDropped);
  result.dropped_blocks.value=count(s.headerLinesDropped+s.notesDropped+s.styleBlocksDropped+s.regionBlocksDropped);
  result.stripped_tags.value=count(s.tagsStripped+s.overridesStripped);
  return publish(id,outputPath,converted.bytes,std::move(result),published);
 }
};
Session::Session(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
Session::~Session()=default;
std::unique_ptr<Session> Session::open(const std::filesystem::path& root,std::string& error) noexcept {
 try {
  auto files=arken::files::workspace::v1::NativeFiles::open(root,error);if(!files)return {};
  (void)server();auto value=std::make_unique<Impl>();value->files=std::move(files);
  return std::unique_ptr<Session>(new Session(std::move(value)));
 }catch(const std::exception&){error="Conversion server could not initialize.";return {};}
}
std::string catalog(){
 auto result=Json::parse(S::commandCatalog);
 const auto descriptions=presentations();
 for(auto& c:result.at("commands")) {
  for(const auto& p:descriptions)if(c.at("commandId")==p.commandId)c["description"]=p.description;
 }
 return result.dump();
}
Json Session::request(std::string_view text) noexcept {
 Json id=nullptr;
 bool published=false;
 try {
  const auto plan=server().plan(text);if(!plan.isCommand)return plan.response;id=plan.id;
  J::require(!plan.metadata.contains("x402/payment"),"Payments are not supported by this server");
  const auto command=plan.commandId;
  switch(family(command)) {
  case Family::image:return impl_->image(id,command,plan.arguments,published);
  case Family::audio:return impl_->audio(id,command,plan.arguments,published);
  case Family::document:return impl_->document(id,command,plan.arguments,published);
  case Family::table:return impl_->table(id,command,plan.arguments,published);
  case Family::markup:return impl_->markup(id,command,plan.arguments,published);
  case Family::unknown:break;
  }
  throw std::invalid_argument("Unknown conversion");
 }catch(const std::exception&){
  return mcp::failure(id,published?-32603:-32602,published
   ?"Output was published; inspect the requested path before retrying."
   :"Invalid conversion request");
 }
}
}
