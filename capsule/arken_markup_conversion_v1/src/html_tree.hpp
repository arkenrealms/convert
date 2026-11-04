#pragma once
#include "detail.hpp"

// Swap boundary. The HTML tokenizer and tree builder live behind this one
// interface: bytes in, element tree out. The Markdown mapping
// (html_to_markdown.cpp) depends only on these types, so an exported HTML
// owner (for example an arken_data_* capsule) can replace html_tree.cpp by
// filling the same Tree without touching the mapping, the Markdown -> HTML
// renderer or the subtitle converters.
//
// Contract a replacement must keep:
// - nodes[0] is the synthetic root named "#document"; every other node is
//   reachable from it exactly once, children in document order;
// - element names and attribute names are ASCII-lowercase; attribute values
//   and text have character references decoded; text is UTF-8 with LF line
//   ends and no U+0000; adjacent text siblings are merged;
// - html/head/body tags are not represented (their content is reparented to
//   the root); comments, doctype and processing instructions are not
//   represented and are only counted;
// - raw-text elements (script, style, xmp, iframe, noembed, noframes,
//   noscript) and escapable raw-text elements (textarea, title) hold their
//   content as a single text child, never parsed as markup;
// - violations of limits throw Failure (depth_limit, node_limit), encoding
//   problems throw invalid_utf8 / unsupported_charset.
namespace arken::markup::v1::detail::html {
struct Attribute {
    std::string name;
    std::string value;
};
struct Node {
    bool text=false;
    std::string name;   // Element name, or "#document" for the root. Empty for text.
    std::string data;   // Text content of a text node.
    std::vector<Attribute> attributes;
    std::vector<std::uint32_t> children;
    [[nodiscard]] const std::string* Attr(std::string_view n) const {
        for(const auto& a:attributes)if(a.name==n)return &a.value;
        return nullptr;
    }
};
struct Tree {
    std::vector<Node> nodes;
};
struct TreeLimits {
    std::uint32_t maximumDepth=256;
    std::uint64_t maximumNodes=1'000'000;
    std::uint32_t maximumAttributes=64;
};
struct TreeStats {
    std::uint64_t commentsSkipped=0;
    std::uint64_t unknownEntities=0;
};
[[nodiscard]] Tree ParseHtml(Bytes bytes,const TreeLimits& limits,TreeStats& stats);
}
