#include "detail.hpp"
#include <map>
#include <memory>

// CommonMark subset -> HTML. The block parser follows the CommonMark
// reference algorithm (commonmark.js blocks.js: open-block continuation, lazy
// paragraph continuation, column-accurate tab handling); the inline parser
// follows its delimiter-stack algorithm (inlines.js: left/right-flanking runs,
// the rule of three, openers_bottom, bracket stack with link deactivation).
// Supported and unsupported constructs are listed in NOTES.md.
namespace arken::markup::v1::detail {
std::string MarkdownToHtmlText(Bytes bytes,const MarkdownToHtmlOptions& options,MarkdownToHtmlReport& report);

namespace {
bool SpaceOrTab(int c) {return c==' '||c=='\t';}

std::string EscapeHtml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for(const char c:s) {
        switch(c) {
            case '&': out+="&amp;";break;
            case '<': out+="&lt;";break;
            case '>': out+="&gt;";break;
            case '"': out+="&quot;";break;
            default: out.push_back(c);
        }
    }
    return out;
}

char32_t MarkdownNumeric(std::uint64_t v) {
    if(v==0||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0xfffd;
    return char32_t(v);
}
// Decodes an entity or numeric reference at s[p]=='&'. Returns the length
// consumed, or 0 when there is none. `unknown` counts &name; outside the table.
std::size_t Entity(std::string_view s,std::size_t p,std::string& out,std::uint64_t* unknown) {
    auto q=p+1;
    if(q<s.size()&&s[q]=='#') {
        ++q;
        const bool hex=q<s.size()&&(s[q]=='x'||s[q]=='X');
        if(hex)++q;
        const auto start=q;
        std::uint64_t v=0;
        while(q<s.size()&&(hex?IsHexDigit(s[q]):IsAsciiDigit(s[q]))&&q-start<(hex?6u:7u)) {
            const char d=s[q];
            v=v*(hex?16:10)+(IsAsciiDigit(d)?unsigned(d-'0'):unsigned(AsciiLower(d)-'a'+10));
            ++q;
        }
        if(q==start||q>=s.size()||s[q]!=';')return 0;
        AppendUtf8(out,MarkdownNumeric(v));
        return q+1-p;
    }
    if(q>=s.size()||!IsAsciiAlpha(s[q]))return 0;
    auto e=q+1;
    while(e<s.size()&&IsAsciiAlnum(s[e])&&e-q<32)++e;
    if(e>=s.size()||s[e]!=';'||e-q<2)return 0;
    const auto v=LookupEntity(s.substr(q,e-q));
    if(!v) {if(unknown)++*unknown;return 0;}
    AppendUtf8(out,v);
    return e+1-p;
}
// Backslash escapes and references, as in link destinations, titles and info strings.
std::string Unescape(std::string_view s) {
    std::string out;
    for(std::size_t i=0;i<s.size();) {
        if(s[i]=='\\'&&i+1<s.size()&&IsAsciiPunctuation(s[i+1])) {out.push_back(s[i+1]);i+=2;continue;}
        if(s[i]=='&') {
            if(const auto n=Entity(s,i,out,nullptr)) {i+=n;continue;}
        }
        out.push_back(s[i++]);
    }
    return out;
}
// Percent-encodes characters outside the URI set, keeping valid %XX escapes.
std::string NormalizeUri(std::string_view s) {
    static constexpr std::string_view safe=";/?:@&=+$,-_.!~*'()#";
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    for(std::size_t i=0;i<s.size();++i) {
        const char c=s[i];
        const auto u=std::uint8_t(c);
        if(c=='%'&&i+2<s.size()&&IsHexDigit(s[i+1])&&IsHexDigit(s[i+2])) {out.append(s.substr(i,3));i+=2;continue;}
        if(IsAsciiAlnum(c)||(u<0x80&&safe.find(c)!=std::string_view::npos)) {out.push_back(c);continue;}
        out.push_back('%');out.push_back(hex[u>>4]);out.push_back(hex[u&15]);
    }
    return out;
}
// href/src policy: http, https (and mailto for links) when a scheme is present;
// otherwise a relative reference that is not scheme-relative ("//host").
bool SafeUrl(std::string_view url,bool allowMailto) {
    std::string_view scheme;
    if(HasScheme(url,scheme)) {
        if(EqualsNoCase(scheme,"http")||EqualsNoCase(scheme,"https"))return true;
        return allowMailto&&EqualsNoCase(scheme,"mailto");
    }
    return !(url.size()>=2&&(url[0]=='/'||url[0]=='\\')&&(url[1]=='/'||url[1]=='\\'));
}

// Unicode classification for delimiter flanking. Whitespace is exactly the
// CommonMark set (Zs plus TAB, LF, FF, CR). Punctuation is ASCII punctuation
// exactly, plus a fixed table of the common non-ASCII punctuation and symbol
// blocks (an approximation of the Unicode P and S categories).
bool UnicodeWhitespace(char32_t c) {
    return c==' '||c=='\t'||c=='\n'||c=='\f'||c=='\r'||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)
        ||c==0x202f||c==0x205f||c==0x3000;
}
bool UnicodePunctuation(char32_t c) {
    if(c<0x80)return IsAsciiPunctuation(char(c));
    static constexpr std::pair<char32_t,char32_t> ranges[]={
        {0xa1,0xa9},{0xab,0xac},{0xae,0xb1},{0xb4,0xb4},{0xb6,0xb8},{0xbb,0xbb},{0xbf,0xbf},{0xd7,0xd7},{0xf7,0xf7},
        {0x2010,0x2027},{0x2030,0x205e},{0x20a0,0x20c0},{0x2100,0x214f},{0x2190,0x23ff},{0x2500,0x27bf},
        {0x27c0,0x27ff},{0x2900,0x2bff},{0x2e00,0x2e7f},{0x3001,0x3003},{0x3008,0x3020},{0x30fb,0x30fb},
        {0xfe10,0xfe19},{0xfe30,0xfe6b},{0xff01,0xff0f},{0xff1a,0xff20},{0xff3b,0xff40},{0xff5b,0xff65}};
    for(const auto& [a,b]:ranges)if(c>=a&&c<=b)return true;
    return false;
}

// ================= Block structure =================
enum class BT : std::uint8_t {document,quote,list,item,paragraph,heading,thematic,code,table};
struct BlockNode {
    BT type=BT::document;
    BlockNode* parent=nullptr;
    std::vector<std::unique_ptr<BlockNode>> children;
    bool open=true;
    std::uint64_t startLine=0,endLine=0;
    // list / item
    bool ordered=false;
    char bulletChar=0,delimiter=0;
    std::uint32_t start=1;
    int markerOffset=0,padding=0;
    bool tight=true;
    // heading
    int level=0;
    // code
    bool fenced=false;
    char fenceChar=0;
    int fenceLength=0,fenceOffset=0;
    std::string info;
    std::string content;
    // table
    std::vector<char> align;   // 0 none, 'l', 'c', 'r'
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};
bool AcceptsLines(BT t) {return t==BT::paragraph||t==BT::code||t==BT::table;}
bool CanContain(BT parent,BT child) {
    switch(parent) {
        case BT::document: case BT::quote: case BT::item: return child!=BT::item;
        case BT::list: return child==BT::item;
        default: return false;
    }
}

// GFM table row: optional leading/trailing pipe, cells split on unescaped '|'.
std::vector<std::string> SplitRow(std::string_view line) {
    auto t=TrimAsciiSpace(line);
    if(!t.empty()&&t.front()=='|')t.remove_prefix(1);
    if(!t.empty()&&t.back()=='|'&&!(t.size()>=2&&t[t.size()-2]=='\\'))t.remove_suffix(1);
    std::vector<std::string> cells;
    std::string cell;
    for(std::size_t i=0;i<t.size();++i) {
        if(t[i]=='\\'&&i+1<t.size()&&t[i+1]=='|') {cell.push_back('|');++i;continue;}
        if(t[i]=='\\'&&i+1<t.size()) {cell.push_back('\\');cell.push_back(t[++i]);continue;}
        if(t[i]=='|') {cells.emplace_back(TrimAsciiSpace(cell));cell.clear();continue;}
        cell.push_back(t[i]);
    }
    cells.emplace_back(TrimAsciiSpace(cell));
    return cells;
}
bool DelimiterRow(std::string_view line,std::vector<char>& align) {
    if(line.find('|')==std::string_view::npos)return false;
    const auto cells=SplitRow(line);
    align.clear();
    for(const auto& c:cells) {
        std::string_view v=c;
        if(v.empty())return false;
        const bool left=v.front()==':',right=v.back()==':';
        if(left)v.remove_prefix(1);
        if(right&&!v.empty())v.remove_suffix(1);
        if(v.empty()||v.find_first_not_of('-')!=std::string_view::npos)return false;
        align.push_back(left&&right?'c':left?'l':right?'r':0);
    }
    return !align.empty();
}

class BlockParser {
public:
    BlockParser(const MarkdownToHtmlOptions& options):options_(options) {
        doc_=std::make_unique<BlockNode>();
        doc_->startLine=1;
        tip_=doc_.get();
        oldtip_=tip_;
        lastMatched_=tip_;
    }
    std::unique_ptr<BlockNode> Parse(std::string_view text) {
        std::vector<std::string_view> lines;
        for(std::size_t start=0;start<text.size();) {
            auto end=text.find('\n',start);
            if(end==std::string_view::npos)end=text.size();
            lines.push_back(text.substr(start,end-start));
            start=end+1;
        }
        for(const auto l:lines)Incorporate(l);
        while(tip_)Finalize(tip_,lines.size());
        return std::move(doc_);
    }
private:
    int Peek(std::size_t p) const {return p<line_.size()?static_cast<unsigned char>(line_[p]):-1;}
    void FindNextNonspace() {
        auto i=offset_;
        auto cols=column_;
        int c=-1;
        while((c=Peek(i))!=-1) {
            if(c==' ') {++i;++cols;}
            else if(c=='\t') {++i;cols+=4-(cols%4);}
            else break;
        }
        blank_=c==-1;
        nextNonspace_=i;
        nextNonspaceColumn_=cols;
        indent_=nextNonspaceColumn_-column_;
        indented_=indent_>=4;
    }
    void AdvanceNextNonspace() {
        offset_=nextNonspace_;
        column_=nextNonspaceColumn_;
        partiallyConsumedTab_=false;
    }
    void AdvanceOffset(std::size_t count,bool columns) {
        int c=-1;
        while(count>0&&(c=Peek(offset_))!=-1) {
            if(c=='\t') {
                const std::size_t charsToTab=4-(column_%4);
                if(columns) {
                    partiallyConsumedTab_=charsToTab>count;
                    const auto advance=charsToTab>count?count:charsToTab;
                    column_+=advance;
                    offset_+=partiallyConsumedTab_?0:1;
                    count-=advance;
                } else {
                    partiallyConsumedTab_=false;
                    column_+=charsToTab;
                    offset_+=1;
                    count-=1;
                }
            } else {
                partiallyConsumedTab_=false;
                ++offset_;++column_;--count;
            }
        }
    }
    void AddLine() {
        if(partiallyConsumedTab_) {
            ++offset_;
            const auto charsToTab=4-(column_%4);
            tip_->content.append(charsToTab,' ');
        }
        if(tip_->type==BT::table) {
            const auto rest=line_.substr(std::min(offset_,line_.size()));
            if(!TrimAsciiSpace(rest).empty())tip_->rows.push_back(SplitRow(rest));
            return;
        }
        tip_->content.append(line_.substr(std::min(offset_,line_.size())));
        tip_->content.push_back('\n');
    }
    BlockNode* AddChild(BT type) {
        while(!CanContain(tip_->type,type))Finalize(tip_,lineNumber_-1);
        if(type==BT::quote||type==BT::item) {
            std::uint32_t depth=1;
            for(auto* b=tip_;b;b=b->parent)if(b->type==BT::quote||b->type==BT::item)++depth;
            if(depth>options_.maximumNesting)Fail(Error::depth_limit);
        }
        auto block=std::make_unique<BlockNode>();
        block->type=type;
        block->parent=tip_;
        block->startLine=lineNumber_;
        auto* raw=block.get();
        tip_->children.push_back(std::move(block));
        tip_=raw;
        return raw;
    }
    void CloseUnmatchedBlocks() {
        if(allClosed_)return;
        while(oldtip_!=lastMatched_) {
            auto* parent=oldtip_->parent;
            Finalize(oldtip_,lineNumber_-1);
            oldtip_=parent;
        }
        allClosed_=true;
    }
    void Finalize(BlockNode* block,std::uint64_t line) {
        auto* above=block->parent;
        block->open=false;
        block->endLine=line;
        switch(block->type) {
            case BT::code:
                if(block->fenced) {
                    const auto nl=block->content.find('\n');
                    const auto first=std::string_view(block->content).substr(0,nl);
                    block->info=Unescape(TrimAsciiSpace(first));
                    block->content=nl==std::string::npos?std::string():block->content.substr(nl+1);
                } else {
                    auto& s=block->content;
                    auto end=s.size();
                    for(;;) {
                        auto j=end;
                        while(j>0&&s[j-1]==' ')--j;
                        if(j>0&&s[j-1]=='\n') {end=j-1;continue;}
                        break;
                    }
                    s=s.substr(0,end)+"\n";
                }
                break;
            case BT::item:
                if(!block->children.empty())block->endLine=block->children.back()->endLine;
                else block->endLine=block->startLine;
                break;
            case BT::list: {
                const auto endsWithBlankLine=[](const BlockNode& b,const BlockNode& next) {return b.endLine+1!=next.startLine;};
                const auto& items=block->children;
                for(std::size_t i=0;i<items.size()&&block->tight;++i) {
                    if(i+1<items.size()&&endsWithBlankLine(*items[i],*items[i+1])) {block->tight=false;break;}
                    const auto& sub=items[i]->children;
                    for(std::size_t j=0;j+1<sub.size();++j)
                        if(endsWithBlankLine(*sub[j],*sub[j+1])) {block->tight=false;break;}
                }
                if(!items.empty())block->endLine=items.back()->endLine;
                break;
            }
            default: break;
        }
        tip_=above;
    }

