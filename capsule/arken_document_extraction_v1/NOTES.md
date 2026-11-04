# arken_document_extraction_v1

Bounded, memory-only extraction of the textual structure of DOCX and RTF
documents, serialized as UTF-8 plain text or Markdown. This is the
implementation for plan-task d85e2b6cc4a2f792 (handoff section 11). It extracts
readable text. It is not a word processor and does not reproduce page layout.

The capsule only takes bytes in and gives bytes out: `std::span<const std::byte>`
in, `TextResult`/`ExtractResult` out. It does not open files, use the network,
start processes, read the clock or call Office. Every unsupported, encrypted,
macro-enabled, malformed or over-budget input fails with a typed `Error`. A
failure never returns an empty document that looks like a success. A
genuinely empty Word document is the only empty success.

Surface status: exposed through the standalone MCP only, as `convert_document`,
`docx_to_txt`, `docx_to_markdown`, `rtf_to_txt` and `rtf_to_markdown`, via
`ConvertStandaloneDocument` in `arken_file_conversion_v1` (component
`standalone`): 32 MiB source, 16 MiB per ZIP member, 32 MiB output. "Remaining
steps" below is kept as the integration record; the commands use ids 20-24 and
the result also reports block counts. Not exposed through the browser converter
or the hosted MCP. The real-document corpus requested below is still outstanding. The browser and hosted surfaces need their
own admission decisions (handoff sections 3 and 13). Nothing here authorizes them.

## Public API (`api.hpp`)

- `ExtractDocx(bytes, ExtractOptions)` / `ExtractRtf(bytes, ExtractOptions)` return
  an `ExtractResult` holding the common `Document` model.
- `SerializeText(Document, SerializeOptions)` / `SerializeMarkdown(...)` return a `TextResult`.
- `Convert(SourceFormat, OutputFormat, bytes, ConvertOptions)` runs both steps.
- `ErrorCode(Error)` (stable snake_case code), `ErrorMessage(Error)` (actionable
  sentence), `MediaType(OutputFormat)` (`text/plain; charset=utf-8`,
  `text/markdown; charset=utf-8`), `revision`.

The common model is a list of blocks. A block is a paragraph, a heading
(level 1-6), a list item (level 0-8, ordered flag) or a table (rows of cells).
Paragraphs, headings and list items hold runs. A run is UTF-8 text with an
optional raw link target and bold/italic flags. Both extractors fill this one
model and both serializers read it. There are no format-specific serializers.

## DOCX profile

Container (`src/zip.cpp`, `src/inflate.cpp`):
- The end-of-central-directory record must close the file exactly. The
  directory must end at that record, so prefixed data (self-extracting stubs) is
  rejected. Any ZIP64 marker is rejected (`zip_unsupported`): the locator, 0xFFFF
  or 0xFFFFFFFF sentinels, or the 0x0001 extra field. Spanning is rejected the same way.
- Only the stored (0) and deflate (8) methods are accepted. Traditional
  encryption, strong encryption, masked headers and AES (method 99 / extra field
  0x9901) return `zip_encrypted_entry`. Patched data and undeclared flag bits are rejected.
- Member names must be valid UTF-8, at most 1024 bytes, contain no control
  characters or backslashes, not start with `/` or a drive letter, and contain no
  empty, `.` or `..` segments. Duplicates are compared case-insensitively, as OPC
  part names are (`zip_duplicate_name`).
- Every local header must repeat the directory's flags, method, name, CRC and
  sizes. Member byte ranges (header, data, descriptor) must not overlap. This
  rejects quine and overlap bombs.
- Data descriptors (flag bit 3) are supported. The local header may hold zeros.
  The descriptor follows the data, with or without its signature, and must
  repeat the directory's CRC and sizes.
- Members are inflated only when the extractor names them. Each read is
  charged its declared size against the per-entry and total budgets *before*
  inflation. Raw deflate goes through the pinned stb_image 2.30 inflater
  (`stbi__do_zlib`, `parse_header=0`) into a fixed buffer of the declared size
  plus 259 bytes of slack. It never grows. A stream that expands past its
  declared size returns `zip_size_limit`. A stream that ends early, runs past
  its compressed bytes or carries trailing bytes returns `zip_corrupt_data`.
  Eight zero guard bytes make the consumed bit count exact. CRC-32 is checked
  on every member read (`zip_crc_mismatch`).

