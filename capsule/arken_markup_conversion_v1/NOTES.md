# arken_markup_conversion_v1

Bounded, memory-only text-markup conversion:

- HTML to Markdown (CommonMark plus GFM tables and strikethrough),
- Markdown (a documented CommonMark subset plus GFM tables and strikethrough) to HTML,
- SRT to WebVTT and WebVTT to SRT.

The capsule only takes bytes in and gives bytes out: `std::span<const std::byte>`
in, a result struct out. It does not open files, use the network, start
processes, read the clock or run scripts. It has no third-party dependencies.
Every malformed, unsupported or over-budget input fails with a typed `Error`.
A failure never returns a truncated document that looks like a success. Output
is deterministic: the same bytes and options give the same bytes and report.
Nothing iterates an unordered container, and nothing depends on the locale,
time or addresses.

Surface status: **not exposed anywhere yet.** It is a library with a standalone
qualification project (`tests/`). "Remaining steps" below lists the exact work
needed to expose `html_to_markdown`, `markdown_to_html`, `srt_to_vtt` and
`vtt_to_srt` through the standalone MCP. Nothing here authorizes the browser
converter or the hosted MCP surfaces.

## Public API (`api.hpp`)

| Function | Options | Result |
|---|---|---|
| `HtmlToMarkdown(bytes, HtmlToMarkdownOptions)` | source/output bytes, depth, nodes, attributes, `dropNavigation` | `HtmlToMarkdownResult{error, markdown, report}` |
| `MarkdownToHtml(bytes, MarkdownToHtmlOptions)` | source/output bytes, nesting, links, `fullDocument`, `title` | `MarkdownToHtmlResult{error, html, report}` |
| `SrtToVtt(bytes, SubtitleOptions)` | source/output bytes, cues, payload bytes, `keepFontColor`, `crlf` | `SubtitleResult{error, text, line, report}` |
| `VttToSrt(bytes, SubtitleOptions)` | same | same |

The API also provides `ErrorCode(Error)` (a stable snake_case code),
`ErrorMessage(Error)` (an actionable sentence), `MediaType(Format)` and
`revision` (`arken-markup-conversion-v1-r1`, printed by the test binary's
`--version`). Subtitle errors carry the 1-based input `line`. Each result's
`report` counts what was dropped or rewritten (see the profiles below). A
failed result always has empty text.

## Internal structure and the HTML swap boundary

```
src/detail.hpp           UTF-8 decode/validate, input preparation, Output budget, URL helpers, Failure/Guard
src/entities.cpp         named/numeric character-reference table shared by all three converters
src/html_tree.hpp/.cpp   HTML tokenizer + tree builder           <- swap boundary
src/html_to_markdown.cpp element tree -> Markdown mapping
src/markdown_to_html.cpp CommonMark block parser, inline parser, HTML renderer
src/subtitles.cpp        SRT/WebVTT readers and writers over one cue model
src/api.cpp              option validation, noexcept boundary, error codes/messages, media types
```

**Boundary.** The HTML tokenizer and tree builder sit behind one internal
interface in `src/html_tree.hpp`: `html::ParseHtml(bytes, TreeLimits,
TreeStats&) -> html::Tree` (bytes in, element tree out). The Markdown mapping
depends only on `html::Tree`/`html::Node`. The Markdown to HTML renderer and
the subtitle converters do not depend on it at all. The monorepo's
`arken_data_*` capsules may already own HTML processing. If an exported owner
becomes available, replace `html_tree.cpp` with an adapter that fills the same
`Tree`, and do not touch any other file. The header documents the contract an
adapter must keep:

- the root is `#document`;
- names are lowercase;
- references are decoded;
- there is no `html`/`head`/`body` node;
- raw-text elements have a single text child;
- limits throw `depth_limit`/`node_limit`;
- encoding problems throw `invalid_utf8`/`unsupported_charset`.

The decision to keep a capsule-local tokenizer is reversible at that one file.

## HTML to Markdown

### Tokenizer (`html_tree.cpp`)

Input handling:
- **UTF-8 only.** A UTF-8 BOM is stripped. A UTF-16 BOM, a `<meta charset>`,
  a `<meta http-equiv="content-type" content="...charset=...">` or an
  `<?xml ... encoding="...">` naming anything other than a WHATWG UTF-8 label
  (`utf-8`, `utf8`, `unicode-1-1-utf-8`, `unicode11utf8`, `unicode20utf8`,
  `x-unicode20utf8`) returns `unsupported_charset`. This includes `us-ascii`,
  which WHATWG maps to windows-1252.
