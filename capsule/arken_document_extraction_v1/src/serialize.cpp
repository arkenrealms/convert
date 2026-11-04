#include "detail.hpp"
#include <algorithm>

namespace arken::document::v1::detail {
namespace {
class Output {
public:
    explicit Output(std::uint64_t limit):limit_(limit) {}
    void Put(std::string_view s) {
        if(s.size()>limit_-std::min<std::uint64_t>(limit_,text_.size()))Fail(Error::output_limit);
        text_.append(s);
    }
    std::string& Text() {return text_;}
private:
    std::uint64_t limit_;
    std::string text_;
};

bool AllowedLink(std::string_view url) {
    for(const std::string_view scheme:{std::string_view("http://"),std::string_view("https://"),std::string_view("mailto:")})
        if(url.size()>scheme.size()&&EqualsNoCase(url.substr(0,scheme.size()),scheme))return true;
    return false;
}
// Percent-encodes everything outside the URI reserved/unreserved sets (and
// parentheses, which would end a Markdown destination).
std::string Destination(std::string_view url) {
    static constexpr std::string_view safe="-._~:/?#[]@!$&'*+,;=%";
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    for(const char c:url) {
        const auto u=std::uint8_t(c);
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||(u<0x80&&safe.find(c)!=std::string_view::npos)) {out.push_back(c);continue;}
        out.push_back('%');out.push_back(hex[u>>4]);out.push_back(hex[u&15]);
    }
    return out;
}
// Groups consecutive runs that share a link target.
template<class F> void LinkGroups(const std::vector<Run>& runs,F&& visit) {
    for(std::size_t i=0;i<runs.size();) {
        auto j=i+1;
        while(j<runs.size()&&runs[j].link==runs[i].link)++j;
        visit(std::span<const Run>(runs.data()+i,j-i),runs[i].link);
        i=j;
    }
}
std::string Joined(std::span<const Run> runs) {
    std::string out;
    for(const auto& r:runs)out+=r.text;
    return out;
}
bool Whitespace(char c) {return c==' '||c=='\t'||c=='\n';}

// Ordered numbering per nesting level; deeper levels reset when a shallower
// item appears, and a bullet resets its level.
class ListCounters {
public:
    int Next(std::size_t level,bool ordered) {
        counters_.resize(level+1,0);
        if(!ordered) {counters_[level]=0;return 0;}
        return ++counters_[level];
    }
    void Reset() {counters_.clear();}
private:
    std::vector<int> counters_;
};

// ---- TXT ----
std::string PlainInline(const std::vector<Run>& runs) {
    std::string out;
    LinkGroups(runs,[&](std::span<const Run> group,const std::string& link) {
        const auto text=Joined(group);
        out+=text;
        if(!link.empty()&&AllowedLink(link)&&text!=link) {
            std::string_view shown=link;
            if(EqualsNoCase(shown.substr(0,7),"mailto:")&&text==shown.substr(7))return;
            out+=" (";out+=link;out+=")";
        }
    });
    return out;
}
std::string PlainCell(const Cell& cell) {
    auto text=PlainInline(cell);
    for(auto& c:text)if(c=='\t'||c=='\n')c=' ';
    return text;
}

// ---- Markdown ----
enum class Mode {block,singleLine};
class MarkdownInline {
public:
    MarkdownInline(Mode mode,std::string continuation):mode_(mode),continuation_(std::move(continuation)) {}
    std::string Render(const std::vector<Run>& source) {
        // Leading/trailing whitespace of the whole block is never significant.
        auto runs=source;
        while(!runs.empty()) {
            auto& t=runs.front().text;
            const auto first=std::find_if_not(t.begin(),t.end(),Whitespace);
            t.erase(t.begin(),first);
            if(!t.empty())break;
            runs.erase(runs.begin());
        }
        while(!runs.empty()) {
            auto& t=runs.back().text;
            while(!t.empty()&&Whitespace(t.back()))t.pop_back();
            if(!t.empty())break;
            runs.pop_back();
        }
        LinkGroups(runs,[&](std::span<const Run> group,const std::string& link) {
            if(link.empty()||!AllowedLink(link)) {Emphasis(group,out_);return;}
            // Whitespace at the group's edges stays outside the link brackets.
            std::vector<Run> inner(group.begin(),group.end());
            std::string lead,trail;
            while(!inner.empty()&&!inner.front().text.empty()&&Whitespace(inner.front().text.front())) {
                lead.push_back(inner.front().text.front());inner.front().text.erase(0,1);
                if(inner.front().text.empty())inner.erase(inner.begin());
            }
            while(!inner.empty()&&!inner.back().text.empty()&&Whitespace(inner.back().text.back())) {
                trail.insert(trail.begin(),inner.back().text.back());inner.back().text.pop_back();
                if(inner.back().text.empty())inner.pop_back();
            }
            Escape(lead,out_);
            std::string label;
            Emphasis(inner,label);
            if(label.empty()) {
                const auto shown=EqualsNoCase(std::string_view(link).substr(0,7),"mailto:")?link.substr(7):link;
                Escape(shown,label);
            }
            out_+="[";out_+=label;out_+="](";out_+=Destination(link);out_+=")";
            lineStart_=false;
            Escape(trail,out_);
        });
        return std::move(out_);
    }
private:
    // Emphasis as properly nested delimiter runs. Delimiters change only next to
    // non-whitespace text, closers before pending whitespace and openers after
    // it, so every opener is left-flanking and every closer right-flanking.
    void Emphasis(std::span<const Run> runs,std::string& out) {
        std::vector<char> open; // 'b' or 'i', innermost last.
        std::string pendingSpace;
        const auto close=[&](std::size_t keep) {
            while(open.size()>keep) {out+=open.back()=='b'?"**":"*";open.pop_back();}
        };
        for(const auto& run:runs) {
            const auto& t=run.text;
            for(std::size_t i=0;i<t.size();) {
                if(Whitespace(t[i])) {pendingSpace.push_back(t[i]);++i;continue;}
                auto j=i;
                while(j<t.size()&&!Whitespace(t[j]))++j;
                // Keep the longest prefix of open delimiters that is still wanted.
                std::size_t keep=0;
                while(keep<open.size()&&((open[keep]=='b'&&run.bold)||(open[keep]=='i'&&run.italic)))++keep;
                close(keep);
                Escape(pendingSpace,out);
                pendingSpace.clear();
                const bool hasBold=std::find(open.begin(),open.end(),'b')!=open.end();
                const bool hasItalic=std::find(open.begin(),open.end(),'i')!=open.end();
                if(run.bold&&!hasBold) {out+="**";open.push_back('b');lineStart_=false;}
                if(run.italic&&!hasItalic) {out+="*";open.push_back('i');lineStart_=false;}
                Escape(t.substr(i,j-i),out);
                i=j;
            }
        }
        close(0);
        Escape(pendingSpace,out);
    }
    void Escape(std::string_view text,std::string& out) {
        for(std::size_t i=0;i<text.size();++i) {
            char c=text[i];
            if(c=='\n') {
                if(mode_==Mode::singleLine) {c=' ';}
                else {
                    // Spaces before a hard break are insignificant (and would be a second break form).
                    while(!out.empty()&&(out.back()==' '||out.back()=='\t'))out.pop_back();
                    out+="\\\n";
                    out+=continuation_;
                    lineStart_=true;
                    continue;
                }
            }
            if(c=='\t'&&mode_==Mode::singleLine)c=' ';
            if(lineStart_) {
                if(c==' '||c=='\t')continue; // Indentation would change block structure.
                lineStart_=false;
                if(c=='-'||c=='+'||c=='=') {out.push_back('\\');out.push_back(c);continue;}
                if(c>='0'&&c<='9') {
                    auto j=i;
                    while(j<text.size()&&text[j]>='0'&&text[j]<='9')++j;
                    out.append(text.substr(i,j-i));
                    if(j<text.size()&&(text[j]=='.'||text[j]==')')) {out.push_back('\\');out.push_back(text[j]);++j;}
                    i=j-1;
                    continue;
                }
            }
            switch(c) {
                case '\\': case '`': case '*': case '_': case '[': case ']': case '<': case '>':
                case '|': case '~': case '&': case '#':
                    out.push_back('\\');break;
                default: break;
            }
            out.push_back(c);
        }
    }
    Mode mode_;
    std::string continuation_;
    std::string out_;
    bool lineStart_=true;
};
std::string MarkdownText(const std::vector<Run>& runs,Mode mode,std::string continuation={}) {
    return MarkdownInline(mode,std::move(continuation)).Render(runs);
}
bool Rectangular(const std::vector<Row>& rows) {
    if(rows.empty()||rows.front().empty())return false;
    for(const auto& row:rows)if(row.size()!=rows.front().size())return false;
    return true;
}
void StripTrailingSpace(std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t lineEnd=0;
    for(const char c:text) {
        if(c=='\n') {out.resize(lineEnd);out.push_back('\n');lineEnd=out.size();continue;}
        out.push_back(c);
        if(c!=' '&&c!='\t')lineEnd=out.size();
    }
    out.resize(lineEnd);
    text=std::move(out);
}
// Code text exactly as held, without the trailing line breaks that end the block.
std::string CodeText(const Block& block) {
    auto text=Joined(block.runs);
    while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();
    return text;
}
// A language label becomes a fence info string only when it is plainly a name.
std::string_view InfoString(std::string_view language) {
    if(language.empty()||language.size()>32)return {};
    for(const char c:language)
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='+'||c=='.'||c=='#'||c=='-'))return {};
    return language;
}
std::string FencedCode(const Block& block) {
    const auto code=CodeText(block);
    if(!ModelBuilder::HasText(block.runs))return {};
    // The fence is longer than any backtick run in the code, so no content line can close it.
    std::size_t longest=0,run=0;
    for(const char c:code) {run=c=='`'?run+1:0;longest=std::max(longest,run);}
    const std::string fence(std::max<std::size_t>(3,longest+1),'`');
    return fence+std::string(InfoString(block.language))+"\n"+code+"\n"+fence;
}
}

