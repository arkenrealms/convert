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

## Tabular (`tabular.hpp`, namespace `arken::document::v1::tabular`)

Bounded, memory-only conversion between tabular formats: CSV, TSV, JSON, XLSX
and ODS are read into one rectangular `Table`, which is written out as CSV, TSV
or JSON. It shares the capsule's ZIP reader, XML parser and UTF-8 helpers
through `src/detail.hpp` and does not change them. Revision string:
`arken-document-tabular-v1-r1`. Like the rest of the capsule it takes bytes and
returns bytes: no files, network, processes, clock or Office, and **no formula
evaluation**. A failed read never returns a partial table, and a failed write
never returns partial text.

Files: `tabular.hpp` (public), `src/tabular_detail.hpp` (internal),
`src/tabular.cpp` (CSV/TSV/JSON and the public entry points), `src/xlsx.cpp`,
`src/ods.cpp`, `tests/tabular_tests.cpp`.

### Ownership boundary for CSV/TSV/JSON

The monorepo may already have `arken_data_*` capsules that own CSV and JSON.
This repository does not contain them. So all CSV/TSV/JSON code is kept in
`src/tabular.cpp`, behind four internal functions:
`TableFromDelimited(text, delimiter, inferTypes, limits, report)`,
`TableFromJson(text, limits, report)`,
`DelimitedFromTable(table, delimiter, options)` and
`JsonFromTable(table, options, renamed)`.
That is Table in and bytes out, or the reverse. The XLSX and ODS readers depend
only on the `Table` model, the `SparseTable`/`TextBudget` helpers in
`src/tabular_detail.hpp` and the package helpers in `src/detail.hpp`. They never
call the delimited or JSON code. If that owner is exported later, re-point those
four functions (or `ReadTable`/`WriteTable` for csv/tsv/json) at it. `xlsx.cpp`
and `ods.cpp` need no change, and the tests for those groups keep their meaning.

### Model

- `Table{rows, columns}` is always rectangular. Short rows are padded with empty
  cells, and the source widths that differed are counted in
  `TableReport::raggedRows`.
- `TableCell{kind, value, formatted}`. The kinds are `empty`, `text`, `number`,
  `boolean`, `date` and `error`.
  - `value` is canonical. A number keeps its source lexical form (CSV field,
    JSON number, XLSX `<v>`, ODS `office:value`). A spreadsheet boolean is
    `TRUE`/`FALSE`. A date is ISO 8601: `YYYY-MM-DD`,
    `YYYY-MM-DDThh:mm:ss[.fff]` or `hh:mm:ss[.fff]`.
  - `formatted` keeps the original source text only when it differs from
    `value`: the XLSX serial behind a date, the `1`/`0` behind a boolean, or the
    ODS `text:p` display such as `1,234.50` or `50%`.
  - `WriteOptions::formattedText` writes `Display()` (the formatted text if
    present, otherwise the value) instead of `value`.

### CSV/TSV (RFC 4180)

Reader:
- Delimiter `,`, `;` or tab. TSV always uses tab with the same quoting grammar.
- Quoted fields may hold the delimiter, `""` and line breaks. Line breaks inside
  quotes are kept verbatim, CRLF included.
- Records end at CRLF, LF or a lone CR. The last record may lack a terminator.
  An optional UTF-8 BOM is stripped and reported.
- Strict UTF-8 is required (`csv_encoding`). An unterminated quoted field, or
  text after a closing quote, is `csv_malformed`. A bare `"` inside an unquoted
  field is kept literally.
- A blank line is one empty field.
- Type inference (`inferTypes`, on by default): a field matching the JSON number
  grammar is a `number`, and `true`/`false` (any case) is a `boolean`. Anything
  else, including `007`, `1,5` and dates, stays `text`.

Writer:
- RFC 4180 quoting. A field is quoted only when it contains the delimiter, `"`,
  CR or LF, or when it is the single empty field of a one-column row (so it
  cannot read back as a blank line).
- UTF-8, LF by default (`crlf` option), no BOM by default (`byteOrderMark` option).
- There is no protection against spreadsheet formula injection (`=`, `+`, `-`,
  `@` prefixes). The data is written as is.

### JSON (RFC 8259)

The parser is written for this capsule and has no dependencies. It is strict:
- No comments, trailing commas, leading zeros, `+` signs or raw control
  characters. Escapes are checked, and a lone surrogate is `json_malformed`.
- Duplicate member names, and nesting deeper than 64, are `json_malformed`.
- Invalid UTF-8 is `json_encoding`. An optional BOM is accepted.

Accepted shapes are an array of scalar arrays, or an array of flat objects with
scalar values. For objects, the header row is the union of the keys in
first-seen order, and missing members are empty cells. In cells, `null` becomes
empty, `""` becomes empty, strings become text, numbers become number (lexical
kept) and `true`/`false` become boolean. Any other valid JSON is
`json_not_table`: a top-level object or scalar, nested values in cells, or
arrays mixed with objects.

Writer:
- Output is deterministic: `[`, one row per line, `]`, LF. An empty table is `[]`.
- `JsonShape::arrays` (default) writes an array of arrays. `JsonShape::objects`
  writes an array of objects keyed by the first row, in column order. An empty
  header becomes `column_N`, and a duplicate gains `_2`, `_3`... (counted in
  `renamedKeys`).
- A cell is a JSON number only when its kind is `number` and its value is
  already the shortest binary64 round-trip text (`std::to_chars`). So `1`,
  `0.1`, `-0`, `1e+21` and `1e-07` are numbers, while `1.50`, `1e3`, `1E5` and
  `12345678901234567890` are strings, and no consumer can see a different number.
- Booleans are JSON booleans. Everything else is a string escaped per RFC 8259:
  `\" \\ \b \f \n \r \t` and `\u00XX` for the other C0 controls. Non-ASCII is
  written as raw UTF-8.
