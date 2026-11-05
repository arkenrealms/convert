# Corpus validation: documents, tables and markup (2026-11-05)

The document, table and markup tools were run through `arken-convert-commands
--stdio` (MCP 2025-11-25). A Python driver sent one `tools/call` per file to a
single server process, gave each call 60 s, and restarted the server on a crash or
timeout. All inputs are real files from public repositories, shallow-cloned on
2026-11-05. **None of them is committed.** Build: Release, GCC 13, Ubuntu 24.04.

## Corpora

| Source | Used for | Producers |
| --- | --- | --- |
| apache/poi `test-data/{document,spreadsheet,slideshow}` | DOCX/DOCM, XLSX/XLSM, PPTX/PPTM | Word, Excel and PowerPoint 2007–365, web downloads, LibreOffice, fuzzers (clusterfuzz, deliberately broken) |
| jmcnamara/XlsxWriter `test/comparison/xlsx_files` (150 of 999, evenly sampled) | XLSX | Excel (re-saved reference files) |
| python-openxml/python-docx, scanny/python-pptx test files | DOCX, PPTX | Word, PowerPoint, python templates |
| LibreOffice/core `sw/qa/extras/{odfexport,odfimport,rtfexport,rtfimport,htmlimport}`, `sc/qa/unit/data/ods`, `sd/qa/unit/data/odp` | ODT, ODS, ODP, RTF, HTML, some DOCX/PPTX | LibreOffice, OpenOffice.org 1.x–2.x, Word, regression inputs (some deliberately broken) |
| IDPF/epub3-samples `30/` (45 books, zipped by us with `mimetype` stored first), futurepress/epub.js fixtures (2) | EPUB 3 | real publishers' EPUB 3 sources |
| w3c/wcag (150 of 960), mdn/learning-area (150 of 154) | HTML | hand-written and generated documentation pages |
| LibreOffice htmlimport, epub.js examples, web-platform-tests `webvtt` (60 sampled) | HTML | test pages |
| fivethirtyeight/data (11 datasets), datasets/country-codes, mledoze/countries, maxogden/csv-spectrum, poi CSVs | CSV | real datasets, RFC 4180 edge cases |
| byroot/pysrt `tests/static`, papnkukn/subsrt | SRT | real subtitles in several encodings |
| mozilla/vtt.js `tests`, web-platform-tests `webvtt` (300 of about 430, sampled), epub3 media overlays | WebVTT | parser conformance tests (mostly deliberately invalid) |

Not reached: `python-openpyxl/openpyxl` is not on GitHub (the clone asks for
credentials; upstream is on Heptapod). Project Gutenberg EPUB 2 mirrors were not
used, so **EPUB 2 coverage is nil**: every EPUB tried is EPUB 3. The 192 Apache
httpd `docs/manual` `.html` files from the first run are type maps, not HTML, so
they were replaced by the WCAG and MDN pages. They are listed separately below.

## Method and checks

- One call per file: `docx_to_markdown` and `docx_to_txt`, `xlsx_to_csv` and
  `xlsx_to_json`, and one tool for each other format (`pptx_to_markdown`,
  `odt_to_markdown`, `odp_to_markdown`, `ods_to_csv`, `epub_to_markdown`,
  `rtf_to_markdown`, `html_to_markdown`, `csv_to_json`, `srt_to_vtt`,
  `vtt_to_srt`). A CSV whose first 4 KiB held more `;` than `,` got `delimiter:
  semicolon`.
- Every Markdown output was rendered back with `markdown_to_html` on the same
  server. **0 failures across all 1,476 Markdown outputs (second run).**
- Independent references, computed with Python `zipfile` and ElementTree:
  - DOCX: words in `w:body` `w:t`, leaving out text boxes, `mc:Fallback` and
    `w:del`, compared with the TXT word count (tolerance max(5, 5%)).
  - XLSX: the first visible worksheet's last row that holds a `v`, `is` or `f`,
    compared with the CSV row count and the selected sheet name.
  - PPTX: `a:t` words in the slides (flagged below 60%) and the slide count.
  - ODT and ODP: `text:p`/`text:h` words (flagged below 60%).
  - EPUB: spine `itemref` count compared with `chapters`, `excluded_non_linear`
    and `excluded_navigation`.
- Every flagged file and every rejection was inspected by hand (source XML or
  bytes) and given a class:
  - (a) expected rejection or correct empty result;
  - (b) false rejection of a valid file;
  - (c) crash, hang or timeout;
  - (d) wrong output.
