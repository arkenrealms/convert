#include "detail.hpp"

namespace arken::markup::v1 {
namespace detail {
std::string HtmlToMarkdownText(Bytes,const HtmlToMarkdownOptions&,HtmlToMarkdownReport&);
std::string MarkdownToHtmlText(Bytes,const MarkdownToHtmlOptions&,MarkdownToHtmlReport&);
std::string SrtToVttText(Bytes,const SubtitleOptions&,SubtitleReport&);
std::string VttToSrtText(Bytes,const SubtitleOptions&,SubtitleReport&);
}
namespace {
// Recursion in the HTML mapping and the Markdown block renderer is bounded by
// these nesting limits, so they are capped to keep the C++ stack small.
constexpr std::uint32_t maximumHtmlDepth=4096;
constexpr std::uint32_t maximumMarkdownNesting=1024;

template<class Result,class Options,class Body> Result Run(std::span<const std::byte> bytes,const Options& options,bool valid,Body&& body) noexcept {
    Result result;
    if(!valid) {result.error=Error::invalid_options;return result;}
    if(bytes.size()>options.maximumSourceBytes) {result.error=Error::source_limit;return result;}
    result.error=detail::Guard([&] {body(result);});
    return result;
}
}

HtmlToMarkdownResult HtmlToMarkdown(std::span<const std::byte> bytes,const HtmlToMarkdownOptions& o) noexcept {
    const bool valid=o.maximumSourceBytes&&o.maximumOutputBytes&&o.maximumDepth&&o.maximumDepth<=maximumHtmlDepth
        &&o.maximumNodes&&o.maximumAttributes;
    auto result=Run<HtmlToMarkdownResult>(bytes,o,valid,[&](HtmlToMarkdownResult& r) {
        r.markdown=detail::HtmlToMarkdownText(bytes,o,r.report);
    });
    if(!result)result.markdown.clear();
    return result;
}

MarkdownToHtmlResult MarkdownToHtml(std::span<const std::byte> bytes,const MarkdownToHtmlOptions& o) noexcept {
    const bool valid=o.maximumSourceBytes&&o.maximumOutputBytes&&o.maximumNesting&&o.maximumNesting<=maximumMarkdownNesting
        &&o.maximumLinks;
    auto result=Run<MarkdownToHtmlResult>(bytes,o,valid,[&](MarkdownToHtmlResult& r) {
        r.html=detail::MarkdownToHtmlText(bytes,o,r.report);
    });
    if(!result)result.html.clear();
    return result;
}

namespace {
template<class F> SubtitleResult Subtitles(std::span<const std::byte> bytes,const SubtitleOptions& o,F convert) noexcept {
    SubtitleResult result;
    if(!o.maximumSourceBytes||!o.maximumOutputBytes||!o.maximumCues||!o.maximumPayloadBytes) {
        result.error=Error::invalid_options;return result;
    }
    if(bytes.size()>o.maximumSourceBytes) {result.error=Error::source_limit;return result;}
    result.error=detail::Guard([&] {result.text=convert(bytes,o,result.report);},&result.line);
    if(!result)result.text.clear();
    return result;
}
}
SubtitleResult SrtToVtt(std::span<const std::byte> bytes,const SubtitleOptions& o) noexcept {
    return Subtitles(bytes,o,detail::SrtToVttText);
}
SubtitleResult VttToSrt(std::span<const std::byte> bytes,const SubtitleOptions& o) noexcept {
    return Subtitles(bytes,o,detail::VttToSrtText);
}

std::string_view ErrorCode(Error error) noexcept {
    switch(error) {
        case Error::none: return "none";
        case Error::invalid_options: return "invalid_options";
        case Error::source_limit: return "source_limit";
        case Error::output_limit: return "output_limit";
        case Error::allocation_failed: return "allocation_failed";
        case Error::invalid_utf8: return "invalid_utf8";
        case Error::unsupported_charset: return "unsupported_charset";
        case Error::depth_limit: return "depth_limit";
        case Error::node_limit: return "node_limit";
        case Error::link_limit: return "link_limit";
        case Error::subtitle_missing_header: return "subtitle_missing_header";
        case Error::subtitle_malformed_block: return "subtitle_malformed_block";
        case Error::subtitle_malformed_timestamp: return "subtitle_malformed_timestamp";
        case Error::subtitle_time_order: return "subtitle_time_order";
        case Error::subtitle_non_monotonic: return "subtitle_non_monotonic";
        case Error::subtitle_cue_limit: return "subtitle_cue_limit";
        case Error::subtitle_payload_limit: return "subtitle_payload_limit";
    }
    return "unknown";
}

std::string_view ErrorMessage(Error error) noexcept {
    switch(error) {
        case Error::none: return "No error.";
        case Error::invalid_options: return "The conversion options are invalid: every limit must be nonzero (HTML depth at most 4096, Markdown nesting at most 1024).";
        case Error::source_limit: return "The input is larger than the configured source limit.";
        case Error::output_limit: return "The converted output would exceed the configured output limit; nothing was truncated.";
        case Error::allocation_failed: return "Memory for the conversion could not be allocated.";
        case Error::invalid_utf8: return "The input is not valid UTF-8; convert it to UTF-8 first.";
        case Error::unsupported_charset: return "The HTML declares a character encoding other than UTF-8 (or starts with a UTF-16 byte-order mark); only UTF-8 is supported.";
        case Error::depth_limit: return "Elements, block quotes or list items are nested deeper than the configured limit.";
        case Error::node_limit: return "The HTML has more elements and text nodes than the configured limit.";
        case Error::link_limit: return "The Markdown has more links and images than the configured limit.";
        case Error::subtitle_missing_header: return "The WebVTT file does not start with the WEBVTT signature line.";
        case Error::subtitle_malformed_block: return "A subtitle block is not an index or identifier followed by a timing line; see the reported line.";
        case Error::subtitle_malformed_timestamp: return "A subtitle timing line is malformed (expected HH:MM:SS,mmm --> HH:MM:SS,mmm for SRT or [HH:]MM:SS.mmm --> [HH:]MM:SS.mmm for WebVTT); see the reported line.";
        case Error::subtitle_time_order: return "A subtitle cue ends before it starts; see the reported line.";
        case Error::subtitle_non_monotonic: return "A subtitle cue starts before the previous cue; cues must be in start-time order. See the reported line.";
        case Error::subtitle_cue_limit: return "The subtitle file has more cues than the configured limit.";
        case Error::subtitle_payload_limit: return "A subtitle cue's text is longer than the configured limit; see the reported line.";
    }
    return "Unknown error.";
}

std::string_view MediaType(Format format) noexcept {
    switch(format) {
        case Format::html: return "text/html; charset=utf-8";
        case Format::markdown: return "text/markdown; charset=utf-8";
        case Format::srt: return "application/x-subrip";
        case Format::vtt: return "text/vtt";
    }
    return "application/octet-stream";
}
}