- Empty cells are `""`, or `null` with `emptyAsNull`.

### XLSX profile (`src/xlsx.cpp`)

Package checks:
- An OLE compound file is `ole_compound_file`. That covers a legacy `.xls` and
  a password-protected Office file, which cannot be told apart without OLE
  parsing.
- `EncryptedPackage`/`EncryptionInfo` members are `package_encrypted`.
- `vbaProject.bin`, `vbaData.xml`, a VBA override or an `.xlsm`/`.xltm`/`.xlam`
  main content type is `package_macro_enabled`.
- `[Content_Types].xml`, `_rels/.rels` and `xl/_rels/workbook.xml.rels` are
  validated as for DOCX (`package_invalid`). The single `officeDocument` part
  must be the SpreadsheetML workbook or template type. XLSB and other documents
  are `not_spreadsheet`.
- Transitional and Strict namespaces are both accepted.

Workbook and supporting parts:
- `workbook.xml`: sheet names, order and `state` (`visible`, `hidden`,
  `veryHidden`), plus `workbookPr date1904`. Every sheet's `r:id` must resolve.
  Chart, dialog and macro sheets are listed with their `SheetKind`. Selecting
  one is `sheet_unsupported`.
- Sheet selection is by zero-based index, by exact name, or by default the first
  visible worksheet (otherwise the first worksheet). `sheet_not_found`
  otherwise. `TableReport::sheets` lists every sheet.
- `sharedStrings.xml`: `si/t` or rich-text `r/t` runs are concatenated, and
  phonetic `rPh` is skipped. `xml:space="preserve"` is honoured. Without it,
  edge spaces, tabs and LFs are trimmed. `_xHHHH_` escapes are decoded and
  `_x005F_` escapes an underscore. The table is bounded by `maximumSharedStrings`.
- `styles.xml`: `cellXfs/xf@numFmtId` with `numFmts`. These built-in IDs are dates:
  - 14-17: date.
  - 22: date-time.
  - 18-21 and 45-47: time.

  A custom format is a date or time when its first section contains `y`, `d`,
  `e`, `g`, `h`, `s`, `AM/PM`, `A/P` or elapsed `[h]`/`[m]`/`[s]` tokens,
  outside quotes, escapes, `_x`/`*x` padding and bracketed colours or locales.
  `General` and scientific `E+`/`E-` are excluded. A lone `m` is a month; next
  to `h` or `s` it is minutes.
- Serial dates use the 1900 system, including Excel's fictitious 1900-02-29, or
  the 1904 system when declared. Times round to the millisecond. Serial 60
  (1900 system), day 0 of a pure date format, negative serials and years past
  9999 stay numbers. A time format on a serial of one day or more is written as
  a date-time.

Worksheet:
- `dimension` is reported as declared.
- Rows use `r`, or the previous row plus one. Cells use their A1 `r`, or the
  previous column plus one. A reference must lie within XFD1048576 and in its
  own row, and a duplicate position is `xlsx_malformed`. Sparse cells are placed
  by reference. Leading empty rows and columns are kept, and trailing styled
  empty cells do not extend the table.
- Cell types:
  - `n` and absent: number, or date via the style.
  - `s`: shared string; the index is checked.
  - `str`: formula string.
  - `inlineStr`: `is`.
  - `b`: `0`/`1`/`true`/`false`.
  - `e`: the error literal.
  - `d`: ISO text as stored.

  Anything else, or a non-numeric `n`, is `xlsx_malformed`.
- Formulas (`f`) are counted. Their cached `<v>` is used. A formula with no
  cached value is an empty cell, counted in `formulasWithoutValue`.
- `mergeCells/mergeCell@ref` ranges are validated and reported, never expanded.
  Only the anchor cell holds the value.

### ODS profile (`src/ods.cpp`)

Package checks:
- The `mimetype` member must exist and be
  `application/vnd.oasis.opendocument.spreadsheet` (or `-template`). Another
  ODF type is `not_spreadsheet`, and anything else is `package_invalid`. Its
  position and compression are not checked.
- `META-INF/manifest.xml` must exist, and its `/` entry must repeat the
  mimetype. Any `manifest:encryption-data` is `package_encrypted`.
- `content.xml` is required (`package_missing_part`).

Tables:
- `office:spreadsheet/table:table` elements are listed with their names.
- A table whose automatic `table`-family style has `table:display="false"` is
  reported hidden. Selection works as for XLSX.

Rows and cells:
- `table:table-row` is read directly or inside `table-header-rows`,
  `table-rows` and `table-row-group`.
- `table:table-cell` and `table:covered-table-cell` both occupy a column.
  Covered cells are empty placeholders, and a covering cell's
  `number-columns-spanned`/`number-rows-spanned` is reported as a merged range.
- `number-rows-repeated` and `number-columns-repeated` must be 1 to 2^31.
  Repeats holding data are expanded. Their size is checked against the row,
  column and cell limits **before** expansion (`row_limit`, `column_limit`,
  `cell_limit`).
- Empty repeats are never materialized. Those beyond the final data extent are
  counted in `trimmedRepeatedRows`, and in `trimmedRepeatedCells` (empty cell
  runs in data rows past the last data column, times their row repeat).
- Values by `office:value-type`:
  - `float`, `percentage` and `currency` read `office:value` as a number.
  - `date` reads `office:date-value` as stored.
  - `time` reads `office:time-value`. The ISO duration `PnDTnHnMn.nS` becomes
    `hh:mm:ss[.fff]` with total hours.
  - `boolean` reads `office:boolean-value` as `TRUE`/`FALSE`.
  - `string` reads `office:string-value`, otherwise the paragraphs.
  - No type (or `void`) uses the paragraph text as text.
  - `calcext:value-type="error"` takes the paragraph text as the error literal.
  - A missing or invalid typed value is `ods_malformed`.