XML (`src/xml.cpp`):
- UTF-8 only. A UTF-8 BOM is accepted. A UTF-16/32 BOM, NUL-led text or a
  declared encoding other than UTF-8 returns `xml_encoding`. The whole part is
  validated as strict UTF-8 and as XML `Char` before parsing.
- Any `<!DOCTYPE` returns `xml_doctype`. No DTD, internal subset, entity
  declaration or external identifier can therefore exist. Only `&lt; &gt; &amp;
  &apos; &quot;` and numeric references to legal XML characters are expanded.
  Any other entity reference returns `xml_entity`. Nothing is ever fetched.
- Comments and processing instructions are skipped. CDATA becomes text. Line
  ends are normalized to LF, and attribute whitespace to spaces.
- Namespaces are resolved, so element matching never depends on the prefix.
  Duplicate attributes, unbound prefixes, mismatched tags and a second root are
  rejected as `xml_malformed`.
- Limits per part: depth, attributes per element, name bytes, token bytes (one
  text node, CDATA section or attribute value), nodes and decoded text bytes.

Package (`src/docx.cpp`):
- An OLE compound file (the container of password-protected Office files), or
  a ZIP holding `EncryptedPackage`/`EncryptionInfo`, returns `docx_encrypted`.
- A `vbaProject.bin`/`vbaData.xml` member, a VBA content-type override, or a
  `.docm`/`.dotm` main content type returns `docx_macro_enabled`. Macros are never read.
- `[Content_Types].xml` and `_rels/.rels` are validated. Wrong root namespace,
  unknown elements, missing attributes and duplicate extensions, part names or
  relationship IDs all return `docx_invalid_package`. Exactly one internal
  `officeDocument` relationship is required. Its part must have the
  WordprocessingML document or template main content type, otherwise
  `docx_not_wordprocessing`. The part must exist, otherwise `docx_missing_part`.
- Relationship targets are resolved as part names (`..` may not escape the
  root). External relationships are only ever used as hyperlink text.
- Transitional and Strict WordprocessingML namespaces are both accepted.

Extracted from `word/document.xml` (the main story only):
- `w:p` paragraphs and `w:r` runs. `w:t` honours `xml:space="preserve"`, including
  when inherited. Without it, edge whitespace is trimmed. `w:tab` becomes a tab.
  `w:br` (all types) and `w:cr` become line breaks. `w:noBreakHyphen` becomes
  U+2011. Split runs are merged when their formatting and link match.
- Bold and italic come from direct run properties (`w:b`, `w:i` with on/off values).
- Headings come from `w:pStyle`, resolved through `word/styles.xml` by style name
  (`heading 1`-`heading 6`, `Title` gives level 1) or by `w:outlineLvl` 0-5,
  following `w:basedOn` up to 16 levels. Without a styles part, or for an unknown
  ID, the built-in IDs `Heading1`-`Heading6` and `Title` are recognized.
  Direct `w:outlineLvl` 0-5 also makes a heading.
- Lists come from `w:numPr` (direct or inherited from the paragraph style). The
  level is `w:ilvl`, clamped to 0-8. A list is ordered when `word/numbering.xml`
  gives the level a `numFmt` other than `bullet`/`none`, and `w:lvlOverride` is
  honoured. Without numbering.xml, lists are bullets. `numId 0` means no list.
- Tables: `w:tbl`/`w:tr`/`w:tc`, including those wrapped in content controls.
  Paragraphs inside a cell are joined with one space. Nested tables are
  flattened into their cell's text. Rows carrying a deleted-row revision are dropped.
- Hyperlinks: `w:hyperlink r:id` resolves through
  `word/_rels/document.xml.rels` (external hyperlink relationships only).
  `w:anchor`-only links are internal bookmarks and stay plain text.
  `w:fldSimple` and complex fields (`w:fldChar` begin/separate/end with
  `w:instrText`) whose instruction is `HYPERLINK "url"` become links. `\l`,
  `\o` and `\t` switches are understood.
