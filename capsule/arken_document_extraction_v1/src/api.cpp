#include "detail.hpp"
#include <limits>

namespace arken::document::v1 {
namespace detail {
std::string RenderText(const Document&,const SerializeOptions&);
std::string RenderMarkdown(const Document&,const SerializeOptions&);
}
namespace {
bool Valid(const ExtractOptions& o) noexcept {
    return o.maximumSourceBytes&&o.maximumTextBytes&&o.maximumBlocks
        &&o.zip.maximumEntries&&o.zip.maximumNameBytes&&o.zip.maximumTotalExpandedBytes
        &&o.zip.maximumEntryExpandedBytes<=std::uint64_t(std::numeric_limits<int>::max())-1024
        &&o.xml.maximumDepth&&o.xml.maximumAttributes&&o.xml.maximumNameBytes&&o.xml.maximumTokenBytes
        &&o.xml.maximumNodes&&o.xml.maximumTextBytes
        &&o.rtf.maximumDepth&&o.rtf.maximumControlWordBytes&&o.rtf.maximumTokens;
}
ExtractResult Extract(Error (*extract)(detail::Bytes,const ExtractOptions&,Document&) noexcept,
    std::span<const std::byte> bytes,const ExtractOptions& options) noexcept {
    ExtractResult result;
    if(!Valid(options)) {result.error=Error::invalid_options;return result;}
    if(bytes.size()>options.maximumSourceBytes) {result.error=Error::source_limit;return result;}
    result.error=extract(bytes,options,result.document);
    if(result.error!=Error::none)result.document.blocks.clear();
    return result;
}
template<class F> TextResult Serialize(F&& render,const Document& document,const SerializeOptions& options) noexcept {
    TextResult result;
    if(!options.maximumOutputBytes) {result.error=Error::invalid_options;return result;}
    result.error=detail::Guard([&] {result.text=render(document,options);});
    if(result.error!=Error::none)result.text.clear();
    return result;
}
}

ExtractResult ExtractDocx(std::span<const std::byte> bytes,const ExtractOptions& options) noexcept {
    return Extract(detail::ExtractDocxInto,bytes,options);
}
ExtractResult ExtractRtf(std::span<const std::byte> bytes,const ExtractOptions& options) noexcept {
    return Extract(detail::ExtractRtfInto,bytes,options);
}
TextResult SerializeText(const Document& document,const SerializeOptions& options) noexcept {
    return Serialize(detail::RenderText,document,options);
}
TextResult SerializeMarkdown(const Document& document,const SerializeOptions& options) noexcept {
    return Serialize(detail::RenderMarkdown,document,options);
}
TextResult Convert(SourceFormat source,OutputFormat output,std::span<const std::byte> bytes,const ConvertOptions& options) noexcept {
    TextResult result;
    if((source!=SourceFormat::docx&&source!=SourceFormat::rtf)||(output!=OutputFormat::text&&output!=OutputFormat::markdown)) {
        result.error=Error::invalid_options;return result;
    }
    auto extracted=source==SourceFormat::docx?ExtractDocx(bytes,options.extract):ExtractRtf(bytes,options.extract);
    if(!extracted) {result.error=extracted.error;return result;}
    return output==OutputFormat::text?SerializeText(extracted.document,options.serialize)
        :SerializeMarkdown(extracted.document,options.serialize);
}

std::string_view ErrorCode(Error error) noexcept {
    switch(error) {
        case Error::none: return "none";
        case Error::invalid_options: return "invalid_options";
        case Error::source_limit: return "source_limit";
        case Error::text_limit: return "text_limit";
        case Error::output_limit: return "output_limit";
        case Error::allocation_failed: return "allocation_failed";
        case Error::zip_not_archive: return "zip_not_archive";
        case Error::zip_malformed: return "zip_malformed";
        case Error::zip_unsupported: return "zip_unsupported";
        case Error::zip_encrypted_entry: return "zip_encrypted_entry";
        case Error::zip_unsafe_name: return "zip_unsafe_name";
        case Error::zip_duplicate_name: return "zip_duplicate_name";
        case Error::zip_entry_limit: return "zip_entry_limit";
        case Error::zip_size_limit: return "zip_size_limit";
        case Error::zip_crc_mismatch: return "zip_crc_mismatch";
        case Error::zip_corrupt_data: return "zip_corrupt_data";
        case Error::xml_malformed: return "xml_malformed";
        case Error::xml_doctype: return "xml_doctype";
        case Error::xml_encoding: return "xml_encoding";
        case Error::xml_entity: return "xml_entity";
        case Error::xml_limit: return "xml_limit";
        case Error::docx_encrypted: return "docx_encrypted";
        case Error::docx_macro_enabled: return "docx_macro_enabled";
        case Error::docx_not_wordprocessing: return "docx_not_wordprocessing";
        case Error::docx_missing_part: return "docx_missing_part";
        case Error::docx_invalid_package: return "docx_invalid_package";
        case Error::rtf_not_rtf: return "rtf_not_rtf";
        case Error::rtf_malformed: return "rtf_malformed";
        case Error::rtf_unsupported_code_page: return "rtf_unsupported_code_page";
        case Error::rtf_limit: return "rtf_limit";
    }
    return "unknown";
}

std::string_view ErrorMessage(Error error) noexcept {
    switch(error) {
        case Error::none: return "No error.";
        case Error::invalid_options: return "The extraction options or formats are invalid.";
        case Error::source_limit: return "The document exceeds the source size limit.";
        case Error::text_limit: return "The document contains more text or blocks than the extraction limit allows.";
        case Error::output_limit: return "The converted text exceeds the output size limit.";
        case Error::allocation_failed: return "Memory could not be allocated for the document.";
        case Error::zip_not_archive: return "The file is not a ZIP-based DOCX package.";
        case Error::zip_malformed: return "The DOCX package has an inconsistent ZIP structure.";
        case Error::zip_unsupported: return "The DOCX package uses an unsupported ZIP feature (ZIP64, spanning or an unsupported compression method).";
        case Error::zip_encrypted_entry: return "The DOCX package contains encrypted ZIP entries.";
        case Error::zip_unsafe_name: return "The DOCX package contains an unsafe member name.";
        case Error::zip_duplicate_name: return "The DOCX package contains duplicate member names.";
        case Error::zip_entry_limit: return "The DOCX package contains too many members.";
        case Error::zip_size_limit: return "A DOCX package member expands beyond the size limit or its declared size.";
        case Error::zip_crc_mismatch: return "A DOCX package member failed its CRC-32 check.";
        case Error::zip_corrupt_data: return "A DOCX package member has corrupt compressed data.";
        case Error::xml_malformed: return "A DOCX part is not well-formed XML.";
        case Error::xml_doctype: return "A DOCX part contains a DOCTYPE declaration, which is not accepted.";
        case Error::xml_encoding: return "A DOCX part is not UTF-8 encoded.";
        case Error::xml_entity: return "A DOCX part references an undefined XML entity.";
        case Error::xml_limit: return "A DOCX part exceeds the XML nesting, size or node limits.";
        case Error::docx_encrypted: return "The document is password-protected or encrypted; remove the protection and try again.";
        case Error::docx_macro_enabled: return "Macro-enabled Word documents are not accepted; save the document as .docx without macros.";
        case Error::docx_not_wordprocessing: return "The package is not a Word (WordprocessingML) document.";
        case Error::docx_missing_part: return "The DOCX package is missing its main document part.";
        case Error::docx_invalid_package: return "The DOCX package has invalid content types or relationships.";
        case Error::rtf_not_rtf: return "The file is not an RTF document.";
        case Error::rtf_malformed: return "The RTF document is malformed.";
        case Error::rtf_unsupported_code_page: return "The RTF document uses an unsupported legacy code page.";
        case Error::rtf_limit: return "The RTF document exceeds the nesting or token limits.";
    }
    return "Unknown document extraction error.";
}

std::string_view MediaType(OutputFormat format) noexcept {
    return format==OutputFormat::markdown?"text/markdown; charset=utf-8":"text/plain; charset=utf-8";
}
}