- Paragraph text: `text:p`/`text:h` are joined with LF. `text:s` (`c` bounded by
  the cell limit), `text:tab`, `text:line-break`, spans and links are kept.
  `office:annotation` and `text:note` are dropped.
- `table:formula` is counted (per materialized repeat), and the cached value is used.

### Explicit limitations

- No formula evaluation, ever. XLSX `<v>` and ODS `office:value` are the
  values the producer last cached. A file saved without recalculation, or by a
  library that writes formulas without values, gives empty or stale cells.
- No formatting: number formats are used only to recognize dates. Currency,
  percent, thousands separators, decimals, fonts, colours, conditional formats,
  column widths and hidden rows or columns are not applied. ODS display text is
  available through `formattedText`.
- No charts, drawings, images, comments or notes, pivot tables, data
  validation, defined names, external links or hyperlinks. Rich-text runs are
  flattened.
- Merged ranges are reported, not expanded.
- One sheet per conversion.
- Not supported:
  - XLSB (`not_spreadsheet`).
  - XLS/BIFF and other OLE files (`ole_compound_file`).
  - Encrypted or macro-enabled workbooks.
  - Flat ODS (`.fods`).
  - CSV encodings other than UTF-8.
- XLSX `t="d"` and ODS `date-value` strings are passed through without
  calendar validation.
- CSV dates are not inferred, and CSV booleans keep their source spelling.
- Processing is resident, not streaming. The DOM-shaped XML parser holds the
  worksheet's node tree.

### Limits (`ReadOptions`/`WriteOptions` defaults)

| Limit | Default | Error |
|---|---|---|
| Source bytes | 32 MiB | `source_limit` |
| Rows | 1,048,576 | `row_limit` |
| Columns | 16,384 | `column_limit` |
| Cells (dense rows x columns, and cells read) | 2,000,000 | `cell_limit` |
| One cell (value + formatted) | 1 MiB | `cell_size_limit` |
| All cell text | 64 MiB | `text_limit` |
| XLSX shared strings | 2,000,000 | `cell_limit` |
| ODS repeat attribute | 1 to 2^31 | `ods_malformed` |
| JSON nesting | 64 | `json_malformed` |
| XML nodes per part (tabular default) | 8,000,000 | `xml_limit` |
| Other ZIP/XML limits | as `ZipLimits`/`XmlLimits` above | `zip_*`, `xml_*` |
| Output bytes | 64 MiB | `output_limit` |

The dense-table check runs **before** allocation. A single XLSX cell at
`XFD1048576` is therefore `cell_limit`, not a 17-billion-cell allocation.

Measured on GCC 13 -O2 (Linux x86-64, one run each, recorded 2026-11-04) with a
scratch harness linking the static library. The figures cover the whole process,
including the harness's own copies of the input, with the ZIP/XML limits raised
for the run:
- XLSX, 200,000 rows x 5 columns (1,000,000 cells; a 44 MB stored worksheet
  part) to JSON: 3.2 s, peak RSS 748 MB.
- CSV, 400,000 rows x 5 columns (14 MB) to JSON: 0.42 s, peak RSS 282 MB.

So the XLSX path costs about 15 to 17 bytes of memory per byte of worksheet XML,
dominated by the XML node tree. With the default 32 MiB per-member expansion
budget, one XLSX call can approach 550 MB. The standalone MCP should set
`zip.maximumEntryExpandedBytes` to 16 MiB (about 280 MB per call) unless its
process budget is larger. A streaming worksheet reader would remove this cost,
and is the main follow-up if larger sheets matter.

### Verification

`tests/tabular_tests.cpp` builds the `arken-document-tabular-tests` executable.
Its ctest groups are `DocumentTabular.{csv,json,xlsx,ods,limits,determinism,mutation}`
and `DocumentTabular.Version`. XLSX and ODS fixtures are authored in the test,
using a stored ZIP writer and a stored-block deflate writer (method 8), with
DOCX/encrypted packages from `test_support/docx_fixtures.hpp`.

All groups pass under GCC 13 with `-Wall -Wextra -Wpedantic -Werror`, and under
`-fsanitize=address,undefined` (plus a local `mutation 10000` campaign: 80,000
conversions).

**No Excel-, LibreOffice-, Numbers- or Google-Sheets-produced file has been run
yet.** Before admitting the MCP methods, add a small corpus of real workbooks
(shared strings, styles with date formats, the 1904 system, merged cells,
LibreOffice repeat runs) with reviewed expected CSV/JSON bytes.

### Remaining steps to expose tabular conversion through the standalone MCP

These steps are in owners this capsule must not edit. They are for the integrator.
`arken_file_conversion_v1` already depends on this capsule, so no module-graph
change is needed. The new sources are added to the existing library target in
`CMakeLists.txt`.

1. **Conversion owner.** Add `capsule/arken_file_conversion_v1/tables.hpp` and
   `src/table.cpp`, mirroring `documents.hpp`:
   ```cpp
   namespace arken::file_conversion::v1 {
   namespace tabular = arken::document::v1::tabular;
   inline constexpr std::uint32_t maximumTableSourceBytes = 32u * 1024u * 1024u;
   inline constexpr std::uint32_t maximumTableEntryExpandedBytes = 16u * 1024u * 1024u;
   inline constexpr std::uint32_t maximumTableOutputBytes = 64u * 1024u * 1024u;
   struct TableResult { Error error = Error::none; tabular::Error tableError = tabular::Error::none;
       tabular::TableReport report; std::uint64_t renamedKeys = 0; std::vector<std::byte> bytes;
       explicit operator bool() const noexcept { return error == Error::none; } };
   [[nodiscard]] tabular::ConvertOptions StandaloneTableOptions() noexcept;
   [[nodiscard]] TableResult ConvertStandaloneTable(tabular::TableFormat in, tabular::TableFormat out,
       std::span<const std::byte>, const tabular::ConvertOptions&) noexcept;
   [[nodiscard]] std::string_view TableErrorMessage(Error) noexcept;
   }
   ```
   - Apply the source, member and output bounds above.
   - Map errors as follows:
     - `source_limit`, `row_limit`, `column_limit`, `cell_limit`,
       `cell_size_limit`, `text_limit`, `zip_size_limit`, `zip_entry_limit`
       and `xml_limit` go to `input_limit`.
     - `output_limit` goes to `encode_failed`.
     - `allocation_failed` stays `allocation_failed`.
     - `invalid_options` goes to the existing invalid-arguments path.
     - Everything else goes to `decode_failed`, keeping `tableError` for the message.
   - Add `src/table.cpp` to `cmake/ArkenFileConversionImages.cmake` next to
     `src/document.cpp`.