    // Continuation: 0 matched, 1 not matched, 2 line consumed.
    int Continue(BlockNode* c) {
        switch(c->type) {
            case BT::quote:
                if(!indented_&&Peek(nextNonspace_)=='>') {
                    AdvanceNextNonspace();
                    AdvanceOffset(1,false);
                    if(SpaceOrTab(Peek(offset_)))AdvanceOffset(1,true);
                    return 0;
                }
                return 1;
            case BT::item:
                if(blank_) {
                    if(c->children.empty())return 1;
                    AdvanceNextNonspace();
                } else if(indent_>=std::size_t(c->markerOffset+c->padding)) {
                    AdvanceOffset(std::size_t(c->markerOffset+c->padding),true);
                } else {
                    return 1;
                }
                return 0;
            case BT::heading: case BT::thematic: return 1;
            case BT::code:
                if(c->fenced) {
                    if(indent_<=3&&Peek(nextNonspace_)==c->fenceChar) {
                        auto p=nextNonspace_;
                        while(Peek(p)==c->fenceChar)++p;
                        const auto run=p-nextNonspace_;
                        while(SpaceOrTab(Peek(p)))++p;
                        if(Peek(p)==-1&&run>=std::size_t(c->fenceLength)) {
                            Finalize(c,lineNumber_);
                            return 2;
                        }
                    }
                    for(int i=c->fenceOffset;i>0&&SpaceOrTab(Peek(offset_));--i)AdvanceOffset(1,true);
                } else {
                    if(indent_>=4) AdvanceOffset(4,true);
                    else if(blank_) AdvanceNextNonspace();
                    else return 1;
                }
                return 0;
            case BT::paragraph: case BT::table: return blank_?1:0;
            default: return 0;
        }
    }
    // Block starts: 0 none, 1 container, 2 leaf.
    int TryStarts(BlockNode*& container) {
        const auto rest=line_.substr(std::min(nextNonspace_,line_.size()));
        // Block quote.
        if(!indented_&&Peek(nextNonspace_)=='>') {
            AdvanceNextNonspace();
            AdvanceOffset(1,false);
            if(SpaceOrTab(Peek(offset_)))AdvanceOffset(1,true);
            CloseUnmatchedBlocks();
            container=AddChild(BT::quote);
            return 1;
        }
        // ATX heading.
        if(!indented_&&!rest.empty()&&rest[0]=='#') {
            std::size_t n=0;
            while(n<rest.size()&&rest[n]=='#')++n;
            if(n<=6&&(n==rest.size()||SpaceOrTab(rest[n]))) {
                AdvanceNextNonspace();
                AdvanceOffset(n,false);
                CloseUnmatchedBlocks();
                container=AddChild(BT::heading);
                container->level=int(n);
                auto body=line_.substr(std::min(offset_,line_.size()));
                while(!body.empty()&&SpaceOrTab(body.back()))body.remove_suffix(1);
                auto k=body.size();
                while(k>0&&body[k-1]=='#')--k;
                if(k<body.size()) {
                    std::size_t lead=0;
                    while(lead<k&&SpaceOrTab(body[lead]))++lead;
                    if(lead==k) body=std::string_view();
                    else if(SpaceOrTab(body[k-1])) body=body.substr(0,k);
                }
                container->content=std::string(body);
                AdvanceOffset(line_.size()-offset_,false);
                return 2;
            }
        }
        // Fenced code.
        if(!indented_&&!rest.empty()&&(rest[0]=='`'||rest[0]=='~')) {
            const char fc=rest[0];
            std::size_t n=0;
            while(n<rest.size()&&rest[n]==fc)++n;
            if(n>=3&&!(fc=='`'&&rest.find('`',n)!=std::string_view::npos)) {
                CloseUnmatchedBlocks();
                container=AddChild(BT::code);
                container->fenced=true;
                container->fenceLength=int(n);
                container->fenceChar=fc;
                container->fenceOffset=int(indent_);
                AdvanceNextNonspace();
                AdvanceOffset(n,false);
                return 2;
            }
        }
        // GFM table delimiter row under a paragraph whose last line is the header.
        if(!indented_&&container->type==BT::paragraph) {
            std::vector<char> align;
            if(DelimiterRow(rest,align)) {
                auto& content=container->content;
                std::string_view body(content);
                if(!body.empty()&&body.back()=='\n')body.remove_suffix(1);
                const auto nl=body.rfind('\n');
                const auto headerLine=nl==std::string_view::npos?body:body.substr(nl+1);
                auto header=SplitRow(headerLine);
                if(header.size()==align.size()) {
                    CloseUnmatchedBlocks();
                    content.resize(nl==std::string_view::npos?0:nl+1);
                    BlockNode* table=nullptr;
                    if(content.empty()) {
                        table=container;
                        table->type=BT::table;
                    } else {
                        Finalize(container,lineNumber_-1);
                        table=AddChild(BT::table);
                        table->startLine=lineNumber_-1;
                    }
                    table->align=std::move(align);
                    table->header=std::move(header);
                    tip_=table;
                    container=table;
                    AdvanceOffset(line_.size()-offset_,false);
                    return 2;
                }
            }
        }
        // Setext heading underline.
        if(!indented_&&container->type==BT::paragraph&&!rest.empty()&&(rest[0]=='='||rest[0]=='-')) {
            std::size_t n=0;
            while(n<rest.size()&&rest[n]==rest[0])++n;
            auto m=n;
            while(m<rest.size()&&SpaceOrTab(rest[m]))++m;
            if(m==rest.size()) {
                CloseUnmatchedBlocks();
                container->type=BT::heading;
                container->level=rest[0]=='='?1:2;
                tip_=container;
                AdvanceOffset(line_.size()-offset_,false);
                return 2;
            }
        }
        // Thematic break.
        if(!indented_&&!rest.empty()&&(rest[0]=='*'||rest[0]=='_'||rest[0]=='-')) {
            std::size_t count=0;
            bool ok=true;
            for(const char ch:rest) {
                if(ch==rest[0]) ++count;
                else if(!SpaceOrTab(ch)) {ok=false;break;}
            }
            if(ok&&count>=3) {
                CloseUnmatchedBlocks();
                container=AddChild(BT::thematic);
                AdvanceOffset(line_.size()-offset_,false);
                return 2;
            }
        }
        // List item.
        if(!indented_||container->type==BT::list) {
            if(ListItem(container))return 1;
        }
        // Indented code.
        if(indented_&&tip_->type!=BT::paragraph&&!blank_) {
            AdvanceOffset(4,true);
            CloseUnmatchedBlocks();
            container=AddChild(BT::code);
            return 2;
        }
        return 0;
    }
    bool ListItem(BlockNode*& container) {
        if(indent_>=4)return false;
        const auto rest=line_.substr(std::min(nextNonspace_,line_.size()));
        BlockNode data;
        std::size_t markerLength=0;
        if(!rest.empty()&&(rest[0]=='*'||rest[0]=='+'||rest[0]=='-')) {
            data.bulletChar=rest[0];
            markerLength=1;
        } else {
            std::size_t d=0;
            while(d<rest.size()&&d<10&&IsAsciiDigit(rest[d]))++d;
            if(d<1||d>9||d>=rest.size()||(rest[d]!='.'&&rest[d]!=')'))return false;
            const auto start=std::stoul(std::string(rest.substr(0,d)));
            if(container->type==BT::paragraph&&start!=1)return false;
            data.ordered=true;
            data.start=std::uint32_t(start);
            data.delimiter=rest[d];
            markerLength=d+1;
        }
        const int next=Peek(nextNonspace_+markerLength);
        if(!(next==-1||SpaceOrTab(next)))return false;
        if(container->type==BT::paragraph&&TrimAsciiSpace(rest.substr(markerLength)).empty())return false;
        const auto markerOffset=int(indent_);
        AdvanceNextNonspace();
        AdvanceOffset(markerLength,true);
        const auto spacesStartCol=column_;
        const auto spacesStartOffset=offset_;
        int c=-1;
        do {
            AdvanceOffset(1,true);
            c=Peek(offset_);
        } while(column_-spacesStartCol<5&&SpaceOrTab(c));
        const bool blankItem=Peek(offset_)==-1;
        const auto spacesAfterMarker=column_-spacesStartCol;
        if(spacesAfterMarker>=5||spacesAfterMarker<1||blankItem) {
            data.padding=int(markerLength)+1;
            column_=spacesStartCol;
            offset_=spacesStartOffset;
            if(SpaceOrTab(Peek(offset_)))AdvanceOffset(1,true);
        } else {
            data.padding=int(markerLength+spacesAfterMarker);
        }
        data.markerOffset=markerOffset;
        CloseUnmatchedBlocks();
        const auto matches=[&](const BlockNode& list) {
            return list.ordered==data.ordered&&list.delimiter==data.delimiter&&list.bulletChar==data.bulletChar;
        };
        if(tip_->type!=BT::list||!matches(*tip_)) {
            container=AddChild(BT::list);
            container->ordered=data.ordered;container->bulletChar=data.bulletChar;
            container->delimiter=data.delimiter;container->start=data.start;
        }
        container=AddChild(BT::item);
        container->ordered=data.ordered;container->bulletChar=data.bulletChar;container->delimiter=data.delimiter;
        container->markerOffset=data.markerOffset;container->padding=data.padding;
        return true;
    }

