#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bounded, memory-only text extraction from DOCX (WordprocessingML) and RTF
// into one structural document model, serialized as UTF-8 TXT or Markdown.
// No file, network, process or Office access: the caller supplies bytes and
// receives bytes. Every malformed, encrypted, macro-enabled or over-budget
// input is a typed error, never an empty successful document.
namespace arken::document::v1 {
inline constexpr std::string_view revision="arken-document-extraction-v1-r1";

enum class SourceFormat : std::uint32_t {docx=1, rtf=2};
enum class OutputFormat : std::uint32_t {text=1, markdown=2};

enum class Error : std::uint32_t {
    none=0,
    invalid_options,
    source_limit,        // Source exceeds maximumSourceBytes.
    text_limit,          // Extracted text or block count exceeds its budget.
    output_limit,        // Serialized result exceeds maximumOutputBytes.
    allocation_failed,
    // ZIP container (DOCX).
    zip_not_archive,     // No valid end-of-central-directory record.
    zip_malformed,       // Inconsistent directory, local header, descriptor or overlap.
    zip_unsupported,     // ZIP64, spanning, patched data or a method other than stored/deflate.
    zip_encrypted_entry, // Traditional, strong or AES entry encryption.
    zip_unsafe_name,     // Absolute, traversal, backslash, control-character or invalid UTF-8 names.
    zip_duplicate_name,  // Case-insensitive duplicate member (OPC part names are case-insensitive).
    zip_entry_limit,
    zip_size_limit,      // Per-entry compressed/expanded or total expanded budget, or expansion beyond the declared size.
    zip_crc_mismatch,
    zip_corrupt_data,    // Invalid deflate stream, trailing data or short output.
    // XML.
    xml_malformed,
    xml_doctype,         // DOCTYPE/DTD declarations are rejected outright.
    xml_encoding,        // Only UTF-8 is admitted.
    xml_entity,          // Only the five predefined entities and numeric references.
    xml_limit,
    // Open Packaging / WordprocessingML.
    docx_encrypted,      // OLE compound file or EncryptedPackage/EncryptionInfo members.
    docx_macro_enabled,  // .docm/.dotm main part or a VBA project part.
    docx_not_wordprocessing, // No officeDocument relationship to a WordprocessingML main part.
    docx_missing_part,   // A required part named by the package is absent.
    docx_invalid_package,// Invalid [Content_Types].xml or relationships.
    // RTF.
    rtf_not_rtf,
    rtf_malformed,
    rtf_unsupported_code_page, // A byte above 0x7F had to be decoded through an unsupported code page.
    rtf_limit,
};

struct ZipLimits {
    std::uint32_t maximumEntries=4096;
    std::uint32_t maximumNameBytes=1024;
    std::uint64_t maximumEntryCompressedBytes=8ull*1024*1024;
    std::uint64_t maximumEntryExpandedBytes=32ull*1024*1024;   // At most 2^31-1024.
    std::uint64_t maximumTotalExpandedBytes=64ull*1024*1024;   // Sum over every member actually read.
};
struct XmlLimits {
    std::uint32_t maximumDepth=256;
    std::uint32_t maximumAttributes=64;      // Per element, including namespace declarations.
    std::uint32_t maximumNameBytes=256;
    std::uint64_t maximumTokenBytes=4ull*1024*1024;  // One text node, CDATA section or attribute value.
    std::uint64_t maximumNodes=2'000'000;            // Elements plus text nodes per part.
    std::uint64_t maximumTextBytes=32ull*1024*1024;  // Decoded character data per part.
};
struct RtfLimits {
    std::uint32_t maximumDepth=256;
    std::uint32_t maximumControlWordBytes=32;  // Letters of one control word.
    std::uint64_t maximumTokens=16'000'000;
};
struct ExtractOptions {
    std::uint64_t maximumSourceBytes=8ull*1024*1024;
    std::uint64_t maximumTextBytes=16ull*1024*1024; // UTF-8 bytes held by the extracted model.
    std::uint64_t maximumBlocks=1'000'000;
    ZipLimits zip;
    XmlLimits xml;
    RtfLimits rtf;
};
struct SerializeOptions {
    std::uint64_t maximumOutputBytes=8ull*1024*1024;
};

// Common structural model. Text is UTF-8 with LF line breaks and literal tabs.
struct Run {
    std::string text;
    std::string link;  // Raw target as authored; serializers decide whether it may become a link.
    bool bold=false, italic=false;
};
using Cell=std::vector<Run>;
using Row=std::vector<Cell>;
enum class BlockKind : std::uint8_t {paragraph, heading, list_item, table};
struct Block {
    BlockKind kind=BlockKind::paragraph;
    std::uint8_t level=0;   // heading: 1..6; list_item: 0..8 nesting depth.
    bool ordered=false;     // list_item only.
    std::vector<Run> runs;  // paragraph, heading, list_item.
    std::vector<Row> rows;  // table.
};
struct Document {
    std::vector<Block> blocks;
};
struct ExtractResult {
    Error error=Error::none;
    Document document;
    explicit operator bool() const noexcept {return error==Error::none;}
};
struct TextResult {
    Error error=Error::none;
    std::string text;
    explicit operator bool() const noexcept {return error==Error::none;}
};
struct ConvertOptions {
    ExtractOptions extract;
    SerializeOptions serialize;
};

[[nodiscard]] ExtractResult ExtractDocx(std::span<const std::byte>,const ExtractOptions& ={}) noexcept;
[[nodiscard]] ExtractResult ExtractRtf(std::span<const std::byte>,const ExtractOptions& ={}) noexcept;
// TXT: UTF-8, LF, one blank line between blocks, tabs preserved, table rows as
// tab-separated lines. Always ends with LF unless the document is empty.
[[nodiscard]] TextResult SerializeText(const Document&,const SerializeOptions& ={}) noexcept;
// Markdown (CommonMark/GFM subset): escaped text, ATX headings, nested lists,
// pipe tables only when rectangular, links only for http/https/mailto. Never raw HTML.
[[nodiscard]] TextResult SerializeMarkdown(const Document&,const SerializeOptions& ={}) noexcept;
[[nodiscard]] TextResult Convert(SourceFormat,OutputFormat,std::span<const std::byte>,
    const ConvertOptions& ={}) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
[[nodiscard]] std::string_view ErrorCode(Error) noexcept;
[[nodiscard]] std::string_view MediaType(OutputFormat) noexcept;
}