std::string RenderText(const Document& document,const SerializeOptions& options) {
    Output out(options.maximumOutputBytes);
    ListCounters counters;
    // A thematic break is one extra blank line before the next block; leading, trailing
    // and repeated breaks add nothing.
    bool first=true,previousList=false,pendingBreak=false;
    for(const auto& block:document.blocks) {
        const bool list=block.kind==BlockKind::list_item;
        if(!list)counters.Reset();
        if(block.kind==BlockKind::thematic_break) {pendingBreak=!first;continue;}
        if(block.kind==BlockKind::code_block&&!ModelBuilder::HasText(block.runs))continue;
        if(!first)out.Put(pendingBreak?"\n\n\n":list&&previousList?"\n":"\n\n");
        first=false;previousList=list;pendingBreak=false;
        switch(block.kind) {
            case BlockKind::paragraph:
            case BlockKind::heading:
            case BlockKind::block_quote:
                out.Put(PlainInline(block.runs));
                break;
            case BlockKind::code_block:
                out.Put(CodeText(block));
                break;
            case BlockKind::thematic_break:
                break;
            case BlockKind::list_item: {
                const std::string indent(std::size_t(block.level)*2,' ');
                const auto number=counters.Next(block.level,block.ordered);
                const auto marker=block.ordered?std::to_string(number)+". ":std::string("- ");
                std::string body=indent+marker;
                const std::string continuation="\n"+indent+std::string(marker.size(),' ');
                for(const char c:PlainInline(block.runs)) {if(c=='\n')body+=continuation;else body.push_back(c);}
                out.Put(body);
                break;
            }
            case BlockKind::table:
                for(std::size_t r=0;r<block.rows.size();++r) {
                    if(r)out.Put("\n");
                    for(std::size_t c=0;c<block.rows[r].size();++c) {
                        if(c)out.Put("\t");
                        out.Put(PlainCell(block.rows[r][c]));
                    }
                }
                break;
        }
    }
    if(!first)out.Put("\n");
    return std::move(out.Text());
}

