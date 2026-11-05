#include "html_tree.hpp"
#include <functional>
#include <initializer_list>

// HTML element tree (html_tree.hpp) -> CommonMark + GFM (tables, strikethrough).
// The mapping never emits raw HTML: every literal character that could be read
// as Markdown syntax is backslash-escaped, and only http/https/mailto links and
// http/https images become Markdown links and images.
namespace arken::markup::v1::detail {
std::string HtmlToMarkdownText(Bytes bytes,const HtmlToMarkdownOptions& options,HtmlToMarkdownReport& report);

namespace {
using html::Node;
using html::Tree;

bool In(std::string_view name,std::initializer_list<std::string_view> set) {
    for(const auto s:set)if(s==name)return true;
    return false;
}
bool IsSpace(char c) {return c==' '||c=='\t'||c=='\n'||c=='\f'||c=='\r';}
bool Blank(std::string_view s) {
    for(const char c:s)if(!IsSpace(c))return false;
    return true;
}

struct Style {
    bool bold=false,italic=false,strike=false,code=false;
    std::string link;  // Admitted href, or empty.
    bool SameFormat(const Style& o) const {return bold==o.bold&&italic==o.italic&&strike==o.strike&&code==o.code&&link==o.link;}
};
enum class RunKind : std::uint8_t {text,lineBreak,image};
struct Run {
    RunKind kind=RunKind::text;
    std::string text;   // Text, or image alt.
    Style style;
    std::string src;    // Image source.
};
enum class InlineMode : std::uint8_t {paragraph,heading,cell};
enum class BlockKind : std::uint8_t {paragraph,list,other};
struct Block {
    BlockKind kind=BlockKind::paragraph;
    std::string text;
    bool ordered=false,alternate=false;
    bool interruptsParagraph=true;  // False for an ordered list not starting at 1.
};
struct Container {
    InlineMode mode=InlineMode::paragraph;
    std::vector<Block> blocks;
    std::vector<Run> runs;
    bool spacePending=false;
};

// Percent-encodes everything outside the URI reserved/unreserved sets, plus
// parentheses and angle brackets, so the destination never ends early.
std::string Destination(std::string_view url) {
    static constexpr std::string_view safe="-._~:/?#[]@!$&'*+,;=%";
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    for(const char c:url) {
        const auto u=std::uint8_t(c);
        if(IsAsciiAlnum(c)||(u<0x80&&safe.find(c)!=std::string_view::npos)) {out.push_back(c);continue;}
        out.push_back('%');out.push_back(hex[u>>4]);out.push_back(hex[u&15]);
    }
    return out;
}

// Backslash-escapes literal text. `lineStart` is true when the text begins a
// Markdown line, where block markers must also be neutralized.
std::string Escape(std::string_view text,bool& lineStart,InlineMode mode) {
    std::string out;
    std::size_t i=0;
    if(lineStart&&!text.empty()) {
        if(text[0]=='#'||text[0]=='>'||text[0]=='-'||text[0]=='+'||text[0]=='=') {
            out.push_back('\\');
        } else if(IsAsciiDigit(text[0])) {
            std::size_t d=0;
            while(d<text.size()&&IsAsciiDigit(text[d]))++d;
            if(d<text.size()&&(text[d]=='.'||text[d]==')')) {
                out.append(text.substr(0,d));out.push_back('\\');i=d;
            }
        }
    }
    if(!text.empty())lineStart=false;
    for(;i<text.size();++i) {
        const char c=text[i];
        const bool always=c=='\\'||c=='`'||c=='*'||c=='_'||c=='['||c==']'||c=='<'||c=='|'||c=='~';
        const bool entity=c=='&'&&i+1<text.size()&&(IsAsciiAlnum(text[i+1])||text[i+1]=='#');
        const bool heading=c=='#'&&mode==InlineMode::heading;
        if(always||entity||heading)out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string CodeSpan(std::string_view text,InlineMode mode) {
    std::size_t longest=0,run=0;
    for(const char c:text) {
        run=c=='`'?run+1:0;
        longest=std::max(longest,run);
    }
    std::string body(text);
    if(mode==InlineMode::cell) {
        std::string escaped;
        for(const char c:body) {if(c=='|')escaped.push_back('\\');escaped.push_back(c);}
        body=std::move(escaped);
    }
    const std::string fence(longest+1,'`');
    const bool pad=!body.empty()&&(body.front()=='`'||body.back()=='`'||(body.front()==' '&&body.back()==' '&&!Blank(body)));
    return fence+(pad?" ":"")+body+(pad?" ":"")+fence;
}

class InlineRenderer {
public:
    explicit InlineRenderer(InlineMode mode):mode_(mode) {}
    std::string Render(std::vector<Run> runs) {
        Trim(runs);
        for(std::size_t i=0;i<runs.size();) {
            auto j=i+1;
            while(j<runs.size()&&runs[j].style.link==runs[i].style.link)++j;
            Group(std::span<const Run>(runs.data()+i,j-i),runs[i].style.link);
            i=j;
        }
        return std::move(out_);
    }
private:
    // Leading/trailing whitespace of the block and around line breaks is not content.
    static void Trim(std::vector<Run>& runs) {
        const auto lead=[](Run& r) {
            const auto n=std::size_t(std::find_if_not(r.text.begin(),r.text.end(),IsSpace)-r.text.begin());
            r.text.erase(0,n);
        };
        const auto trail=[](Run& r) {while(!r.text.empty()&&IsSpace(r.text.back()))r.text.pop_back();};
        std::vector<Run> kept;
        bool atStart=true;
        for(auto& r:runs) {
            if(r.kind==RunKind::lineBreak) {
                while(!kept.empty()&&kept.back().kind==RunKind::text) {
                    trail(kept.back());
                    if(!kept.back().text.empty())break;
                    kept.pop_back();
                }
                if(kept.empty())continue;  // A break before any content is not a line.
                kept.push_back(std::move(r));
                atStart=true;
                continue;
            }
            if(r.kind==RunKind::text&&atStart) {
                lead(r);
                if(r.text.empty())continue;
            }
            atStart=false;
            kept.push_back(std::move(r));
        }
        while(!kept.empty()) {
            auto& r=kept.back();
            if(r.kind==RunKind::lineBreak) {kept.pop_back();continue;}
            if(r.kind==RunKind::text) {
                trail(r);
                if(r.text.empty()) {kept.pop_back();continue;}
            }
            break;
        }
        runs=std::move(kept);
    }
    void Group(std::span<const Run> group,const std::string& link) {
        if(link.empty()) {Formatted(group);return;}
        // Whitespace at the group's edges stays outside the link brackets.
        std::vector<Run> inner(group.begin(),group.end());
        std::string lead,trail;
        if(!inner.empty()&&inner.front().kind==RunKind::text) {
            auto& t=inner.front().text;
            while(!t.empty()&&IsSpace(t.front())) {lead=" ";t.erase(0,1);}
        }
        if(!inner.empty()&&inner.back().kind==RunKind::text) {
            auto& t=inner.back().text;
            while(!t.empty()&&IsSpace(t.back())) {trail=" ";t.pop_back();}
        }
        Put(lead);
        bool empty=true;
        for(const auto& r:inner)if(r.kind==RunKind::image||(r.kind==RunKind::text&&!Blank(r.text)))empty=false;
        Put("[");
        if(empty) {
            Put(Escape(link,lineStart_,mode_));
        } else {
            Formatted(inner);
        }
        Put("](");Put(Destination(link));Put(")");
        Put(trail);
    }
    void Put(std::string_view s) {
        if(s.empty())return;
        out_.append(s);
        lineStart_=false;
    }
    void Close(std::size_t keep) {
        while(open_.size()>keep) {
            Put(Delimiter(open_.back()));
            open_.pop_back();
        }
    }
    static std::string_view Delimiter(char kind) {return kind=='b'?"**":kind=='i'?"*":"~~";}
    void Transition(const Style& s) {
        const auto wanted=[&](char k) {return k=='b'?s.bold:k=='i'?s.italic:s.strike;};
        std::size_t keep=0;
        while(keep<open_.size()&&wanted(open_[keep]))++keep;
        Close(keep);
        Put(pendingSpace_);
        pendingSpace_.clear();
        for(const char k:{'s','b','i'}) {
            if(wanted(k)&&std::find(open_.begin(),open_.end(),k)==open_.end()) {
                open_.push_back(k);
                Put(Delimiter(k));
            }
        }
    }
    void Formatted(std::span<const Run> runs) {
        for(const auto& r:runs) {
            if(r.kind==RunKind::lineBreak) {
                Close(0);
                pendingSpace_.clear();
                if(mode_==InlineMode::paragraph) {out_+="\\\n";lineStart_=true;} else {Put(" ");}
                continue;
            }
            if(r.kind==RunKind::image) {
                Transition(r.style);
                Put("![");
                Put(Escape(r.text,lineStart_,mode_));
                Put("](");Put(Destination(r.src));Put(")");
                continue;
            }
            std::string_view t=r.text;
            std::size_t a=0,b=t.size();
            while(a<b&&IsSpace(t[a]))++a;
            while(b>a&&IsSpace(t[b-1]))--b;
            if(a==b) {if(!t.empty())pendingSpace_=" ";continue;}
            if(a>0)pendingSpace_=" ";
            Transition(r.style);
            if(r.style.code) {
                Put(CodeSpan(t.substr(a,b-a),mode_));
            } else {
                auto escaped=Escape(t.substr(a,b-a),lineStart_,mode_);
                out_+=escaped;
            }
            if(b<t.size())pendingSpace_=" ";
        }
        Close(0);
        Put(pendingSpace_);
        pendingSpace_.clear();
    }

    InlineMode mode_;
    std::string out_;
    std::vector<char> open_;
    std::string pendingSpace_;
    bool lineStart_=true;
};

std::string Indent(std::string_view text,std::size_t width,std::string_view first) {
    std::string out(first);
    const std::string pad(width,' ');
    bool atLineStart=false;
    for(std::size_t i=0;i<text.size();++i) {
        if(atLineStart&&text[i]!='\n')out+=pad;
        out.push_back(text[i]);
        atLineStart=text[i]=='\n';
    }
    return out;
}
std::string Prefix(std::string_view text,std::string_view prefix,std::string_view emptyPrefix) {
    std::string out;
    std::size_t start=0;
    while(start<=text.size()) {
        auto end=text.find('\n',start);
        if(end==std::string_view::npos)end=text.size();
        const auto line=text.substr(start,end-start);
        out+=line.empty()?emptyPrefix:prefix;
        out+=line;
        if(end==text.size())break;
        out.push_back('\n');
        start=end+1;
    }
    return out;
}
std::string Join(const std::vector<Block>& blocks,std::string_view separator) {
    std::string out;
    for(std::size_t i=0;i<blocks.size();++i) {
        if(i)out+=separator;
        out+=blocks[i].text;
    }
    return out;
}

// As the URL parser does: strip leading/trailing spaces and remove every TAB, LF and CR.
std::string UrlValue(std::string_view raw) {
    std::string out;
    for(const char c:TrimAsciiSpace(raw))if(c!='\t'&&c!='\n'&&c!='\r')out.push_back(c);
    return out;
}

class Mapper {
public:
    Mapper(const Tree& tree,const HtmlToMarkdownOptions& options,HtmlToMarkdownReport& report)
        :tree_(tree),options_(options),report_(report) {}
    std::string Convert() {
        Container root;
        Walk(0,Style{},root);
        Flush(root);
        auto out=Join(root.blocks,"\n\n");
        if(!out.empty())out.push_back('\n');
        return out;
    }

private:
    const Node& At(std::uint32_t id) const {return tree_.nodes[id];}
    void Walk(std::uint32_t id,const Style& style,Container& c) {
        for(const auto child:At(id).children)Child(child,style,c);
    }
    void AppendText(Container& c,std::string_view text,const Style& style) {
        std::string collapsed;
        for(const char ch:text) {
            if(IsSpace(ch)) {c.spacePending=true;continue;}
            if(c.spacePending) {
                if(!c.runs.empty()&&c.runs.back().kind!=RunKind::lineBreak)collapsed.push_back(' ');
                else if(!collapsed.empty())collapsed.push_back(' ');
                c.spacePending=false;
            }
            collapsed.push_back(ch);
        }
        if(collapsed.empty())return;
        if(!c.runs.empty()&&c.runs.back().kind==RunKind::text&&c.runs.back().style.SameFormat(style)) {
            c.runs.back().text+=collapsed;
        } else {
            c.runs.push_back(Run{RunKind::text,std::move(collapsed),style,{}});
        }
    }
    void Flush(Container& c) {
        c.spacePending=false;
        if(c.runs.empty())return;
        auto text=InlineRenderer(c.mode).Render(std::move(c.runs));
        c.runs.clear();
        if(!text.empty())c.blocks.push_back(Block{BlockKind::paragraph,std::move(text)});
    }
    void Push(Container& c,Block&& block) {
        Flush(c);
        if(!block.text.empty())c.blocks.push_back(std::move(block));
    }
    // Inline content of `id` flattened onto one line (headings, cells).
    std::string Line(std::uint32_t id,const Style& style,InlineMode mode,bool* complex=nullptr) {
        Container c;c.mode=mode;
        Walk(id,style,c);
        Flush(c);
        if(complex)*complex=c.blocks.size()>1||(c.blocks.size()==1&&c.blocks[0].kind!=BlockKind::paragraph);
        std::string out;
        for(const auto& b:c.blocks) {
            if(b.text.empty())continue;
            if(!out.empty())out.push_back(' ');
            for(const char ch:b.text)out.push_back(ch=='\n'?' ':ch);
        }
        return out;
    }

    bool Hidden(const Node& n) const {
        if(n.Attr("hidden"))return true;
        if(const auto v=n.Attr("aria-hidden");v&&EqualsNoCase(TrimAsciiSpace(*v),"true"))return true;
        if(const auto v=n.Attr("style")) {
            std::string compact;
            for(const char ch:*v)if(!IsSpace(ch))compact.push_back(AsciiLower(ch));
            if(compact.find("display:none")!=std::string::npos||compact.find("visibility:hidden")!=std::string::npos)return true;
        }
        return false;
    }

    void Child(std::uint32_t id,const Style& style,Container& c) {
        const auto& n=At(id);
        if(n.text) {AppendText(c,n.data,style);return;}
        const auto& name=n.name;
        if(In(name,{"script","style","noscript","template"})) {++report_.scriptStyleDropped;return;}
        if(In(name,{"iframe","object","embed","applet","svg","math","canvas","audio","video","title","noembed","noframes",
            "frame","map","head","link","meta","base"})) {
            if(!In(name,{"link","meta","base"}))++report_.embeddedDropped;
            return;
        }
        if(Hidden(n)) {++report_.hiddenDropped;return;}
        if(options_.dropNavigation&&In(name,{"nav","aside","header","footer"})) {++report_.navigationDropped;return;}
        if(name=="br") {
            c.spacePending=false;
            c.runs.push_back(Run{RunKind::lineBreak,{},style,{}});
            return;
        }
        if(name=="img") {Image(n,style,c);return;}
        if(name=="hr") {Push(c,Block{BlockKind::other,"---"});return;}
        if(name.size()==2&&name[0]=='h'&&name[1]>='1'&&name[1]<='6') {
            Flush(c);
            auto text=Line(id,style,InlineMode::heading);
            if(!text.empty())Push(c,Block{BlockKind::other,std::string(std::size_t(name[1]-'0'),'#')+" "+text});
            return;
        }
        if(In(name,{"ul","ol","menu","dir"})) {List(id,style,c,name=="ol");return;}
        if(name=="dl") {DefinitionList(id,style,c);return;}
        if(name=="blockquote") {
            Flush(c);
            Container q;
            Walk(id,style,q);
            Flush(q);
            const auto text=Join(q.blocks,"\n\n");
            if(!text.empty())Push(c,Block{BlockKind::other,Prefix(text,"> ",">")});
            return;
        }
        if(In(name,{"pre","listing","xmp","plaintext"})) {Pre(id,c);return;}
        if(name=="table") {Table(id,style,c);return;}
        if(In(name,{"p","div","section","article","main","header","footer","nav","aside","address","center","figure",
            "figcaption","form","fieldset","details","summary","hgroup","legend","option","optgroup","select","datalist",
            "caption","search","dialog","li","dd","dt","tr","td","th","thead","tbody","tfoot"})) {
            Flush(c);
            Walk(id,style,c);
            Flush(c);
            return;
        }
        Style inner=style;
        if(In(name,{"b","strong"}))inner.bold=true;
        else if(In(name,{"i","em"}))inner.italic=true;
        else if(In(name,{"s","strike","del"}))inner.strike=true;
        else if(In(name,{"code","kbd","samp","tt"}))inner.code=true;
        else if(name=="a") {
            if(const auto href=n.Attr("href")) {
                const auto target=UrlValue(*href);
                if(AbsoluteAllowed(target,true)) {inner.link=target;} else {++report_.linksAsText;}
            }
        }
        Walk(id,inner,c);
    }

    void Image(const Node& n,const Style& style,Container& c) {
        std::string alt;
        if(const auto a=n.Attr("alt")) {
            bool space=false;
            for(const char ch:*a) {
                if(IsSpace(ch)) {space=true;continue;}
                if(space&&!alt.empty())alt.push_back(' ');
                space=false;
                alt.push_back(ch);
            }
        }
        const auto src=n.Attr("src");
        const auto target=src?UrlValue(*src):std::string();
        if(src&&AbsoluteAllowed(target,false)) {
            if(c.spacePending&&!c.runs.empty()&&c.runs.back().kind!=RunKind::lineBreak)
                c.runs.push_back(Run{RunKind::text," ",style,{}});
            c.spacePending=false;
            c.runs.push_back(Run{RunKind::image,alt,style,target});
            return;
        }
        if(alt.empty()) {++report_.imagesDropped;return;}
        ++report_.imagesAsText;
        Style italic=style;
        italic.italic=true;
        AppendText(c,alt,italic);
    }

    void Pre(std::uint32_t id,Container& c) {
        Flush(c);
        std::string text,language;
        const auto take=[&](const Node& n) {
            if(!language.empty())return;
            if(const auto cls=n.Attr("class")) {
                std::size_t p=0;
                while(p<cls->size()) {
                    while(p<cls->size()&&IsSpace((*cls)[p]))++p;
                    auto e=p;
                    while(e<cls->size()&&!IsSpace((*cls)[e]))++e;
                    std::string_view token(cls->data()+p,e-p);
                    for(const std::string_view prefix:{std::string_view("language-"),std::string_view("lang-")}) {
                        if(language.empty()&&token.size()>prefix.size()&&token.substr(0,prefix.size())==prefix) {
                            for(const char ch:token.substr(prefix.size()))
                                if(IsAsciiAlnum(ch)||ch=='_'||ch=='+'||ch=='.'||ch=='#'||ch=='-')language.push_back(ch);
                        }
                    }
                    p=e;
                }
            }
        };
        take(At(id));
        Gather(id,text,take);
        if(!text.empty()&&text.back()=='\n')text.pop_back();
        std::size_t longest=0,run=0;
        for(const char ch:text) {run=ch=='`'?run+1:0;longest=std::max(longest,run);}
        const std::string fence(std::max<std::size_t>(3,longest+1),'`');
        Push(c,Block{BlockKind::other,fence+language+"\n"+text+(text.empty()?"":"\n")+fence});
    }
    template<class Take> void Gather(std::uint32_t id,std::string& text,Take& take) {
        for(const auto child:At(id).children) {
            const auto& n=At(child);
            if(n.text) {text+=n.data;continue;}
            if(In(n.name,{"script","style","noscript","template"})) {++report_.scriptStyleDropped;continue;}
            if(Hidden(n)) {++report_.hiddenDropped;continue;}
            if(n.name=="br") {text.push_back('\n');continue;}
            if(n.name=="code")take(n);
            Gather(child,text,take);
        }
    }

    void List(std::uint32_t id,const Style& style,Container& c,bool ordered) {
        Flush(c);
        const auto& n=At(id);
        std::uint64_t start=1;
        if(ordered) {
            if(const auto s=n.Attr("start")) {
                const auto v=TrimAsciiSpace(*s);
                if(!v.empty()&&v.size()<=9&&std::all_of(v.begin(),v.end(),IsAsciiDigit))start=std::stoull(std::string(v));
            }
        }
        const bool alternate=!c.blocks.empty()&&c.blocks.back().kind==BlockKind::list
            &&c.blocks.back().ordered==ordered&&!c.blocks.back().alternate;
        // Items: each li, plus stray content between items as its own item.
        std::vector<std::vector<Block>> items;
        Container stray;
        const auto flushStray=[&] {
            Flush(stray);
            if(!stray.blocks.empty())items.push_back(std::move(stray.blocks));
            stray=Container{};
        };
        for(const auto child:n.children) {
            const auto& item=At(child);
            if(!item.text&&item.name=="li") {
                flushStray();
                if(Hidden(item)) {++report_.hiddenDropped;continue;}
                Container ic;
                Walk(child,style,ic);
                Flush(ic);
                items.push_back(std::move(ic.blocks));
                continue;
            }
            if(item.text&&Blank(item.data))continue;
            Child(child,style,stray);
        }
        flushStray();
        if(items.empty())return;
        bool tight=true;
        for(const auto& blocks:items) {
            for(std::size_t i=0;i<blocks.size();++i) {
                if(blocks[i].kind==BlockKind::list) {
                    if(i>0&&!blocks[i].interruptsParagraph)tight=false;
                } else if(i>0) {
                    tight=false;
                }
            }
        }
        std::string text;
        for(std::size_t i=0;i<items.size();++i) {
            std::string marker=ordered?std::to_string(start+i)+(alternate?")":"."):std::string(alternate?"*":"-");
            const auto width=marker.size()+1;
            std::string body;
            const auto& blocks=items[i];
            for(std::size_t b=0;b<blocks.size();++b) {
                if(b)body+=tight?"\n":"\n\n";
                body+=blocks[b].text;
            }
            if(i)text+=tight?"\n":"\n\n";
            text+=body.empty()?marker:Indent(body,width,marker+" ");
        }
        Block block{BlockKind::list,std::move(text)};
        block.ordered=ordered;
        block.alternate=alternate;
        block.interruptsParagraph=!ordered||start==1;
        Push(c,std::move(block));
    }

    void DefinitionList(std::uint32_t id,const Style& style,Container& c) {
        Flush(c);
        for(const auto child:At(id).children) {
            const auto& n=At(child);
            if(n.text) {AppendText(c,n.data,style);continue;}
            if(n.name=="dt"&&!Hidden(n)) {
                Flush(c);
                Style bold=style;bold.bold=true;
                const auto term=Line(child,bold,InlineMode::paragraph);
                if(!term.empty())Push(c,Block{BlockKind::other,term});
                continue;
            }
            if(n.name=="dd"&&!Hidden(n)) {
                Flush(c);
                Container d;
                Walk(child,style,d);
                Flush(d);
                const auto text=Join(d.blocks,"\n\n");
                if(!text.empty())Push(c,Block{BlockKind::other,Prefix(text,"  ","")});
                continue;
            }
            Child(child,style,c);
        }
        Flush(c);
    }

    void Table(std::uint32_t id,const Style& style,Container& c) {
        Flush(c);
        std::vector<std::vector<std::uint32_t>> rows;
        std::vector<std::uint32_t> captions;
        bool simple=true;
        const auto visitRow=[&](std::uint32_t rowId) {
            std::vector<std::uint32_t> cells;
            for(const auto child:At(rowId).children) {
                const auto& n=At(child);
                if(n.text) {if(!Blank(n.data))simple=false;continue;}
                if(n.name=="td"||n.name=="th") {
                    if(Hidden(n)) {++report_.hiddenDropped;continue;}
                    for(const auto attr:{"colspan","rowspan"}) {
                        if(const auto v=n.Attr(attr)) {
                            const auto t=TrimAsciiSpace(*v);
                            if(!t.empty()&&t!="1"&&t!="0")simple=false;
                        }
                    }
                    cells.push_back(child);
                    continue;
                }
                simple=false;
            }
            rows.push_back(std::move(cells));
        };
        const auto visitSection=[&](std::uint32_t sectionId) {
            for(const auto child:At(sectionId).children) {
                const auto& n=At(child);
                if(n.text) {if(!Blank(n.data))simple=false;continue;}
                if(n.name=="tr") {visitRow(child);continue;}
                simple=false;
            }
        };
        for(const auto child:At(id).children) {
            const auto& n=At(child);
            if(n.text) {if(!Blank(n.data))simple=false;continue;}
            if(In(n.name,{"thead","tbody","tfoot"})) {visitSection(child);continue;}
            if(n.name=="tr") {visitRow(child);continue;}
            if(n.name=="caption") {captions.push_back(child);continue;}
            if(n.name=="colgroup"||n.name=="col")continue;
            simple=false;
        }
        for(const auto caption:captions)Child(caption,style,c);
        std::vector<std::vector<std::string>> text;
        if(simple&&!rows.empty()) {
            for(const auto& row:rows) {
                if(row.empty()||row.size()!=rows[0].size()) {simple=false;break;}
                std::vector<std::string> cells;
                for(const auto cell:row) {
                    bool complex=false;
                    cells.push_back(Line(cell,style,InlineMode::cell,&complex));
                    if(complex)simple=false;
                }
                text.push_back(std::move(cells));
            }
        }
        if(simple&&!rows.empty()) {
            std::string out;
            const auto line=[&](const std::vector<std::string>& cells) {
                out+="|";
                for(const auto& cell:cells) {out+=" ";out+=cell;out+=cell.empty()?"|":" |";}
            };
            line(text[0]);
            out+="\n|";
            for(std::size_t i=0;i<text[0].size();++i)out+=" --- |";
            for(std::size_t r=1;r<text.size();++r) {out+="\n";line(text[r]);}
            Push(c,Block{BlockKind::other,std::move(out)});
            return;
        }
        if(rows.empty()&&simple)return;
        ++report_.tablesAsParagraphs;
        // Fallback: every cell (and any stray table content) in document order.
        const std::function<void(std::uint32_t)> fallback=[&](std::uint32_t node) {
            for(const auto child:At(node).children) {
                const auto& n=At(child);
                if(n.text) {AppendText(c,n.data,style);continue;}
                if(n.name=="caption")continue;
                if(In(n.name,{"thead","tbody","tfoot","tr"})) {Flush(c);fallback(child);Flush(c);continue;}
                if(n.name=="td"||n.name=="th") {
                    if(Hidden(n)) {++report_.hiddenDropped;continue;}
                    Flush(c);Walk(child,style,c);Flush(c);continue;
                }
                Child(child,style,c);
            }
        };
        fallback(id);
        Flush(c);
    }

    const Tree& tree_;
    const HtmlToMarkdownOptions& options_;
    HtmlToMarkdownReport& report_;
};
}

std::string HtmlToMarkdownText(Bytes bytes,const HtmlToMarkdownOptions& options,HtmlToMarkdownReport& report) {
    html::TreeStats stats;
    const auto tree=html::ParseHtml(bytes,{options.maximumDepth,options.maximumNodes,options.maximumAttributes},stats);
    report.commentsSkipped=stats.commentsSkipped;
    report.unknownEntities=stats.unknownEntities;
    auto markdown=Mapper(tree,options,report).Convert();
    Output out(options.maximumOutputBytes);
    out.Put(markdown);
    return out.Take();
}
}