- Memory: `/usr/bin/time` is absent, so `VmHWM` of the server process was read
  after each call. It only rises, so it shows peaks, not per-file cost.

## Acceptance table (after fixes; second full run)

"Empty" means the source is valid but converts to no content. Since fix 1 this
returns `emptyOutput`; before, it returned a misleading output-path `RangeError`.
Every empty result with visible source text was checked by hand. In each case the
text lives in content the profile excludes (text boxes, frames, drawings, notes,
comments, headers and footers, math, deleted revisions, fields, an empty first
sheet, a signature-only VTT).

| Format (tool) | Files | OK | Empty (a) | Rejected (a) | (b) | (c) | (d) | Median / p95 / max ms | Largest input |
| --- | --: | --: | --: | --: | --: | --: | --: | --- | --- |
| DOCX (`docx_to_markdown`, `_txt`) | 209 | 150 | 36 | 23 | 0 | 0 | 0 | 1.5 / 3.5 / 281 | saut_page.docx 2.96 MB, ok in 3 ms |
| XLSX (`xlsx_to_csv`) | 523 | 401 | 72 | 47 | 3 (product) | 0 | 0 | 1.1 / 3.9 / 287 | LIBRE_OFFICE-116306-0.xlsx 2.86 MB, rejected (part > 16 MiB); 58325_db.xlsx 2.70 MB ok |
| XLSX (`xlsx_to_json`) | 523 | 473 | 0 | 47 | 3 (same) | 0 | 0 | 1.1 / 2.8 / 299 | same |
| PPTX (`pptx_to_markdown`) | 189 | 160 | 10 | 16 | 3 (product) | 0 | 0 | 1.5 / 3.4 / 13 | KEY02.pptx 2.28 MB ok |
| ODT (`odt_to_markdown`) | 221 | 149 | 69 | 3 | 0 | 0 | 0 | 1.4 / 2.1 / 6 | fdo47267-3.odt 224 KB ok |
| ODP (`odp_to_markdown`) | 200 | 196 | 0 | 2 | 2 (product) | 0 | 0 | 2.2 / 4.4 / 14 | test-embedded-dejavu.odp 2.23 MB ok |
| ODS (`ods_to_csv`) | 346 | 275 | 64 | 5 | 2 | 0 | 0 | 1.2 / 6.8 / 55 | document_with_two_images.ods 221 KB (empty first sheet) |
| EPUB 3 (`epub_to_markdown`) | 47 | 42 | 4 | 1 (limit) | 0 | 0 | 0 | 3.5 / 51 / 696 | cc-shared-culture.epub 31.7 MB ok in 16 ms; mahabharata.epub 11 MB / 2,015 XHTML files over the text limit |
| RTF (`rtf_to_markdown`) | 300 of 527 | 230 | 60 | 10 | 0 | 0 | 0 | 0.8 / 1.2 / 1.7 | tblrepeat.rtf 113 KB ok |
| HTML docs (WCAG, MDN) | 300 | 280 | 20 | 0 | 0 | 0 | 0 | 1.2 / 6.3 / 9.4 | conformance.html 72 KB ok |
| HTML test pages (LO, epub.js, wpt) | 124 | 77 | 45 | 2 | 0 | 0 | 0 | 0.8 / 1.1 / 7.5 | tdf142781.html 52 KB (image only) |
| CSV (`csv_to_json`) | 41 | 39 | 0 | 2 | 0 | 0 | 0 | 1.3 / 24 / 202 | nbaallelo.csv 18 MB (over 2,000,000 cells); marvel-wikia-data.csv 2.4 MB ok in 52 ms |
| SRT (`srt_to_vtt`) | 12 | 6 | 0 | 6 | 0 | 0 | 0 | 0.6 / 1.0 / 2.7 | windows-1252.srt 98 KB (not UTF-8) |
| WebVTT (`vtt_to_srt`) | 300 | 219 | 45 | 36 | 0 | 0 | 0 | 0.8 / 1.1 / 4.1 | 8.7 KB |

There were **no crashes, hangs or timeouts (class c) in 7,172 calls**, counting
both runs and the HTML rerun. Peak server RSS was 152 MB after `bug62181.xlsx`
(875 KB, ok in 287 ms), and 227 MB while rejecting the 18 MB `nbaallelo.csv` at
the cell limit. Every other format stayed below those marks. The first run's
timings (median about 7 ms) included a cold page cache; the table uses the warm
second run.

The table excludes the 192 httpd type-map ".html" files from the first run: all
192 converted, as text. Before fixes, the first run had 5 more XLSX (b), 1 DOCX
(d) and 1 ODT (d); those are the fixes below.

