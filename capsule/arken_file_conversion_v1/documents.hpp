#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_document_extraction_v1/api.hpp"

namespace arken::file_conversion::v1 {
namespace document = arken::document::v1;
// Standalone native MCP document profile: DOCX or RTF to UTF-8 TXT or Markdown, main
// story only, resident (not streaming). Separate from every other surface's admission.
//
// 32 MiB of source: a DOCX is mostly embedded images and fonts, which are never inflated,
// so the source bound mostly limits the file read; RTF is read whole and tokenized.
// 16 MiB per expanded ZIP member (the capsule default is 32 MiB): the extractor holds the
// inflated main part, its XML node tree and the model together, measured at 8-10x the
// expanded part, so 16 MiB keeps one call near 160 MiB instead of ~320 MiB.
// 32 MiB of output: the model holds at most 16 MiB of text, and Markdown escaping, list
// indentation and table pipes can add to it; a larger result is rejected, never cut.
inline constexpr std::uint32_t maximumDocumentSourceBytes = 32u * 1024u * 1024u;
inline constexpr std::uint32_t maximumDocumentEntryExpandedBytes = 16u * 1024u * 1024u;
inline constexpr std::uint32_t maximumDocumentOutputBytes = 32u * 1024u * 1024u;

struct DocumentResult {
    Error error = Error::none;
    document::Error documentError = document::Error::none;
    // Blocks extracted from the main story (tables count once; tableRows counts their rows).
    std::uint32_t paragraphs = 0, headings = 0, listItems = 0, tables = 0, tableRows = 0;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] document::ConvertOptions StandaloneDocumentOptions() noexcept;
[[nodiscard]] DocumentResult ConvertStandaloneDocument(document::SourceFormat, document::OutputFormat,
    std::span<const std::byte>) noexcept;
[[nodiscard]] std::string_view DocumentErrorMessage(Error) noexcept;
}
