#include "arken_markup_conversion_v1/api.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Authored fixtures with exact expected outputs. Cases marked "CommonMark
// spec example N" / "GFM spec example N" reproduce the example's input and
// expected HTML from the CommonMark 0.31.2 / GFM 0.29 specifications.
namespace mk=arken::markup::v1;
using mk::Error;

namespace {
int failures=0;

std::string Visible(std::string_view s) {
    std::string out;
    for(const char c:s) {
        if(c=='\n')out+="\\n\n";
        else if(c=='\t')out+="\\t";
        else if(c=='\r')out+="\\r";
        else out.push_back(c);
    }
    return out;
}
void Expect(bool ok,std::string_view what) {
    if(ok)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n";
}
void ExpectEqual(std::string_view got,std::string_view want,std::string_view what) {
    if(got==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n--- got ---\n"<<Visible(got)<<"\n--- want ---\n"<<Visible(want)<<"\n";
}
std::span<const std::byte> Span(std::string_view s) {return std::as_bytes(std::span<const char>(s.data(),s.size()));}

mk::HtmlToMarkdownResult H(std::string_view html,const mk::HtmlToMarkdownOptions& o={}) {return mk::HtmlToMarkdown(Span(html),o);}
mk::MarkdownToHtmlResult M(std::string_view md,const mk::MarkdownToHtmlOptions& o={}) {return mk::MarkdownToHtml(Span(md),o);}
mk::SubtitleResult S2V(std::string_view s,const mk::SubtitleOptions& o={}) {return mk::SrtToVtt(Span(s),o);}
mk::SubtitleResult V2S(std::string_view s,const mk::SubtitleOptions& o={}) {return mk::VttToSrt(Span(s),o);}

void HM(std::string_view html,std::string_view want,std::string_view what,const mk::HtmlToMarkdownOptions& o={}) {
    const auto r=H(html,o);
    if(!r) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<mk::ErrorCode(r.error)<<"\n";return;}
    ExpectEqual(r.markdown,want,what);
}
void MH(std::string_view md,std::string_view want,std::string_view what,const mk::MarkdownToHtmlOptions& o={}) {
    const auto r=M(md,o);
    if(!r) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<mk::ErrorCode(r.error)<<"\n";return;}
    ExpectEqual(r.html,want,what);
}
void Sub(const mk::SubtitleResult& r,std::string_view want,std::string_view what) {
    if(!r) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<mk::ErrorCode(r.error)<<" line "<<r.line<<"\n";return;}
    ExpectEqual(r.text,want,what);
}
void ExpectError(Error got,Error want,std::string_view what) {
    if(got==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<": got "<<mk::ErrorCode(got)<<", want "<<mk::ErrorCode(want)<<"\n";
}
void ExpectSubError(const mk::SubtitleResult& r,Error want,std::uint64_t line,std::string_view what) {
    ExpectError(r.error,want,what);
    if(r.line!=line) {++failures;std::cerr<<"FAIL: "<<what<<": line "<<r.line<<", want "<<line<<"\n";}
    Expect(r.text.empty(),"failed subtitle conversion carries no text");
}
bool ValidUtf8(std::string_view s) {
    for(std::size_t i=0;i<s.size();) {
        const auto b=std::uint8_t(s[i]);
        std::size_t n=b<0x80?1:b>=0xc2&&b<=0xdf?2:b>=0xe0&&b<=0xef?3:b>=0xf0&&b<=0xf4?4:0;
        if(!n||s.size()-i<n)return false;
        for(std::size_t k=1;k<n;++k)if((std::uint8_t(s[i+k])&0xc0)!=0x80)return false;
        i+=n;
    }
    return true;
}

// ================= HTML tokenizer and tree builder =================
void HtmlTokenizerTests() {
    // Character references: named (supported set, with and without ';'),
    // numeric decimal/hex, windows-1252 remap, invalid -> U+FFFD, unknown kept literally.
    HM("<p>&amp; &lt;&gt; &quot;&apos; &copy &copy; &eacute;&hellip;&mdash;&euro; &#65;&#x42;&#X43; &#128; &#0; &#xD800; &#1114112;</p>",
        "& \\<> \"' \xC2\xA9 \xC2\xA9 \xC3\xA9\xE2\x80\xA6\xE2\x80\x94\xE2\x82\xAC ABC \xE2\x82\xAC \xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD\n",
        "character references");
    {
        const auto r=H("<p>&bogus; &notanentity &zz; &amp</p>");
        Expect(r&&r.markdown=="\\&bogus; \xC2\xAC" "anentity \\&zz; &\n","unknown names stay literal; legacy prefix &not decodes");
        Expect(r.report.unknownEntities==2,"unknown terminated references are counted");
    }
    // Attribute quoting variants and the attribute legacy-reference rule.
    HM("<a href=https://a.example/x?a=1&amp;b=2>u</a> <a href='https://b.example/'>s</a> <a href=\"https://c.example/?q=1&copy=2\">d</a> <a\nhref\n=\n\"https://e.example\">n</a>",
        "[u](https://a.example/x?a=1&b=2) [s](https://b.example/) [d](https://c.example/?q=1&copy=2) [n](https://e.example)\n",
        "attribute quoting variants");
    // Tag soup: unclosed p, uppercase names, stray end tags, '<' that is not a tag, EOF inside a tag.
    HM("<P>one<p>two</span></div><P>a < b and a<3 </>x<p>three <b", "one\n\ntwo\n\na \\< b and a\\<3 x\n\nthree\n", "tag soup");
    // Comments, doctype, processing instructions, CDATA are skipped and counted.
    {
        const auto r=H("<!DOCTYPE html><!-- c1 --><p>a<!-->b<!--->c<!-- x -- y -->d<?php echo 1 ?>e<![CDATA[f]]>g</p><!-- unclosed");
        Expect(r&&r.markdown=="abcdeg\n","comments skipped");
        Expect(r.report.commentsSkipped==8,"comments counted");
    }
    // Raw text: script/style content never parsed as markup and dropped;
    // textarea/title are RCDATA (references decoded, tags literal).
    {
        const auto r=H("<script>if(a<b){document.write('<p>x</p>')}</script><style>p>a{}</style><p>t</p><textarea>\n<b>&amp;</b></textarea><title>T<b></title>");
        Expect(r&&r.markdown=="t\n\n\\<b>&\\</b>\n","raw text and RCDATA");
        Expect(r.report.scriptStyleDropped==2&&r.report.embeddedDropped==1,"script/style/title counted");
    }
    HM("<script>a</SCRIPT x><p>after</p><script>never closed <p>hidden", "after\n", "raw text end tag is case-insensitive; unterminated script swallows the rest");
    // Implicit end tags: p, li, dd/dt, tr/td/th, option, headings.
    HM("<p>a<div>b</div><p>c<ul><li>x<li>y</ul>", "a\n\nb\n\nc\n\n- x\n- y\n", "p closed by block start; li closes li");
    HM("<dl><dt>T<dd>D<dt>U<dd>E</dl>", "**T**\n\n  D\n\n**U**\n\n  E\n", "dt/dd implicit close");
    HM("<table><tr><th>A<th>B<tr><td>1<td>2</table>", "| A | B |\n| --- | --- |\n| 1 | 2 |\n", "tr/td/th implicit close and implicit tbody");
    HM("<table><td>x<td>y</table>", "| x | y |\n| --- | --- |\n", "td without tr gets an implicit row");
    HM("<select><option>A<option>B<optgroup><option>C</select>", "A\n\nB\n\nC\n", "option implicit close");
    HM("<h1>A<h2>B</h2>", "# A\n\n## B\n", "heading start closes an open heading");
    HM("<a href=\"https://1.example\">one<a href=\"https://2.example\">two</a>", "[one](https://1.example)[two](https://2.example)\n", "a closes an open a");
    // Void elements and self-closing syntax on non-void elements.
    HM("<p>a<br/>b<br>c<img alt=x/><span/>d</span></p>", "a\\\nb\\\nc*x/*d\n", "void elements; self-closing span ignored");
    // Encoding: UTF-8 BOM honoured, other declared charsets rejected, invalid UTF-8 rejected.
    HM("\xEF\xBB\xBF<meta charset=\"UTF-8\"><p>caf\xC3\xA9</p>", "caf\xC3\xA9\n", "UTF-8 BOM and charset=UTF-8");
    HM("<meta http-equiv=\"content-type\" content=\"text/html; charset=utf8\"><p>x", "x\n", "http-equiv utf8 label");
    ExpectError(H("<meta charset=\"windows-1252\"><p>x").error,Error::unsupported_charset,"meta charset windows-1252");
    ExpectError(H("<meta http-equiv=Content-Type content='text/html; charset=ISO-8859-1'>").error,Error::unsupported_charset,"http-equiv ISO-8859-1");
    ExpectError(H("<?xml version=\"1.0\" encoding=\"shift_jis\"?><p>x").error,Error::unsupported_charset,"XML declaration encoding");
    ExpectError(H("\xFF\xFE<\0p\0>\0").error,Error::unsupported_charset,"UTF-16 BOM");
    ExpectError(H("<p>caf\xE9</p>").error,Error::invalid_utf8,"Latin-1 byte");
    ExpectError(H("<p>\xC0\xAF</p>").error,Error::invalid_utf8,"overlong UTF-8");
    // NUL and CR normalization.
    HM(std::string_view("<p>a\0b\r\nc\rd</p>",15), "a\xEF\xBF\xBD" "b c d\n", "NUL to U+FFFD, CR/CRLF to LF then collapsed");
}

// ================= HTML -> Markdown mapping =================
void HtmlMarkdownTests() {
    HM("<h1>One</h1><h2>Two</h2><h3>Three</h3><h4>Four</h4><h5>Five</h5><h6>Six</h6>",
        "# One\n\n## Two\n\n### Three\n\n#### Four\n\n##### Five\n\n###### Six\n", "h1-h6");
    HM("<h2>C# and #1 <br>line</h2><h3>   </h3>", "## C\\# and \\#1 line\n", "heading escapes # and flattens br; empty heading dropped");
    HM("<body><main><article><section><div><p>a</p>b</div></section></article></main></body>", "a\n\nb\n", "containers");
    HM("<p>x</p><hr><p>y</p>", "x\n\n---\n\ny\n", "hr -> thematic break");
    HM("<p>a  b\n\t c <span> d </span> e</p>", "a b c d e\n", "whitespace collapse");
    HM("<p>line one<br>   line two<br></p>", "line one\\\nline two\n", "br hard break, leading space after br and trailing br removed");
    HM("<ul><li>a<ul><li>b<ul><li>c</li></ul></li></ul></li><li>d</li></ul>", "- a\n  - b\n    - c\n- d\n", "nested bullet list (tight)");
    HM("<ol start=\"7\"><li>seven<li>eight<ol><li>inner</ol></ol>", "7. seven\n8. eight\n   1. inner\n", "ordered start and nested ordered");
    HM("<ol start=x><li>a</ol><ol start=0><li>b</ol>", "1. a\n\n0) b\n", "invalid start ignored; adjacent ordered lists alternate delimiter");
    HM("<ul><li>a</ul><ul><li>b</ul><ul><li>c</ul>", "- a\n\n* b\n\n- c\n", "adjacent bullet lists alternate marker");
    HM("<ul><li><p>one</p><p>two</p></li><li>three</li></ul>", "- one\n\n  two\n\n- three\n", "loose list");
    HM("<ul><li>a<ol start=3><li>b</ol></ul>", "- a\n\n  3. b\n", "ordered sublist not starting at 1 forces a loose item");
    HM("<ul><li></li><li>x</li></ul>", "-\n- x\n", "empty list item");
    HM("<blockquote><p>q1</p><p>q2</p><blockquote>deep</blockquote></blockquote>", "> q1\n>\n> q2\n>\n> > deep\n", "blockquote with nesting");
    HM("<pre><code class=\"language-cpp\">int a = 1;\n\n  return a &lt; 2;\n</code></pre>", "```cpp\nint a = 1;\n\n  return a < 2;\n```\n", "pre/code fenced with language");
    HM("<pre class=\"lang-py x\">\nprint(1)</pre>", "```py\nprint(1)\n```\n", "pre leading newline dropped; lang- prefix");
    HM("<pre>has ``` fence <b>bold</b><br>x</pre>", "````\nhas ``` fence bold\nx\n````\n", "fence longer than content backticks; markup flattened");
    HM("<pre></pre>", "```\n```\n", "empty pre");
    HM("<p>Use <code>a*b</code> and <code>``x``</code> and <kbd> c </kbd></p>", "Use `a*b` and ``` ``x`` ``` and `c`\n", "inline code spans");
    HM("<p><em>e</em> <i>i</i> <strong>s</strong> <b>b</b> <b><i>bi</i></b> <del>d</del> wo<b>rd</b>s</p>",
        "*e i* **s b *bi*** ~~d~~ wo**rd**s\n", "emphasis mapping (adjacent same-style runs merge across a space)");
    HM("<p><b> spaced </b>x<i>a </i>b</p>", "**spaced** x*a* b\n", "whitespace kept outside delimiters");
    HM("<p><a href=\"https://example.com/a b\">link <b>bold</b></a> <a href=\"mailto:x@y.z\">mail</a> <a href=\"HTTP://UP.EXAMPLE\">up</a></p>",
        "[link **bold**](https://example.com/a%20b) [mail](mailto:x@y.z) [up](HTTP://UP.EXAMPLE)\n", "links");
    {
        const auto r=H("<p><a href=\"/relative\">rel</a> <a href=\"ftp://x\">ftp</a> <a name=\"anchor\">named</a> <a href=\"#top\">frag</a></p>");
        Expect(r&&r.markdown=="rel ftp named frag\n","non-http links become text");
        Expect(r.report.linksAsText==3,"links as text counted");
    }
    HM("<p>a<a href=\"https://e.example\"> </a>b</p>", "a b\n", "a link with no visible content is dropped");
    {
        const auto r=H("<p><img src=\"https://i.example/a.png\" alt=\"A [b]\"> <img src=\"img/local.png\" alt=\"local\"> <img src=\"x.png\"> <a href=\"https://l.example\"><img src=\"https://i.example/l.png\" alt=\"logo\"></a></p>");
        Expect(r&&r.markdown=="![A \\[b\\]](https://i.example/a.png) *local* [![logo](https://i.example/l.png)](https://l.example)\n","images");
        Expect(r.report.imagesAsText==1&&r.report.imagesDropped==1,"image report");
    }
    HM("<table><thead><tr><th>Name</th><th>Value</th></tr></thead><tbody><tr><td>a|b</td><td><code>x|y</code></td></tr><tr><td></td><td>2<br>3</td></tr></tbody></table>",
        "| Name | Value |\n| --- | --- |\n| a\\|b | `x\\|y` |\n| | 2 3 |\n", "rectangular pipe table");
    {
        const auto r=H("<table><caption>Cap</caption><tr><td>a<td>b<tr><td colspan=2>wide</table>");
        Expect(r&&r.markdown=="Cap\n\na\n\nb\n\nwide\n","non-rectangular table becomes paragraphs");
        Expect(r.report.tablesAsParagraphs==1,"table fallback counted");
    }
    HM("<table><tr><td>a</td><td>b</td></tr><tr><td>c</td></tr></table>", "a\n\nb\n\nc\n", "ragged rows become paragraphs");
    HM("<table><tr><td><ul><li>x</ul></td></tr></table>", "- x\n", "block content in a cell becomes paragraphs");
    HM("<dl><dt>Term <i>one</i></dt><dd><p>Def</p><p>More</p></dd></dl>", "**Term *one***\n\n  Def\n\n  More\n", "definition list");
    // Hidden and optional navigation dropping.
    {
        const auto r=H("<p hidden>h</p><div aria-hidden=\"TRUE\">a</div><span style=\"display: none\">s</span><span style=\"VISIBILITY:hidden\">v</span><p>shown</p><nav>nav</nav><footer>foot</footer>");
        Expect(r&&r.markdown=="shown\n\nnav\n\nfoot\n","hidden content dropped, navigation kept by default");
        Expect(r.report.hiddenDropped==4,"hidden counted");
    }
    {
        mk::HtmlToMarkdownOptions o;o.dropNavigation=true;
        const auto r=H("<header>h</header><nav>n</nav><main>m</main><aside>a</aside><footer>f</footer>",o);
        Expect(r&&r.markdown=="m\n","dropNavigation");
        Expect(r.report.navigationDropped==4,"navigation counted");
    }
    // Markdown escaping of literal syntax.
    HM("<p>*a* _b_ `c` [d](e) &lt;f&gt; \\g | h ~i~ &amp;amp; # j</p><p>1. not a list</p><p>- no</p><p>+ no</p><p>&gt; no</p><p>=== no</p><p>2) no</p>",
        "\\*a\\* \\_b\\_ \\`c\\` \\[d\\](e) \\<f> \\\\g \\| h \\~i\\~ \\&amp; # j\n\n1\\. not a list\n\n\\- no\n\n\\+ no\n\n\\> no\n\n\\=== no\n\n2\\) no\n",
        "markdown escaping");
    HM("", "", "empty document");
    HM("<p>   </p><div></div>", "", "whitespace-only document");
}

// ================= HTML security =================
void HtmlSecurityTests() {
    {
        const auto r=H("<p onclick=\"steal()\">a <a href=\"javascript:alert(1)\">js</a> <a href=\" JaVaScRiPt:alert(1)\">js2</a> "
            "<a href=\"java&#x09;script:alert(1)\">js3</a> <a href=\"data:text/html,<script>x</script>\">data</a> "
            "<a href=\"vbscript:x\">vb</a> <img src=\"javascript:alert(1)\" alt=\"img\" onerror=\"x()\"> "
            "<img src=\"data:image/png;base64,AAAA\" alt=\"pixel\"></p>");
        Expect(r&&r.markdown=="a js js2 js3 data vb *img pixel*\n","dangerous schemes never become links or images");
        Expect(r.report.linksAsText==5&&r.report.imagesAsText==2,"dangerous links counted");
    }
    {
        const auto r=H("<script>alert('x')</script><style>body{background:url(javascript:x)}</style><noscript><img src=x></noscript>"
            "<template><p>tpl</p></template><iframe src=\"https://evil\">f</iframe><object data=\"x\">o</object><embed src=\"x\">"
            "<svg onload=\"x()\"><script>y()</script><foreignObject><p>svg</p></foreignObject></svg><math><mi>m</mi></math><p>safe</p>");
        Expect(r&&r.markdown=="safe\n","active and embedded content dropped");
        Expect(r.report.scriptStyleDropped==4&&r.report.embeddedDropped==5,"dropped content counted");
    }
    // Text that looks like HTML is escaped, never emitted raw.
    HM("<p>&lt;script&gt;alert(1)&lt;/script&gt; &lt;img src=x onerror=y&gt;</p>", "\\<script>alert(1)\\</script> \\<img src=x onerror=y>\n", "escaped markup text");
    // Round trip through our own renderer: no active content survives.
    const auto md=H("<p>&lt;b onmouseover=x&gt;hi&lt;/b&gt; <a href=\"https://ok.example\">ok</a></p>");
    const auto html=M(md.markdown);
    ExpectEqual(html.html,"<p>&lt;b onmouseover=x&gt;hi&lt;/b&gt; <a href=\"https://ok.example\">ok</a></p>\n","HTML -> Markdown -> HTML stays inert");
}

// ================= Markdown -> HTML: blocks =================
void MarkdownBlockTests() {
    MH("\tfoo\tbaz\t\tbim\n", "<pre><code>foo\tbaz\t\tbim\n</code></pre>\n", "CommonMark spec example 1 (tabs)");
    MH("  - foo\n\n\tbar\n", "<ul>\n<li>\n<p>foo</p>\n<p>bar</p>\n</li>\n</ul>\n", "CommonMark spec example 5 (tab continuation)");
    MH("***\n---\n___\n", "<hr />\n<hr />\n<hr />\n", "CommonMark spec example 43 (thematic breaks)");
    MH("+++\n", "<p>+++</p>\n", "CommonMark spec example 44");
    MH(" - - -\n", "<hr />\n", "CommonMark thematic break with spaces");
    MH("# foo\n## foo\n### foo\n#### foo\n##### foo\n###### foo\n",
        "<h1>foo</h1>\n<h2>foo</h2>\n<h3>foo</h3>\n<h4>foo</h4>\n<h5>foo</h5>\n<h6>foo</h6>\n", "CommonMark spec example 62 (ATX)");
    MH("####### foo\n", "<p>####### foo</p>\n", "CommonMark spec example 63");
    MH("#5 bolt\n\n#hashtag\n", "<p>#5 bolt</p>\n<p>#hashtag</p>\n", "CommonMark spec example 64");
    MH("# foo *bar* \\*baz\\*\n", "<h1>foo <em>bar</em> *baz*</h1>\n", "CommonMark spec example 66");
    MH("### foo ###     \n", "<h3>foo</h3>\n", "CommonMark closing sequence");
    MH("# foo#\n", "<h1>foo#</h1>\n", "CommonMark spec example 76");
    MH("## \n#\n### ###\n", "<h2></h2>\n<h1></h1>\n<h3></h3>\n", "CommonMark spec example 79 (empty headings)");
    MH("Foo *bar*\n=========\n\nFoo *bar*\n---------\n", "<h1>Foo <em>bar</em></h1>\n<h2>Foo <em>bar</em></h2>\n", "CommonMark spec example 80 (setext)");
    MH("Foo *bar\nbaz*\n====\n", "<h1>Foo <em>bar\nbaz</em></h1>\n", "CommonMark spec example 81 (multi-line setext)");
    MH("> foo\nbar\n===\n", "<blockquote>\n<p>foo\nbar\n===</p>\n</blockquote>\n", "CommonMark spec example 92 (setext not lazy)");
    MH("    a simple\n      indented code block\n", "<pre><code>a simple\n  indented code block\n</code></pre>\n", "CommonMark spec example 107");
    MH("    chunk1\n\n    chunk2\n  \n \n \n    chunk3\n", "<pre><code>chunk1\n\nchunk2\n\n\n\nchunk3\n</code></pre>\n", "CommonMark spec example 111");
    MH("Foo\n    bar\n", "<p>Foo\nbar</p>\n", "CommonMark spec example 113 (indented code cannot interrupt a paragraph)");
    MH("```\n<\n >\n```\n", "<pre><code>&lt;\n &gt;\n</code></pre>\n", "CommonMark spec example 119");
    MH("``\nfoo\n``\n", "<p><code>foo</code></p>\n", "CommonMark spec example 121");
    MH("```\naaa\n~~~\n```\n", "<pre><code>aaa\n~~~\n</code></pre>\n", "CommonMark spec example 122");
    MH("```\n", "<pre><code></code></pre>\n", "CommonMark spec example 126 (unclosed empty fence)");
    MH(" ```\n aaa\naaa\n```\n", "<pre><code>aaa\naaa\n</code></pre>\n", "CommonMark spec example 131 (fence indentation removed)");
    MH("```ruby\ndef foo(x)\n  return 3\nend\n```\n", "<pre><code class=\"language-ruby\">def foo(x)\n  return 3\nend\n</code></pre>\n", "CommonMark spec example 142 (info string)");
    MH("``` aa ```\nfoo\n", "<p><code>aa</code>\nfoo</p>\n", "CommonMark spec example 145 (backtick info may not contain backticks)");
    MH("aaa\n\nbbb\n", "<p>aaa</p>\n<p>bbb</p>\n", "CommonMark spec example 219");
    MH("  aaa\n bbb\n", "<p>aaa\nbbb</p>\n", "CommonMark spec example 221");
    MH("> # Foo\n> bar\n> baz\n", "<blockquote>\n<h1>Foo</h1>\n<p>bar\nbaz</p>\n</blockquote>\n", "CommonMark spec example 228");
    MH("> bar\nbaz\n> foo\n", "<blockquote>\n<p>bar\nbaz\nfoo</p>\n</blockquote>\n", "CommonMark lazy continuation");
    MH("> - foo\n- bar\n", "<blockquote>\n<ul>\n<li>foo</li>\n</ul>\n</blockquote>\n<ul>\n<li>bar</li>\n</ul>\n", "CommonMark spec example 238");
    MH("> > > foo\nbar\n", "<blockquote>\n<blockquote>\n<blockquote>\n<p>foo\nbar</p>\n</blockquote>\n</blockquote>\n</blockquote>\n", "CommonMark spec example 250");
    MH("1.  A paragraph\n    with two lines.\n\n        indented code\n\n    > A block quote.\n",
        "<ol>\n<li>\n<p>A paragraph\nwith two lines.</p>\n<pre><code>indented code\n</code></pre>\n<blockquote>\n<p>A block quote.</p>\n</blockquote>\n</li>\n</ol>\n",
        "CommonMark spec example 254 (list item)");
    MH("- one\n\n two\n", "<ul>\n<li>one</li>\n</ul>\n<p>two</p>\n", "CommonMark spec example 255");
    MH("- one\n\n  two\n", "<ul>\n<li>\n<p>one</p>\n<p>two</p>\n</li>\n</ul>\n", "CommonMark spec example 256");
    MH("-one\n\n2.two\n", "<p>-one</p>\n<p>2.two</p>\n", "CommonMark spec example 261");
    MH("123456789. ok\n", "<ol start=\"123456789\">\n<li>ok</li>\n</ol>\n", "CommonMark spec example 265");
    MH("1234567890. not ok\n", "<p>1234567890. not ok</p>\n", "CommonMark spec example 266");
    MH("- foo\n  - bar\n    - baz\n      - boo\n", "<ul>\n<li>foo\n<ul>\n<li>bar\n<ul>\n<li>baz\n<ul>\n<li>boo</li>\n</ul>\n</li>\n</ul>\n</li>\n</ul>\n</li>\n</ul>\n", "CommonMark spec example 294 (nesting)");
    MH("- foo\n- bar\n+ baz\n", "<ul>\n<li>foo</li>\n<li>bar</li>\n</ul>\n<ul>\n<li>baz</li>\n</ul>\n", "CommonMark spec example 301 (bullet change)");
    MH("1. foo\n2. bar\n3) baz\n", "<ol>\n<li>foo</li>\n<li>bar</li>\n</ol>\n<ol start=\"3\">\n<li>baz</li>\n</ol>\n", "CommonMark spec example 302");
    MH("The number of windows in my house is\n14.  The number of doors is 6.\n", "<p>The number of windows in my house is\n14.  The number of doors is 6.</p>\n", "CommonMark spec example 304");
    MH("- a\n- b\n\n- c\n", "<ul>\n<li>\n<p>a</p>\n</li>\n<li>\n<p>b</p>\n</li>\n<li>\n<p>c</p>\n</li>\n</ul>\n", "CommonMark spec example 309 (loose)");
    MH("- a\n- b\n\n  c\n- d\n", "<ul>\n<li>\n<p>a</p>\n</li>\n<li>\n<p>b</p>\n<p>c</p>\n</li>\n<li>\n<p>d</p>\n</li>\n</ul>\n", "CommonMark spec example 312");
    MH("- a\n  - b\n\n    c\n- d\n", "<ul>\n<li>a\n<ul>\n<li>\n<p>b</p>\n<p>c</p>\n</li>\n</ul>\n</li>\n<li>d</li>\n</ul>\n", "CommonMark spec example 318 (tight outer, loose inner)");
    MH("* foo\n  * bar\n\n  baz\n", "<ul>\n<li>\n<p>foo</p>\n<ul>\n<li>bar</li>\n</ul>\n<p>baz</p>\n</li>\n</ul>\n", "CommonMark spec example 320");
    // HTML blocks are not recognized: their text is ordinary Markdown with '<' escaped.
    MH("<div>\n*foo*\n</div>\n", "<p>&lt;div&gt;\n<em>foo</em>\n&lt;/div&gt;</p>\n", "HTML block escaped as text");
    MH("<script>\nalert(1)\n</script>\n", "<p>&lt;script&gt;\nalert(1)\n&lt;/script&gt;</p>\n", "script block escaped as text");
    // GFM tables.
    MH("| foo | bar |\n| --- | --- |\n| baz | bim |\n",
        "<table>\n<thead>\n<tr>\n<th>foo</th>\n<th>bar</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>baz</td>\n<td>bim</td>\n</tr>\n</tbody>\n</table>\n",
        "GFM spec example 198");
    MH("| abc | defghi |\n:-: | -----------:\nbar | baz\n",
        "<table>\n<thead>\n<tr>\n<th align=\"center\">abc</th>\n<th align=\"right\">defghi</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td align=\"center\">bar</td>\n<td align=\"right\">baz</td>\n</tr>\n</tbody>\n</table>\n",
        "GFM spec example 199 (alignment)");
    MH("| f\\|oo  |\n| ------ |\n| b `\\|` az |\n| b **\\|** im |\n",
        "<table>\n<thead>\n<tr>\n<th>f|oo</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>b <code>|</code> az</td>\n</tr>\n<tr>\n<td>b <strong>|</strong> im</td>\n</tr>\n</tbody>\n</table>\n",
        "GFM spec example 200 (escaped pipes)");
    MH("| abc | def |\n| --- | --- |\n| bar | baz |\n> bar\n",
        "<table>\n<thead>\n<tr>\n<th>abc</th>\n<th>def</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>bar</td>\n<td>baz</td>\n</tr>\n</tbody>\n</table>\n<blockquote>\n<p>bar</p>\n</blockquote>\n",
        "GFM spec example 201 (block start ends the table)");
    MH("| abc | def |\n| --- |\n| bar |\n", "<p>| abc | def |\n| --- |\n| bar |</p>\n", "GFM spec example 203 (header/delimiter mismatch)");
    MH("| abc | def |\n| --- | --- |\n| bar |\n| bar | baz | boo |\n",
        "<table>\n<thead>\n<tr>\n<th>abc</th>\n<th>def</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>bar</td>\n<td></td>\n</tr>\n<tr>\n<td>bar</td>\n<td>baz</td>\n</tr>\n</tbody>\n</table>\n",
        "GFM spec example 204 (ragged rows)");
    MH("| abc | def |\n| --- | --- |\n", "<table>\n<thead>\n<tr>\n<th>abc</th>\n<th>def</th>\n</tr>\n</thead>\n</table>\n", "GFM spec example 205 (no body)");
    MH("intro\n| a |\n| - |\n", "<p>intro</p>\n<table>\n<thead>\n<tr>\n<th>a</th>\n</tr>\n</thead>\n</table>\n", "table header split from a preceding paragraph");
    // Full document option.
    {
        mk::MarkdownToHtmlOptions o;o.fullDocument=true;
        MH("Intro\n\n# T <&>\n", "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>T &lt;&amp;&gt;</title>\n</head>\n<body>\n<p>Intro</p>\n<h1>T &lt;&amp;&gt;</h1>\n</body>\n</html>\n", "full document, title from first heading",o);
        o.title="Given";
        MH("x", "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>Given</title>\n</head>\n<body>\n<p>x</p>\n</body>\n</html>\n", "full document, explicit title",o);
    }
    MH("", "", "empty Markdown");
    MH("\xEF\xBB\xBF# BOM\r\ntext\r\n", "<h1>BOM</h1>\n<p>text</p>\n", "BOM and CRLF");
}

// ================= Markdown -> HTML: inlines =================
void MarkdownInlineTests() {
    MH("\\!\\\"\\#\\$\\%\\&\\'\\(\\)\\*\\+\\,\\-\\.\\/\\:\\;\\<\\=\\>\\?\\@\\[\\\\\\]\\^\\_\\`\\{\\|\\}\\~\n",
        "<p>!&quot;#$%&amp;'()*+,-./:;&lt;=&gt;?@[\\]^_`{|}~</p>\n", "CommonMark spec example 12 (escapable punctuation)");
    MH("\\\t\\A\\a\\ \\3\\\xCF\x86\\\xC2\xAB\n", "<p>\\\t\\A\\a\\ \\3\\\xCF\x86\\\xC2\xAB</p>\n", "CommonMark spec example 13 (non-escapable)");
    MH("\\*not emphasized*\n\\<br/> not a tag\n\\[not a link](/foo)\n\\`not code`\n",
        "<p>*not emphasized*\n&lt;br/&gt; not a tag\n[not a link](/foo)\n`not code`</p>\n", "CommonMark spec example 14");
    MH("foo\\\nbar\n", "<p>foo<br />\nbar</p>\n", "CommonMark spec example 15 (backslash hard break)");
    MH("`` \\[\\` ``\n", "<p><code>\\[\\`</code></p>\n", "CommonMark spec example 16 (no escapes in code)");
    MH("&nbsp; &amp; &copy; &AElig;\n", "<p>\xC2\xA0 &amp; \xC2\xA9 \xC3\x86</p>\n", "CommonMark entity references (supported subset)");
    MH("&#35; &#1234; &#992; &#0;\n", "<p># \xD3\x92 \xCF\xA0 \xEF\xBF\xBD</p>\n", "CommonMark spec example 26 (decimal)");
    MH("&#X22; &#XD06; &#xcab;\n", "<p>&quot; \xE0\xB4\x86 \xE0\xB2\xAB</p>\n", "CommonMark spec example 27 (hex)");
    {
        const auto r=M("&nbsp &x; &#; &#x;\n&#87654321;\n&#abcdef0;\n&ThisIsNotDefined; &hi?;\n");
        ExpectEqual(r.html,"<p>&amp;nbsp &amp;x; &amp;#; &amp;#x;\n&amp;#87654321;\n&amp;#abcdef0;\n&amp;ThisIsNotDefined; &amp;hi?;</p>\n","CommonMark spec example 28 (not references)");
        Expect(r.report.unknownEntities==1,"unknown entity names counted (only well-formed &name; with 2+ characters)");
    }
    MH("`foo`\n", "<p><code>foo</code></p>\n", "CommonMark spec example 328");
    MH("`` foo ` bar ``\n", "<p><code>foo ` bar</code></p>\n", "CommonMark spec example 329");
    MH("` `` `\n", "<p><code>``</code></p>\n", "CommonMark spec example 330");
    MH("`  ``  `\n", "<p><code> `` </code></p>\n", "CommonMark spec example 331");
    MH("` a`\n", "<p><code> a</code></p>\n", "CommonMark spec example 332");
    MH("`foo   bar \nbaz`\n", "<p><code>foo   bar  baz</code></p>\n", "CommonMark spec example 337 (line ending to space)");
    MH("```foo``\n", "<p>```foo``</p>\n", "CommonMark spec example 346 (unmatched run)");
    MH("`foo``bar``\n", "<p>`foo<code>bar</code></p>\n", "CommonMark spec example 348");
    MH("*foo bar*\n", "<p><em>foo bar</em></p>\n", "CommonMark spec example 350");
    MH("a * foo bar*\n", "<p>a * foo bar*</p>\n", "CommonMark spec example 351 (not left-flanking)");
    MH("a*\"foo\"*\n", "<p>a*&quot;foo&quot;*</p>\n", "CommonMark spec example 352 (punctuation rule)");
    MH("* a *\n", "<ul>\n<li>a *</li>\n</ul>\n", "list marker, then unmatched '*'");
    MH("foo*bar*\n", "<p>foo<em>bar</em></p>\n", "CommonMark spec example 355 (intraword *)");
    MH("5*6*78\n", "<p>5<em>6</em>78</p>\n", "CommonMark spec example 356");
    MH("_foo bar_\n", "<p><em>foo bar</em></p>\n", "CommonMark spec example 357");
    MH("foo_bar_\n", "<p>foo_bar_</p>\n", "CommonMark spec example 362 (no intraword _)");
    MH("5_6_78\n", "<p>5_6_78</p>\n", "CommonMark spec example 363");
    MH("aa_\"bb\"_cc\n", "<p>aa_&quot;bb&quot;_cc</p>\n", "CommonMark spec example 365");
    MH("foo-_(bar)_\n", "<p>foo-<em>(bar)</em></p>\n", "CommonMark spec example 366");
    MH("_foo*\n", "<p>_foo*</p>\n", "CommonMark spec example 367");
    MH("*foo bar *\n", "<p>*foo bar *</p>\n", "CommonMark spec example 368");
    MH("*(*foo)\n", "<p>*(*foo)</p>\n", "CommonMark spec example 370");
    MH("*(*foo*)*\n", "<p><em>(<em>foo</em>)</em></p>\n", "CommonMark spec example 371");
    MH("*foo*bar\n", "<p><em>foo</em>bar</p>\n", "CommonMark spec example 372");
    MH("_foo_bar\n", "<p>_foo_bar</p>\n", "CommonMark spec example 375");
    MH("**foo bar**\n", "<p><strong>foo bar</strong></p>\n", "CommonMark spec example 378");
    MH("** foo bar**\n", "<p>** foo bar**</p>\n", "CommonMark spec example 379");
    MH("foo**bar**\n", "<p>foo<strong>bar</strong></p>\n", "CommonMark spec example 382");
    MH("__foo, __bar__, baz__\n", "<p><strong>foo, <strong>bar</strong>, baz</strong></p>\n", "CommonMark spec example 384");
    MH("*foo**bar**baz*\n", "<p><em>foo<strong>bar</strong>baz</em></p>\n", "CommonMark spec example 413");
    MH("*foo**bar*\n", "<p><em>foo**bar</em></p>\n", "CommonMark spec example 414 (rule of three)");
    MH("***foo** bar*\n", "<p><em><strong>foo</strong> bar</em></p>\n", "CommonMark spec example 415");
    MH("foo***bar***baz\n", "<p>foo<em><strong>bar</strong></em>baz</p>\n", "CommonMark spec example 418");
    MH("foo******bar*********baz\n", "<p>foo<strong><strong><strong>bar</strong></strong></strong>***baz</p>\n", "CommonMark spec example 419");
    MH("**foo*\n", "<p>*<em>foo</em></p>\n", "CommonMark spec example 451");
    MH("*foo**\n", "<p><em>foo</em>*</p>\n", "CommonMark spec example 459");
    MH("***foo***\n", "<p><em><strong>foo</strong></em></p>\n", "CommonMark spec example 473");
    MH("*[bar*](/url)\n", "<p>*<a href=\"/url\">bar*</a></p>\n", "CommonMark spec example 476 (links bind tighter)");
    MH("[link](/uri \"title\")\n", "<p><a href=\"/uri\" title=\"title\">link</a></p>\n", "CommonMark spec example 482");
    MH("[link](/uri)\n", "<p><a href=\"/uri\">link</a></p>\n", "CommonMark spec example 483");
    MH("[link]()\n", "<p><a href=\"\">link</a></p>\n", "CommonMark spec example 485");
    MH("[link](<>)\n", "<p><a href=\"\">link</a></p>\n", "CommonMark spec example 486");
    MH("[link](/my uri)\n", "<p>[link](/my uri)</p>\n", "CommonMark spec example 488");
    MH("[link](</my uri>)\n", "<p><a href=\"/my%20uri\">link</a></p>\n", "CommonMark spec example 489");
    MH("[link](\\(foo\\))\n", "<p><a href=\"(foo)\">link</a></p>\n", "CommonMark spec example 493");
    MH("[link](foo(and(bar)))\n", "<p><a href=\"foo(and(bar))\">link</a></p>\n", "CommonMark spec example 494");
    // CommonMark spec example 497 parses to href "foo):"; the URL policy refuses it
    // because a ':' before any '/', '?' or '#' names a scheme that is not allowed.
    MH("[link](foo\\)\\:)\n", "<p>link</p>\n", "CommonMark spec example 497 (parsed, refused by the URL policy)");
    MH("[link](foo%20b&auml;)\n", "<p><a href=\"foo%20b%C3%A4\">link</a></p>\n", "CommonMark spec example 502 (normalization)");
    MH("[link](/url \"title \\\"&quot;\")\n", "<p><a href=\"/url\" title=\"title &quot;&quot;\">link</a></p>\n", "CommonMark spec example 505");
    MH("[link [foo [bar]]](/uri)\n", "<p><a href=\"/uri\">link [foo [bar]]</a></p>\n", "CommonMark spec example 512");
    MH("[link *foo **bar** `#`*](/uri)\n", "<p><a href=\"/uri\">link <em>foo <strong>bar</strong> <code>#</code></em></a></p>\n", "CommonMark spec example 516");
    MH("[![moon](moon.jpg)](/uri)\n", "<p><a href=\"/uri\"><img src=\"moon.jpg\" alt=\"moon\" /></a></p>\n", "CommonMark spec example 517");
    MH("[foo [bar](/uri)](/uri)\n", "<p>[foo <a href=\"/uri\">bar</a>](/uri)</p>\n", "CommonMark spec example 518 (no links in links)");
    MH("![foo *bar*](/url \"title\")\n", "<p><img src=\"/url\" alt=\"foo bar\" title=\"title\" /></p>\n", "CommonMark spec example 572 style (image alt is plain text)");
    MH("<http://foo.bar.baz>\n", "<p><a href=\"http://foo.bar.baz\">http://foo.bar.baz</a></p>\n", "CommonMark spec example 594");
    MH("<https://foo.bar.baz/test?q=hello&id=22&boolean>\n", "<p><a href=\"https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean\">https://foo.bar.baz/test?q=hello&amp;id=22&amp;boolean</a></p>\n", "CommonMark spec example 595");
    MH("<foo@bar.example.com>\n", "<p><a href=\"mailto:foo@bar.example.com\">foo@bar.example.com</a></p>\n", "CommonMark spec example 602");
    MH("<https://foo.bar/baz bim>\n", "<p>&lt;https://foo.bar/baz bim&gt;</p>\n", "CommonMark spec example 600");
    MH("foo  \nbaz\n", "<p>foo<br />\nbaz</p>\n", "CommonMark spec example 633 (two-space hard break)");
    MH("foo       \nbaz\n", "<p>foo<br />\nbaz</p>\n", "CommonMark spec example 635");
    MH("foo  \n     bar\n", "<p>foo<br />\nbar</p>\n", "CommonMark spec example 636");
    MH("foo\\\n", "<p>foo\\</p>\n", "CommonMark spec example 645 (no break at end of block)");
    MH("### foo  \n", "<h3>foo</h3>\n", "CommonMark spec example 647");
    MH("foo \nbaz\n", "<p>foo\nbaz</p>\n", "CommonMark spec example 649 (soft break)");
    MH("~~Hi~~ Hello, ~there~ world!\n", "<p><del>Hi</del> Hello, <del>there</del> world!</p>\n", "GFM spec example 491 (strikethrough)");
    MH("This ~~has a\n\nnew paragraph~~.\n", "<p>This ~~has a</p>\n<p>new paragraph~~.</p>\n", "GFM spec example 492");
    MH("This will ~~~not~~~ strike.\n", "<p>This will ~~~not~~~ strike.</p>\n", "GFM spec example 493");
    MH("*a ~~b* c~~\n", "<p><em>a ~~b</em> c~~</p>\n", "strikethrough cannot cross emphasis");
    MH("caf\xC3\xA9 *\xC2\xABx\xC2\xBB* \xE2\x80\x9C*q*\xE2\x80\x9D\n", "<p>caf\xC3\xA9 <em>\xC2\xABx\xC2\xBB</em> \xE2\x80\x9C<em>q</em>\xE2\x80\x9D</p>\n", "Unicode punctuation in flanking");
    MH("a\xC2\xA0*b*\xC2\xA0" "c\n", "<p>a\xC2\xA0<em>b</em>\xC2\xA0" "c</p>\n", "Unicode whitespace (NBSP) in flanking");
}

// ================= Markdown security =================
void MarkdownSecurityTests() {
    {
        const auto r=M("[a](javascript:alert(1)) [b](JAVASCRIPT:alert(1)) [c](java&#x09;script:x) [d](data:text/html,x) [e](vbscript:x) "
            "[f](//evil.example) <javascript:alert(1)> [g](file:///etc/passwd)\n");
        // java&#x09;script: decodes to a TAB, is percent-encoded to java%09script: and,
        // having a ':' before any '/', is refused as an unknown scheme.
        ExpectEqual(r.html,"<p>a b c d e f javascript:alert(1) g</p>\n","unsafe link schemes rendered as text");
        Expect(r.report.unsafeLinks==8&&r.report.links==8,"unsafe links counted");
    }
    {
        const auto r=M("![x](data:image/png;base64,AAAA) ![*y*](javascript:x) ![z](mailto:a@b) ![ok](https://i.example/a.png)\n");
        ExpectEqual(r.html,"<p>x y z <img src=\"https://i.example/a.png\" alt=\"ok\" /></p>\n","unsafe image sources rendered as alt text");
        Expect(r.report.unsafeImages==3&&r.report.images==4,"unsafe images counted");
    }
    {
        const auto r=M("<script>alert(1)</script>\n\n<img src=x onerror=alert(1)> <a href=\"javascript:x\">x</a> <!-- c --> <?php ?>\n\n"
            "[q](https://ok.example \"\\\" onmouseover=\\\"x\") <style>*{}</style>\n");
        ExpectEqual(r.html,"<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>\n<p>&lt;img src=x onerror=alert(1)&gt; &lt;a href=&quot;javascript:x&quot;&gt;x&lt;/a&gt; &lt;!-- c --&gt; &lt;?php ?&gt;</p>\n"
            "<p><a href=\"https://ok.example\" title=\"&quot; onmouseover=&quot;x\">q</a> &lt;style&gt;*{}&lt;/style&gt;</p>\n","inline HTML and attribute breakouts escaped");
        Expect(r.report.htmlEscaped==9,"escaped HTML counted");
    }
    MH("```\" onclick=\"x\n<b>\n```\n", "<pre><code class=\"language-&quot;\">&lt;b&gt;\n</code></pre>\n", "info string escaped");
    MH("[x](https://e.example/\"><script>)\n", "<p><a href=\"https://e.example/%22%3E%3Cscript%3E\">x</a></p>\n", "destination percent-encoded");
    // Output never contains an active tag or event-handler attribute.
    for(const std::string_view md:{"<svg onload=x>","<iframe src=x>","[x](<javascript:x>)","<a href=x onclick=y>z</a>","&lt;script&gt;"}) {
        const auto r=M(md);
        Expect(r&&r.html.find("<svg")==std::string::npos&&r.html.find("<iframe")==std::string::npos
            &&r.html.find("javascript:")==std::string::npos&&r.html.find("<a href=x")==std::string::npos
            &&r.html.find("<script")==std::string::npos,"no active content");
    }
}

// ================= SRT -> WebVTT =================
const std::string_view plainSrt=
    "1\n00:00:01,000 --> 00:00:02,500\nHello, world.\n\n"
    "2\n00:00:03,000 --> 00:00:04,000\nTwo lines\nof text\n\n"
    "3\n01:02:03,004 --> 01:02:05,000\nA & B < C > D\n\n";
void SrtTests() {
    Sub(S2V(plainSrt),
        "WEBVTT\n\n00:00:01.000 --> 00:00:02.500\nHello, world.\n\n00:00:03.000 --> 00:00:04.000\nTwo lines\nof text\n\n"
        "01:02:03.004 --> 01:02:05.000\nA &amp; B &lt; C &gt; D\n\n","plain SRT to WebVTT");
    // Round trip SRT -> VTT -> SRT is identical for plain cues in canonical form.
    {
        const auto vtt=S2V(plainSrt);
        const auto back=V2S(vtt.text);
        ExpectEqual(back.text,plainSrt,"SRT -> VTT -> SRT round trip");
        const auto again=S2V(back.text);
        ExpectEqual(again.text,vtt.text,"VTT -> SRT -> VTT round trip");
    }
    // Tolerance: BOM, CRLF, missing indices, missing blank line, coordinates, whitespace-only separators, '.' millis.
    {
        const auto r=S2V("\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,000\r\nfirst\r\n\r\n"
            "00:00:03,000-->00:00:04,000  X1:10 X2:20 Y1:5 Y2:9\r\nno index\r\n"
            "3\r\n00:00:05.000 --> 00:00:06,000\r\nno blank line before me\r\n \r\n"
            "00:00:07,000 --> 00:00:08,000\r\nlast, no trailing newline");
        Sub(r,"WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nfirst\n\n00:00:03.000 --> 00:00:04.000\nno index\n\n"
            "00:00:05.000 --> 00:00:06.000\nno blank line before me\n\n00:00:07.000 --> 00:00:08.000\nlast, no trailing newline\n\n","tolerant SRT");
        Expect(r.report.cues==4&&r.report.missingIndices==2&&r.report.settingsDropped==1,"SRT tolerance report");
    }
    // Basic tags: b/i/u kept, font stripped by default, kept as WebVTT colour class when asked.
    {
        const std::string_view tagged="1\n00:00:01,000 --> 00:00:02,000\n<i>it</i> <B>bold</B> <u>u</u> <font color=\"#FF0000\">red</font> "
            "<font color=green face=\"Arial\">green</font> {\\an8}top <span>s</span> a <3 --> b\n";
        const auto plain=S2V(tagged);
        Sub(plain,"WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n<i>it</i> <b>bold</b> <u>u</u> red green top s a &lt;3 --&gt; b\n\n","SRT tags, font stripped");
        Expect(plain.report.tagsStripped==3&&plain.report.overridesStripped==1,"SRT tag report");
        mk::SubtitleOptions o;o.keepFontColor=true;
        Sub(S2V(tagged,o),"WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n<i>it</i> <b>bold</b> <u>u</u> <c.red>red</c> green top s a &lt;3 --&gt; b\n\n","SRT font colour kept");
    }
    // Empty payload and a payload line that only held an override code.
    Sub(S2V("1\n00:00:01,000 --> 00:00:02,000\n{\\an8}\ntext\n\n2\n00:00:03,000 --> 00:00:04,000\n\n"),
        "WEBVTT\n\n00:00:01.000 --> 00:00:02.000\ntext\n\n00:00:03.000 --> 00:00:04.000\n\n","empty lines and payloads");
    // Malformed input: typed errors with line numbers.
    ExpectSubError(S2V("1\n00:00:01,000 --> 00:00:0x,000\nbad\n"),Error::subtitle_malformed_timestamp,2,"bad seconds");
    ExpectSubError(S2V("1\n00:00:01,000 --> 00:00:02,000\nok\n\n2\n00:61:00,000 --> 01:00:00,000\nbad\n"),Error::subtitle_malformed_timestamp,6,"minutes out of range");
    ExpectSubError(S2V("1\n00:00:01,00 --> 00:00:02,000\nbad\n"),Error::subtitle_malformed_timestamp,2,"two-digit millis");
    ExpectSubError(S2V("1\n00:00:01,000 -> 00:00:02,000\n"),Error::subtitle_malformed_block,1,"index without timing line");
    ExpectSubError(S2V("hello\n00:00:01,000 --> 00:00:02,000\n"),Error::subtitle_malformed_block,1,"text where index expected");
    ExpectSubError(S2V("1\n00:00:05,000 --> 00:00:04,000\nback\n"),Error::subtitle_time_order,2,"end before start");
    ExpectSubError(S2V("1\n00:00:05,000 --> 00:00:06,000\na\n\n2\n00:00:04,000 --> 00:00:07,000\nb\n"),Error::subtitle_non_monotonic,6,"non-monotonic starts");
    ExpectSubError(S2V("1\n00:00:01,000 --> 00:00:02,000\n\xFF\n"),Error::invalid_utf8,0,"invalid UTF-8");
    // Overlapping cues with equal starts are allowed.
    Sub(S2V("1\n00:00:01,000 --> 00:00:05,000\na\n\n2\n00:00:01,000 --> 00:00:02,000\nb\n"),
        "WEBVTT\n\n00:00:01.000 --> 00:00:05.000\na\n\n00:00:01.000 --> 00:00:02.000\nb\n\n","overlap and equal starts");
    Sub(S2V(""),"WEBVTT\n\n","empty SRT");
}

// ================= WebVTT -> SRT =================
void VttTests() {
    {
        const auto r=V2S("WEBVTT - Title text\nKind: captions\nLanguage: en\n\nSTYLE\n::cue { color: red }\n\nREGION\nid:fred\nwidth:40%\n\n"
            "NOTE This is a comment\nspanning lines\n\nintro\n00:01.000 --> 00:02.000 align:start line:0 position:10%\n"
            "<v Bob>Hi</v> <v.loud Ann>there</v> &amp; &lt;3 &gt; &nbsp;&lrm;<00:01.500>more\n\n"
            "2\n00:00:03.000 --> 00:00:04.000\n<b.big>bold</b> <i>it</i> <u>u</u> <c.yellow.bg_blue>yel</c> <lang en>x</lang> <ruby>a<rt>b</rt></ruby>\n\n"
            "NOTE trailing\n");
        Sub(r,"1\n00:00:01,000 --> 00:00:02,000\nHi there & <3 > \xC2\xA0\xE2\x80\x8Emore\n\n"
            "2\n00:00:03,000 --> 00:00:04,000\n<b>bold</b> <i>it</i> <u>u</u> yel x ab\n\n","WebVTT to SRT");
        Expect(r.report.cues==2&&r.report.headerLinesDropped==3&&r.report.styleBlocksDropped==1&&r.report.regionBlocksDropped==1
            &&r.report.notesDropped==2&&r.report.settingsDropped==1&&r.report.identifiersDropped==1&&r.report.tagsStripped==7,"WebVTT drop report");
    }
    {
        mk::SubtitleOptions o;o.keepFontColor=true;o.crlf=true;
        Sub(V2S("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n<c.red>r</c> <c.lime>g</c> <c.custom>c</c>\n",o),
            "1\r\n00:00:01,000 --> 00:00:02,000\r\n<font color=\"red\">r</font> <font color=\"lime\">g</font> c\r\n\r\n","colour kept, CRLF output");
    }
    // Missing blank line: a line containing --> starts the next cue (WebVTT parser rule). Long hours.
    Sub(V2S("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\na\n00:00:03.000 --> 00:00:04.000\nb\n\n123:00:00.000 --> 123:00:01.000\nc\n"),
        "1\n00:00:01,000 --> 00:00:02,000\na\n\n2\n00:00:03,000 --> 00:00:04,000\nb\n\n3\n123:00:00,000 --> 123:00:01,000\nc\n\n","WebVTT without blank line; hours above 99");
    ExpectSubError(V2S("WEBVTTX\n\n"),Error::subtitle_missing_header,1,"bad signature");
    ExpectSubError(V2S("1\n00:00:01,000 --> 00:00:02,000\nsrt\n"),Error::subtitle_missing_header,1,"SRT given as WebVTT");
    ExpectSubError(V2S("WEBVTT\n\n00:00:01,000 --> 00:00:02,000\ncomma\n"),Error::subtitle_malformed_timestamp,3,"WebVTT requires '.'");
    ExpectSubError(V2S("WEBVTT\n\n0:00:01.000 --> 00:00:02.000\nshort hours\n"),Error::subtitle_malformed_timestamp,3,"one-digit hours");
    ExpectSubError(V2S("WEBVTT\n\nid\nnot a timing\n"),Error::subtitle_malformed_block,3,"identifier without timing");
    ExpectSubError(V2S("WEBVTT\n\n00:00:02.000 --> 00:00:03.000\na\n\n00:00:01.000 --> 00:00:04.000\nb\n"),Error::subtitle_non_monotonic,6,"WebVTT non-monotonic");
    ExpectSubError(V2S("WEBVTT\n\n00:00:02.000 --> 00:00:01.000\na\n"),Error::subtitle_time_order,3,"WebVTT end before start");
    Sub(V2S("\xEF\xBB\xBFWEBVTT\r\n\r\n00:00.500 --> 00:01.000\r\nx\r\n"),"1\n00:00:00,500 --> 00:00:01,000\nx\n\n","BOM and CRLF WebVTT");
    Sub(V2S("WEBVTT"),"","header only");
}

// ================= Limits =================
void LimitTests() {
    {
        mk::HtmlToMarkdownOptions o;o.maximumSourceBytes=4;
        ExpectError(H("<p>12345</p>",o).error,Error::source_limit,"HTML source limit");
        o=mk::HtmlToMarkdownOptions{};o.maximumOutputBytes=4;
        const auto r=H("<p>12345</p>",o);
        ExpectError(r.error,Error::output_limit,"HTML output limit");
        Expect(r.markdown.empty(),"failed result carries no text");
        o=mk::HtmlToMarkdownOptions{};o.maximumDepth=8;
        ExpectError(H(std::string(9,'x').replace(0,9,"")+"<div><div><div><div><div><div><div><div><div>x",o).error,Error::depth_limit,"HTML depth limit");
        HM("<div><div><div><div><div><div><div><div>x","x\n","eight levels fit",o);
        o=mk::HtmlToMarkdownOptions{};o.maximumNodes=5;
        ExpectError(H("<p>a<b>b</b>c<i>d</i></p>",o).error,Error::node_limit,"HTML node limit");
        o=mk::HtmlToMarkdownOptions{};o.maximumAttributes=1;
        HM("<a title=t href=\"https://x\">x</a>","x\n","attributes beyond the limit are ignored",o);
        o=mk::HtmlToMarkdownOptions{};o.maximumDepth=0;
        ExpectError(H("x",o).error,Error::invalid_options,"zero depth invalid");
        o.maximumDepth=5000;
        ExpectError(H("x",o).error,Error::invalid_options,"excessive depth invalid");
        // Unbounded tag soup stays bounded: 100k unclosed <b> hits the depth limit, not the stack.
        std::string soup;
        for(int i=0;i<100000;++i)soup+="<b>";
        ExpectError(H(soup).error,Error::depth_limit,"unclosed inline soup");
        std::string closed;
        for(int i=0;i<100000;++i)closed+="<p>x";
        Expect(H(closed).markdown.size()==299999,"100k implicitly closed paragraphs");
    }
    {
        mk::MarkdownToHtmlOptions o;o.maximumSourceBytes=3;
        ExpectError(M("abcd",o).error,Error::source_limit,"Markdown source limit");
        o=mk::MarkdownToHtmlOptions{};o.maximumOutputBytes=8;
        ExpectError(M("abcdefgh",o).error,Error::output_limit,"Markdown output limit");
        o=mk::MarkdownToHtmlOptions{};o.maximumNesting=3;
        ExpectError(M("> > > > x",o).error,Error::depth_limit,"block quote nesting");
        ExpectError(M("- - - - x",o).error,Error::depth_limit,"list nesting");
        MH("> > > x","<blockquote>\n<blockquote>\n<blockquote>\n<p>x</p>\n</blockquote>\n</blockquote>\n</blockquote>\n","three levels fit",o);
        o=mk::MarkdownToHtmlOptions{};o.maximumLinks=2;
        ExpectError(M("[a](b) [c](d) ![e](f)",o).error,Error::link_limit,"link limit");
        MH("[a](b) <https://c.example>","<p><a href=\"b\">a</a> <a href=\"https://c.example\">https://c.example</a></p>\n","two links fit",o);
        o=mk::MarkdownToHtmlOptions{};o.maximumNesting=2000;
        ExpectError(M("x",o).error,Error::invalid_options,"excessive nesting invalid");
        // Pathological inputs stay linear enough: deep emphasis, many brackets, many backtick runs.
        std::string deep(200000,'*');
        deep+="a";
        deep+=std::string(200000,'*');
        const auto r=M(deep);
        Expect(r&&r.html.size()>200000,"deep emphasis renders without recursion");
        std::string brackets;
        for(int i=0;i<50000;++i)brackets+="[](x";
        Expect(bool(M(brackets)),"unclosed link destinations");
        std::string ticks;
        for(int i=1;i<2000;++i)ticks+=std::string(std::size_t(i%50+1),'`')+"a";
        Expect(bool(M(ticks)),"many unmatched backtick runs");
        ExpectError(M("caf\xE9").error,Error::invalid_utf8,"Markdown invalid UTF-8");
    }
    {
        mk::SubtitleOptions o;o.maximumCues=2;
        ExpectSubError(S2V(plainSrt,o),Error::subtitle_cue_limit,11,"cue limit");
        o=mk::SubtitleOptions{};o.maximumPayloadBytes=10;
        ExpectSubError(S2V("1\n00:00:01,000 --> 00:00:02,000\n0123456789A\n",o),Error::subtitle_payload_limit,3,"payload limit");
        Sub(S2V("1\n00:00:01,000 --> 00:00:02,000\n0123456789\n",o),"WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n0123456789\n\n","payload at the limit");
        o=mk::SubtitleOptions{};o.maximumSourceBytes=5;
        ExpectError(S2V(plainSrt,o).error,Error::source_limit,"subtitle source limit");
        o=mk::SubtitleOptions{};o.maximumOutputBytes=10;
        ExpectError(S2V(plainSrt,o).error,Error::output_limit,"subtitle output limit");
        o=mk::SubtitleOptions{};o.maximumCues=0;
        ExpectError(V2S("WEBVTT",o).error,Error::invalid_options,"zero cue limit invalid");
    }
    for(auto e=std::uint32_t(Error::none);e<=std::uint32_t(Error::subtitle_payload_limit);++e) {
        Expect(mk::ErrorCode(Error(e))!="unknown"&&!mk::ErrorMessage(Error(e)).empty(),"every error has a code and message");
    }
    Expect(mk::MediaType(mk::Format::markdown)=="text/markdown; charset=utf-8"&&mk::MediaType(mk::Format::html)=="text/html; charset=utf-8"
        &&mk::MediaType(mk::Format::srt)=="application/x-subrip"&&mk::MediaType(mk::Format::vtt)=="text/vtt","media types");
}

// ================= Determinism =================
const std::string_view richHtml=
    "<html><head><title>x</title></head><body><h1>T</h1><p>a <b>b</b> <a href=\"https://e.example\">c</a></p>"
    "<ul><li>1<li>2</ul><table><tr><th>h<tr><td>d</table><pre>code</pre><dl><dt>t<dd>d</dl></body></html>";
const std::string_view richMarkdown=
    "# T\n\nPara *em* **st** `c` [l](https://e.example) ~~d~~\n\n- a\n- b\n\n1. x\n\n> q\n\n```c\nint\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n";
void DeterminismTests() {
    const auto h1=H(richHtml),h2=H(richHtml);
    Expect(h1&&h1.markdown==h2.markdown,"HTML -> Markdown deterministic");
    const auto m1=M(richMarkdown),m2=M(richMarkdown);
    Expect(m1&&m1.html==m2.html,"Markdown -> HTML deterministic");
    const auto s1=S2V(plainSrt),s2=S2V(plainSrt);
    Expect(s1&&s1.text==s2.text,"SRT -> WebVTT deterministic");
    // Markdown emitted from HTML renders back to the same structure.
    ExpectEqual(M(h1.markdown).html,
        "<h1>T</h1>\n<p>a <strong>b</strong> <a href=\"https://e.example\">c</a></p>\n<ul>\n<li>1</li>\n<li>2</li>\n</ul>\n"
        "<table>\n<thead>\n<tr>\n<th>h</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n<td>d</td>\n</tr>\n</tbody>\n</table>\n"
        "<pre><code>code\n</code></pre>\n<p><strong>t</strong></p>\n<p>d</p>\n","HTML -> Markdown -> HTML structure");
    // Escaping is exact: literal text survives HTML -> Markdown -> HTML unchanged.
    const auto escapeHtml=[](std::string_view s) {
        std::string out;
        for(const char c:s) {
            if(c=='&')out+="&amp;";
            else if(c=='<')out+="&lt;";
            else if(c=='>')out+="&gt;";
            else if(c=='"')out+="&quot;";
            else out.push_back(c);
        }
        return out;
    };
    for(const std::string_view text:{"*a* _b_ `c` [d](e) <f> \\g | h ~i~ ~~j~~ # k","1. x","12) y","- y","+ z","> q","=== x","---","***",
        "![x](y)","**","foo_bar_","<http://x.example>","&copy; &#65; &","a\\","#hash","[link]: /url","| a | b |","\xC2\xAB*x*\xC2\xBB",
        "`` ` ``","<!-- c -->","x <b>y</b>","https://bare.example"}) {
        const auto md=H("<p>"+escapeHtml(text)+"</p>");
        ExpectEqual(M(md.markdown).html,"<p>"+escapeHtml(text)+"</p>\n","escaping round trip: "+std::string(text));
    }
    // Emphasis written by the mapper parses back as the same emphasis (real flanking rules).
    ExpectEqual(M(H("<p>a<b>b<i>c</i></b>d <i>x</i><b>y</b> z</p>").markdown).html,
        "<p>a<strong>b<em>c</em></strong>d <em>x</em><strong>y</strong> z</p>\n","mapper emphasis round trip");
}

// Seeded mutations: every outcome is a typed error or valid UTF-8 without crashes or hangs.
long mutationRounds=1500;
void MutationTests() {
    std::uint64_t state=0x9e3779b97f4a7c15ull;
    const auto next=[&] {state^=state<<13;state^=state>>7;state^=state<<17;return state;};
    const std::string seeds[]={std::string(richHtml),std::string(richMarkdown),std::string(plainSrt),
        std::string(S2V(plainSrt).text)};
    std::size_t successes=0,errors=0;
    for(std::size_t s=0;s<4;++s) {
        for(long round=0;round<mutationRounds;++round) {
            auto bytes=seeds[s];
            const auto edits=1+next()%4;
            for(std::uint64_t e=0;e<edits&&!bytes.empty();++e) {
                const auto at=std::size_t(next()%bytes.size());
                switch(next()%5) {
                    case 0: bytes[at]=char(next());break;
                    case 1: bytes[at]=char(bytes[at]^(1<<(next()%8)));break;
                    case 2: bytes.resize(at);break;
                    case 3: bytes.insert(bytes.begin()+std::ptrdiff_t(at),char(next()));break;
                    default: bytes[at]="<>&*_[]`|\n:-"[next()%12];break;
                }
            }
            std::string out;
            bool ok=false;
            if(s==0) {const auto r=H(bytes);ok=bool(r);out=r.markdown;}
            else if(s==1) {const auto r=M(bytes);ok=bool(r);out=r.html;}
            else if(s==2) {const auto r=S2V(bytes);ok=bool(r);out=r.text;}
            else {const auto r=V2S(bytes);ok=bool(r);out=r.text;}
            if(!ok) {++errors;Expect(out.empty(),"error carries no text");continue;}
            ++successes;
            Expect(ValidUtf8(out),"mutated input produced invalid UTF-8");
            if(s==1)Expect(out.find("<script")==std::string::npos&&out.find("javascript:")==std::string::npos,"mutated Markdown stays inert");
        }
    }
    Expect(successes>0&&errors>0,"mutations exercise both outcomes");
}
}

int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version") {std::cout<<mk::revision<<"\n";return 0;}
    const std::map<std::string,std::function<void()>> groups={
        {"html_tokenizer",HtmlTokenizerTests},{"html_markdown",HtmlMarkdownTests},{"html_security",HtmlSecurityTests},
        {"markdown_blocks",MarkdownBlockTests},{"markdown_inlines",MarkdownInlineTests},{"markdown_security",MarkdownSecurityTests},
        {"srt_vtt",SrtTests},{"vtt_srt",VttTests},{"limits",LimitTests},{"determinism",DeterminismTests},{"mutation",MutationTests}};
    if(argc>1) {
        const auto found=groups.find(argv[1]);
        if(found==groups.end()) {std::cerr<<"unknown group "<<argv[1]<<"\n";return 2;}
        if(argc>2)mutationRounds=std::max(1L,std::atol(argv[2]));
        found->second();
    } else {
        for(const auto& [name,run]:groups)run();
    }
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::cout<<"ok\n";
    return 0;
}