2. **Schema** (`arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`).
   Use the next free ids, after any taken by the concurrent PPTX/ODT/EPUB work
   (enum 7+, semantic type after the last, type 10+, command 25+ at the time of
   writing).
   - Enums:
     - `table_input_kind {csv, tsv, json, xlsx, ods}`.
     - `table_output_kind {csv, tsv, json}`.
     - `json_shape_kind {arrays, objects}`.
     - `delimiter_kind {comma, semicolon, tab}`.
   - `model table_request`:
     - `source_path`, `output_path`, `input_format`, `output_format`;
     - optional `delimiter` (CSV input and output);
     - optional `sheet_name: text` and `sheet_index: count` (mutually exclusive;
       if neither is given, the first visible sheet);
     - optional `json_shape` (default `arrays`; `objects` uses the first row as keys);
     - optional `formatted_text: boolean`.
   - `model table_result`:
     - `output_path`, `media_type`, `output_bytes`, `sha256`, `durable`;
     - `rows`, `columns`, `sheet_count: count`, `sheet_names: text` (JSON array
       text, or a repeated field if the schema language allows);
     - `selected_sheet: count`, `ragged_rows`, `merged_ranges: count`,
       `formulas`, `formulas_without_value`, `trimmed_repeats: count`,
       `renamed_keys: count`.
   - Commands, all `errors command_error; idempotency none; streaming unary; targets cpp;`:
     - `convert_table` (`table_request`), the generic one.
     - `xlsx_to_csv`, `xlsx_to_json`, `ods_to_csv`, `csv_to_json`, `json_to_csv`
       and `csv_to_tsv`, each with `file_request` (or a reduced request with
       the sheet selector for the XLSX/ODS ones), returning `table_result`.
3. **Commands** (`src/commands.cpp`):
   - Add a `tablePairs` table with descriptions that state the profile. For
     example: "Convert the first visible worksheet of an .xlsx to UTF-8 CSV
     (RFC 4180, LF). Cached formula values only (never recalculated); dates as
     ISO 8601; merged cells reported, not expanded; no formatting. Encrypted,
     macro-enabled, XLSB and XLS files are rejected. Limits: 32 MiB input, 2M
     cells, 64 MiB output."
   - Dispatch before the image pairs. Read with
     `impl_->files->read(path, C::maximumTableSourceBytes)` and convert. On
     failure, return `error(id, "conversionFailed",
     TableErrorMessage(error) + ": " + tabular::ErrorMessage(tableError))`,
     optionally with `tabular::ErrorCode` as `reason`.
   - Set `media_type` from `tabular::MediaType(output)`:
     `text/csv; charset=utf-8`, `text/tab-separated-values` or `application/json`.
   - Write with the existing exclusive `impl_->files->create`, which never overwrites.
   - Extend the server description where it lists families: "tables (CSV,
     TSV, JSON, XLSX, ODS; 32 MiB)".
4. **Tests.** In `capsule/arken_file_conversion_commands_v1/tests/commands.cpp`:
   - Update the tool count and names.
   - For each tool, write a stored-ZIP XLSX/ODS (reuse this capsule's fixture
     builders, or move them into `test_support/`) and CSV/JSON sources into the
     temporary root. Call each tool through the stdio server and compare the
     output bytes, `sha256`, `output_bytes`, `media_type` and report fields.
   - Cover no-overwrite, traversal rejection, an `.xlsm` (`conversionFailed`
     naming macros), a hidden-sheet selection, `sheet_not_found`, and a source
     over 32 MiB.
   - Add an `arken_file_conversion_v1` unit test for the error mapping.
5. **Manifest/README.**
   - Add the capabilities `bounded_csv_tsv_json_tables`,
     `xlsx_cached_value_extraction` and `ods_cached_value_extraction` to this
     capsule's `capsule.manifest.json`. They were left out here to avoid
     conflicting with concurrent edits.
   - Document the seven tools, their limits and the limitations above in the
     standalone README.

## Presentations, OpenDocument and EPUB

`text_documents.hpp` adds PPTX, ODT, ODP and EPUB 2/3 extraction. It fills the
same `Document` model and is serialized by the unchanged `SerializeText` /
`SerializeMarkdown`. The ZIP reader, the XML tokenizer and `ModelBuilder` are
reused as they are. The new code is `src/text_documents.cpp` (API and errors),
`src/text_documents_detail.hpp` (OPC helpers and the shared `Flow` block
assembler), `src/pptx.cpp`, `src/odf_text.cpp` and `src/epub.cpp`. No existing
source file or `api.hpp` was changed.

Surface status: library only. Nothing is exposed through any MCP yet. The steps
are listed at the end of this section.

### API

- `ExtractTextDocument(TextDocumentFormat, bytes, TextDocumentOptions)` returns a
  `TextDocumentExtractResult` (error, `Document`, `TextDocumentReport`).
- `ConvertTextDocument(TextDocumentFormat, OutputFormat, bytes, options)` returns
  a `TextDocumentResult` (error, text, report).
- `ErrorCode(TextDocumentError)`, `ErrorMessage(TextDocumentError)`,
  `FormatName(TextDocumentFormat)`, `textDocumentsRevision`.
