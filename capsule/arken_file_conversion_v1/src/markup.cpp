#include "arken_file_conversion_v1/markup.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
using markup::Format;
MarkupResult Failure(Error error, markup::Error reason = markup::Error::none, std::uint64_t line = 0) {
    MarkupResult result; result.error = error; result.markupError = reason; result.line = line; return result;
}
Error Map(markup::Error error) noexcept {
    switch (error) {
    case markup::Error::source_limit: return Error::input_limit;
    case markup::Error::output_limit: return Error::encode_failed;
    case markup::Error::allocation_failed: return Error::allocation_failed;
    case markup::Error::invalid_options: return Error::invalid_options;
    default: return Error::decode_failed;
    }
}
std::vector<std::byte> Bytes(const std::string& text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}
}
bool MarkupPairAdmitted(Format input, Format output) noexcept {
    return (input == Format::html && output == Format::markdown) || (input == Format::markdown && output == Format::html)
        || (input == Format::srt && output == Format::vtt) || (input == Format::vtt && output == Format::srt);
}
MarkupResult ConvertStandaloneMarkup(const MarkupOptions& options, std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > maximumMarkupSourceBytes) return Failure(Error::input_limit);
    if (options.input != Format::html && options.input != Format::markdown && options.input != Format::srt
        && options.input != Format::vtt)
        return Failure(Error::input_format_unavailable);
    if (!MarkupPairAdmitted(options.input, options.output)) return Failure(Error::output_format_unavailable);
    if ((options.dropNavigation && options.input != Format::html) || (options.fullDocument && options.output != Format::html))
        return Failure(Error::invalid_options);
    try {
        MarkupResult result;
        if (options.input == Format::html) {
            markup::HtmlToMarkdownOptions settings;
            settings.maximumSourceBytes = maximumMarkupSourceBytes;
            settings.maximumOutputBytes = maximumMarkupOutputBytes;
            settings.dropNavigation = options.dropNavigation;
            auto converted = markup::HtmlToMarkdown(bytes, settings);
            if (!converted) return Failure(Map(converted.error), converted.error);
            result.html = converted.report; result.bytes = Bytes(converted.markdown);
        } else if (options.input == Format::markdown) {
            markup::MarkdownToHtmlOptions settings;
            settings.maximumSourceBytes = maximumMarkupSourceBytes;
            settings.maximumOutputBytes = maximumMarkupOutputBytes;
            settings.fullDocument = options.fullDocument;
            auto converted = markup::MarkdownToHtml(bytes, settings);
            if (!converted) return Failure(Map(converted.error), converted.error);
            result.markdown = converted.report; result.bytes = Bytes(converted.html);
        } else {
            markup::SubtitleOptions settings;
            settings.maximumSourceBytes = maximumMarkupSourceBytes;
            settings.maximumOutputBytes = maximumMarkupOutputBytes;
            auto converted = options.input == Format::srt ? markup::SrtToVtt(bytes, settings) : markup::VttToSrt(bytes, settings);
            if (!converted) return Failure(Map(converted.error), converted.error, converted.line);
            result.subtitles = converted.report; result.bytes = Bytes(converted.text);
        }
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view MarkupErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Markup converted";
    case Error::input_limit: return "Input must contain between 1 byte and 8 MiB";
    case Error::decode_failed: return "Markup source is invalid, not UTF-8 or outside the supported profile";
    case Error::encode_failed: return "Converted markup exceeds the 8 MiB output limit";
    case Error::invalid_options: return "Markup options do not apply to this input and output";
    default: return ErrorMessage(error);
    }
}
}
