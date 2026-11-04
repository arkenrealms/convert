#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_document_extraction_v1/api.hpp"
#include "arken_document_extraction_v1/text_documents.hpp"

namespace arken::file_conversion::v1 {
namespace document = arken::document::v1;
// Standalone native MCP document profile: DOCX (main story), RTF, PPTX and ODP (slide
// text), ODT (body) and EPUB 2/3 (XHTML spine) to UTF-8 TXT or Markdown, resident (not
// streaming). Separate from every other surface's admission.
//
// 32 MiB of source: an Office/OpenDocument/EPUB package is mostly images and fonts, which
// are never inflated, so the source bound mostly limits the file read; RTF is read whole.
// 16 MiB per expanded ZIP member (the capsule default is 32 MiB): the extractor holds the
// inflated part, its XML node tree and the model together, measured at 8-10x the expanded
// part for DOCX, so 16 MiB keeps one call near 160 MiB instead of ~320 MiB. PPTX, ODT, ODP
// and EPUB were not measured; they parse one part at a time with the same parser, so the
// DOCX figure is the expected order of magnitude. Every part read counts against the
// capsule's 64 MiB total expansion budget.
// 32 MiB of output: the model holds at most 16 MiB of text, and Markdown escaping, list
// indentation and table pipes can add to it; a larger result is rejected, never cut.
inline constexpr std::uint32_t maximumDocumentSourceBytes = 32u * 1024u * 1024u;
inline constexpr std::uint32_t maximumDocumentEntryExpandedBytes = 16u * 1024u * 1024u;
inline constexpr std::uint32_t maximumDocumentOutputBytes = 32u * 1024u * 1024u;
// PPTX/ODT/ODP/EPUB structure caps: the capsule's defaults, stated so a capsule change
// cannot silently widen this profile.
inline constexpr std::uint32_t maximumDocumentItems = 4096;        // Slides, ODP pages or EPUB spine items.
inline constexpr std::uint32_t maximumDocumentNesting = 128;       // Lists, spans, groups, divs, sections.
inline constexpr std::uint32_t maximumDocumentTableCells = 1'000'000; // Including ODF repeats and padding.

// Values equal the schema's document_input_kind.
enum class DocumentInput : std::uint32_t { docx = 1, rtf = 2, pptx = 3, odt = 4, odp = 5, epub = 6 };
struct DocumentOptions {
    DocumentInput input = DocumentInput::docx;
    document::OutputFormat output = document::OutputFormat::text;
    // PPTX/ODP only (invalid_options otherwise): append each slide's speaker notes as a
    // trailing "Notes:" paragraph. Off by default, and then counted in excluded.notes.
    bool includeNotes = false;
};
struct DocumentResult {
    Error error = Error::none;
    document::Error documentError = document::Error::none;                   // DOCX/RTF reason.
    document::TextDocumentError textDocumentError = document::TextDocumentError::none; // PPTX/ODT/ODP/EPUB reason.
    // Blocks of the extracted model (tables count once; tableRows counts their rows).
    std::uint32_t paragraphs = 0, headings = 0, listItems = 0, tables = 0, tableRows = 0;
    std::uint32_t blockQuotes = 0, codeBlocks = 0, thematicBreaks = 0;
    // PPTX/ODT/ODP/EPUB: slides, chapters, notes and what was deliberately left out
    // (all zero for DOCX/RTF, whose extractor has no exclusion report).
    document::TextDocumentReport report;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] document::ConvertOptions StandaloneDocumentOptions() noexcept;
[[nodiscard]] document::TextDocumentOptions StandaloneTextDocumentOptions(bool includeNotes) noexcept;
[[nodiscard]] DocumentResult ConvertStandaloneDocument(const DocumentOptions&, std::span<const std::byte>) noexcept;
// DOCX/RTF form, equal to the DocumentOptions form with includeNotes=false.
[[nodiscard]] DocumentResult ConvertStandaloneDocument(document::SourceFormat, document::OutputFormat,
    std::span<const std::byte>) noexcept;
[[nodiscard]] std::string_view DocumentErrorMessage(Error) noexcept;
}