    void Incorporate(std::string_view line) {
        line_=line;
        offset_=0;column_=0;blank_=false;partiallyConsumedTab_=false;
        ++lineNumber_;
        BlockNode* container=doc_.get();
        oldtip_=tip_;
        bool allMatched=true;
        while(!container->children.empty()&&container->children.back()->open) {
            container=container->children.back().get();
            FindNextNonspace();
            const auto r=Continue(container);
            if(r==2)return;
            if(r==1) {
                allMatched=false;
                container=container->parent;
                break;
            }
        }
        (void)allMatched;
        allClosed_=container==oldtip_;
        lastMatched_=container;
        bool matchedLeaf=container->type!=BT::paragraph&&container->type!=BT::table&&AcceptsLines(container->type);
        while(!matchedLeaf) {
            FindNextNonspace();
            const auto r=TryStarts(container);
            if(r==1) {container=tip_;continue;}
            if(r==2) {container=tip_;matchedLeaf=true;break;}
            AdvanceNextNonspace();
            break;
        }
        if(!allClosed_&&!blank_&&tip_->type==BT::paragraph) {
            AddLine();  // Lazy paragraph continuation.
            return;
        }
        CloseUnmatchedBlocks();
        if(AcceptsLines(container->type)) {
            AddLine();
        } else if(offset_<line_.size()&&!blank_) {
            container=AddChild(BT::paragraph);
            AdvanceNextNonspace();
            AddLine();
        }
    }

