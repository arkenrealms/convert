#include "html_tree.hpp"
#include <initializer_list>

// Bounded, tolerant HTML5-style tokenizer and simplified tree builder.
// Tokenizer: data, tag open/name, attribute (double-quoted, single-quoted,
// unquoted, valueless), self-closing flag, character references, comments,
// bogus comments (doctype, <? ?>, <![CDATA[ ]]>), raw text and RCDATA.
// Tree builder: HTML's implicit end tags for p, li, dd/dt, tr/td/th, table
// sections, option/optgroup, headings and nested a/button, scope-checked end
// tags, void elements and a nesting limit. It does not implement the adoption
// agency algorithm, foster parenting or template contents: a misnested
// formatting element closes at the first matching end tag.
namespace arken::markup::v1::detail::html {
namespace {
bool In(std::string_view name,std::initializer_list<std::string_view> set) {
    for(const auto s:set)if(s==name)return true;
    return false;
}
bool IsHeading(std::string_view n) {return n.size()==2&&n[0]=='h'&&n[1]>='1'&&n[1]<='6';}
bool IsVoid(std::string_view n) {
    return In(n,{"area","base","basefont","bgsound","br","col","embed","frame","hr","img","image","input","keygen",
        "link","meta","param","source","track","wbr"});
}
bool IsRawText(std::string_view n) {return In(n,{"script","style","xmp","iframe","noembed","noframes","noscript"});}
bool IsRcdata(std::string_view n) {return n=="textarea"||n=="title";}
bool IsSpecial(std::string_view n) {
    return IsHeading(n)||In(n,{"address","applet","area","article","aside","base","basefont","bgsound","blockquote","body","br",
        "button","caption","center","col","colgroup","dd","details","dialog","dir","div","dl","dt","embed","fieldset","figcaption",
        "figure","footer","form","frame","frameset","head","header","hgroup","hr","html","iframe","img","input","keygen","li",
        "link","listing","main","marquee","menu","meta","nav","noembed","noframes","noscript","object","ol","p","param",
        "plaintext","pre","script","search","section","select","source","style","summary","table","tbody","td","template",
        "textarea","tfoot","th","thead","title","tr","track","ul","wbr","xmp"});
}
bool ClosesP(std::string_view n) {
    return IsHeading(n)||In(n,{"address","article","aside","blockquote","center","details","dialog","dir","div","dl","dd","dt",
        "fieldset","figcaption","figure","footer","form","header","hgroup","hr","li","listing","main","menu","nav","ol","p",
        "plaintext","pre","search","section","summary","table","ul","xmp"});
}
bool DefaultScopeBarrier(std::string_view n) {
    return In(n,{"applet","caption","html","table","td","th","marquee","object","template","svg","math"});
}
bool IsUtf8Label(std::string_view label) {
    const auto l=AsciiLower(TrimAsciiSpace(label));
    return In(l,{"utf-8","utf8","unicode-1-1-utf-8","unicode11utf8","unicode20utf8","x-unicode20utf8"});
}
bool IsHtmlSpace(char c) {return c==' '||c=='\t'||c=='\n'||c=='\f';}

class Builder {
public:
    Builder(std::string_view s,const TreeLimits& limits,TreeStats& stats):s_(s),limits_(limits),stats_(stats) {
        tree_.nodes.push_back(Node{false,"#document",{},{},{}});
        stack_.push_back(0);
    }
    Tree Run() {
        while(p_<s_.size()) {
            const char c=s_[p_];
            if(c=='<') {Tag();continue;}
            const auto stop=s_.find_first_of("<&",p_);
            const auto end=stop==std::string_view::npos?s_.size():stop;
            if(c=='&') {
                std::string text;
                const auto used=Reference(s_,p_,false,text);
                if(used) {AddText(text);p_+=used;} else {AddText("&");++p_;}
                continue;
            }
            AddText(s_.substr(p_,end-p_));
            p_=end;
        }
        return std::move(tree_);
    }

private:
    // ---- Character references ----
    std::size_t Reference(std::string_view s,std::size_t p,bool attribute,std::string& out) {
        const auto n=s.size();
        auto q=p+1;
        if(q<n&&s[q]=='#') {
            ++q;
            const bool hex=q<n&&(s[q]=='x'||s[q]=='X');
            if(hex)++q;
            const auto start=q;
            std::uint64_t value=0;
            while(q<n&&(hex?IsHexDigit(s[q]):IsAsciiDigit(s[q]))) {
                const char d=s[q];
                const unsigned digit=IsAsciiDigit(d)?unsigned(d-'0'):unsigned(AsciiLower(d)-'a'+10);
                value=std::min<std::uint64_t>(value*(hex?16:10)+digit,0x110000);
                ++q;
            }
            if(q==start)return 0;
            if(q<n&&s[q]==';')++q;
            AppendUtf8(out,NumericReference(value));
            return q-p;
        }
        auto e=q;
        while(e<n&&IsAsciiAlnum(s[e])&&e-q<32)++e;
        if(e==q)return 0;
        const auto name=s.substr(q,e-q);
        if(e<n&&s[e]==';') {
            if(const auto v=LookupEntity(name)) {AppendUtf8(out,v);return e+1-p;}
        }
        char32_t v=0;
        if(const auto len=LongestLegacyEntity(name,v)) {
            const auto after=q+len;
            if(attribute&&after<n&&(s[after]=='='||IsAsciiAlnum(s[after])))return 0;
            AppendUtf8(out,v);
            return len+1;
        }
        if(e<n&&s[e]==';')++stats_.unknownEntities;
        return 0;
    }
    std::string Decode(std::string_view raw,bool attribute) {
        std::string out;
        for(std::size_t i=0;i<raw.size();) {
            if(raw[i]!='&') {out.push_back(raw[i++]);continue;}
            // Resolved against the raw slice so lookahead never crosses its end.
            const auto used=Reference(raw,i,attribute,out);
            if(used) {i+=used;} else {out.push_back('&');++i;}
        }
        return out;
    }

