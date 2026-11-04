#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Bounded, memory-only text-markup conversion: HTML to Markdown, Markdown to
// HTML, and SRT <-> WebVTT subtitles. The caller supplies bytes and receives
// bytes; nothing opens files, uses the network, starts processes or reads the
// clock. Every malformed or over-budget input is a typed error, never a
// truncated success. Output is deterministic: the same bytes and options give
// the same bytes and report on every run and platform.
namespace arken::markup::v1 {
inline constexpr std::string_view revision="arken-markup-conversion-v1-r1";

enum class Format : std::uint32_t {html=1, markdown=2, srt=3, vtt=4};

enum class Error : std::uint32_t {
    none=0,
    invalid_options,     // A zero limit or another impossible option.
    source_limit,        // Input exceeds maximumSourceBytes.
    output_limit,        // Result would exceed maximumOutputBytes.
    allocation_failed,
    invalid_utf8,        // Input is not strict UTF-8 (after an optional UTF-8 BOM).
    unsupported_charset, // HTML declares a charset other than UTF-8, or starts with a UTF-16 BOM.
    depth_limit,         // HTML element nesting or Markdown container nesting exceeds its limit.
    node_limit,          // HTML element/text node count exceeds maximumNodes.
    link_limit,          // Markdown links plus images exceed maximumLinks.
    subtitle_missing_header,      // WebVTT input does not start with "WEBVTT".
    subtitle_malformed_block,     // A cue block is not index/identifier + timing + payload.
    subtitle_malformed_timestamp, // A timing line does not parse; `line` names it.
    subtitle_time_order,          // A cue ends before it starts.
    subtitle_non_monotonic,       // A cue starts before the previous cue.
    subtitle_cue_limit,           // More cues than maximumCues.
    subtitle_payload_limit,       // One cue's payload exceeds maximumPayloadBytes.
};

// ---- HTML -> Markdown ----
struct HtmlToMarkdownOptions {
    std::uint64_t maximumSourceBytes=8ull*1024*1024;
    std::uint64_t maximumOutputBytes=16ull*1024*1024;
    std::uint32_t maximumDepth=256;           // Open elements at once.
    std::uint64_t maximumNodes=1'000'000;     // Elements plus text nodes.
    std::uint32_t maximumAttributes=64;       // Kept per element; extras are ignored.
    bool dropNavigation=false;                // Drop nav, aside, header and footer subtrees.
};
struct HtmlToMarkdownReport {
    std::uint64_t scriptStyleDropped=0;   // script, style, noscript, template contents.
    std::uint64_t embeddedDropped=0;      // iframe, object, embed, applet, svg, math, canvas, audio, video, title.
    std::uint64_t hiddenDropped=0;        // hidden, aria-hidden="true", style display:none/visibility:hidden.
    std::uint64_t navigationDropped=0;    // nav/aside/header/footer under dropNavigation.
    std::uint64_t commentsSkipped=0;      // Comments, doctype, processing instructions, CDATA.
    std::uint64_t linksAsText=0;          // a[href] whose scheme is not http/https/mailto.
    std::uint64_t imagesAsText=0;         // img rendered as its alt text (non-http(s) src).
    std::uint64_t imagesDropped=0;        // img without alt and without an http(s) src.
    std::uint64_t tablesAsParagraphs=0;   // Non-rectangular or block-bearing tables.
    std::uint64_t unknownEntities=0;      // &name; references outside the supported set, kept literally.
};
struct HtmlToMarkdownResult {
    Error error=Error::none;
    std::string markdown;
    HtmlToMarkdownReport report;
    explicit operator bool() const noexcept {return error==Error::none;}
};

// ---- Markdown -> HTML ----
struct MarkdownToHtmlOptions {
    std::uint64_t maximumSourceBytes=8ull*1024*1024;
    std::uint64_t maximumOutputBytes=32ull*1024*1024;
    std::uint32_t maximumNesting=64;          // Block quotes and list items open at once.
    std::uint64_t maximumLinks=100'000;       // Links plus images (inline and autolinks).
    bool fullDocument=false;                  // Wrap in <!DOCTYPE html>...<body>.
    std::string title;                        // Full document <title>; empty: first heading, else "Document".
};
struct MarkdownToHtmlReport {
    std::uint64_t links=0, images=0;
    std::uint64_t unsafeLinks=0;     // Link destinations refused by the URL policy; rendered as their text.
    std::uint64_t unsafeImages=0;    // Image sources refused by the URL policy; rendered as their alt text.
    std::uint64_t htmlEscaped=0;     // Raw-HTML-looking tags (blocks or inline) emitted as escaped text.
    std::uint64_t unknownEntities=0; // &name; references outside the supported set, kept literally.
};
struct MarkdownToHtmlResult {
    Error error=Error::none;
    std::string html;
    MarkdownToHtmlReport report;
    explicit operator bool() const noexcept {return error==Error::none;}
};

// ---- Subtitles ----
struct SubtitleOptions {
    std::uint64_t maximumSourceBytes=16ull*1024*1024;
    std::uint64_t maximumOutputBytes=64ull*1024*1024;
    std::uint64_t maximumCues=200'000;
    std::uint64_t maximumPayloadBytes=16*1024;  // UTF-8 bytes of one cue's text lines.
    bool keepFontColor=false;   // SRT <font color> <-> WebVTT <c.colour> for the eight WebVTT default colours.
    bool crlf=false;            // SRT output line ends (WebVTT output always uses LF).
};
struct SubtitleReport {
    std::uint64_t cues=0;
    std::uint64_t missingIndices=0;      // SRT cues without a numeric index line (tolerated).
    std::uint64_t identifiersDropped=0;  // WebVTT cue identifiers that differ from the SRT index written.
    std::uint64_t settingsDropped=0;     // Cues whose WebVTT settings or SRT coordinates were dropped.
    std::uint64_t headerLinesDropped=0;  // WebVTT header text and metadata lines.
    std::uint64_t notesDropped=0, styleBlocksDropped=0, regionBlocksDropped=0;
    std::uint64_t tagsStripped=0;        // Tags with no equivalent in the target (v, c, lang, ruby, rt, timestamps, font, unknown).
    std::uint64_t overridesStripped=0;   // SRT {\...} override codes.
};
struct SubtitleResult {
    Error error=Error::none;
    std::string text;
    std::uint64_t line=0;   // 1-based input line of a subtitle_* error; 0 otherwise.
    SubtitleReport report;
    explicit operator bool() const noexcept {return error==Error::none;}
};

[[nodiscard]] HtmlToMarkdownResult HtmlToMarkdown(std::span<const std::byte>,const HtmlToMarkdownOptions& ={}) noexcept;
[[nodiscard]] MarkdownToHtmlResult MarkdownToHtml(std::span<const std::byte>,const MarkdownToHtmlOptions& ={}) noexcept;
[[nodiscard]] SubtitleResult SrtToVtt(std::span<const std::byte>,const SubtitleOptions& ={}) noexcept;
[[nodiscard]] SubtitleResult VttToSrt(std::span<const std::byte>,const SubtitleOptions& ={}) noexcept;

[[nodiscard]] std::string_view ErrorCode(Error) noexcept;     // Stable snake_case code.
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;  // Actionable sentence.
[[nodiscard]] std::string_view MediaType(Format) noexcept;
}