- `TextDocumentOptions` holds `extract` (the existing `ExtractOptions`: source,
  model, ZIP and XML budgets), `serialize`, `includeNotes` (default **off**),
  `maximumItems` (slides or spine items, default 4096), `maximumNesting`
  (structural recursion, default 128) and `maximumTableCells` (default 1,000,000).
- `TextDocumentError` is one error space for all four formats. The ZIP, XML
  and budget codes repeat `Error`'s names, with format-neutral messages (the
  core messages say "DOCX"). Format codes are `pptx_*`, `odf_*` and `epub_*`.
  `item_limit`, `nesting_limit` and `table_limit` are new budget codes.
  `internal` is reported only if an unexpected exception escapes; it is never
  expected.
- `TextDocumentReport` counts `slides`, `chapters`, `notesIncluded`,
  `tocHeadings` and `blocks`. `excluded` counts what was deliberately left out:
  `images`, `graphics` (charts, SmartArt, OLE/objects, MathML, media, ink,
  non-table graphic frames, ODT frames/text boxes), `notes` (speaker notes left
  out, ODF footnotes/endnotes), `annotations`, `deletions` (ODF tracked
  deletions), `placeholders` (date/footer/slide-number/header), `scripts`,
  `navigation`, `nonLinear` and `unsupportedItems`. On failure the report is
  reset along with the document.

### Shared block assembly (`Flow`)

All three extractors push through one assembler. A block boundary inside a
list item becomes a line break inside that item, because the model has no
multi-paragraph list items. A boundary inside a table cell becomes a space, as
for DOCX. Nested tables are flattened into their cell. Nested lists end the
enclosing item's text, and text after them in the same item becomes another
item at the outer level. ODF and XHTML text uses whitespace collapsing: runs of
space, tab, CR and LF become one space, and the space is dropped at a block or
line start and at block end. Literal text (`text:s`, `text:tab`, DrawingML
runs, `<pre>`) is kept exactly.

### PPTX profile (`src/pptx.cpp`)

- Package: an OLE compound file or `EncryptedPackage`/`EncryptionInfo` gives
  `pptx_encrypted`. `vbaProject.bin`/`vbaData.xml`, a VBA override, or a
  `.pptm`/`.ppsm`/`.potm`/add-in main type gives `pptx_macro_enabled`.
  `[Content_Types].xml` and `_rels/.rels` are validated as for DOCX, and exactly
  one internal `officeDocument` relationship is required. The main part must
  be a presentation, slideshow (`.ppsx`) or template (`.potx`), otherwise
  `pptx_not_presentation`. Transitional and Strict namespaces are both accepted.
- Slides come from `p:presentation/p:sldIdLst` in document order. Each `r:id`
  is resolved through `ppt/_rels/presentation.xml.rels` and must be an internal
  `slide` relationship to a part whose content type is the slide type. Unknown
  or duplicate ids give `pptx_invalid_package`. A missing slide part gives
  `pptx_missing_part`. Hidden slides (`show="0"`) are included.
- Each slide produces a level-1 heading `Slide N` (N is the position in
  `sldIdLst`). If there is a `title`/`ctrTitle` placeholder (the first one in
  shape-tree order, groups included), its text follows as a level-2 heading;
  line breaks become spaces. The other shapes follow in shape-tree order.
  `p:grpSp` groups are recursed, and only the `mc:Fallback` branch of
  `mc:AlternateContent` is walked.
- Paragraphs: `a:p` with `a:r`/`a:fld` `a:t` text (kept verbatim), `a:br` line
  breaks, and `b`/`i` run attributes. `a:hlinkClick r:id` becomes a link when it
  resolves to an **external** hyperlink relationship of the slide. Slide jumps
  and other actions stay plain text.
- Lists: `a:pPr lvl` (0-8) is the level. `buNone` makes a plain paragraph,
  `buAutoNum` an ordered item, and `buChar`/`buBlip` a bullet item. With no
  explicit bullet properties, paragraphs of body/content placeholders (`ph`
  without a type, or `type="body"`/`"obj"`) are bullets and all other shapes
  are plain paragraphs. This is a heuristic: layout and master inheritance is
  not resolved.
- Tables: `p:graphicFrame` holding `a:tbl/a:tr/a:tc`. Paragraphs in a cell are
  joined with a space. `hMerge`/`vMerge` continuation cells are empty, so the
  grid stays rectangular.
- Speaker notes: with `includeNotes`, the slide's `notesSlide` relationship is
  followed and the body placeholder's paragraphs are emitted after the slide
  content as one paragraph, `**Notes:** first line` with further paragraphs on
  following lines. Notes are neither read nor validated when the option is off,
  and each slide with a notes relationship increments `excluded.notes`. Links
  inside notes are plain text.
- Excluded: `p:pic` (images), non-table `p:graphicFrame` (charts, SmartArt
  diagrams, OLE, media), `p:contentPart` (ink), date/footer/slide-number/header
  placeholders, connectors, slide masters and layouts (never read),
  animations/timing, comments, and equations (`a14:m`).

### ODT and ODP profile (`src/odf_text.cpp`)

- Package: the `mimetype` member must be `application/vnd.oasis.opendocument.text`
  or `-text-template` (ODT), or `presentation` or `presentation-template` (ODP).
  Another `application/vnd.oasis.opendocument.*` type gives `odf_wrong_kind`.
  Anything else, or no `mimetype` member, gives `odf_not_opendocument`. Its
  position and compression in the ZIP are not checked. `META-INF/manifest.xml`
  is required (`odf_missing_part`) and validated: root element, `full-path` on
  every entry, and the `/` media type equal to `mimetype` when present.
  `manifest:encryption-data` on any entry gives `odf_encrypted`, checked before
  `content.xml` is read. `content.xml` must have `office:document-content/office:body`
  with `office:text` (ODT) or `office:presentation` (ODP). Another
  `office:*` body gives `odf_wrong_kind`. Flat XML (`.fodt`/`.fodp`) is not supported.