- Fields of every other type keep their result text. Their instructions are never output.
- Revisions: `w:ins` and `w:moveTo` content is kept. `w:del`, `w:moveFrom` and
  `w:delText` content is dropped.
- Content controls (`w:sdt`), `w:customXml`, `w:smartTag`, `w:dir` and `w:bdo`
  are transparent.

## RTF profile (`src/rtf.cpp`)

- The input must start with `{\rtf`. The parser keeps a group stack (depth
  bounded). Control words are limited to 32 letters, with an optional signed
  parameter of at most 10 digits in int32 range. Control symbols are handled.
  Unbalanced braces, text after the final `}` (other than whitespace or NUL)
  and bad escapes return `rtf_malformed`.
- `\uN` (negative values wrap to 16 bits) skips its fallback according to the
  group-scoped `\ucN`. Each byte, `\'hh`, control symbol or control word counts
  as one fallback character, and braces end the skip. UTF-16 surrogate pairs
  across two `\u` combine. A lone surrogate becomes U+FFFD.
- `\'hh` is decoded through the active code page: the font's `\cpg`, else its
  `\fcharset`, else `\ansicpg`/`\ansi`. Tables are Windows-1252, 1250, 1251,
  1253, 1254 and 1257 (generated from Python's codecs; undefined bytes become
  U+FFFD). Other code pages are recognized and rejected **lazily**: they return
  `rtf_unsupported_code_page` only when a byte above 0x7F must be emitted as
  text through them. This covers 932/936/949/950/1361 (DBCS), 1255/1256/1258/874,
  Mac (`\mac`, charset 77) and OEM (`\pc`, `\pca`, charsets 254/255). A document
  that only uses them as `\u` fallback converts. Symbol-charset fonts emit U+FFFD
  for bytes above 0x7F.
- `\binN` payloads are skipped byte-exactly in every destination and never
  decoded. A payload running past the end of the input is `rtf_malformed`.
- The following destinations are dropped: `fonttbl` (parsed for charsets),
  `colortbl`, `stylesheet` (parsed for heading names), `info`, `pict`,
  `object` (including `objdata` and `result`), shapes (`shp`, `shpinst`,
  `shprslt`), header/footer variants, `footnote`, annotations and comments,
  bookmarks, index/TOC entries, list tables, theme/data store tables, `pn`
  and `listtext`/`pntext`, plus every unknown `\*` destination.
- Fields: `fldinst` is dropped and `fldrslt` is kept. A `HYPERLINK "url"`
  instruction links the result runs.
- `\par`, `\sect` and `\page` end a paragraph. `\line` is a line break and
  `\tab` a tab. `\pard` resets paragraph properties and `\plain` resets
  character properties. `\b` and `\i` set Markdown emphasis. `\v` hidden text
  is dropped. `\~ \_ \emdash \endash \bullet` and the quote symbols map to Unicode.
- Headings: `\outlinelevelN` (0-5), or a `\sN` whose stylesheet name is
  `heading 1`-`heading 6`/`Title`.
- Lists: `\lsN` (level `\ilvlN`) or a `\pntext`/`\listtext` marker group make a
  list item. All RTF lists are **degraded to bullets**, because numbering formats
  live in `\listtable`/`\pn` and are not interpreted.
- Tables: `\intbl` paragraphs collect into cells at `\cell` and into rows at
  `\row`. A non-table paragraph closes the table. Paragraphs inside one cell are
  joined with a space. Nested tables (`\nestcell`, `\nestrow`) are flattened to
  spaces in the enclosing cell. Ragged rows (different cell counts) serialize as
  paragraphs in Markdown.

## Serialization (`src/serialize.cpp`)

TXT: UTF-8 with LF only. Blocks are separated by exactly one blank line.
Consecutive list items are separated by one newline. The output ends with LF
unless the document is empty. Tabs are preserved. Whitespace-only paragraphs are
dropped. Headings are plain lines. List items are indented two spaces per level
and use `- ` or `N. ` (numbering restarts per level after a shallower item and
after any non-list block). Continuation lines are indented under the text.
Table rows are tab-separated lines, and tabs or newlines inside a cell become
spaces. A link with an http, https or mailto target is written `text (url)`
unless the text already equals the URL. Other targets are dropped.