    // ---- Tokenizer ----
    void Comment(std::size_t end,std::size_t skip) {
        ++stats_.commentsSkipped;
        p_=end==std::string_view::npos?s_.size():end+skip;
    }
    void Tag() {
        const auto n=s_.size();
        const auto q=p_+1;
        if(q>=n) {AddText("<");p_=n;return;}
        const char c=s_[q];
        if(c=='!') {
            if(s_.substr(q,3)=="!--") {
                const auto body=q+3;
                if(s_.substr(body,1)==">") {Comment(body,1);return;}
                if(s_.substr(body,2)=="->") {Comment(body,2);return;}
                auto end=s_.find("-->",body);
                const auto bang=s_.find("--!>",body);
                if(bang<end) {Comment(bang,4);return;}
                Comment(end,3);
                return;
            }
            Comment(s_.find('>',q),1);
            return;
        }
        if(c=='?') {
            const auto end=s_.find('>',q);
            const auto body=s_.substr(q,(end==std::string_view::npos?n:end)-q);
            if(StartsWithNoCase(body,"?xml")) {
                const auto at=AsciiLower(body).find("encoding");
                if(at!=std::string::npos) {
                    auto v=body.substr(at+8);
                    while(!v.empty()&&(IsHtmlSpace(v.front())||v.front()=='='))v.remove_prefix(1);
                    if(!v.empty()&&(v.front()=='"'||v.front()=='\'')) {
                        const char quote=v.front();v.remove_prefix(1);
                        const auto close=v.find(quote);
                        if(!IsUtf8Label(v.substr(0,close)))Fail(Error::unsupported_charset);
                    }
                }
            }
            Comment(end,1);
            return;
        }
        if(c=='/') {
            if(q+1>=n) {AddText("</");p_=n;return;}
            if(s_[q+1]=='>') {p_=q+2;return;}
            if(!IsAsciiAlpha(s_[q+1])) {Comment(s_.find('>',q),1);return;}
            p_=q+1;
            std::string name;
            std::vector<Attribute> ignored;
            bool selfClosing=false;
            if(!ReadTag(name,ignored,selfClosing))return;
            EndTag(name);
            return;
        }
        if(!IsAsciiAlpha(c)) {AddText("<");++p_;return;}
        p_=q;
        std::string name;
        std::vector<Attribute> attributes;
        bool selfClosing=false;
        if(!ReadTag(name,attributes,selfClosing))return;
        StartTag(name,std::move(attributes),selfClosing);
    }
    // Reads a tag name and attributes from p_ up to and including '>'. EOF in
    // a tag drops the tag (as HTML does) and returns false.
    bool ReadTag(std::string& name,std::vector<Attribute>& attributes,bool& selfClosing) {
        const auto n=s_.size();
        while(p_<n&&!IsHtmlSpace(s_[p_])&&s_[p_]!='/'&&s_[p_]!='>')name.push_back(AsciiLower(s_[p_++]));
        for(;;) {
            while(p_<n&&(IsHtmlSpace(s_[p_])||s_[p_]=='/')) {
                selfClosing=s_[p_]=='/'&&p_+1<n&&s_[p_+1]=='>';
                ++p_;
            }
            if(p_>=n) return false;
            if(s_[p_]=='>') {++p_;return true;}
            selfClosing=false;
            std::string attrName;
            attrName.push_back(AsciiLower(s_[p_++]));
            while(p_<n&&!IsHtmlSpace(s_[p_])&&s_[p_]!='/'&&s_[p_]!='>'&&s_[p_]!='=')attrName.push_back(AsciiLower(s_[p_++]));
            auto r=p_;
            while(r<n&&IsHtmlSpace(s_[r]))++r;
            std::string value;
            if(r<n&&s_[r]=='=') {
                ++r;
                while(r<n&&IsHtmlSpace(s_[r]))++r;
                if(r>=n) {p_=n;return false;}
                if(s_[r]=='"'||s_[r]=='\'') {
                    const char quote=s_[r];
                    const auto close=s_.find(quote,r+1);
                    if(close==std::string_view::npos) {p_=n;return false;}
                    value=Decode(s_.substr(r+1,close-r-1),true);
                    r=close+1;
                } else {
                    const auto start=r;
                    while(r<n&&!IsHtmlSpace(s_[r])&&s_[r]!='>')++r;
                    value=Decode(s_.substr(start,r-start),true);
                }
                p_=r;
            }
            bool duplicate=false;
            for(const auto& a:attributes)duplicate=duplicate||a.name==attrName;
            if(!duplicate&&attributes.size()<limits_.maximumAttributes)attributes.push_back({std::move(attrName),std::move(value)});
        }
    }
    // Raw text / RCDATA content up to the matching end tag (or EOF).
    void RawContent(const std::string& name,bool rcdata) {
        const auto n=s_.size();
        auto end=n,resume=n;
        for(auto at=s_.find("</",p_);at!=std::string_view::npos;at=s_.find("</",at+2)) {
            const auto after=at+2+name.size();
            if(after>n||!EqualsNoCase(s_.substr(at+2,name.size()),name))continue;
            if(after<n&&!IsHtmlSpace(s_[after])&&s_[after]!='/'&&s_[after]!='>')continue;
            end=at;
            const auto close=s_.find('>',after);
            resume=close==std::string_view::npos?n:close+1;
            break;
        }
        auto content=s_.substr(p_,end-p_);
        if(name=="textarea"&&!content.empty()&&content.front()=='\n')content.remove_prefix(1);
        if(!content.empty())AddText(rcdata?Decode(content,false):std::string(content));
        p_=resume;
        PopThrough(name);
    }

