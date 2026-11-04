#include "text_documents_detail.hpp"
#include <limits>

namespace arken::document::v1 {
namespace {
using detail::textdocs::TextFailure;
using E=TextDocumentError;

E FromCore(Error error) noexcept {
    switch(error) {
        case Error::none: return E::none;
        case Error::invalid_options: return E::invalid_options;
        case Error::source_limit: return E::source_limit;
        case Error::text_limit: return E::text_limit;
        case Error::output_limit: return E::output_limit;
        case Error::allocation_failed: return E::allocation_failed;
        case Error::zip_not_archive: return E::zip_not_archive;
        case Error::zip_malformed: return E::zip_malformed;
        case Error::zip_unsupported: return E::zip_unsupported;
        case Error::zip_encrypted_entry: return E::zip_encrypted_entry;
        case Error::zip_unsafe_name: return E::zip_unsafe_name;
        case Error::zip_duplicate_name: return E::zip_duplicate_name;
        case Error::zip_entry_limit: return E::zip_entry_limit;
        case Error::zip_size_limit: return E::zip_size_limit;
        case Error::zip_crc_mismatch: return E::zip_crc_mismatch;
        case Error::zip_corrupt_data: return E::zip_corrupt_data;
        case Error::xml_malformed: return E::xml_malformed;
        case Error::xml_doctype: return E::xml_doctype;
        case Error::xml_encoding: return E::xml_encoding;
        case Error::xml_entity: return E::xml_entity;
        case Error::xml_limit: return E::xml_limit;
        default: return E::internal; // DOCX/RTF codes are never raised by these extractors.
    }
}
template<class F> E Guard(F&& body) noexcept {
    try {body();return E::none;}
    catch(const TextFailure& failure) {return failure.error;}
    catch(const detail::Failure& failure) {return FromCore(failure.error);}
    catch(const std::bad_alloc&) {return E::allocation_failed;}
    catch(...) {return E::internal;}
}
bool Valid(const TextDocumentOptions& options) noexcept {
    const auto& o=options.extract;
    return o.maximumSourceBytes&&o.maximumTextBytes&&o.maximumBlocks
        &&o.zip.maximumEntries&&o.zip.maximumNameBytes&&o.zip.maximumTotalExpandedBytes
        &&o.zip.maximumEntryExpandedBytes<=std::uint64_t(std::numeric_limits<int>::max())-1024
        &&o.xml.maximumDepth&&o.xml.maximumAttributes&&o.xml.maximumNameBytes&&o.xml.maximumTokenBytes
        &&o.xml.maximumNodes&&o.xml.maximumTextBytes
        &&options.serialize.maximumOutputBytes
        &&options.maximumItems&&options.maximumNesting&&options.maximumTableCells;
}
bool KnownFormat(TextDocumentFormat format) noexcept {
    return format==TextDocumentFormat::pptx||format==TextDocumentFormat::odt
        ||format==TextDocumentFormat::odp||format==TextDocumentFormat::epub;
}
}

TextDocumentExtractResult ExtractTextDocument(TextDocumentFormat format,std::span<const std::byte> bytes,
    const TextDocumentOptions& options) noexcept {
    TextDocumentExtractResult result;
    if(!Valid(options)||!KnownFormat(format)) {result.error=E::invalid_options;return result;}
    if(bytes.size()>options.extract.maximumSourceBytes) {result.error=E::source_limit;return result;}
    result.error=Guard([&] {
        detail::textdocs::Context context{options,result.report};
        switch(format) {
            case TextDocumentFormat::pptx: detail::textdocs::ExtractPptx(bytes,context,result.document);break;
            case TextDocumentFormat::odt: detail::textdocs::ExtractOdf(bytes,false,context,result.document);break;
            case TextDocumentFormat::odp: detail::textdocs::ExtractOdf(bytes,true,context,result.document);break;
            case TextDocumentFormat::epub: detail::textdocs::ExtractEpub(bytes,context,result.document);break;
        }
    });
    if(result.error!=E::none) {result.document.blocks.clear();result.report=TextDocumentReport{};}
    else result.report.blocks=result.document.blocks.size();
    return result;
}

TextDocumentResult ConvertTextDocument(TextDocumentFormat format,OutputFormat output,std::span<const std::byte> bytes,
    const TextDocumentOptions& options) noexcept {
    TextDocumentResult result;
    if(output!=OutputFormat::text&&output!=OutputFormat::markdown) {result.error=E::invalid_options;return result;}
    auto extracted=ExtractTextDocument(format,bytes,options);
    if(!extracted) {result.error=extracted.error;return result;}
    auto text=output==OutputFormat::text?SerializeText(extracted.document,options.serialize)
        :SerializeMarkdown(extracted.document,options.serialize);
    if(!text) {result.error=FromCore(text.error);return result;}
    result.text=std::move(text.text);
    result.report=extracted.report;
    return result;
}

std::string_view FormatName(TextDocumentFormat format) noexcept {
    switch(format) {
        case TextDocumentFormat::pptx: return "pptx";
        case TextDocumentFormat::odt: return "odt";
        case TextDocumentFormat::odp: return "odp";
        case TextDocumentFormat::epub: return "epub";
    }
    return "unknown";
}

std::string_view ErrorCode(TextDocumentError error) noexcept {
    switch(error) {
        case E::none: return "none";
        case E::invalid_options: return "invalid_options";
        case E::source_limit: return "source_limit";
        case E::text_limit: return "text_limit";
        case E::output_limit: return "output_limit";
        case E::allocation_failed: return "allocation_failed";
        case E::internal: return "internal";
        case E::item_limit: return "item_limit";
        case E::nesting_limit: return "nesting_limit";
        case E::table_limit: return "table_limit";
        case E::zip_not_archive: return "zip_not_archive";
        case E::zip_malformed: return "zip_malformed";
        case E::zip_unsupported: return "zip_unsupported";
        case E::zip_encrypted_entry: return "zip_encrypted_entry";
        case E::zip_unsafe_name: return "zip_unsafe_name";
        case E::zip_duplicate_name: return "zip_duplicate_name";
        case E::zip_entry_limit: return "zip_entry_limit";
        case E::zip_size_limit: return "zip_size_limit";
        case E::zip_crc_mismatch: return "zip_crc_mismatch";
        case E::zip_corrupt_data: return "zip_corrupt_data";
        case E::xml_malformed: return "xml_malformed";
        case E::xml_doctype: return "xml_doctype";
        case E::xml_encoding: return "xml_encoding";
        case E::xml_entity: return "xml_entity";
        case E::xml_limit: return "xml_limit";
        case E::pptx_encrypted: return "pptx_encrypted";
        case E::pptx_macro_enabled: return "pptx_macro_enabled";
        case E::pptx_not_presentation: return "pptx_not_presentation";
        case E::pptx_invalid_package: return "pptx_invalid_package";
        case E::pptx_missing_part: return "pptx_missing_part";
        case E::odf_not_opendocument: return "odf_not_opendocument";
        case E::odf_wrong_kind: return "odf_wrong_kind";
        case E::odf_invalid_package: return "odf_invalid_package";
        case E::odf_missing_part: return "odf_missing_part";
        case E::odf_encrypted: return "odf_encrypted";
        case E::epub_not_epub: return "epub_not_epub";
        case E::epub_invalid_package: return "epub_invalid_package";
        case E::epub_missing_part: return "epub_missing_part";
        case E::epub_drm: return "epub_drm";
        case E::epub_content_not_xhtml: return "epub_content_not_xhtml";
    }
    return "unknown";
}

std::string_view ErrorMessage(TextDocumentError error) noexcept {
    switch(error) {
        case E::none: return "No error.";
        case E::invalid_options: return "The extraction options or formats are invalid.";
        case E::source_limit: return "The document exceeds the source size limit.";
        case E::text_limit: return "The document contains more text or blocks than the extraction limit allows.";
        case E::output_limit: return "The converted text exceeds the output size limit.";
        case E::allocation_failed: return "Memory could not be allocated for the document.";
        case E::internal: return "The document could not be converted because of an unexpected internal error.";
        case E::item_limit: return "The document has more slides, pages or chapters than the extraction limit allows.";
        case E::nesting_limit: return "The document nests lists, groups or sections more deeply than the extraction limit allows.";
        case E::table_limit: return "The document's tables contain more cells than the extraction limit allows.";
        case E::zip_not_archive: return "The file is not a ZIP-based document package.";
        case E::zip_malformed: return "The document package has an inconsistent ZIP structure.";
        case E::zip_unsupported: return "The document package uses an unsupported ZIP feature (ZIP64, spanning or an unsupported compression method).";
        case E::zip_encrypted_entry: return "The document package contains encrypted ZIP entries.";
        case E::zip_unsafe_name: return "The document package contains an unsafe member name.";
        case E::zip_duplicate_name: return "The document package contains duplicate member names.";
        case E::zip_entry_limit: return "The document package contains too many members.";
        case E::zip_size_limit: return "A document package member expands beyond the size limit or its declared size.";
        case E::zip_crc_mismatch: return "A document package member failed its CRC-32 check.";
        case E::zip_corrupt_data: return "A document package member has corrupt compressed data.";
        case E::xml_malformed: return "A document part is not well-formed XML.";
        case E::xml_doctype: return "A document part contains a DOCTYPE declaration that is not accepted.";
        case E::xml_encoding: return "A document part is not UTF-8 encoded.";
        case E::xml_entity: return "A document part references an undefined XML entity.";
        case E::xml_limit: return "A document part exceeds the XML nesting, size or node limits.";
        case E::pptx_encrypted: return "The presentation is password-protected or encrypted; remove the protection and try again.";
        case E::pptx_macro_enabled: return "Macro-enabled PowerPoint files are not accepted; save the presentation as .pptx without macros.";
        case E::pptx_not_presentation: return "The package is not a PowerPoint (PresentationML) presentation.";
        case E::pptx_invalid_package: return "The PPTX package has invalid content types, relationships or presentation structure.";
        case E::pptx_missing_part: return "The PPTX package is missing its presentation or a slide part.";
        case E::odf_not_opendocument: return "The file is not an OpenDocument package (missing or unknown mimetype).";
        case E::odf_wrong_kind: return "The OpenDocument file is of a different kind than requested (for example a spreadsheet instead of a text document).";
        case E::odf_invalid_package: return "The OpenDocument package has an invalid manifest or content structure.";
        case E::odf_missing_part: return "The OpenDocument package is missing its manifest or content.xml.";
        case E::odf_encrypted: return "The OpenDocument file is password-protected; remove the password and try again.";
        case E::epub_not_epub: return "The file is not an EPUB package (missing or wrong mimetype).";
        case E::epub_invalid_package: return "The EPUB package has an invalid container, package document, manifest or spine.";
        case E::epub_missing_part: return "The EPUB package is missing its package document or a spine item.";
        case E::epub_drm: return "The EPUB is protected by DRM (encrypted content); only unprotected books can be converted.";
        case E::epub_content_not_xhtml: return "An EPUB chapter is not well-formed XHTML; HTML that is not XML is not accepted.";
    }
    return "Unknown document extraction error.";
}
}
