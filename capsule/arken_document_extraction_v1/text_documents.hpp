#pragma once
#include "api.hpp"

// Bounded, memory-only text extraction from PowerPoint (PPTX), OpenDocument
// text and presentation (ODT, ODP) and EPUB 2/3 packages into the common
// Document model of api.hpp, serialized by the existing TXT and Markdown
// serializers. The ZIP reader and XML tokenizer of this capsule are reused.
// No file, network, process or Office access; every malformed, encrypted,
// DRM-protected, macro-enabled or over-budget input is a typed error, never an
// empty successful document. See NOTES.md, "Presentations, OpenDocument and EPUB".
namespace arken::document::v1 {
// r2: EPUB blockquote, hr and pre become block quotes, thematic breaks and code blocks.
inline constexpr std::string_view textDocumentsRevision="arken-document-extraction-v1-textdocs-r2";

enum class TextDocumentFormat : std::uint32_t {pptx=1, odt=2, odp=3, epub=4};

// One error space for all four formats. The container, XML and budget codes
// mirror api.hpp's Error but carry format-neutral messages.
enum class TextDocumentError : std::uint32_t {
    none=0,
    invalid_options,
    source_limit,
    text_limit,          // Model text bytes or blocks over budget.
    output_limit,
    allocation_failed,
    internal,            // An unexpected internal failure (never expected; reported, not hidden).
    item_limit,          // Slides, pages or spine items exceed maximumItems.
    nesting_limit,       // Structural nesting exceeds maximumNesting.
    table_limit,         // Materialized table cells (after repeats) exceed maximumTableCells.
    zip_not_archive,
    zip_malformed,
    zip_unsupported,
    zip_encrypted_entry,
    zip_unsafe_name,
    zip_duplicate_name,
    zip_entry_limit,
    zip_size_limit,
    zip_crc_mismatch,
    zip_corrupt_data,
    xml_malformed,
    xml_doctype,
    xml_encoding,
    xml_entity,
    xml_limit,
    // PowerPoint (PresentationML).
    pptx_encrypted,          // OLE compound file or EncryptedPackage/EncryptionInfo members.
    pptx_macro_enabled,      // .pptm/.ppsm/.potm main part or a VBA project part.
    pptx_not_presentation,   // No officeDocument relationship to a PresentationML main part.
    pptx_invalid_package,    // Invalid [Content_Types].xml, relationships or presentation part.
    pptx_missing_part,       // The presentation or a listed slide part is absent.
    // OpenDocument.
    odf_not_opendocument,    // No mimetype member, or not an OpenDocument media type.
    odf_wrong_kind,          // OpenDocument of another kind (for example a spreadsheet given as ODT).
    odf_invalid_package,     // Invalid manifest or content.xml structure.
    odf_missing_part,        // content.xml or the manifest is absent.
    odf_encrypted,           // manifest:encryption-data present (password-protected document).
    // EPUB.
    epub_not_epub,           // No "application/epub+zip" mimetype member.
    epub_invalid_package,    // Invalid container.xml, OPF package, manifest or spine.
    epub_missing_part,       // The OPF package or a spine item is absent.
    epub_drm,                // META-INF/encryption.xml encrypting content, or META-INF/rights.xml.
    epub_content_not_xhtml,  // A spine item is HTML that is not well-formed XHTML.
};

struct TextDocumentOptions {
    ExtractOptions extract;      // Source, model, ZIP and XML budgets (rtf limits are unused).
    SerializeOptions serialize;
    bool includeNotes=false;     // PPTX notesSlides / ODP presentation:notes as a trailing "Notes:" paragraph per slide.
    std::uint32_t maximumItems=4096;        // Slides (PPTX/ODP) or spine items (EPUB).
    std::uint32_t maximumNesting=128;       // Structural recursion depth (lists, spans, groups, divs).
    std::uint64_t maximumTableCells=1'000'000; // Cells materialized per document, including ODF repeats and padding.
};

// What was extracted and what was deliberately left out. Exclusion counters
// count elements (or items) seen and skipped, not characters.
struct TextDocumentReport {
    std::uint32_t slides=0;        // PPTX slides or ODP draw:page elements extracted.
    std::uint32_t chapters=0;      // EPUB spine items extracted.
    std::uint32_t notesIncluded=0; // Slides whose speaker notes were emitted (includeNotes).
    std::uint32_t tocHeadings=0;   // EPUB chapters given a heading from the table of contents.
    std::uint64_t blocks=0;
    struct Excluded {
        std::uint64_t images=0;       // p:pic, draw:image frames, img, svg.
        std::uint64_t graphics=0;     // Charts, SmartArt, OLE/objects, math, media, ink, non-table graphicFrames.
        std::uint64_t notes=0;        // Speaker notes left out (option off) and ODF footnotes/endnotes.
        std::uint64_t annotations=0;  // ODF office:annotation comments.
        std::uint64_t deletions=0;    // ODF tracked-change deletion regions.
        std::uint64_t placeholders=0; // Date, footer, slide-number and header placeholders.
        std::uint64_t scripts=0;      // script, style, template, noscript.
        std::uint64_t navigation=0;   // nav elements and navigation documents in the spine.
        std::uint64_t nonLinear=0;    // EPUB spine items with linear="no".
        std::uint64_t unsupportedItems=0; // EPUB spine items that are not XHTML (SVG, DTBook, ...).
    } excluded;
};

struct TextDocumentExtractResult {
    TextDocumentError error=TextDocumentError::none;
    Document document;
    TextDocumentReport report;
    explicit operator bool() const noexcept {return error==TextDocumentError::none;}
};
struct TextDocumentResult {
    TextDocumentError error=TextDocumentError::none;
    std::string text;
    TextDocumentReport report;
    explicit operator bool() const noexcept {return error==TextDocumentError::none;}
};

[[nodiscard]] TextDocumentExtractResult ExtractTextDocument(TextDocumentFormat,std::span<const std::byte>,
    const TextDocumentOptions& ={}) noexcept;
// Extract, then serialize with SerializeText / SerializeMarkdown from api.hpp.
[[nodiscard]] TextDocumentResult ConvertTextDocument(TextDocumentFormat,OutputFormat,std::span<const std::byte>,
    const TextDocumentOptions& ={}) noexcept;
[[nodiscard]] std::string_view ErrorCode(TextDocumentError) noexcept;
[[nodiscard]] std::string_view ErrorMessage(TextDocumentError) noexcept;
[[nodiscard]] std::string_view FormatName(TextDocumentFormat) noexcept; // "pptx", "odt", "odp", "epub".
}
