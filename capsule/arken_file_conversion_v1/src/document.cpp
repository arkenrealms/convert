#include "arken_file_conversion_v1/documents.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
using TE = document::TextDocumentError;
DocumentResult Failure(Error error, document::Error reason = document::Error::none, TE text = TE::none) {
    DocumentResult result; result.error = error; result.documentError = reason; result.textDocumentError = text; return result;
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
Error Map(TE error) noexcept {
    switch (error) {
    case TE::source_limit: case TE::text_limit: case TE::zip_size_limit: case TE::zip_entry_limit:
    case TE::item_limit: case TE::nesting_limit: case TE::table_limit:
        return Error::input_limit;
    case TE::output_limit: return Error::encode_failed;
    case TE::allocation_failed: return Error::allocation_failed;
    case TE::invalid_options: return Error::invalid_options;
    case TE::internal: return Error::internal_failure;
    default: return Error::decode_failed;
    }
}
bool Presentation(DocumentInput input) noexcept { return input == DocumentInput::pptx || input == DocumentInput::odp; }
// Counts what the model holds; the result reports it next to the text.
void Count(const document::Document& model, DocumentResult& result) {
    for (const auto& block : model.blocks) {
        switch (block.kind) {
        case document::BlockKind::paragraph: ++result.paragraphs; break;
        case document::BlockKind::heading: ++result.headings; break;
        case document::BlockKind::list_item: ++result.listItems; break;
        case document::BlockKind::table: ++result.tables; result.tableRows += std::uint32_t(block.rows.size()); break;
        case document::BlockKind::block_quote: ++result.blockQuotes; break;
        case document::BlockKind::thematic_break: ++result.thematicBreaks; break;
        case document::BlockKind::code_block: ++result.codeBlocks; break;
        }
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
document::TextDocumentOptions StandaloneTextDocumentOptions(bool includeNotes) noexcept {
    const auto base = StandaloneDocumentOptions();
    document::TextDocumentOptions options;
    options.extract = base.extract;
    options.serialize = base.serialize;
    options.includeNotes = includeNotes;
    options.maximumItems = maximumDocumentItems;
    options.maximumNesting = maximumDocumentNesting;
    options.maximumTableCells = maximumDocumentTableCells;
    return options;
}
DocumentResult ConvertStandaloneDocument(const DocumentOptions& options, std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > maximumDocumentSourceBytes) return Failure(Error::input_limit);
    const auto input = static_cast<std::uint32_t>(options.input);
    if (input < 1 || input > 6) return Failure(Error::input_format_unavailable);
    if (options.output != document::OutputFormat::text && options.output != document::OutputFormat::markdown)
        return Failure(Error::output_format_unavailable);
    if (options.includeNotes && !Presentation(options.input)) return Failure(Error::invalid_options);
    try {
        // Extraction and serialization run separately so the result can report what the
        // model held; the composition is the same as document::Convert/ConvertTextDocument.
        DocumentResult result;
        document::TextResult text;
        if (options.input == DocumentInput::docx || options.input == DocumentInput::rtf) {
            const auto settings = StandaloneDocumentOptions();
            const auto extracted = options.input == DocumentInput::docx
                ? document::ExtractDocx(bytes, settings.extract) : document::ExtractRtf(bytes, settings.extract);
            if (!extracted) return Failure(Map(extracted.error), extracted.error);
            text = options.output == document::OutputFormat::text
                ? document::SerializeText(extracted.document, settings.serialize)
                : document::SerializeMarkdown(extracted.document, settings.serialize);
            if (!text) return Failure(Map(text.error), text.error);
            Count(extracted.document, result);
        } else {
            // DocumentInput 3-6 are TextDocumentFormat pptx=1, odt=2, odp=3, epub=4.
            const auto format = static_cast<document::TextDocumentFormat>(input - 2);
            const auto settings = StandaloneTextDocumentOptions(options.includeNotes);
            auto extracted = document::ExtractTextDocument(format, bytes, settings);
            if (!extracted) return Failure(Map(extracted.error), document::Error::none, extracted.error);
            text = options.output == document::OutputFormat::text
                ? document::SerializeText(extracted.document, settings.serialize)
                : document::SerializeMarkdown(extracted.document, settings.serialize);
            if (!text) {
                // The serializers speak document::Error; the only failures are output and allocation.
                const auto reason = text.error == document::Error::output_limit ? TE::output_limit : TE::allocation_failed;
                return Failure(Map(reason), document::Error::none, reason);
            }
            Count(extracted.document, result);
            result.report = extracted.report;
        }
        const auto* begin = reinterpret_cast<const std::byte*>(text.text.data());
        result.bytes.assign(begin, begin + text.text.size());
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
DocumentResult ConvertStandaloneDocument(document::SourceFormat source, document::OutputFormat output,
                                         std::span<const std::byte> bytes) noexcept {
    if (source != document::SourceFormat::docx && source != document::SourceFormat::rtf) {
        if (bytes.empty() || bytes.size() > maximumDocumentSourceBytes) return Failure(Error::input_limit);
        return Failure(Error::input_format_unavailable);
    }
    return ConvertStandaloneDocument(DocumentOptions{static_cast<DocumentInput>(source), output, false}, bytes);
}
std::string_view DocumentErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Document converted";
    case Error::input_limit: return "Input must contain between 1 byte and 32 MiB and stay within the extraction limits";
    case Error::decode_failed: return "Document is invalid, encrypted, DRM-protected, macro-enabled or outside the supported profile";
    case Error::encode_failed: return "Extracted text exceeds the 32 MiB output limit";
    case Error::invalid_options: return "Document options do not apply to this input";
    default: return ErrorMessage(error);
    }
}
}