Markdown (CommonMark plus GFM pipe tables):
- `\ ` * _ [ ] < > | ~ & #` are always backslash-escaped. At the start of each
  line, leading spaces and tabs are removed (so no indented code block can form),
  and a leading `- + =`, as well as `N.`/`N)`, is escaped. No text can therefore
  become HTML, an autolink, a code span, emphasis, a list, a heading, a block
  quote or a thematic or setext break. Raw HTML is never emitted.
- Headings are ATX `#`-`######`, kept on one line. Line breaks become `\`
  hard breaks. Spaces before a break and trailing spaces on any line are removed.
- Emphasis uses properly nested `**`/`*` that change only next to non-whitespace,
  so closers are right-flanking and openers left-flanking.
- Lists: nesting may deepen only one level at a time (deeper jumps are clamped).
  Each level is indented to its parent's content column, and ordered items carry their real number.
- Tables become pipe tables only when every row has the same number of cells
  (the first row becomes the header). Any other table becomes one paragraph per
  row, with cells separated by ` \| `.
- Links are `[text](destination)` only for `http://`, `https://` and `mailto:`
  targets (case-insensitive). The destination is percent-encoded outside the URI
  character set, including spaces, parentheses and `<>`. Every other scheme
  (`javascript:`, `file:`, relative, bookmark) is plain text.

## Explicit limitations

- No layout: pages, sections, columns, indentation, alignment, fonts, colours,
  sizes, underline, strike-through and character styles are not represented. Bold
  and italic are taken from direct run formatting only (style-inherited
  emphasis is not resolved).
- Not extracted from DOCX: headers, footers, footnotes, endnotes, comments, text
  boxes and shapes (`w:txbxContent`, `mc:AlternateContent`), images and drawing
  text, charts, SmartArt, OLE objects, equations (`m:oMath`), embedded fonts,
  `w:altChunk` imported content, glossary documents, and `w:sym` symbol characters.
  Footnote and comment reference marks are dropped too.
- Tracked changes are accepted as if all insertions were accepted and all
  deletions applied. Formatting-only revisions are ignored.
- Fields other than HYPERLINK are never evaluated. The cached result is used as
  stored, so TOC, PAGE, DATE and similar fields may be stale.
- List numbers are recomputed per level by the serializer, not taken from Word's
  restart rules or `w:start`. The number format (letters, roman numerals) is
  reduced to "ordered". All RTF lists are bullets.
- Merged cells (`gridSpan`, `vMerge`) are not expanded. A row with a horizontal
  merge has fewer cells and makes the table non-rectangular, so Markdown uses
  paragraphs. Vertical-merge continuation cells are empty cells.
- Part names with percent-encoding are matched literally. The OPC
  interleaved-part (`[1].piece`) form is not supported, so such a document
  reports a missing part.
- RTF: no `\listtable` numbering, no `\upr` alternate text, no DBCS or other
  unlisted code pages (rejected lazily, see above), no Symbol/Wingdings
  glyph mapping, no `\shp` text boxes, no annotations, and no nested-table
  structure (it is flattened).
- Processing is resident, not streaming: the inflated main part, its XML node
  tree and the model coexist in memory.

## Limits (defaults in `ExtractOptions`/`SerializeOptions`)

| Limit | Default | Error |
|---|---|---|
| Source bytes | 8 MiB | `source_limit` |
| ZIP entries | 4096 | `zip_entry_limit` |
| ZIP member name | 1024 bytes | `zip_unsafe_name` |
| Member compressed bytes | 8 MiB | `zip_size_limit` |
| Member expanded bytes | 32 MiB (hard cap 2^31-1024 for the inflater) | `zip_size_limit` |
| Total expanded bytes over parts read | 64 MiB | `zip_size_limit` |
| XML depth / attributes / name | 256 / 64 / 256 bytes | `xml_limit` |
| XML token / nodes / text per part | 4 MiB / 2,000,000 / 32 MiB | `xml_limit` |
| RTF depth / control word / tokens | 256 / 32 letters / 16,000,000 | `rtf_limit` |
| Model text bytes | 16 MiB | `text_limit` |
| Model blocks | 1,000,000 | `text_limit` |
| Output bytes | 8 MiB | `output_limit` |
| HYPERLINK instruction | 4096 bytes (excess ignored) | n/a |
| DOCX field nesting | 64 | `xml_limit` |