- Invalid UTF-8 returns `invalid_utf8`.
- CRLF and lone CR become LF. U+0000 becomes U+FFFD.

Syntax handled:
- **Tags.** Tag names and attribute names are ASCII-lowercased. Attribute
  values may be double-quoted, single-quoted, unquoted or absent. The first of
  any duplicate attributes wins. Attributes beyond `maximumAttributes` are
  ignored. A trailing `/` sets the self-closing flag, which only void elements
  and `svg`/`math` content honour. End of input inside a tag drops the tag, as
  HTML does. A `<` not followed by a letter, `/` or `!`/`?` is literal text.
- **Comments.** `<!-- -->`, `<!-->`, `<!--->` and `--!>` are skipped. So are
  the doctype, `<! ...>`, `<? ...>` and `<![CDATA[...]]>`, which HTML parses
  as bogus comments outside foreign content. An unclosed comment runs to the
  end of input. All of these are counted in `commentsSkipped`.
- **Raw text** (`script`, `style`, `xmp`, `iframe`, `noembed`, `noframes`,
  `noscript`): the content up to the case-insensitive matching end tag
  (`</name` followed by whitespace, `/` or `>`) is one text node. It is never
  parsed as markup. **RCDATA** (`textarea`, `title`) is the same, except that
  references are decoded. `plaintext` takes the rest of the input as text.
- **Character references.**
  - All numeric references are supported, decimal and hex. 0, surrogates and
    values above U+10FFFF become U+FFFD. 0x80-0x9F map through windows-1252,
    as HTML5 specifies. Other C0 controls (except TAB/LF/FF) and DEL become
    U+FFFD.
  - Named references come from a fixed set of 259 names: the 252 HTML 4.01 /
    XHTML 1.0 entities, `&apos;`, and the HTML5 uppercase aliases `&AMP;
    &LT; &GT; &QUOT; &COPY; &REG;`. `&lang;`/`&rang;` take their HTML5
    values U+27E8/U+27E9. This set covers the Latin-1 letters and symbols,
    typographic punctuation, Greek letters and the common math and arrow
    symbols that real pages use. The full HTML5 set has 2,231 names. Most of
    the rest are MathML names that ordinary pages almost never use, and
    embedding the full table adds about 40 KiB of data for little benefit.
  - Exactly the HTML5 "legacy" names are recognized without the semicolon:
    the Latin-1 set plus `amp`/`lt`/`gt`/`quot` and the six uppercase
    aliases, using the longest match (so `&notin` without `;` is `¬in`). In
    attribute values, a legacy match followed by `=` or an alphanumeric is
    left literal (`?a=1&copy=2` stays intact).
  - **Unknown names are left as literal text** (`&bogus;` stays `&bogus;`,
    and is then Markdown-escaped as `\&bogus;`). Terminated unknown names are
    counted in `unknownEntities`.

### Tree builder

The tree builder:
- **Ignores** `html`, `head`, `body` and `frameset` tags. Their content
  belongs to the root, and text in `<head>` becomes body text, as HTML does.
- **Inserts implicit end tags:**
  - A block start (address, article, aside, blockquote, center, details,
    dialog, dir, div, dl, dd, dt, fieldset, figcaption, figure, footer, form,
    h1-h6, header, hgroup, hr, li, listing, main, menu, nav, ol, p, plaintext,
    pre, search, section, summary, table, ul, xmp) closes an open `p` in
    button scope.
  - A heading start closes an open heading.
  - `li` closes an open `li`, unless a special element other than
    address/div/p intervenes. `dd`/`dt` do the same for `dd`/`dt`.
  - `tr`, `thead`, `tbody`, `tfoot`, `caption` and `colgroup` close the open
    cell, row or section of the nearest table. `td`/`th` close the open cell
    and insert an implicit `tr` when no row is open.
  - `option` closes `option`. `optgroup` closes `option`/`optgroup`.
  - `a` closes an open `a`, and `button` closes an open `button`.
  - A `table` directly inside a table (not in a cell or caption) closes the
    first table.
- **Handles end tags by scope:** `</p>`, `</li>` (list item scope),
  `</h1>`-`</h6>` (any heading), table end tags (table scope), and special
  elements (default scope). Any other end tag closes the nearest element with
  that name, unless a special element intervenes, in which case the end tag
  is ignored. `</br>` is `<br>`.
- **Never pushes void elements** onto the stack: area, base, basefont,
  bgsound, br, col, embed, frame, hr, img, image, input, keygen, link, meta,
  param, source, track, wbr.
- **Limits:** `maximumDepth` bounds the open elements and `maximumNodes`
  bounds the elements plus text nodes. Exceeding either is a typed error. No
  limit is enforced by silently cutting input.