- Styles: bold comes from `fo:font-weight` (`bold` or a value of 600 or more)
  and italic from `fo:font-style` (`italic`/`oblique`; `normal` switches either
  off). They are read from `style:style` (families `text` and `paragraph`) in
  `content.xml` automatic styles and `styles.xml` `office:styles`, following
  `style:parent-style-name` up to 16 steps. Paragraph styles give the base for a
  paragraph, and `text:span` styles apply on top. Default styles and
  `styles.xml` automatic styles are not read.
- Lists: `text:list`/`text:list-item`/`text:list-header`, nested to any depth
  (model level clamped to 0-8). A nested list without `text:style-name`
  inherits its parent's style. A level is ordered when its `text:list-style`
  (automatic in `content.xml`, or named in `styles.xml`) has a
  `text:list-level-style-number` with a non-empty `style:num-format`. Bullet and
  image levels, an empty number format, an unknown style and paragraph-style
  list associations (`style:list-style-name`) all give **bullets**. `text:number`
  labels are dropped because the serializer numbers items. `text:start-value`
  and `continue-numbering` are ignored. `text:numbered-paragraph` is a list item
  at `text:level`.
- Text: `text:h` uses `text:outline-level` (default 1; 7-10 clamp to 6),
  `text:p` makes a paragraph, `text:s` inserts `text:c` spaces (at most 10,000),
  `text:tab` a tab and `text:line-break` a line break. `text:a xlink:href` makes
  a link (only http/https/mailto are rendered by the serializers). `text:ruby`
  keeps its base text. Fields (date, page number, references and others) keep
  their displayed text. `text:section` and the `text:index-body` of TOCs and
  indexes are walked.
- Tracked changes: the body already holds accepted text. Inserted text sits
  between `text:change-start`/`text:change-end` and is kept. Deleted text lives
  only in `text:tracked-changes` and is skipped (each deletion region counts
  in `excluded.deletions`), so the output reads as if all changes were accepted.
- Tables: `table:table`, rows inside `table:table-header-rows`,
  `table:table-rows` or `table:table-row-group`, and both `table:table-cell`
  and `table:covered-table-cell` (merged cells are empty). `number-columns-repeated`
  and `number-rows-repeated` are expanded (values clamp at 2^30). Trailing
  empty cells and rows are dropped before expansion, as are the 1024- and
  1,048,576-wide padding runs office suites write. Interior empty runs are
  materialized and rows are padded to the widest row. Every materialized cell,
  and each interior empty row, is charged against `maximumTableCells` *before*
  allocation (`table_limit`).
- Excluded (ODT): `text:note` (footnotes and endnotes), `office:annotation`,
  `draw:*` frames (images, text boxes, objects, charts) and shapes,
  `text:hidden-text`, bookmarks, reference/index marks, and declarations.
- ODP: each `draw:page` is a slide. It produces a `Slide N` heading, then the
  `presentation:class="title"` frame's text-box (searched inside `draw:g` too)
  as a level-2 heading, then the page's frames and shapes in document order.
  Text-boxes are walked like ODT body text (lists, tables, spans). A frame
  holding `table:table` becomes a table. `draw:custom-shape` and the other
  drawing shapes contribute their `text:p`/`text:list` children. Groups and
  `draw:a` are recursed. Frames classed `header`/`footer`/`date-time`/`page-number`
  are excluded placeholders, and image/object frames are excluded. Speaker
  notes (`presentation:notes`, `notes` frame) behave as for PPTX: off by
  default and counted when present.

### EPUB profile (`src/epub.cpp`)

- Package: the `mimetype` member must be `application/epub+zip` (trailing
  whitespace tolerated), otherwise `epub_not_epub`. `META-INF/rights.xml`
  gives `epub_drm`. `META-INF/encryption.xml` is parsed. Every `EncryptedData`
  must use font obfuscation (`http://www.idpf.org/2008/embedding` or Adobe's
  `http://ns.adobe.com/pdf/enc#RC`), which only scrambles fonts that are never
  read; any other algorithm (or an unexpected root) gives `epub_drm`.
  `META-INF/container.xml` must name a rootfile of type
  `application/oebps-package+xml`. A missing container or an escaping path
  gives `epub_invalid_package`, and a missing OPF gives `epub_missing_part`.
- OPF (EPUB 2 and 3): `package` in the OPF namespace with `version` 2.x or 3.x.
  `manifest` and `spine` are required. Items need `id`, `href` and
  `media-type`, and ids must be unique. Hrefs are resolved relative to the OPF
  directory with percent-decoding; a path escaping the root is invalid.
  Remote (absolute IRI) items are kept without a part. Spine `idref`s must
  exist. More than `maximumItems` itemrefs gives `item_limit`.
- Spine processing in order: `linear="no"` items are skipped
  (`excluded.nonLinear`). The navigation document (`properties="nav"`) and an
  NCX in the spine are skipped (`excluded.navigation`). `text/html` gives
  `epub_content_not_xhtml`. Any other non-XHTML type (SVG, DTBook, ...) is
  skipped (`excluded.unsupportedItems`). A missing part gives `epub_missing_part`.
- XHTML: each item is parsed by the capsule's XML tokenizer, so it must be
  well-formed XML. A tokenizer `xml_malformed` becomes `epub_content_not_xhtml`,
  as does a root other than XHTML `html`. There is no HTML parser. One adapter
  runs before tokenizing, in `epub.cpp` only: a prolog DOCTYPE **without an
  internal subset** naming `html` (HTML5 `<!DOCTYPE html>` or the XHTML 1.x
  public identifiers) is blanked. A DOCTYPE with an internal subset is left
  in, and the tokenizer rejects it (`xml_doctype`). When the DOCTYPE names an
  XHTML DTD (`-//W3C//DTD XHTML`), 142 common named entities (Latin-1,
  typographic punctuation, arrows, `euro`, `trade`, ...) are rewritten as
  numeric references before parsing. Other named entities still give
  `xml_entity`, and without that DTD every named entity does. The rewrite does
  not distinguish CDATA or comments, so a literal `&nbsp;` inside CDATA in such
  a document would be decoded.
