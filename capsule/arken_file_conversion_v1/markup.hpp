#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_markup_conversion_v1/api.hpp"

namespace arken::file_conversion::v1 {
namespace markup = arken::markup::v1;
// Standalone native MCP markup profile: UTF-8 HTML to Markdown, Markdown (a CommonMark
// subset with GFM tables and strikethrough) to sanitized HTML, and SRT <-> WebVTT. Resident,
// deterministic, no network, no script or raw-HTML pass-through. Separate from every other
// surface's admission.
//
// 8 MiB of source and 8 MiB of output for every pair (the capsule defaults are 8 MiB source
// and 16/32/64 MiB output). The capsule measured hostile all-syntax Markdown at about 0.5 s
// per MiB and deeply nested HTML lists at about 3 s for 8 MiB of text, so 8 MiB keeps one call
// to a few seconds. Markdown tables pad ragged rows and HTML escaping grows text, so a source
// near the limit can exceed the output limit; that result is rejected, never cut.
inline constexpr std::uint32_t maximumMarkupSourceBytes = 8u * 1024u * 1024u;
inline constexpr std::uint32_t maximumMarkupOutputBytes = 8u * 1024u * 1024u;

struct MarkupOptions {
    markup::Format input = markup::Format::html;
    markup::Format output = markup::Format::markdown;
    bool dropNavigation = false; // HTML input only: drop nav, aside, header and footer subtrees.
    bool fullDocument = false;   // HTML output only: <!DOCTYPE html> document instead of a fragment.
};
// What the source carried that the output does not, by category. Each count comes from the
// capsule's report for the pair actually run; the others stay zero.
struct MarkupResult {
    Error error = Error::none;
    markup::Error markupError = markup::Error::none;
    std::uint64_t line = 0; // 1-based input line of a subtitle error.
    markup::HtmlToMarkdownReport html;
    markup::MarkdownToHtmlReport markdown;
    markup::SubtitleReport subtitles;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
// Pairs other than html->markdown, markdown->html, srt->vtt and vtt->srt are
// output_format_unavailable; an option for another pair is invalid_options.
[[nodiscard]] MarkupResult ConvertStandaloneMarkup(const MarkupOptions&, std::span<const std::byte>) noexcept;
[[nodiscard]] bool MarkupPairAdmitted(markup::Format input, markup::Format output) noexcept;
[[nodiscard]] std::string_view MarkupErrorMessage(Error) noexcept;
}