### Rejections by class, with representative messages

- **DOCX, 23, all (a).**
  - 14 fuzzer or truncated files: "The file is not a ZIP-based DOCX package." and
    "The DOCX package has an inconsistent ZIP structure."
  - 3 encrypted: "The document is password-protected or encrypted; …". These are
    `bug53475-password-is-*.docx` and an OLE-encrypted `.docm`.
  - 3 macro-enabled `.docm`.
  - `ExternalEntityInText.docx`: an XXE test with a DOCTYPE.
  - `deep-table-cell.docx`: over the nesting limit by design.
  - `tdf76322_columnBreakInHeader.docx`: an ODT with a `.docx` name, so content
    types are invalid.
- **XLSX, 47 (a).**
  - 19 `.xlsm`: "Macro-enabled workbooks are not accepted; …".
  - 15 clusterfuzz ZIPs.
  - 4 DOCTYPE: the `54764*` XXE tests and the `poc-xmlbomb*` tests.
  - 3 OLE or encrypted (`58616.xlsx`, `protected_passtika.xlsx`, a crash file).
  - 2 duplicate member names, built on purpose.
  - 2 malformed cells, built on purpose (`invalid_cell_reference.xlsx`,
    `poc-shared-strings.xlsx`).
  - `sample-beta.xlsx`: Excel 2007 beta namespace, "not an XLSX/ODS spreadsheet".
  - `xlsx-corrupted.xlsx`: not well-formed.
  - `60825.xlsx`: a sheet with `r:id=""` left after a VBA module was removed.
    The "invalid content-type list, relationships…" rejection is defensible, but
    Excel repairs this file.
- **XLSX, 3 product decisions.** Listed under "Product decisions" below.
- **PPTX, 16 (a).**
  - 6 not ZIP: 5 clusterfuzz files, and `Divino_Revelado.pptx`, which has no end
    record (Python `zipfile` also refuses it).
  - 5 clusterfuzz files with an inconsistent structure.
  - 5 macro-enabled.
- **PPTX, 3 (b), product decision.** `au.asn…Maloney.pptx`, `bug62513.pptx` and
  `ca.ubc…HowWeRefactor.pptx` are real web downloads with **one trailing `\n`
  after the end-of-central-directory record**. They fail with "not a ZIP-based
  document package"; PowerPoint and Python open them.