- **Not implemented:** the adoption agency algorithm (misnested formatting
  closes at the first matching end tag, and formatting is not reconstructed
  in the next paragraph), foster parenting (stray text inside a table makes
  the table non-rectangular instead), template contents, the full
  insertion-mode state machine and quirks mode.

### Mapping (`html_to_markdown.cpp`)

| HTML | Markdown |
|---|---|
| `h1`-`h6` | ATX heading `#`..`######`. Inline content on one line (`br` becomes a space). Every `#` is escaped. Empty headings are dropped. |
| `p`, `div`, `section`, `article`, `main`, `header`, `footer`, `nav`, `aside`, `address`, `center`, `figure`, `figcaption`, `form`, `fieldset`, `details`, `summary`, `hgroup`, `legend`, `select`, `option`, `optgroup`, `datalist`, `search`, `dialog`, stray `li`/`dd`/`dt`/table parts | Block containers: paragraph boundaries. Inline runs between blocks become paragraphs. |
| `br` | Hard break `\` + newline. A space in headings and table cells. Leading and trailing breaks are dropped. |
| `hr` | `---` |
| `ul`, `menu`, `dir` / `ol` | `- ` items / `N. ` items from `start` (0-999999999, otherwise 1). Nesting indents by the marker width. The list is tight when every item is at most one paragraph followed by sublists, otherwise loose. A sublist that cannot interrupt a paragraph (ordered, start not 1) makes the list loose. Adjacent lists alternate `-`/`*` or `.`/`)` so they stay separate. Content between items becomes its own item. |
| `blockquote` | Every line prefixed `> ` (blank lines `>`). Nests. |
| `pre`, `listing`, `xmp`, `plaintext` | Fenced code. The fence is at least 3 backticks and longer than any backtick run in the content. The language comes from `class="language-x"` or `lang-x` on the `pre` or a descendant `code`, sanitized to `[A-Za-z0-9_+.#-]`. Text is verbatim, `br` becomes a newline, and markup is flattened. One trailing newline is dropped. |
| `code`, `kbd`, `samp`, `tt` (outside pre) | Code span. The backtick fence is longer than any run inside, padded when needed. `|` is escaped inside table cells. |
| `em`, `i` / `strong`, `b` / `s`, `strike`, `del` | `*x*` / `**x**` / `~~x~~`. Edge whitespace is moved outside the delimiters. Adjacent runs with the same format merge across a single space. |
| `a[href]` | `[text](dest)` only for `http://`, `https://` or `mailto:` (case-insensitive, after removing TAB/LF/CR and trimming). The destination is percent-encoded outside the URI set and for `( ) < >` and spaces. Any other `href` (relative, fragment, `ftp:`, `javascript:`, `data:`, ...) becomes plain text and is counted in `linksAsText`. A link with no visible content is dropped. |
| `img` | `![alt](src)` only for `http(s)` sources. Otherwise the alt text in italics (`imagesAsText`). With no alt either, the image is dropped (`imagesDropped`). An image inside a link nests: `[![a](s)](h)`. |
| `table`/`thead`/`tbody`/`tfoot`/`tr`/`th`/`td`/`caption` | GFM pipe table when rectangular: at least one row, every row the same nonzero cell count, no `colspan`/`rowspan` > 1, no stray text or elements, and every cell a single paragraph. The first row is the header row, whether it is `th` or `td`. The caption is a paragraph before the table. Otherwise every cell's content becomes paragraphs in document order (`tablesAsParagraphs`). |
| `dl`/`dt`/`dd` | `**term**` paragraph, then the definition blocks indented by two spaces. CommonMark renders that indentation as an ordinary paragraph, and four spaces would make a code block. |
| `script`, `style`, `noscript`, `template` | Dropped (`scriptStyleDropped`). |
| `iframe`, `object`, `embed`, `applet`, `svg`, `math`, `canvas`, `audio`, `video`, `title`, `noembed`, `noframes`, `frame`, `map` | Dropped with their content (`embeddedDropped`). `link`/`meta`/`base` are dropped silently. |
| `hidden`, `aria-hidden="true"`, `style` with `display:none` / `visibility:hidden` | Subtree dropped (`hiddenDropped`). |
| `nav`, `aside`, `header`, `footer` with `dropNavigation=true` | Subtree dropped (`navigationDropped`). The default keeps them. |
| everything else (`span`, `u`, `ins`, `mark`, `sub`, `sup`, `abbr`, `q`, `label`, `button`, `textarea`, unknown) | Transparent: the content is kept. |

**Whitespace.** HTML whitespace (space, TAB, LF, FF, CR) collapses to one
space. Block edges and the spaces around hard breaks are trimmed. U+00A0 is
preserved. Text in `pre` is verbatim.

**Escaping.** Escaping never emits raw HTML. It always backslash-escapes
`\ ` * _ [ ] < | ~`, and `&` when it is followed by an alphanumeric or `#`.
At the start of a line it also escapes `#`, `>`, `-`, `+` and `=`, and turns
`N.`/`N)` into `N\.`/`N\)`. In headings it escapes `#` everywhere. A test
checks that literal text survives HTML to Markdown to HTML unchanged for
inputs such as `***`, `---`, `1. x`, `[link]: /url`, `<!-- c -->` and
`&copy;`.

Output blocks are separated by one blank line, and a nonempty document ends
with LF.

## Markdown to HTML

### Supported CommonMark subset

The block and inline algorithms are ports of the CommonMark reference
implementation (commonmark.js `blocks.js`/`inlines.js`). Expected outputs in
the tests are CommonMark 0.31.2 / GFM 0.29 spec examples where marked.

| Construct | Status |
|---|---|
| Tabs (4-column stops, partial tab consumption) | Supported |
| Thematic breaks (`***`, `---`, `___`, with spaces) | Supported |
| ATX headings (1-6, optional closing sequence) | Supported |
| Setext headings (`===`, `---`, multi-line, no lazy underline) | Supported |
| Indented code blocks (trailing blank lines trimmed) | Supported |
| Fenced code blocks (backtick/tilde, length rules, indentation removal, info string, first word becomes `class="language-x"`) | Supported |
| Paragraphs, lazy continuation | Supported |
| Block quotes (nesting, laziness) | Supported |
| Bullet and ordered lists (`-+*`, `.`/`)`, start up to 9 digits, interrupting rules, nesting, tight/loose by blank lines between items or their children) | Supported |
| Backslash escapes (ASCII punctuation), backslash hard breaks | Supported |
| Entity and numeric references | Numeric: full rules. Named: the 259-name set above. Unknown names stay literal (`&amp;bogus;`) and are counted in `unknownEntities`. |
| Code spans (backtick runs, space stripping, line ends become spaces) | Supported |
| Emphasis and strong (`*`, `_`): left/right-flanking runs, `_` intraword rules, rule of three, openers_bottom | Supported. Unicode whitespace is exact (Zs + TAB/LF/FF/CR). Unicode punctuation is ASCII punctuation plus a fixed table of the common punctuation and symbol blocks (Latin-1 punctuation, General Punctuation, currency, letterlike, arrows, math, technical, box/geometric, dingbats, supplemental punctuation, CJK and fullwidth punctuation), not the full P/S categories. |
| Inline links `[text](dest "title")`, `<dest>`, balanced parentheses (depth 32), titles in `"`/`'`/`()`; images `![alt](src "title")` with alt as plain text; no links inside links | Supported |
| Autolinks `<scheme:...>` and `<email>` | Supported |
| Hard line breaks (2+ spaces or `\`) and soft line breaks | Supported (soft break is `\n`) |
| GFM pipe tables (alignment, escaped `\|`, ragged rows, a header split off a preceding paragraph, ended by a blank line or block start) | Supported. The delimiter row must contain a `\|`. |
| GFM strikethrough `~x~` / `~~x~~` (same flanking rules; 3+ tildes are literal) | Supported |
| Reference links and link reference definitions | **Not supported.** `[foo]: /url` stays paragraph text, and `[foo][bar]`/`[foo]` are literal. |
| Raw HTML blocks and inline raw HTML | **Not recognized** (like markdown-it with `html: false`). Lines that would start an HTML block are ordinary Markdown, and every `<` is emitted as `&lt;`. Tag-like `<` are counted in `htmlEscaped`. Output therefore never contains author HTML. |
| GFM autolink extension (bare `www.`/URLs), task lists, footnotes, the GFM tagfilter, smart punctuation | Not supported (bare URLs stay text). |

### Output format

Output matches commonmark.js / cmark-gfm HTML:
- `<p>`, `<h1>`-`<h6>`, `<hr />`, `<br />`, `<pre><code class="language-x">`,
  `<blockquote>`, `<ul>`/`<ol start="n">`/`<li>` (tight lists omit `<p>`),
  `<em>`, `<strong>`, `<del>`, `<code>`, `<a href title>`, `<img src alt
  title />`;
- tables as `<table><thead><tr><th align="...">` plus `<tbody>` when there
  are rows.

Every text node and attribute value escapes `& < > "`. Output is a UTF-8 HTML
fragment ending with LF. With `fullDocument=true` it becomes `<!DOCTYPE
html>`, `<html>`, `<head>`, `<meta charset="utf-8">`, an escaped `<title>`
(`options.title`, else the first heading's plain text, else `Document`), then
`<body>`.

## SRT and WebVTT

Both directions parse into one cue model: start and end in milliseconds, plus
a token list of text, `b`/`i`/`u` open/close and colour open/close. Input is
strict UTF-8 (`invalid_utf8`), a BOM is stripped, and CRLF/CR become LF.

**SRT reader:**
- Accepts an optional numeric index line, then
  `H:MM:SS,mmm --> H:MM:SS,mmm`. Hours are 1-5 digits; minutes and seconds
  are 2 digits and at most 59; milliseconds are exactly 3 digits; a `.`
  separator is tolerated; spaces around `-->` are optional.
- Payload lines run until a blank (or whitespace-only) line.
- Tolerated: a missing index (`missingIndices`); a missing blank line when the
  next line is a valid timing line, optionally after an index; and
  coordinates after the end time (`X1:... Y2:...`, dropped and counted in
  `settingsDropped`).
- A line with `-->` that does not parse returns
  `subtitle_malformed_timestamp`. Text where an index or timing is expected
  returns `subtitle_malformed_block`.
- Tags: `b`, `i` and `u` are kept (case-insensitive). `<font color=...>` is
  stripped, or with `keepFontColor` mapped to the WebVTT default colour
  classes (white, lime, cyan, red, yellow, magenta, blue, black, by name or
  `#RRGGBB`). Other colours and other tags are stripped (`tagsStripped`).
  `{\...}` override codes are stripped (`overridesStripped`).
- SRT text is literal: `&amp;` is not decoded.

**WebVTT reader:**
- The first line must be `WEBVTT`, optionally followed by a space or TAB and
  text (`subtitle_missing_header`). The header text and metadata lines up to
  the first blank line are dropped (`headerLinesDropped`).
- `NOTE`, `STYLE` and `REGION` blocks are dropped (`notesDropped`,
  `styleBlocksDropped`, `regionBlocksDropped`).
- A cue is an optional identifier line, then `[HH:]MM:SS.mmm -->
  [HH:]MM:SS.mmm`. Hours are 2-5 digits when present. The `.` is required.
  Cue settings are dropped (`settingsDropped`).
- Payload lines run until a blank line or a line containing `-->`, as the
  WebVTT parser does.
- Identifiers are not carried. One that differs from the SRT index written is
  counted in `identifiersDropped`.
- Tags: `b`, `i` and `u` are kept and their classes dropped. `c` is stripped,
  or with `keepFontColor` mapped to `<font color="name">` when a class is a
  default colour. `v` (the speaker name is dropped), `lang`, `ruby`, `rt`,
  timestamp tags and unknown tags are stripped and their text kept
  (`tagsStripped`).
- References are decoded with the shared table, plus numeric references.

**Validation:**
- A cue ending before it starts returns `subtitle_time_order`. Equal start
  and end are allowed.
- A cue starting before the previous cue returns `subtitle_non_monotonic`.
  Overlap and equal starts are allowed.
- Too many cues returns `subtitle_cue_limit`. A payload over
  `maximumPayloadBytes` returns `subtitle_payload_limit`.
- Every subtitle error reports its 1-based line.

**Writers:**
- WebVTT: `WEBVTT`, a blank line, then each cue as `HH:MM:SS.mmm -->
  HH:MM:SS.mmm`, its payload and a blank line. Text escapes `& < >`, so
  `-->` in text becomes `--&gt;`.
- SRT: each cue as its index (1..n), `HH:MM:SS,mmm --> HH:MM:SS,mmm`, its
  payload and a blank line. Line ends are LF, or CRLF with `crlf=true`.
  Hours use at least 2 digits.
- Both writers remove payload lines that end up empty, so a cue never splits.
  SRT to WebVTT to SRT is byte-identical for plain cues in this canonical
  form (tested).

## Security policy

- **No active content can be produced.**
  - HTML to Markdown never emits raw HTML. All `<` are escaped. Script,
    style, template, noscript and embedded content is dropped. Event-handler
    attributes are never read. Only `http`/`https`/`mailto` links and
    `http`/`https` images become Markdown links and images.
  - Markdown to HTML never passes raw HTML through, escapes every text node
    and attribute value, and emits only the fixed tag set listed above.
- **URL policy for Markdown to HTML.** The destination is unescaped,
  percent-encoded outside the URI set, and then checked:
  - When a `:` occurs before any `/`, `?` or `#`, the prefix is a scheme and
    must be `http`, `https` or (links only) `mailto`.
  - Otherwise the URL must be relative and not scheme-relative (`//host` or
    `\\host` are refused).

  So `javascript:`, `data:`, `vbscript:`, `file:` and encoded variants such
  as `java&#x09;script:` (which becomes `java%09script:`) are refused. A
  refused link renders as its text (`unsafeLinks`). A refused image renders
  as its plain alt text (`unsafeImages`). CommonMark example 497
  (`[link](foo\)\:)`) is therefore rendered as text.
- **No external resources.** No DTDs, no external entities, no fetching,
  just memory in and memory out.
- **Bounded resources.**
  - Every input is bounded by `maximumSourceBytes` and every output by
    `maximumOutputBytes`.
  - Nesting is bounded (HTML `maximumDepth` <= 4096, Markdown
    `maximumNesting` <= 1024), so mapping and block-rendering recursion
    cannot exhaust the stack.
  - Inline rendering is iterative, so 200,000-deep emphasis is fine.
  - Backtick-run matching is memoized.
  - Link destination and title scanning share a budget of 4 x the inline
    subject + 64 KiB. Once it is spent, further `](` are literal text. This
    keeps `[](x[](x...` linear.

## Limits (defaults)

| Option | Default | Error |
|---|---|---|
| `HtmlToMarkdownOptions::maximumSourceBytes` | 8 MiB | `source_limit` |
| `HtmlToMarkdownOptions::maximumOutputBytes` | 16 MiB | `output_limit` |
| `HtmlToMarkdownOptions::maximumDepth` | 256 open elements (1-4096) | `depth_limit` |
| `HtmlToMarkdownOptions::maximumNodes` | 1,000,000 elements + text nodes | `node_limit` |
| `HtmlToMarkdownOptions::maximumAttributes` | 64 per element (extras ignored) | none |
| `MarkdownToHtmlOptions::maximumSourceBytes` | 8 MiB | `source_limit` |
| `MarkdownToHtmlOptions::maximumOutputBytes` | 32 MiB (tables pad ragged rows, so output can exceed input) | `output_limit` |
| `MarkdownToHtmlOptions::maximumNesting` | 64 block quotes + list items (1-1024) | `depth_limit` |
| `MarkdownToHtmlOptions::maximumLinks` | 100,000 links + images | `link_limit` |
| `SubtitleOptions::maximumSourceBytes` | 16 MiB | `source_limit` |
| `SubtitleOptions::maximumOutputBytes` | 64 MiB | `output_limit` |
| `SubtitleOptions::maximumCues` | 200,000 | `subtitle_cue_limit` |
| `SubtitleOptions::maximumPayloadBytes` | 16 KiB per cue | `subtitle_payload_limit` |

A zero limit, or a depth or nesting above its cap, returns `invalid_options`.

Throughput, measured on the development container in a Release build at
8 MiB inputs:
- **Ordinary documents:** well under a second.
- **Hostile Markdown made only of syntax characters** (8 MiB of `[`, `<a` or
  `_a_ `): linear, about 0.5 s per MiB. Every character becomes an inline
  node.
- **HTML with 120 nested lists around 8 MiB of text:** about 3 s, because each
  list level re-indents its content (O(depth x text)).

An integrating surface should keep its own source limit near the defaults.

## Explicit limitations

- **HTML parsing.**
  - The tree builder is a simplified HTML5 builder (no adoption agency,
    formatting reconstruction, foster parenting or templates), so
    pathological misnesting can differ from a browser's DOM.
  - Only UTF-8 is accepted.
  - The named-reference table is the 259-name subset described above.
- **HTML mapping.**
  - CSS is not evaluated beyond the inline `display:none`/`visibility:hidden`
    check.
  - Emphasis never spans a link boundary (`**a** [**b**](u) **c**`).
  - Emphasis whose content starts or ends with punctuation next to a word
    character may not parse back as emphasis.
  - `u`, `ins`, `mark`, `sub` and `sup` lose their formatting.
  - Relative links are not resolved (there is no base URL), so they become
    text.
  - `colspan`/`rowspan` tables become paragraphs.
  - Definition-list indentation is visual only.
- **Markdown.** See the Supported CommonMark subset section. Reference links,
  raw HTML pass-through and the GFM extensions not listed there are out of
  scope.
- **Subtitles.**
  - Cue positioning (WebVTT settings, SRT coordinates), styling (STYLE
    blocks, classes), regions, voice names and ruby annotations are lost.
    Each loss is counted.
  - SRT has no escaping, so WebVTT text containing `<b>` (written as
    `&lt;b&gt;`) becomes literal `<b>` in SRT, where players may treat it
    as a tag. A decoded `-->` in WebVTT text also reaches SRT literally.
  - SRT payload lines that are themselves valid timing lines are read as a
    new cue.
  - Only UTF-8 subtitles are accepted. Legacy code-page SRT files must be
    converted first.

## Verification

```
cmake -S capsule/arken_markup_conversion_v1/tests -B build-markup -DCMAKE_BUILD_TYPE=Release
cmake --build build-markup -j2
ctest --test-dir build-markup --output-on-failure
```

The project resolves the first-party module graph from `tests/lib.manifest.json`
through `sdk/arken_sdk_dev/cmake/ArkenModuleGraph.cmake`. It fails configuration
unless exactly `arken_markup_conversion_v1` resolves. It builds with
`-Wall -Wextra -Wpedantic -Werror` and registers these tests:

| Test | Checks |
|---|---|
| `Version` | `--version` prints `revision` |
| `html_tokenizer` | references, quoting, tag soup, comments, raw text, implicit end tags, charset, UTF-8 |
| `html_markdown` | every mapping-table row, escaping, reports |
| `html_security` | dangerous schemes, script, style, embedded content, inertness through both converters |
| `markdown_blocks` | CommonMark/GFM spec examples for blocks and tables, HTML-block escaping, full document |
| `markdown_inlines` | CommonMark/GFM spec examples for escapes, references, code spans, flanking, links, autolinks, breaks, strikethrough |
| `markdown_security` | URL policy, raw HTML, attribute breakouts, info strings |
| `srt_vtt` | SRT tolerance, tags, colour option, round trips, typed errors with line numbers |
| `vtt_srt` | header, NOTE, STYLE and REGION blocks, settings, identifiers, tags, CRLF, typed errors |
| `limits` | every limit and error code, stack-safety on deep or pathological input |
| `determinism` | repeated runs, structure through HTML to Markdown to HTML, exact escaping |
| `mutation` | 6,000 seeded byte mutations giving a typed error or valid UTF-8, never active content |

`arken-markup-conversion-tests mutation N` runs a longer local campaign.

## Remaining steps to expose `html_to_markdown`, `markdown_to_html`, `srt_to_vtt`, `vtt_to_srt`

These steps belong to owners this capsule must not edit. They are for the
integrator and mirror how `arken_document_extraction_v1` was exposed
(`convert_document`, ids 20-24).

1. **Module graph.** In `capsule/arken_file_conversion_v1/capsule.manifest.json`:
   - Add `"arken_markup_conversion_v1"` to the `public_dependencies` of the
     `standalone` component.
   - Add `"standalone_markup_conversion"` to `capabilities`.

   No external recipe is needed, because this capsule has none. Keep the root
   `CMakeLists.txt` module denylist unchanged.
2. **Conversion owner.** Add `capsule/arken_file_conversion_v1/markup.hpp`
   and `src/markup.cpp`, next to `documents.hpp` and `src/document.cpp`:
   ```cpp
   namespace arken::file_conversion::v1 {
   namespace markup = arken::markup::v1;
   inline constexpr std::uint32_t maximumMarkupSourceBytes = 8u*1024u*1024u;
   inline constexpr std::uint32_t maximumMarkupOutputBytes = 32u*1024u*1024u;
   enum class MarkupConversion : std::uint32_t {html_to_markdown=1, markdown_to_html=2, srt_to_vtt=3, vtt_to_srt=4};
   struct MarkupResult { Error error = Error::none; markup::Error markupError = markup::Error::none;
                         std::uint64_t line = 0;          // subtitle errors
                         std::uint64_t dropped = 0;       // sum of the report's drop counters
                         std::uint64_t cues = 0, links = 0;
                         std::vector<std::byte> bytes;
                         explicit operator bool() const noexcept { return error == Error::none; } };
   [[nodiscard]] MarkupResult ConvertStandaloneMarkup(MarkupConversion, std::span<const std::byte>) noexcept;
   [[nodiscard]] std::string_view MarkupMediaType(MarkupConversion) noexcept;
   }
   ```
   - Call the matching `markup::` function with
     `maximumSourceBytes = maximumMarkupSourceBytes`,
     `maximumOutputBytes = maximumMarkupOutputBytes` and default options
     otherwise (`dropNavigation=false`, `fullDocument=false`,
     `keepFontColor=false`, `crlf=false`).
   - Map errors: `source_limit` goes to `input_limit`; `output_limit` goes to
     `encode_failed`; `allocation_failed` goes to `allocation_failed`; every
     other error goes to `decode_failed`, with `markupError` (and `line`)
     kept for the message.
   - Media types are the output media types: `text/markdown; charset=utf-8`,
     `text/html; charset=utf-8`, `text/vtt` and `application/x-subrip`, from
     `markup::MediaType`.
   - In `cmake/ArkenFileConversionImages.cmake`:
     - In `arken_add_file_conversion_standalone_media`, add
       `add_library(arken_file_conversion_markup_v1 STATIC "${owner}/src/markup.cpp")`
       linked `PUBLIC arken_file_conversion_images_v1 arken_markup_conversion_v1`,
       and add it to the `foreach` that sets features and warnings.
     - Extend the `FATAL_ERROR` guard with
       `TARGET arken_markup_conversion_v1`.
     - Link `arken_file_conversion_markup_v1` wherever
       `arken_file_conversion_documents_v1` is linked for the standalone server
       and the media tests.
3. **Schema.** In
   `capsule/arken_file_conversion_commands_v1/schema/conversion_files.arken-schema`:
   - Add
     `model markup_result @type_id(10) { field output_path: text @field_id(1) { immutable true; } field media_type: text @field_id(2); field output_bytes: count @field_id(3); field sha256: text @field_id(4); field durable: boolean @field_id(5); field dropped: count @field_id(6); field cues: count @field_id(7); field links: count @field_id(8); key identity { kind primary; fields output_path; } }`.
   - Add four unary commands with
     `request file_request; response markup_result; errors command_error; idempotency none; streaming unary; targets cpp;`:
     `html_to_markdown @command_id(25)`, `markdown_to_html @command_id(26)`,
     `srt_to_vtt @command_id(27)` and `vtt_to_srt @command_id(28)`.
   - Use the next free ids if 25-28 have been taken by then. Regenerate the
     schema output as the repository does for the existing commands.
4. **Commands** (`capsule/arken_file_conversion_commands_v1/src/commands.cpp`):
   - Add a `markupPairs` table (id, `MarkupConversion`, description) and
     include it in `presentations()` like `documentPairs`. Suggested
     descriptions:
     - **html_to_markdown:** "Convert a UTF-8 HTML file to Markdown
       (CommonMark with GFM tables). Scripts, styles, embedded content and
       hidden elements are dropped; only http/https/mailto links and
       http/https images are kept; other encodings are rejected. Limits: 8 MiB
       input, 32 MiB output."
     - **markdown_to_html:** "Render Markdown (a CommonMark subset with GFM
       tables and strikethrough; no reference links) to an HTML fragment.
       Raw HTML is shown as text, never passed through; unsafe link schemes
       become plain text."
     - **srt_to_vtt / vtt_to_srt:** "...cue settings, styles, regions, notes
       and voice tags are dropped and counted; malformed timestamps fail with
       the line number."
   - Dispatch ids 25-28 next to the document dispatch. Read the source with
     `impl_->files->read(value.source, C::maximumMarkupSourceBytes)`, call
     `C::ConvertStandaloneMarkup`, and on failure return
     `error(id, "conversionFailed", ...)` with
     `C::ErrorMessage(error) + ": " + markup::ErrorMessage(markupError)`,
     plus `" (line N)"` when `line` is nonzero.
   - Fill `media_type`, `output_bytes`, `sha256`, `durable`, `dropped`, `cues`
     and `links`, then `publish` through the existing exclusive
     `impl_->files->create`, which never overwrites and leaves the source
     untouched.
5. **Tests.**
   - In `capsule/arken_file_conversion_commands_v1/tests/commands.cpp`:
     - Add the four names to the expected tool set; twenty-three tools
       become twenty-seven. Update the assertion text.
     - Add `converted` cases that write `inputs/page.html`,
       `inputs/notes.md`, `inputs/subs.srt` and `inputs/subs.vtt` and compare
       the output bytes with this capsule's fixtures, for example
       `<h1>T</h1><p>a <b>b</b></p>` gives `# T\n\na **b**\n`, and
       `plainSrt` from `tests/markup_tests.cpp` gives its WebVTT text.
     - Check `media_type`, `sha256`, `output_bytes`, no-overwrite,
       traversal rejection, a `windows-1252` HTML (`conversionFailed` naming
       the encoding), a malformed SRT timestamp (message carries the line),
       and a source over 8 MiB.
   - Add an `arken_file_conversion_v1` media test for the error mapping.
   - Optionally add this capsule's `tests/` project to the root test set.
6. **README.** Document the four tools, their limits, the supported profile
   (the CommonMark subset and the HTML mapping table above), the security
   policy and the limitations section.