    const MarkdownToHtmlOptions& options_;
    std::unique_ptr<BlockNode> doc_;
    BlockNode* tip_=nullptr;
    BlockNode* oldtip_=nullptr;
    BlockNode* lastMatched_=nullptr;
    bool allClosed_=true;
    std::string_view line_;
    std::uint64_t lineNumber_=0;
    std::size_t offset_=0,column_=0,nextNonspace_=0,nextNonspaceColumn_=0,indent_=0;
    bool indented_=false,blank_=false,partiallyConsumedTab_=false;
};

// ================= Inlines =================
enum class IT : std::uint8_t {root,text,softbreak,hardbreak,code,emph,strong,del,link,image};
struct INode {
    IT type=IT::text;
    std::string literal,dest,title;
    int parent=-1,first=-1,last=-1,prev=-1,next=-1;
};
struct Delimiter {
    char cc=0;
    std::size_t numdelims=0,origdelims=0;
    int node=-1,prev=-1,next=-1;
    bool canOpen=false,canClose=false;
};
struct Bracket {
    int node=-1,prev=-1,prevDelimiter=-1;
    bool image=false,active=true;
};

class InlineParser {
public:
    InlineParser(const MarkdownToHtmlOptions& options,MarkdownToHtmlReport& report,std::uint64_t& linkCount)
        :options_(options),report_(report),links_(linkCount) {}
    // Parses `subject` into a fresh tree; returns the root index (always 0).
    std::vector<INode> Parse(std::string_view subject) {
        nodes_.clear();
        delims_.clear();brackets_.clear();
        top_=-1;bracketTop_=-1;
        subject_=subject;pos_=0;
        noCloserFrom_.clear();
        scanBudget_=4*subject.size()+65536;
        nodes_.push_back(INode{IT::root,{},{},{}});
        while(pos_<subject_.size())ParseInline();
        ProcessEmphasis(-1);
        return std::move(nodes_);
    }
private:
    int Add(IT type,std::string literal={}) {
        nodes_.push_back(INode{type,std::move(literal),{},{}});
        return int(nodes_.size()-1);
    }
    void Append(int parent,int child) {
        auto& c=nodes_[std::size_t(child)];
        c.parent=parent;c.next=-1;
        auto& p=nodes_[std::size_t(parent)];
        c.prev=p.last;
        if(p.last!=-1)nodes_[std::size_t(p.last)].next=child;
        else p.first=child;
        p.last=child;
    }
    void Unlink(int id) {
        auto& n=nodes_[std::size_t(id)];
        if(n.prev!=-1)nodes_[std::size_t(n.prev)].next=n.next;
        else if(n.parent!=-1)nodes_[std::size_t(n.parent)].first=n.next;
        if(n.next!=-1)nodes_[std::size_t(n.next)].prev=n.prev;
        else if(n.parent!=-1)nodes_[std::size_t(n.parent)].last=n.prev;
        n.parent=n.prev=n.next=-1;
    }
    void InsertAfter(int ref,int id) {
        Unlink(id);
        auto& r=nodes_[std::size_t(ref)];
        auto& n=nodes_[std::size_t(id)];
        n.next=r.next;n.prev=ref;n.parent=r.parent;
        if(r.next!=-1)nodes_[std::size_t(r.next)].prev=id;
        else if(r.parent!=-1)nodes_[std::size_t(r.parent)].last=id;
        nodes_[std::size_t(ref)].next=id;
    }
    void Text(std::string s) {Append(0,Add(IT::text,std::move(s)));}
    int PeekC() const {return pos_<subject_.size()?static_cast<unsigned char>(subject_[pos_]):-1;}
    void CountLink() {
        if(++links_>options_.maximumLinks)Fail(Error::link_limit);
    }

