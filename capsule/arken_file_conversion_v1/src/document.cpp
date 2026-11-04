#include "arken_file_conversion_v1/documents.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
DocumentResult Failure(Error error, document::Error reason = document::Error::none) {
    DocumentResult result; result.error = error; result.documentError = reason; return result;
}
Error Map(document::Error error) noexcept {
    switch (error) {
    case document::Error::source_limit: case document::Error::text_limit:
    case document::Error::zip_size_limit: case document::Error::zip_entry_limit:
        return Error::input_limit;
    case document::Error::output_limit: return Error::encode_failed;
    case document::Error::allocation_failed: return Error::allocation_failed;
    case document::Error::invalid_options: return Error::invalid_options;
    default: return Error::decode_failed;
    }
}
}
document::ConvertOptions StandaloneDocumentOptions() noexcept {
    document::ConvertOptions options;
    options.extract.maximumSourceBytes = maximumDocumentSourceBytes;
    options.extract.zip.maximumEntryCompressedBytes = maximumDocumentEntryExpandedBytes;
    options.extract.zip.maximumEntryExpandedBytes = maximumDocumentEntryExpandedBytes;
    options.serialize.maximumOutputBytes = maximumDocumentOutputBytes;
    return options;
}
DocumentResult ConvertStandaloneDocument(document::SourceFormat source, document::OutputFormat output,
                                         std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > maximumDocumentSourceBytes) return Failure(Error::input_limit);
    if (source != document::SourceFormat::docx && source != document::SourceFormat::rtf)
        return Failure(Error::input_format_unavailable);
    if (output != document::OutputFormat::text && output != document::OutputFormat::markdown)
        return Failure(Error::output_format_unavailable);
    try {
        // Extraction and serialization run separately so the result can report what the
        // model held; the composition is the same as document::Convert.
        const auto options = StandaloneDocumentOptions();
        const auto extracted = source == document::SourceFormat::docx
            ? document::ExtractDocx(bytes, options.extract) : document::ExtractRtf(bytes, options.extract);
        if (!extracted) return Failure(Map(extracted.error), extracted.error);
        const auto text = output == document::OutputFormat::text
            ? document::SerializeText(extracted.document, options.serialize)
            : document::SerializeMarkdown(extracted.document, options.serialize);
        if (!text) return Failure(Map(text.error), text.error);
        DocumentResult result;
        for (const auto& block : extracted.document.blocks) {
            switch (block.kind) {
            case document::BlockKind::paragraph: ++result.paragraphs; break;
            case document::BlockKind::heading: ++result.headings; break;
            case document::BlockKind::list_item: ++result.listItems; break;
            case document::BlockKind::table: ++result.tables; result.tableRows += std::uint32_t(block.rows.size()); break;
            }
        }
        const auto* begin = reinterpret_cast<const std::byte*>(text.text.data());
        result.bytes.assign(begin, begin + text.text.size());
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view DocumentErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Document converted";
    case Error::input_limit: return "Input must contain between 1 byte and 32 MiB and stay within the extraction limits";
    case Error::decode_failed: return "Document is invalid, encrypted, macro-enabled or outside the supported profile";
    case Error::encode_failed: return "Extracted text exceeds the 32 MiB output limit";
    default: return ErrorMessage(error);
    }
}
}