    // ---- Tree ----
    std::uint32_t NewNode(Node&& node) {
        if(tree_.nodes.size()>=limits_.maximumNodes+1)Fail(Error::node_limit);
        tree_.nodes.push_back(std::move(node));
        return std::uint32_t(tree_.nodes.size()-1);
    }
    Node& Current() {return tree_.nodes[stack_.back()];}
    const std::string& NameAt(std::size_t i) const {return tree_.nodes[stack_[i]].name;}
    void AddText(std::string_view text) {
        if(skipNewline_) {
            skipNewline_=false;
            if(!text.empty()&&text.front()=='\n')text.remove_prefix(1);
        }
        if(text.empty())return;
        auto& parent=Current();
        if(!parent.children.empty()) {
            auto& last=tree_.nodes[parent.children.back()];
            if(last.text) {last.data.append(text);return;}
        }
        const auto id=NewNode(Node{true,{},std::string(text),{},{}});
        tree_.nodes[stack_.back()].children.push_back(id);
    }
    std::uint32_t Insert(std::string name,std::vector<Attribute>&& attributes,bool push) {
        skipNewline_=false;
        const auto id=NewNode(Node{false,std::move(name),{},std::move(attributes),{}});
        Current().children.push_back(id);
        if(push) {
            if(stack_.size()>limits_.maximumDepth)Fail(Error::depth_limit);
            stack_.push_back(id);
        }
        return id;
    }
    // Index of the topmost open element named `name`, stopping at barriers.
    template<class Barrier> std::size_t Find(std::string_view name,Barrier&& barrier) const {
        for(std::size_t i=stack_.size();i-->1;) {
            if(NameAt(i)==name)return i;
            if(barrier(NameAt(i)))return 0;
        }
        return 0;
    }
    std::size_t FindDefault(std::string_view name) const {return Find(name,DefaultScopeBarrier);}
    void PopTo(std::size_t index) {if(index)stack_.resize(index);}
    void PopThrough(std::string_view name) {PopTo(FindDefault(name));}
    bool InForeign() const {
        for(std::size_t i=1;i<stack_.size();++i)if(NameAt(i)=="svg"||NameAt(i)=="math")return true;
        return false;
    }
    void CloseP() {
        PopTo(Find("p",[](std::string_view n) {return DefaultScopeBarrier(n)||n=="button";}));
    }
    // li / dd / dt: close an open item unless a special element (other than
    // address, div, p) intervenes.
    void CloseItem(std::initializer_list<std::string_view> names) {
        for(std::size_t i=stack_.size();i-->1;) {
            if(In(NameAt(i),names)) {PopTo(i);return;}
            if(IsSpecial(NameAt(i))&&!In(NameAt(i),{"address","div","p"}))return;
        }
    }
    std::size_t FindAny(std::initializer_list<std::string_view> names) const {
        for(std::size_t i=stack_.size();i-->1;)if(In(NameAt(i),names))return i;
        return 0;
    }
    void CheckCharset(const std::vector<Attribute>& attributes) {
        std::string_view label;
        bool contentType=false;
        std::string_view content;
        for(const auto& a:attributes) {
            if(a.name=="charset")label=a.value;
            if(a.name=="http-equiv"&&EqualsNoCase(TrimAsciiSpace(a.value),"content-type"))contentType=true;
            if(a.name=="content")content=a.value;
        }
        if(label.empty()&&contentType) {
            const auto lower=AsciiLower(content);
            const auto at=lower.find("charset");
            if(at!=std::string::npos) {
                auto v=content.substr(at+7);
                while(!v.empty()&&(IsHtmlSpace(v.front())||v.front()=='='))v.remove_prefix(1);
                if(!v.empty()&&(v.front()=='"'||v.front()=='\''))v.remove_prefix(1);
                auto e=std::size_t(0);
                while(e<v.size()&&!IsHtmlSpace(v[e])&&v[e]!=';'&&v[e]!='"'&&v[e]!='\'')++e;
                label=v.substr(0,e);
                if(label.empty())return;
            }
        }
        if(!label.empty()&&!IsUtf8Label(label))Fail(Error::unsupported_charset);
    }
    void StartTag(std::string name,std::vector<Attribute>&& attributes,bool selfClosing) {
        if(name=="html"||name=="head"||name=="body"||name=="frameset")return;
        if(name=="meta")CheckCharset(attributes);
        if(InForeign()) {
            Insert(std::move(name),std::move(attributes),!selfClosing);
            return;
        }
        if(ClosesP(name))CloseP();
        if(IsHeading(name)&&IsHeading(Current().name))stack_.pop_back();
        if(name=="li")CloseItem({"li"});
        if(name=="dd"||name=="dt")CloseItem({"dd","dt"});
        if(name=="option"&&Current().name=="option")stack_.pop_back();
        if(name=="optgroup") {
            if(Current().name=="option")stack_.pop_back();
            if(Current().name=="optgroup")stack_.pop_back();
        }
        if(name=="a")PopThrough("a");
        if(name=="button")PopThrough("button");
        if(name=="tr"||name=="thead"||name=="tbody"||name=="tfoot"||name=="caption"||name=="colgroup"||name=="col") {
            // Closes any open cell, row or section of the nearest table.
            const auto section=name=="tr"?FindAny({"table","thead","tbody","tfoot"}):FindAny({"table"});
            if(section)PopTo(section+1);
        }
        if(name=="td"||name=="th") {
            const auto at=FindAny({"tr","table","thead","tbody","tfoot"});
            if(at&&NameAt(at)=="tr") {
                PopTo(at+1);
            } else if(at&&In(NameAt(at),{"table","thead","tbody","tfoot"})) {
                PopTo(at+1);
                Insert("tr",{},true);
            }
        }
        if(name=="table") {
            // A table directly inside a table (not inside a cell or caption) closes the first.
            const auto at=FindAny({"table","td","th","caption"});
            if(at&&NameAt(at)=="table")PopTo(at);
        }
        if(name=="svg"||name=="math") {
            Insert(std::move(name),std::move(attributes),!selfClosing);
            return;
        }
        if(IsVoid(name)) {Insert(std::move(name),std::move(attributes),false);return;}
        const bool raw=IsRawText(name),rcdata=IsRcdata(name);
        const bool newline=name=="pre"||name=="listing";
        const bool plaintext=name=="plaintext";
        Insert(name,std::move(attributes),true);
        if(raw||rcdata) {RawContent(name,rcdata);return;}
        if(plaintext) {AddText(s_.substr(p_));p_=s_.size();return;}
        skipNewline_=newline;
    }
    void EndTag(const std::string& name) {
        if(name=="html"||name=="head"||name=="body"||name=="frameset")return;
        if(name=="br") {StartTag("br",{},false);return;}
        if(name=="p") {CloseP();return;}
        if(name=="li") {
            PopTo(Find("li",[](std::string_view n) {return DefaultScopeBarrier(n)||n=="ul"||n=="ol";}));
            return;
        }
        if(IsHeading(name)) {
            for(std::size_t i=stack_.size();i-->1;) {
                if(IsHeading(NameAt(i))) {PopTo(i);return;}
                if(DefaultScopeBarrier(NameAt(i)))return;
            }
            return;
        }
        if(In(name,{"table","thead","tbody","tfoot","tr","td","th","caption"})) {
            PopTo(Find(name,[](std::string_view n) {return n=="html"||n=="template"||n=="table";}));
            return;
        }
        if(IsSpecial(name)||name=="svg"||name=="math") {PopThrough(name);return;}
        // Any other end tag: the nearest same-named element, unless a special
        // element intervenes (then the end tag is ignored).
        for(std::size_t i=stack_.size();i-->1;) {
            if(NameAt(i)==name) {PopTo(i);return;}
            if(IsSpecial(NameAt(i)))return;
        }
    }

    std::string_view s_;
    const TreeLimits& limits_;
    TreeStats& stats_;
    Tree tree_;
    std::vector<std::uint32_t> stack_;
    std::size_t p_=0;
    bool skipNewline_=false;
};
}

Tree ParseHtml(Bytes bytes,const TreeLimits& limits,TreeStats& stats) {
    if(bytes.size()>=2) {
        const auto b0=std::to_integer<std::uint8_t>(bytes[0]),b1=std::to_integer<std::uint8_t>(bytes[1]);
        if((b0==0xfe&&b1==0xff)||(b0==0xff&&b1==0xfe))Fail(Error::unsupported_charset);
    }
    const auto text=PrepareText(bytes);
    return Builder(text,limits,stats).Run();
}
}