std::string RenderMarkdown(const Document& document,const SerializeOptions& options) {
    Output out(options.maximumOutputBytes);
    ListCounters counters;
    std::vector<std::size_t> offsets; // Content column of the open item at each effective level.
    // Thematic breaks are written before the next rendered block, so leading, trailing and
    // repeated breaks add nothing (as in TXT).
    bool first=true,previousList=false,previousQuote=false,pendingBreak=false;
    for(const auto& block:document.blocks) {
        const bool list=block.kind==BlockKind::list_item;
        const bool quote=block.kind==BlockKind::block_quote;
        std::string rendered;
        switch(block.kind) {
            case BlockKind::paragraph:
                rendered=MarkdownText(block.runs,Mode::block);
                break;
            case BlockKind::block_quote: {
                // Hard breaks continue the quotation; '>' in the text is always escaped.
                const auto body=MarkdownText(block.runs,Mode::block,"> ");
                if(!body.empty())rendered="> "+body;
                break;
            }
            case BlockKind::thematic_break:
                if(!first) {pendingBreak=true;counters.Reset();offsets.clear();}
                continue;
            case BlockKind::code_block:
                rendered=FencedCode(block);
                break;
            case BlockKind::heading: {
                const auto text=MarkdownText(block.runs,Mode::singleLine);
                if(!text.empty())rendered=std::string(std::clamp<std::size_t>(block.level,1,6),'#')+" "+text;
                break;
            }
            case BlockKind::list_item: {
                // Levels may only deepen one step at a time; indentation follows
                // the parent's content column so nesting is unambiguous.
                const auto level=std::min<std::size_t>(block.level,previousList?offsets.size():0);
                offsets.resize(level);
                const auto indent=level?offsets.back():0;
                const auto number=counters.Next(level,block.ordered);
                const auto marker=block.ordered?std::to_string(number)+". ":std::string("- ");
                const std::string pad(indent,' ');
                const auto body=MarkdownText(block.runs,Mode::block,pad+std::string(marker.size(),' '));
                if(body.empty())break;
                offsets.push_back(indent+marker.size());
                rendered=pad+marker+body;
                break;
            }
            case BlockKind::table:
                if(Rectangular(block.rows)) {
                    const auto row=[&](const Row& cells) {
                        std::string line="|";
                        for(const auto& cell:cells)line+=" "+MarkdownText(cell,Mode::singleLine)+" |";
                        return line;
                    };
                    rendered=row(block.rows.front());
                    rendered+="\n|";
                    for(std::size_t i=0;i<block.rows.front().size();++i)rendered+=" --- |";
                    for(std::size_t r=1;r<block.rows.size();++r)rendered+="\n"+row(block.rows[r]);
                } else {
                    // Non-rectangular (merged or ragged) tables degrade to one paragraph per row.
                    for(const auto& cells:block.rows) {
                        std::string line;
                        for(const auto& cell:cells) {
                            const auto text=MarkdownText(cell,Mode::singleLine);
                            if(text.empty())continue;
                            if(!line.empty())line+=" \\| ";
                            line+=text;
                        }
                        if(line.empty())continue;
                        if(!rendered.empty())rendered+="\n\n";
                        rendered+=line;
                    }
                }
                break;
        }
        if(rendered.empty())continue;
        if(!list) {counters.Reset();offsets.clear();}
        // Trailing spaces are insignificant everywhere except inside code.
        if(block.kind!=BlockKind::code_block)StripTrailingSpace(rendered);
        if(pendingBreak) {out.Put("\n\n---");pendingBreak=false;previousList=previousQuote=false;}
        if(!first)out.Put(list&&previousList?"\n":quote&&previousQuote?"\n>\n":"\n\n");
        first=false;previousList=list;previousQuote=quote;
        out.Put(rendered);
    }
    if(!first)out.Put("\n");
    return std::move(out.Text());
}
}