- **ODT, 3 (a).**
  - `sha1_correct.odt` and `sha1_wrong.odt`: password-protected.
  - `rovas-numbering.odt`: `office:body` holds `office:texture-mode` in place of
    `office:text`, so the file is broken. The message ("of a different kind than
    requested") is misleading.
- **ODP, 2 (a).**
  - `EDB-26517-1.odp`: an exploit sample, not a ZIP.
  - `ooo72169-1.odp`: an old file with no `mimetype` member.
- **ODP, 2 product decisions.**
  - `pass/ooo56974-1.odp` (OpenOffice.org 2.0): its `META-INF/manifest.xml`
    carries `<!DOCTYPE manifest:manifest PUBLIC "-//OpenOffice.org//DTD Manifest
    1.0//EN" "Manifest.dtd">`. OpenOffice.org 1.x and 2.x wrote that line in
    every ODF package.
  - `tdf148966-withoutflag.odp`: `content.xml` declares `encoding='us-ascii'`.
- **ODS.**
  - 5 (a): `password.ods` and `passwordOld.ods`; `passwordWrongSHA.ods` with a
    broken manifest; `too-many-cols-rows.ods` over the column limit by design;
    `tdf136551.ods`, a 12 MB `content.xml` over the XML node limit.
  - **2 (b), real LibreOffice output.** `cachedValue.ods` (LO 4.1) has
    `office:boolean-value="8"`. `tdf137621_autofillMergedBool.ods` (LO 7.1) has
    `"-1"`. Both fail with "The ODS table has an invalid repeat count or typed
    value." This is remaining defect 1. The fix budget was spent.
- **RTF, 10 (a).**
  - 7 "The RTF document is malformed.", all with unbalanced braces: content or an
    extra `}` after the root group closes (`hyperlink.rtf`, `fdo49655.rtf`,
    `tdf108947.rtf`, …). These are LibreOffice robustness inputs that Word also
    tolerates. That tolerance is a product decision.
  - 3 "unsupported legacy code page": `\mac` (`fdo85889-mac.rtf`), `\pc`
    (`fdo85889-pc.rtf`) and `fdo79384.rtf`. All are documented.
- **EPUB, 1 (a), limit.** `mahabharata.epub` (2,015 spine documents, 35.7 MB of
  XHTML) fails with "The document contains more text or blocks than the
  extraction limit allows."
- **HTML, 2 (a).** `PageAndParagraphFilled.html` declares a non-UTF-8 charset, and
  `emojis16BE.html` is UTF-16BE. Both get "The HTML declares a character encoding
  other than UTF-8 …".
- **CSV, 2 (a).** `police_killings.csv` is Latin-1 ("The delimited text is not
  valid UTF-8; re-save it as UTF-8."). `nbaallelo.csv` is 126,314 × 23 = 2.9 M
  cells, over the 2,000,000-cell limit.
- **SRT, 6 (a).** 5 encoding rejections (UTF-16/32 BOM ×4, windows-1252 ×1) and
  `invalid.srt`, which is malformed on purpose.
- **WebVTT, 36 (a).**
  - These are conformance tests. Each one checked is invalid by the WebVTT
    grammar: `-->` inside a cue identifier, `00:00.000` written as minutes over
    60, 1-digit minutes, missing `WEBVTT` or a BOM followed by garbage, negative
    or truncated timings, and nulls.
  - The converter rejects the whole file. A browser drops the bad cue and keeps
    the rest. That is a product decision.
  - `empty.vtt` (0 bytes) is rejected at the file layer.

### Reference comparisons

- **DOCX words.** 11 of 150 fell outside tolerance; none is a defect.
  - 7 have more words in the output: list numbers (`ComplexNumberedLists`,
    `Numbering*`, `60329`), content controls and the separate `w:t` of the
    reference (`Bug54849`).
  - 4 have fewer: `form_footnotes` (−38) and `endnotes` (−10) differ by
    tokenisation, and a word-multiset diff leaves 4 tokens. `Bug66263-paragraph`
    joins runs exactly as Word displays them.
- **XLSX rows.** The CSV row count and the selected sheet matched the reference
  for 396 of 397 comparable workbooks. The exception is `bug60858.xlsx`: rows 3–4
  hold only a shared string that is whitespace, trimmed by the documented
  no-`xml:space` rule. 4 Strict-namespace workbooks have no reference because the
  script reads Transitional only; the converter handled them.
- **PPTX.** No file was under 60% of its slide-text words, and every slide count
  matched.
- **ODT and ODP.** 14 files were under 60%. Each was traced to excluded content:
  speaker notes, annotations, frames and text boxes, footnotes, tracked
  deletions, or index templates (`ooo77837-1.odt`).
- **EPUB.** In 42 of 42 books, chapters + non-linear + navigation = spine items.
- **Mojibake.** None was introduced. `bug60858.xlsx` has double-encoded UTF-8 in
  its shared strings, and the CSV reproduces those bytes faithfully.

## Fixes (5), each with a synthetic regression test

1. **Typed `emptyOutput` in place of a misleading output-path error.**
   - Problem: the workspace never creates empty files. A valid source that
     extracts to nothing therefore failed with `RangeError` "Output was not
     created. Choose a new relative path with an existing parent…". That
     happened on 444 of 3,436 first-run calls. In the second run, 441 calls
     returned `emptyOutput`: 36 DOCX files × 2 tools, 72 XLSX, 69 ODT, 64 ODS, 60
     RTF, 45 HTML, 45 VTT, 10 PPTX and 4 EPUB. Fixes 4 and 5 gave the other three
     calls real content.
   - Fix: `publish` now returns `code: "emptyOutput"` with a message that names
     the cause.
   - Code: `capsule/arken_file_conversion_commands_v1/src/commands.cpp`.
   - Test: `tests/commands.cpp` converts an empty-sheet XLSX and expects
     `emptyOutput`. The README states the rule.
2. **ZIP: flag bit 11 may differ between the local header and the directory.**
   - Problem: `56957.xlsx` sets the UTF-8 flag only in the central directory, on
     ASCII names, and failed with "inconsistent ZIP structure". Name bytes must
     still match exactly, and every other flag must still agree.
   - Code: `src/zip.cpp`.
   - Test: `extraction_tests.cpp` (accepted with bit 11 in the directory only;
     another flag mismatch still `zip_malformed`).
3. **XLSX: whitespace-collapsed and empty `<v>`.**
   - Problem: `bug69769.xlsx` has `<v>2 </v>` on a shared-string index. Three
     files have `<v/>` on `n` and `s` cells: `56702.xlsx` (a statistics
     exporter), `64508.xlsx` and `64667.xlsx`. All were rejected as
     `xlsx_malformed`.
   - Fix: for `n`, `s` and `b`, whose schema types collapse whitespace, the value
     is trimmed, and an empty value is a blank cell, as Excel shows it. Inner
     whitespace (`4 2`) is still malformed.
   - Code: `src/xlsx.cpp`.
   - Test: `tabular_tests.cpp`.
4. **DOCX ruby (East Asian phonetic guides).**
   - Problem: `w:r/w:ruby` was skipped whole, so `61470.docx` (東京 with a
     とうきょう guide) produced no text.
   - Fix: the `w:rubyBase` runs are now read and `w:rt` is dropped, matching the
     existing ODT `text:ruby` behaviour.
   - Code: `src/docx.cpp`.
   - Test: `extraction_tests.cpp`.
5. **ODT table-of-contents title.**
   - Problem: `text:index-title` inside `text:index-body` was not walked, so the
     displayed "Table of Contents" heading of every LibreOffice TOC was lost
     (`fdo79358.odt`).
   - Code: `src/odf_text.cpp`.
   - Test: `text_documents_tests.cpp` (the title is kept, and the
     `index-title-template` source is not).

All five were checked on the corpus files named above in the second run. `ctest`
passes 24/24.

## Remaining defects and product decisions, ranked

1. **(b) ODS numeric booleans.** LibreOffice 4.1 and 7.1 wrote
   `office:boolean-value="8"` and `"-1"`. A local fix: a boolean value that
   parses as a number is TRUE when non-zero. It was left only because of the
   five-fix cap.
2. **Product decision: bytes after the ZIP end record.** 3 of 95 real PPTX files
   in poi end with one `\n` after the EOCD, typical of web-served or text-mode
   copies. The rule that the end record must close the file exactly is a
   structural, anti-polyglot rule and was not relaxed. One option is to accept up
   to a few CR, LF or NUL bytes. All three formats share this rule.
3. **Product decision: OpenOffice.org 1.x and 2.x manifests.** Their
   `META-INF/manifest.xml` carries a public-ID DOCTYPE with no internal subset.
   Every OOo-2-era ODT, ODS or ODP is rejected. Allowing exactly that DOCTYPE, or
   one with no internal subset (no entity declarations), would admit them without
   enabling entities. This touches the DOCTYPE rule and so was not changed.
4. **Product decision: non-UTF-8 text inputs.** These are SRT in
   UTF-16/32-with-BOM or windows-1252 (5 of 12 SRT), CSV in Latin-1, and XML
   parts declaring `us-ascii` (pure-ASCII bytes, so UTF-8 by content). Policy
   kept: UTF-8 only. A BOM-guided UTF-16 transcode for SRT/VTT would be the
   highest-value relaxation for subtitles.
5. **Product decision: limits on large real inputs.**
   - `LIBRE_OFFICE-116306-0.xlsx`: an 18.2 MB worksheet part, over 16 MiB per
     part.
   - `mahabharata.epub`: over the 16 MiB text limit.
   - `nbaallelo.csv`: over 2,000,000 cells.
   - `tdf136551.ods`: over the XML node limit.

   All are rejected cleanly. Raising them is a memory trade-off: XLSX costs about
   17 bytes per byte of worksheet XML.
6. **Product decision: strict RTF group structure.** 7 LibreOffice inputs have
   content or a `}` after the root group closes; Word and LibreOffice ignore it.
7. **Product decision: strict WebVTT.** A single invalid cue rejects the file,
   whereas browsers skip that cue. That matters for real-world VTT with one bad
   cue. All of this corpus's rejections are conformance tests that are invalid on
   purpose.
8. **Message quality.**
   - `rovas-numbering.odt` (a broken `office:body`) is reported as "of a
     different kind".
   - `60825.xlsx` (`r:id=""`) is reported as "invalid content-type list,
     relationships…".

   Both are correct rejections with an imprecise reason.
9. **Coverage gaps.** No EPUB 2, no Numbers or Google Sheets exports, no openpyxl
   corpus, and few real SRT files (12). No reference for HTML beyond the round-trip
   and a visual check of WCAG and MDN pages.

## Reproduction

The driver, job builder, analyzer and inspection scripts were kept in the session
scratchpad, not in the repository. In outline:

1. Clone the repositories above with `--depth 1`, sparse where noted.
2. Write one JSON job per file:
   `{"tool":…,"source":…,"ext":…,"args":{}}`.
3. Stream the jobs into one `arken-convert-commands --stdio --root <corpus>`
   process, reading one reply per call.
4. Run `markdown_to_html` on each Markdown output.
5. Compare each result with the Python `zipfile`/ElementTree references above.