- Mapping (body only): `h1`-`h6` become headings. `p`, `div`, `blockquote`,
  `section`, `article`, `header`, `footer`, `main`, `aside`, `figure`,
  `figcaption`, `address`, `dl`/`dt`/`dd`, `details`/`summary`, `form`,
  `fieldset` and `legend` are paragraph boundaries. `blockquote` is a **plain
  paragraph** because the model has no quote block. `ul`/`ol`/`li` nest (a list
  directly inside a list is accepted). `table` with
  `thead`/`tbody`/`tfoot`/`tr`/`td`/`th` makes a table (`caption` becomes a
  paragraph before it; `colspan`/`rowspan` are ignored, so spanned tables are
  ragged and become paragraphs in Markdown). `br` is a line break and `hr` a
  boundary. `em`/`i` are italic and `strong`/`b` bold. `pre` text is kept
  exactly (a leading newline is dropped); in Markdown its line-start
  indentation is removed by the serializer's escaping. `code` is plain text.
  `a href` stays a link only for http/https/mailto. Relative references point
  into the package and become plain text, as do other schemes. Elements with
  `hidden` are skipped.
- Excluded: `img`/`picture` and inline SVG (`excluded.images`); MathML,
  `video`, `audio`, `object`, `embed`, `iframe`, `canvas` and `map`
  (`excluded.graphics`); `script`, `style`, `template` and `noscript` inside the
  body (`excluded.scripts`); `nav` (`excluded.navigation`); `head`; and ruby
  `rt`/`rp`. Image `alt` text is not extracted.
- Chapter headings: titles are collected from the EPUB 3 navigation document's
  `nav epub:type="toc"` links, or else from the EPUB 2 NCX `navMap` (the
  spine's `toc` attribute). The first title per target part wins (fragments
  are ignored). A spine item with **no h1-h6 of its own** gets that title as a
  level-1 heading (`report.tocHeadings`). Without a title, chapters are only
  separated by the usual blank line: the model has **no thematic break**.
  The navigation document and NCX must themselves be well-formed when present.

### Model gaps found (reported, handled by adapters, model unchanged)

- No block quote: `blockquote` is a plain paragraph.
- No thematic break: chapters and `hr` are plain boundaries. Missing chapter
  titles are replaced from the TOC instead.
- No multi-paragraph list items: extra paragraphs join the item with a line
  break.
- No code block: `pre` is a paragraph with literal text and line breaks.
- The OPC helpers (`ParseRelationships`, `ParseContentTypes`, `ResolvePart`)
  live in an anonymous namespace in `docx.cpp`. They are duplicated in
  `src/text_documents_detail.hpp` with a caller-chosen error. Moving one copy
  into `detail.hpp` would remove the duplication.
- The core `ErrorMessage(Error)` texts name DOCX. `TextDocumentError` repeats
  those codes with neutral messages instead of reusing them.

### Explicit limitations

- No layout, positions, z-order or reading-order analysis: shapes follow
  document (shape-tree) order. Fonts, colours, sizes, underline, alignment and
  slide transitions are not represented.
- PPTX: no placeholder inheritance from layouts or masters (bullets use the
  heuristic above, and title text exists only if it is on the slide). No
  SmartArt or chart text, no comments, no hidden-slide filtering, no
  `a:fld` recomputation. Slide-number fields keep their stored text.
- ODF: no default styles, no `styles.xml` automatic styles (master-page
  content: headers and footers are not extracted), no conditional styles, no
  paragraph-style list associations, no list start values, no `text:alphabetical-index`
  generation (index bodies are used as stored), no flat XML, no encrypted
  documents, and ODP drawing-shape text is extracted while ODT drawing text is
  excluded (consistent with DOCX text boxes).
- EPUB: no HTML (non-XML) content documents, no CSS (`display:none`, generated
  content and `text-transform` are ignored), no fixed-layout or SVG spine
  items, no media overlays, no `epub:switch` (foreign markup skipped), and no
  footnote popups (`aside epub:type="footnote"` is ordinary text).
- Resident, not streaming: one part, its node tree and the model coexist in
  memory. Each EPUB spine item is parsed and freed in turn. ODF repeats are
  expanded into the model, which is bounded by `maximumTableCells`.

### Limits

| Limit | Default | Error |
|---|---|---|
| Source, ZIP, XML, model and output budgets | as in the table above (`ExtractOptions`, `SerializeOptions`) | the matching `TextDocumentError` |
| Slides / ODP pages / spine itemrefs | 4096 (`maximumItems`) | `item_limit` |
| Structural nesting (lists, spans, groups, divs, sections) | 128 (`maximumNesting`), independent of the XML depth | `nesting_limit` |
| Table cells (including repeats, padding, nested and empty interior rows) | 1,000,000 (`maximumTableCells`) | `table_limit` |
| ODF repeat attribute | clamped to 2^30 | via `table_limit` |
| `text:s` count | 10,000 spaces | clamp |
| Style parent chain | 16 | stops |
| DOCTYPE length (EPUB adapter) | 2048 bytes | left for the tokenizer |

Total input across all parts read (every slide, notes slide, `styles.xml`,
every spine item, the navigation document) is bounded by
`zip.maximumTotalExpandedBytes`, because each read is charged before
inflation. Memory and time have **not** been measured for these formats. The
DOCX figure above (about 8-10 times the largest expanded part) is the expected
order of magnitude.

### Verification