Measured on GCC 13 -O2 (Linux x86-64, one run each, recorded 2026-11-04) using
a scratch harness that links the static library. These figures cover the
**whole process**, including the harness's own copies of the input:
- A DOCX whose `document.xml` expands to 30 MiB converted to 7.6 MB of
  Markdown in 0.9 s with a peak RSS of 360 MB. The part held about 100,000
  paragraphs and 300,000 runs with properties. The test encoder does not
  compress text, so the source limit was raised to 31 MiB for this run.
  Roughly 90 MB of that peak is the harness's own body, ZIP and input copies.
- An 8 MiB RTF converted to 6.0 MB of Markdown in 0.19 s with a peak RSS of 86 MB.

By estimate, the library's own peak memory is about 8 to 10 times the expanded
size of the main part (it holds the XML text, the node tree and the model). The standalone MCP should consider lowering
`zip.maximumEntryExpandedBytes` to 16 MiB if its process budget is tighter.
Browser/Wasm memory has not been measured.

## Verification

`tests/` is a standalone CMake project. It resolves `stb-image-2.30-v1` from its
own `lib.manifest.json` through `ArkenExternalPackageGraph.cmake`, the same
pattern as `arken_image_codecs_v1/tests/heif_dependencies`. Its fixtures are
authored in the test:
- a stored-ZIP writer,
- an independent fixed-Huffman deflate encoder (literals plus
  length/distance matches up to 258),
- a stored-block deflate encoder,
- data-descriptor variants,
- DOCX parts, RTF strings, and expected TXT/Markdown bytes.

Groups: `zip`, `xml`, `docx`, `rtf`, `markdown`, `limits`, `determinism`,
`mutation` (4,500 seeded byte mutations by default, or `mutation N` for N per
seed), plus `--version`.

```
cmake -S capsule/arken_document_extraction_v1/tests -B build/documents
cmake --build build/documents -j2
ctest --test-dir build/documents
```

All groups pass under GCC 13 with `-Wall -Wextra -Wpedantic -Werror`, and under
`-fsanitize=address,undefined`. Under the sanitizers a local
`mutation 60000` campaign (360,000 conversions) also passes. The fixtures are
authored by the tests themselves. **No Word-, LibreOffice- or WordPad-produced
file has been run yet.** Before admitting the MCP methods, add a small corpus of
real, independently created documents with known expected text. The handoff
asks for this, and their exact output bytes should be reviewed.

## Remaining steps to expose `docx_to_txt`, `docx_to_markdown`, `rtf_to_txt`, `rtf_to_markdown`

These steps are in owners this capsule must not edit. They are for the integrator.

1. **Module graph.** In `capsule/arken_file_conversion_v1/capsule.manifest.json`,
   add `"arken_document_extraction_v1"` to the `public_dependencies` of the
   component the standalone build selects (`images`, per the root
   `lib.manifest.json` `module_components`) and to the top-level list. This
   capsule has no `components`, so the module graph adds it through its
   `CMakeLists.txt`. Its single external dependency (stb-image-2.30-v1 `image`)
   is already resolved for `arken_image_codecs_v1`. No new recipe is needed.
   Keep the root `CMakeLists.txt` module denylist unchanged. This capsule has
   no MCP, payment or network dependencies.