    void ParseInline() {
        const char c=subject_[pos_];
        switch(c) {
            case '\n': Newline();return;
            case '\\': Backslash();return;
            case '`': Backticks();return;
            case '*': case '_': case '~': Delim(c);return;
            case '[': ++pos_;Append(0,Add(IT::text,"["));AddBracket(false);return;
            case '!':
                ++pos_;
                if(PeekC()=='[') {++pos_;Append(0,Add(IT::text,"!["));AddBracket(true);}
                else Text("!");
                return;
            case ']': CloseBracket();return;
            case '<': Autolink();return;
            case '&': {
                std::string out;
                if(const auto n=Entity(subject_,pos_,out,&report_.unknownEntities)) {pos_+=n;Text(std::move(out));}
                else {++pos_;Text("&");}
                return;
            }
            default: break;
        }
        const auto stop=subject_.find_first_of("\n\\`*_~[]!<&",pos_);
        const auto end=stop==std::string_view::npos?subject_.size():stop;
        Text(std::string(subject_.substr(pos_,end-pos_)));
        pos_=end;
    }
    void Newline() {
        ++pos_;
        const int last=nodes_[0].last;
        if(last!=-1&&nodes_[std::size_t(last)].type==IT::text&&!nodes_[std::size_t(last)].literal.empty()
            &&nodes_[std::size_t(last)].literal.back()==' ') {
            auto& lit=nodes_[std::size_t(last)].literal;
            const bool hard=lit.size()>=2&&lit[lit.size()-2]==' ';
            while(!lit.empty()&&lit.back()==' ')lit.pop_back();
            Append(0,Add(hard?IT::hardbreak:IT::softbreak));
        } else {
            Append(0,Add(IT::softbreak));
        }
        while(PeekC()==' '||PeekC()=='\t')++pos_;
    }
    void Backslash() {
        ++pos_;
        if(PeekC()=='\n') {++pos_;Append(0,Add(IT::hardbreak));while(PeekC()==' '||PeekC()=='\t')++pos_;return;}
        if(PeekC()!=-1&&IsAsciiPunctuation(char(PeekC()))) {Text(std::string(1,subject_[pos_]));++pos_;return;}
        Text("\\");
    }
    void Backticks() {
        const auto start=pos_;
        while(PeekC()=='`')++pos_;
        const auto length=pos_-start;
        const auto afterOpen=pos_;
        const auto known=noCloserFrom_.find(length);
        if(known==noCloserFrom_.end()||afterOpen<known->second) {
            for(auto q=subject_.find('`',afterOpen);q!=std::string_view::npos;) {
                auto r=q;
                while(r<subject_.size()&&subject_[r]=='`')++r;
                if(r-q==length) {
                    std::string contents(subject_.substr(afterOpen,q-afterOpen));
                    for(auto& ch:contents)if(ch=='\n')ch=' ';
                    if(contents.size()>=2&&contents.front()==' '&&contents.back()==' '
                        &&contents.find_first_not_of(' ')!=std::string::npos)
                        contents=contents.substr(1,contents.size()-2);
                    Append(0,Add(IT::code,std::move(contents)));
                    pos_=r;
                    return;
                }
                q=subject_.find('`',r);
            }
            noCloserFrom_[length]=afterOpen;
        }
        Text(std::string(subject_.substr(start,length)));
    }
    char32_t Before(std::size_t p) const {
        if(p==0)return '\n';
        auto q=p-1;
        while(q>0&&(std::uint8_t(subject_[q])&0xc0)==0x80)--q;
        char32_t c=0;
        return DecodeUtf8(subject_,q,c)?c:char32_t(0xfffd);
    }
    char32_t After(std::size_t p) const {
        if(p>=subject_.size())return '\n';
        char32_t c=0;
        return DecodeUtf8(subject_,p,c)?c:char32_t(0xfffd);
    }
    void Delim(char cc) {
        const auto start=pos_;
        while(PeekC()==cc)++pos_;
        const auto n=pos_-start;
        const auto before=Before(start),after=After(pos_);
        const bool afterWs=UnicodeWhitespace(after),afterPunct=UnicodePunctuation(after);
        const bool beforeWs=UnicodeWhitespace(before),beforePunct=UnicodePunctuation(before);
        const bool left=!afterWs&&(!afterPunct||beforeWs||beforePunct);
        const bool right=!beforeWs&&(!beforePunct||afterWs||afterPunct);
        bool canOpen=left,canClose=right;
        if(cc=='_') {
            canOpen=left&&(!right||beforePunct);
            canClose=right&&(!left||afterPunct);
        }
        const int node=Add(IT::text,std::string(n,cc));
        Append(0,node);
        if(cc=='~'&&n>2)return;  // Only ~ and ~~ delimit strikethrough.
        Delimiter d;
        d.cc=cc;d.numdelims=n;d.origdelims=n;d.node=node;d.prev=top_;d.canOpen=canOpen;d.canClose=canClose;
        delims_.push_back(d);
        const int id=int(delims_.size()-1);
        if(top_!=-1)delims_[std::size_t(top_)].next=id;
        top_=id;
    }
    void RemoveDelimiter(int id) {
        auto& d=delims_[std::size_t(id)];
        if(d.prev!=-1)delims_[std::size_t(d.prev)].next=d.next;
        if(d.next==-1)top_=d.prev;
        else delims_[std::size_t(d.next)].prev=d.prev;
    }
    void ProcessEmphasis(int stackBottom) {
        int openersBottom[16];
        for(auto& b:openersBottom)b=stackBottom;
        int closer=top_;
        while(closer!=-1&&delims_[std::size_t(closer)].prev!=stackBottom)closer=delims_[std::size_t(closer)].prev;
        while(closer!=-1) {
            auto& cl=delims_[std::size_t(closer)];
            if(!cl.canClose) {closer=cl.next;continue;}
            const char cc=cl.cc;
            const int bottomIndex=cc=='~'?12+int(cl.origdelims%2)
                :(cc=='_'?0:6)+(cl.canOpen?3:0)+int(cl.origdelims%3);
            int opener=cl.prev;
            bool found=false;
            while(opener!=-1&&opener!=stackBottom&&opener!=openersBottom[bottomIndex]) {
                const auto& op=delims_[std::size_t(opener)];
                if(cc=='~') {
                    if(op.cc=='~'&&op.canOpen&&op.origdelims==cl.origdelims) {found=true;break;}
                } else {
                    const bool oddMatch=(cl.canOpen||op.canClose)&&cl.origdelims%3!=0
                        &&(op.origdelims+cl.origdelims)%3==0;
                    if(op.cc==cc&&op.canOpen&&!oddMatch) {found=true;break;}
                }
                opener=op.prev;
            }
            const int oldCloser=closer;
            if(!found) {
                closer=cl.next;
                openersBottom[bottomIndex]=delims_[std::size_t(oldCloser)].prev;
                if(!delims_[std::size_t(oldCloser)].canOpen)RemoveDelimiter(oldCloser);
                continue;
            }
            auto& op=delims_[std::size_t(opener)];
            const std::size_t use=cc=='~'?cl.numdelims:(cl.numdelims>=2&&op.numdelims>=2?2:1);
            const int openerNode=op.node,closerNode=cl.node;
            op.numdelims-=use;
            cl.numdelims-=use;
            auto& ol=nodes_[std::size_t(openerNode)].literal;ol.resize(ol.size()-use);
            auto& cll=nodes_[std::size_t(closerNode)].literal;cll.resize(cll.size()-use);
            const int emph=Add(cc=='~'?IT::del:use==1?IT::emph:IT::strong);
            for(int t=nodes_[std::size_t(openerNode)].next;t!=-1&&t!=closerNode;) {
                const int next=nodes_[std::size_t(t)].next;
                Unlink(t);
                Append(emph,t);
                t=next;
            }
            InsertAfter(openerNode,emph);
            // Remove delimiters between opener and closer.
            delims_[std::size_t(opener)].next=closer;
            delims_[std::size_t(closer)].prev=opener;
            if(delims_[std::size_t(opener)].numdelims==0) {
                Unlink(openerNode);
                RemoveDelimiter(opener);
            }
            if(delims_[std::size_t(closer)].numdelims==0) {
                Unlink(closerNode);
                const int next=delims_[std::size_t(closer)].next;
                RemoveDelimiter(closer);
                closer=next;
            }
        }
        while(top_!=-1&&top_!=stackBottom)RemoveDelimiter(top_);
    }