`tests/text_documents_tests.cpp` (executable `arken-text-documents-tests`)
authors every fixture with the stored-ZIP writer in
`test_support/docx_fixtures.hpp` and compares exact TXT and Markdown bytes.
Groups: `pptx`, `odt`, `odp`, `epub`, `errors`, `limits`, `determinism`,
`mutation` (300 rounds per seed by default, or `mutation N`), plus `--version`.
These are registered in ctest as `TextDocuments.*` with timeouts. They pass
under GCC 13 with `-Wall -Wextra -Wpedantic -Werror`, and under
`-fsanitize=address,undefined` including `mutation 8000` (80,000 conversions).
**No PowerPoint-, LibreOffice-, Calibre- or publisher-produced file has been
run.** Add a small real corpus with reviewed expected output before admitting
the MCP tools, as for DOCX.

### Remaining steps to expose `pptx_to_txt`, `pptx_to_markdown`, `odt_to_txt`, `odt_to_markdown`, `odp_to_txt`, `odp_to_markdown`, `epub_to_txt`, `epub_to_markdown`

The DOCX/RTF integration already exists. Mirror it in owners this capsule must
not edit:

1. **Module graph.** No change is needed. `arken_file_conversion_v1` already
   depends on this capsule, and the new sources are added by this capsule's
   `CMakeLists.txt`. Optionally add capabilities
   `pptx_slide_text_extraction`, `opendocument_text_extraction` and
   `epub_xhtml_extraction` to this capsule's `capsule.manifest.json`, and
   mention the formats in its description.
2. **Conversion owner** (`capsule/arken_file_conversion_v1/documents.hpp`,
   `src/document.cpp`): include `arken_document_extraction_v1/text_documents.hpp`
   and add
   `[[nodiscard]] DocumentResult ConvertStandaloneTextDocument(document::TextDocumentFormat, document::OutputFormat, std::span<const std::byte>) noexcept;`.
   Build `TextDocumentOptions` from `StandaloneDocumentOptions()` (copy
   `extract` and `serialize`) and keep `includeNotes=false`, unless a tool
   argument is added. Call `ExtractTextDocument` and then
   `SerializeText`/`SerializeMarkdown`, filling the same
   paragraph/heading/list/table counts. Map `source_limit`, `text_limit`,
   `zip_size_limit`, `zip_entry_limit`, `item_limit`, `nesting_limit` and
   `table_limit` to `input_limit`, `output_limit` to `encode_failed`,
   `allocation_failed` to `allocation_failed`, `invalid_options` to
   `invalid_options`, and everything else to `decode_failed`. Add a
   `document::TextDocumentError textDocumentError` field to `DocumentResult`
   (or a parallel result type) so the message can append
   `document::ErrorMessage(textDocumentError)`. Optionally also return the
   report's `slides`, `chapters` and `excluded` counters.
3. **Schema** (`capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`):
   extend `document_input_kind` with `value pptx @value_id(3); value odt
   @value_id(4); value odp @value_id(5); value epub @value_id(6);` so
   `convert_document` accepts them. Add eight unary commands with
   `request file_request; response document_result; errors command_error;
   idempotency none; streaming unary; targets cpp;` on the next free command
   ids. These are 25-32 if nothing else has claimed them; the concurrent
   XLSX/ODS/CSV/JSON track may also add commands, so allocate ids at merge
   time: `pptx_to_txt`, `pptx_to_markdown`, `odt_to_txt`, `odt_to_markdown`,
   `odp_to_txt`, `odp_to_markdown`, `epub_to_txt`, `epub_to_markdown`.
   Optionally add `slides`, `chapters` and `excluded_*` count fields to
   `document_result` with new field ids.
4. **Commands** (`capsule/arken_file_conversion_commands_v1/src/commands.cpp`):
   add the eight entries to `documentPairs`, carrying a
   `document::TextDocumentFormat` (or a tagged source) next to the existing
   DOCX/RTF ones. Route `convert_document` values 3-6 and the new ids to
   `ConvertStandaloneTextDocument`, and extend the `command>=20&&command<=24`
   dispatch range. Suggested descriptions:
   - PPTX: "Extract slide text from a PowerPoint .pptx to UTF-8 plain text (or
     Markdown): one 'Slide N' heading per slide in presentation order, the slide
     title, body text with bullet levels, tables and http/https/mailto links.
     Images, charts, SmartArt, masters/layouts and speaker notes are excluded.
     Encrypted and macro-enabled files are rejected."
   - ODT: "Extract the body of an OpenDocument .odt: headings, paragraphs,
     nested lists, tables (repeats expanded), bold/italic and links; tracked
     changes appear accepted. Footnotes, comments, frames and images are
     excluded. Password-protected files are rejected."
   - ODP: as PPTX, "OpenDocument .odp".
   - EPUB: "Extract the reading-order text of an EPUB 2/3 book: well-formed
     XHTML chapters in spine order with headings, paragraphs, lists, tables and
     external links; a chapter without a heading is titled from the table of
     contents. Images, scripts, navigation and non-linear items are excluded.
     DRM-protected books and HTML that is not XML are rejected."
   Update the server description, which currently says "DOCX/RTF documents".
5. **Tests.** In `capsule/arken_file_conversion_commands_v1/tests/commands.cpp`,
   extend the expected tool-name set and count by eight, and add round trips
   (write fixtures into the temporary root using builders like those in
   `tests/text_documents_tests.cpp`, call each tool, and compare output bytes,
   `sha256` and `output_bytes`). Also cover a DRM EPUB (`conversionFailed`
   naming DRM), an encrypted ODT, an ODS given to `odt_to_txt`
   (`odf_wrong_kind`), and the no-overwrite and traversal checks. Add an error
   mapping test in `capsule/arken_file_conversion_v1/tests/media_tests.cpp`.
6. **README.** List the eight tools with this profile, the limitations above,
   and the 32 MiB source / 16 MiB member / 32 MiB output limits they inherit.