2. **Conversion owner.** Add `capsule/arken_file_conversion_v1/documents.hpp`
   and `src/document.cpp`, mirroring `ConvertStandaloneImage`:
   ```cpp
   namespace arken::file_conversion::v1 {
   namespace document = arken::document::v1;
   inline constexpr std::uint32_t maximumDocumentSourceBytes = 8u*1024u*1024u;
   inline constexpr std::uint32_t maximumDocumentOutputBytes = 8u*1024u*1024u;
   struct DocumentOptions { document::SourceFormat input = document::SourceFormat::docx;
                            document::OutputFormat output = document::OutputFormat::text; };
   struct DocumentResult { Error error = Error::none; document::Error documentError = document::Error::none;
                           std::vector<std::byte> bytes;
                           explicit operator bool() const noexcept { return error == Error::none; } };
   [[nodiscard]] DocumentResult ConvertStandaloneDocument(std::span<const std::byte>, const DocumentOptions& = {}) noexcept;
   }
   ```
   The implementation should reject a source above `maximumDocumentSourceBytes`
   (`input_limit`), call `document::Convert` with
   `extract.maximumSourceBytes = maximumDocumentSourceBytes` and
   `serialize.maximumOutputBytes = maximumDocumentOutputBytes`, and map errors:
   `source_limit`, `text_limit` and `zip_size_limit` go to `input_limit`;
   `output_limit` goes to `encode_failed`; `allocation_failed` goes to
   `allocation_failed`; everything else goes to `decode_failed`, with
   `documentError` kept for the message. Add `src/document.cpp` to
   `arken_file_conversion_images_v1` in `cmake/ArkenFileConversionImages.cmake`
   and link `arken::document_extraction_v1`.
3. **Schema.** In
   `capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`,
   add a result model without image fields:
   `model document_result @type_id(6) { field output_path: text @field_id(1) { immutable true; } field media_type: text @field_id(2); field output_bytes: count @field_id(3); field sha256: text @field_id(4); field durable: boolean @field_id(5); key identity { kind primary; fields output_path; } }`.
   Then add four unary commands with `request file_request; response document_result; errors command_error;`:
   `docx_to_txt @command_id(17)`, `docx_to_markdown @command_id(18)`,
   `rtf_to_txt @command_id(19)`, `rtf_to_markdown @command_id(20)`. Do not add
   document kinds to `input_kind`/`output_kind`, because `convert_image` stays images-only.
4. **Commands** (`src/commands.cpp`):
   - Add a document pair table, each entry with a description. Suggested:
     "Extract the text of a Word .docx (main story: paragraphs, headings, lists,
     tables, hyperlinks) to UTF-8 plain text. Headers, footers, notes, comments,
     text boxes and images are not included; tracked changes appear accepted.
     Encrypted and macro-enabled files are rejected. Limits: 8 MiB input and output."
     RTF entries should mention that lists become bullets and that DBCS code pages are rejected.
   - Dispatch those IDs before the image `pairs` loop. Read the source with
     `impl_->files->read(value.source, C::maximumDocumentSourceBytes)` and call
     `C::ConvertStandaloneDocument`.
   - On failure, return `error(id, "conversionFailed", ...)` with
     `C::ErrorMessage(error)` plus `": " + document::ErrorMessage(documentError)`.
     Optionally also return `document::ErrorCode` as a `reason` field.
   - Set `media_type` from `document::MediaType(output)`, i.e.
     `text/plain; charset=utf-8` or `text/markdown; charset=utf-8`. Fill
     `output_bytes`, `sha256` and `durable`, and write with the existing
     exclusive `impl_->files->create`, which never overwrites and keeps the
     source untouched.
   - Extend the server description and `convert_image` presentation text only
     where they enumerate capabilities.
5. **Tests.**
   - In `capsule/arken_file_conversion_commands_v1/tests/commands.cpp`, update
     the tool-count assertion (fifteen becomes nineteen tools) and the expected
     names. Add round trips that write a stored-ZIP DOCX and an RTF into the
     temporary root, call each tool, and compare the output file bytes with the
     expected TXT/Markdown. Also check the reported `sha256`/`output_bytes`,
     no-overwrite behavior, traversal rejection, an encrypted DOCX
     (`conversionFailed` naming encryption), and a source over 8 MiB.
   - Add an `arken_file_conversion_v1` unit test for the error mapping.
   - Add this capsule's `tests/` project, or its test executable, to the root
     test set if the integrator wants it in the default `ctest`.
6. **README.** Document the four tools, their limits (8 MiB source and output;
   the expansion and XML limits above), the supported profile, and the
   limitations section above.