    void AddBracket(bool image) {
        Bracket b;
        b.node=nodes_[0].last;b.prev=bracketTop_;b.prevDelimiter=top_;b.image=image;
        brackets_.push_back(b);
        bracketTop_=int(brackets_.size()-1);
    }
    void RemoveBracket() {bracketTop_=brackets_[std::size_t(bracketTop_)].prev;}
    bool Spnl() {
        while(PeekC()==' '||PeekC()=='\t')++pos_;
        if(PeekC()=='\n')++pos_;
        while(PeekC()==' '||PeekC()=='\t')++pos_;
        return true;
    }
    bool Charge(std::size_t bytes) {
        if(bytes>scanBudget_) {scanBudget_=0;return false;}
        scanBudget_-=bytes;
        return true;
    }
    bool LinkDestination(std::string& dest) {
        const auto start=pos_;
        if(PeekC()=='<') {
            auto p=pos_+1;
            while(p<subject_.size()) {
                const char c=subject_[p];
                if(c=='\\'&&p+1<subject_.size()&&IsAsciiPunctuation(subject_[p+1])) {p+=2;continue;}
                if(c=='>'||c=='<'||c=='\n')break;
                ++p;
            }
            if(!Charge(p-start))return false;
            if(p>=subject_.size()||subject_[p]!='>')return false;
            dest=NormalizeUri(Unescape(subject_.substr(start+1,p-start-1)));
            pos_=p+1;
            return true;
        }
        auto p=pos_;
        int depth=0;
        while(p<subject_.size()) {
            const char c=subject_[p];
            if(c=='\\'&&p+1<subject_.size()&&IsAsciiPunctuation(subject_[p+1])) {p+=2;continue;}
            if(c=='(') {if(++depth>32)break;++p;continue;}
            if(c==')') {if(depth<1)break;--depth;++p;continue;}
            if(std::uint8_t(c)<=0x20||c==0x7f)break;
            ++p;
        }
        if(!Charge(p-start))return false;
        if(depth!=0||(p==start&&PeekC()!=')'))return false;
        dest=NormalizeUri(Unescape(subject_.substr(start,p-start)));
        pos_=p;
        return true;
    }
    bool LinkTitle(std::string& title) {
        const int open=PeekC();
        if(open!='"'&&open!='\''&&open!='(')return false;
        const char close=open=='('?')':char(open);
        auto p=pos_+1;
        while(p<subject_.size()) {
            const char c=subject_[p];
            if(c=='\\'&&p+1<subject_.size()&&IsAsciiPunctuation(subject_[p+1])) {p+=2;continue;}
            if(c==close)break;
            if(open=='('&&c=='(')return false;
            ++p;
        }
        if(!Charge(p-pos_))return false;
        if(p>=subject_.size())return false;
        title=Unescape(subject_.substr(pos_+1,p-pos_-1));
        pos_=p+1;
        return true;
    }
    void CloseBracket() {
        ++pos_;
        const auto startpos=pos_;
        if(bracketTop_==-1) {Text("]");return;}
        auto& opener=brackets_[std::size_t(bracketTop_)];
        if(!opener.active) {Text("]");RemoveBracket();return;}
        const bool image=opener.image;
        std::string dest,title;
        bool matched=false;
        if(PeekC()=='('&&scanBudget_>0) {
            ++pos_;
            Spnl();
            if(LinkDestination(dest)) {
                const auto beforeTitle=pos_;
                Spnl();
                const bool spaced=pos_>beforeTitle;
                if(!(spaced&&LinkTitle(title))) {pos_=beforeTitle;title.clear();}
                Spnl();
                if(PeekC()==')') {++pos_;matched=true;}
            }
        }
        if(!matched) {
            RemoveBracket();
            pos_=startpos;
            Text("]");
            return;
        }
        CountLink();
        const int openerNode=opener.node;
        const int prevDelimiter=opener.prevDelimiter;
        const int link=Add(image?IT::image:IT::link);
        nodes_[std::size_t(link)].dest=std::move(dest);
        nodes_[std::size_t(link)].title=std::move(title);
        for(int t=nodes_[std::size_t(openerNode)].next;t!=-1;) {
            const int next=nodes_[std::size_t(t)].next;
            Unlink(t);
            Append(link,t);
            t=next;
        }
        Append(0,link);
        ProcessEmphasis(prevDelimiter);
        RemoveBracket();
        Unlink(openerNode);
        if(!image) {
            for(int b=bracketTop_;b!=-1;b=brackets_[std::size_t(b)].prev)
                if(!brackets_[std::size_t(b)].image)brackets_[std::size_t(b)].active=false;
        }
    }
    void Autolink() {
        const auto rest=subject_.substr(pos_);
        // URI autolink: scheme of 2-32 chars, ':' and no whitespace, '<' or '>'.
        if(rest.size()>3&&IsAsciiAlpha(rest[1])) {
            std::size_t p=2;
            while(p<rest.size()&&p<33&&(IsAsciiAlnum(rest[p])||rest[p]=='+'||rest[p]=='.'||rest[p]=='-'))++p;
            if(p>=3&&p<rest.size()&&rest[p]==':') {
                auto e=p+1;
                while(e<rest.size()&&rest[e]!='<'&&rest[e]!='>'&&std::uint8_t(rest[e])>0x20)++e;
                if(e<rest.size()&&rest[e]=='>') {
                    const auto uri=rest.substr(1,e-1);
                    CountLink();
                    const int link=Add(IT::link);
                    nodes_[std::size_t(link)].dest=NormalizeUri(uri);
                    Append(link,Add(IT::text,std::string(uri)));
                    Append(0,link);
                    pos_+=e+1;
                    return;
                }
            }
        }
        // Email autolink.
        {
            static constexpr std::string_view local=".!#$%&'*+/=?^_`{|}~-";
            std::size_t p=1;
            while(p<rest.size()&&(IsAsciiAlnum(rest[p])||local.find(rest[p])!=std::string_view::npos))++p;
            if(p>1&&p<rest.size()&&rest[p]=='@') {
                auto e=p+1;
                bool ok=true;
                while(ok) {
                    auto l=e;
                    while(l<rest.size()&&(IsAsciiAlnum(rest[l])||rest[l]=='-')&&l-e<63)++l;
                    if(l==e||rest[e]=='-'||rest[l-1]=='-') {ok=false;break;}
                    e=l;
                    if(e<rest.size()&&rest[e]=='.') {++e;continue;}
                    break;
                }
                if(ok&&e<rest.size()&&rest[e]=='>') {
                    const auto email=rest.substr(1,e-1);
                    CountLink();
                    const int link=Add(IT::link);
                    nodes_[std::size_t(link)].dest="mailto:"+NormalizeUri(email);
                    Append(link,Add(IT::text,std::string(email)));
                    Append(0,link);
                    pos_+=e+1;
                    return;
                }
            }
        }
        if(rest.size()>1&&(IsAsciiAlpha(rest[1])||rest[1]=='/'||rest[1]=='!'||rest[1]=='?'))++report_.htmlEscaped;
        ++pos_;
        Text("<");
    }

    const MarkdownToHtmlOptions& options_;
    MarkdownToHtmlReport& report_;
    std::uint64_t& links_;
    std::vector<INode> nodes_;
    std::vector<Delimiter> delims_;
    std::vector<Bracket> brackets_;
    int top_=-1,bracketTop_=-1;
    std::string_view subject_;
    std::size_t pos_=0;
    std::map<std::size_t,std::size_t> noCloserFrom_;
    std::size_t scanBudget_=0;
};

// ================= Rendering =================
class Renderer {
public:
    Renderer(const MarkdownToHtmlOptions& options,MarkdownToHtmlReport& report)
        :options_(options),report_(report),out_(options.maximumOutputBytes),inlines_(options,report,links_) {}
    std::string Render(const BlockNode& doc) {
        if(options_.fullDocument) {
            std::string title=options_.title;
            if(title.empty())title=FirstHeading(doc);
            if(title.empty())title="Document";
            out_.Put("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
            out_.Put(EscapeHtml(title));
            out_.Put("</title>\n</head>\n<body>\n");
        }
        Children(doc,false);
        if(options_.fullDocument) {Cr();out_.Put("</body>\n</html>\n");}
        return out_.Take();
    }
private:
    void Cr() {
        const auto& t=out_.Text();
        if(!t.empty()&&t.back()!='\n')out_.Put('\n');
    }
    std::string FirstHeading(const BlockNode& b) {
        if(b.type==BT::heading) {
            const auto nodes=inlines_.Parse(TrimAsciiSpace(b.content));
            std::string text;
            PlainText(nodes,0,text);
            return text;
        }
        for(const auto& c:b.children) {
            auto t=FirstHeading(*c);
            if(!t.empty())return t;
        }
        return {};
    }
    static void PlainText(const std::vector<INode>& nodes,int id,std::string& out) {
        // Iterative walk: concatenated text and code literals; breaks become spaces.
        int n=nodes[std::size_t(id)].first;
        while(n!=-1) {
            const auto& node=nodes[std::size_t(n)];
            if(node.type==IT::text||node.type==IT::code) out+=node.literal;
            else if(node.type==IT::softbreak||node.type==IT::hardbreak) out+=' ';
            if(node.first!=-1) {n=node.first;continue;}
            while(n!=-1&&nodes[std::size_t(n)].next==-1&&n!=id) {
                n=nodes[std::size_t(n)].parent;
                if(n==id) {n=-1;break;}
            }
            if(n!=-1)n=nodes[std::size_t(n)].next;
        }
    }
    void Children(const BlockNode& b,bool tight) {
        for(const auto& c:b.children)Block(*c,tight);
    }
    void Inline(std::string_view content) {
        const auto nodes=inlines_.Parse(content);
        RenderInlines(nodes);
    }
    void Block(const BlockNode& b,bool tight) {
        switch(b.type) {
            case BT::document: Children(b,false);break;
            case BT::paragraph:
                if(tight) {Inline(TrimAsciiSpace(b.content));break;}
                Cr();out_.Put("<p>");Inline(TrimAsciiSpace(b.content));out_.Put("</p>");Cr();
                break;
            case BT::heading: {
                const std::string tag="h"+std::to_string(b.level);
                Cr();out_.Put("<"+tag+">");Inline(TrimAsciiSpace(b.content));out_.Put("</"+tag+">");Cr();
                break;
            }
            case BT::thematic: Cr();out_.Put("<hr />");Cr();break;
            case BT::code: {
                Cr();
                out_.Put("<pre><code");
                if(!b.info.empty()) {
                    const auto info=std::string_view(b.info);
                    const auto end=info.find_first_of(" \t\n");
                    out_.Put(" class=\"language-");out_.Put(EscapeHtml(info.substr(0,end)));out_.Put("\"");
                }
                out_.Put(">");
                out_.Put(EscapeHtml(b.content));
                out_.Put("</code></pre>");
                Cr();
                break;
            }
            case BT::quote:
                Cr();out_.Put("<blockquote>");Cr();Children(b,false);Cr();out_.Put("</blockquote>");Cr();
                break;
            case BT::list: {
                Cr();
                if(b.ordered) {
                    out_.Put(b.start==1?std::string("<ol>"):"<ol start=\""+std::to_string(b.start)+"\">");
                } else {
                    out_.Put("<ul>");
                }
                Cr();
                for(const auto& item:b.children) {
                    out_.Put("<li>");
                    Children(*item,b.tight);
                    out_.Put("</li>");
                    Cr();
                }
                out_.Put(b.ordered?"</ol>":"</ul>");
                Cr();
                break;
            }
            case BT::item: Children(b,tight);break;
            case BT::table: Table(b);break;
        }
    }
    void Table(const BlockNode& b) {
        const auto cell=[&](std::string_view tag,std::size_t i,const std::string& text) {
            out_.Put("<");out_.Put(tag);
            const char a=i<b.align.size()?b.align[i]:0;
            if(a)out_.Put(a=='l'?" align=\"left\"":a=='c'?" align=\"center\"":" align=\"right\"");
            out_.Put(">");
            Inline(text);
            out_.Put("</");out_.Put(tag);out_.Put(">\n");
        };
        Cr();
        out_.Put("<table>\n<thead>\n<tr>\n");
        for(std::size_t i=0;i<b.header.size();++i)cell("th",i,b.header[i]);
        out_.Put("</tr>\n</thead>\n");
        if(!b.rows.empty()) {
            out_.Put("<tbody>\n");
            for(const auto& row:b.rows) {
                out_.Put("<tr>\n");
                for(std::size_t i=0;i<b.header.size();++i)cell("td",i,i<row.size()?row[i]:std::string());
                out_.Put("</tr>\n");
            }
            out_.Put("</tbody>\n");
        }
        out_.Put("</table>\n");
    }

    void RenderInlines(const std::vector<INode>& nodes) {
        // Iterative pre/post-order walk, so emphasis nesting depth never grows the C++ stack.
        int altDepth=0;
        const auto enter=[&](int id)->bool {
            const auto& n=nodes[std::size_t(id)];
            const bool alt=altDepth>0;
            switch(n.type) {
                case IT::text: out_.Put(EscapeHtml(n.literal));return false;
                case IT::softbreak: out_.Put("\n");return false;
                case IT::hardbreak: out_.Put(alt?"\n":"<br />\n");return false;
                case IT::code:
                    if(alt) {out_.Put(EscapeHtml(n.literal));return false;}
                    out_.Put("<code>");out_.Put(EscapeHtml(n.literal));out_.Put("</code>");return false;
                case IT::emph: if(!alt)out_.Put("<em>");return true;
                case IT::strong: if(!alt)out_.Put("<strong>");return true;
                case IT::del: if(!alt)out_.Put("<del>");return true;
                case IT::link:
                    ++report_.links;
                    if(!SafeUrl(n.dest,true)) {++report_.unsafeLinks;return true;}
                    if(alt)return true;
                    out_.Put("<a href=\"");out_.Put(EscapeHtml(n.dest));out_.Put("\"");
                    if(!n.title.empty()) {out_.Put(" title=\"");out_.Put(EscapeHtml(n.title));out_.Put("\"");}
                    out_.Put(">");
                    return true;
                case IT::image:
                    ++report_.images;
                    if(!SafeUrl(n.dest,false)) {++report_.unsafeImages;++altDepth;return true;}
                    if(alt) {++altDepth;return true;}
                    out_.Put("<img src=\"");out_.Put(EscapeHtml(n.dest));out_.Put("\" alt=\"");
                    ++altDepth;
                    return true;
                case IT::root: return true;
            }
            return false;
        };
        const auto exit=[&](int id) {
            const auto& n=nodes[std::size_t(id)];
            switch(n.type) {
                case IT::emph: if(!altDepth)out_.Put("</em>");break;
                case IT::strong: if(!altDepth)out_.Put("</strong>");break;
                case IT::del: if(!altDepth)out_.Put("</del>");break;
                case IT::link:
                    if(!altDepth&&SafeUrl(n.dest,true))out_.Put("</a>");
                    break;
                case IT::image:
                    --altDepth;
                    if(!SafeUrl(n.dest,false))break;
                    if(altDepth)break;
                    out_.Put("\"");
                    if(!n.title.empty()) {out_.Put(" title=\"");out_.Put(EscapeHtml(n.title));out_.Put("\"");}
                    out_.Put(" />");
                    break;
                default: break;
            }
        };
        int n=nodes[0].first;
        while(n!=-1) {
            const bool descend=enter(n)&&nodes[std::size_t(n)].first!=-1;
            if(descend) {n=nodes[std::size_t(n)].first;continue;}
            if(nodes[std::size_t(n)].type!=IT::text)exit(n);
            while(n!=-1&&nodes[std::size_t(n)].next==-1) {
                n=nodes[std::size_t(n)].parent;
                if(n<=0) {n=-1;break;}
                exit(n);
            }
            if(n!=-1)n=nodes[std::size_t(n)].next;
        }
    }

    const MarkdownToHtmlOptions& options_;
    MarkdownToHtmlReport& report_;
    std::uint64_t links_=0;
    Output out_;
    InlineParser inlines_;
};
}

std::string MarkdownToHtmlText(Bytes bytes,const MarkdownToHtmlOptions& options,MarkdownToHtmlReport& report) {
    const auto text=PrepareText(bytes);
    // Raw HTML is never recognized, as a block or inline: lines that would
    // start a CommonMark HTML block are parsed as ordinary Markdown and every
    // '<' is emitted escaped (the inline parser counts tag-like ones).
    const auto doc=BlockParser(options).Parse(text);
    Renderer renderer(options,report);
    return renderer.Render(*doc);
}
}
