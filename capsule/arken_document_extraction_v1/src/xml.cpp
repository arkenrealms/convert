#include "detail.hpp"
#include <map>

// Bounded, non-validating XML 1.0 reader for package parts. Admits UTF-8 only,
// rejects any DOCTYPE (so no DTD, internal subset, entity declarations or
// external identifiers can exist), expands only the five predefined entities
// and numeric character references, never fetches anything, skips comments
// and processing instructions, and resolves namespaces.
namespace arken::document::v1::detail {
namespace {
bool Space(char c) {return c==' '||c=='\t'||c=='\n'||c=='\r';}
bool XmlChar(char32_t c) {
    return c==0x9||c==0xa||c==0xd||(c>=0x20&&c<=0xd7ff)||(c>=0xe000&&c<=0xfffd)||(c>=0x10000&&c<=0x10ffff);
}
bool NameStart(unsigned char c) {return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c==':'||c>=0x80;}
bool NameChar(unsigned char c) {return NameStart(c)||(c>='0'&&c<='9')||c=='-'||c=='.';}

class Parser {
public:
    Parser(std::string_view s,const XmlLimits& limits):s_(s),limits_(limits) {}
    XmlDocument Run() {
        Prolog();
        Intern(""); // Index 0: no namespace.
        bindings_.push_back({"xml",Intern(xmlNamespace)});
        bindings_.push_back({"",0});
        bool rootSeen=false,rootClosed=false;
        while(p_<s_.size()) {
            if(s_[p_]!='<') {
                const auto start=p_;
                while(p_<s_.size()&&s_[p_]!='<')++p_;
                if(open_.empty()) {
                    for(auto i=start;i<p_;++i)if(!Space(s_[i]))Fail(Error::xml_malformed);
                    continue;
                }
                Text(s_.substr(start,p_-start));
                continue;
            }
            if(Starts("<!--")) {Comment();continue;}
            if(Starts("<![CDATA[")) {
                if(open_.empty())Fail(Error::xml_malformed);
                const auto end=s_.find("]]>",p_+9);
                if(end==std::string_view::npos)Fail(Error::xml_malformed);
                AddText(Normalize(s_.substr(p_+9,end-p_-9)));
                p_=end+3;
                continue;
            }
            if(StartsNoCase("<!DOCTYPE"))Fail(Error::xml_doctype);
            if(Starts("<!"))Fail(Error::xml_malformed);
            if(Starts("<?")) {Instruction();continue;}
            if(Starts("</")) {EndTag();if(open_.empty())rootClosed=true;continue;}
            if(rootClosed||(rootSeen&&open_.empty()))Fail(Error::xml_malformed);
            rootSeen=true;
            StartTag();
            if(open_.empty())rootClosed=true;
        }
        if(!rootSeen||!open_.empty())Fail(Error::xml_malformed);
        return std::move(doc_);
    }
private:
    struct Binding {std::string prefix;std::uint32_t ns;};
    struct Open {std::uint32_t element;std::string qname;std::size_t bindings;};

    bool Starts(std::string_view t) const {return s_.substr(p_,t.size())==t;}
    bool StartsNoCase(std::string_view t) const {return EqualsNoCase(s_.substr(p_,t.size()),t);}
    void SkipSpace() {while(p_<s_.size()&&Space(s_[p_]))++p_;}
    void Expect(char c) {if(p_>=s_.size()||s_[p_]!=c)Fail(Error::xml_malformed);++p_;}

    void Prolog() {
        if(s_.size()>=2&&((s_[0]=='\xfe'&&s_[1]=='\xff')||(s_[0]=='\xff'&&s_[1]=='\xfe')))Fail(Error::xml_encoding);
        if(s_.size()>=2&&(s_[0]=='\0'||s_[1]=='\0'))Fail(Error::xml_encoding); // UTF-16/32 without BOM.
        if(s_.substr(0,3)=="\xef\xbb\xbf")p_=3;
        // Whole-part validation: strict UTF-8 and XML Char only.
        char32_t c=0;
        for(auto q=p_;q<s_.size();) {
            const auto n=DecodeUtf8(s_,q,c);
            if(!n)Fail(Error::xml_encoding);
            if(!XmlChar(c))Fail(Error::xml_malformed);
            q+=n;
        }
        if(Starts("<?xml")&&p_+5<s_.size()&&Space(s_[p_+5])) {
            const auto end=s_.find("?>",p_);
            if(end==std::string_view::npos)Fail(Error::xml_malformed);
            auto decl=s_.substr(p_+5,end-p_-5);
            p_=end+2;
            bool version=false;
            for(std::size_t q=0;;) {
                while(q<decl.size()&&Space(decl[q]))++q;
                if(q>=decl.size())break;
                const auto eq=decl.find('=',q);
                if(eq==std::string_view::npos)Fail(Error::xml_malformed);
                auto name=decl.substr(q,eq-q);
                while(!name.empty()&&Space(name.back()))name.remove_suffix(1);
                q=eq+1;
                while(q<decl.size()&&Space(decl[q]))++q;
                if(q>=decl.size()||(decl[q]!='"'&&decl[q]!='\''))Fail(Error::xml_malformed);
                const auto close=decl.find(decl[q],q+1);
                if(close==std::string_view::npos)Fail(Error::xml_malformed);
                const auto value=decl.substr(q+1,close-q-1);
                q=close+1;
                if(name=="version") {if(value.substr(0,2)!="1.")Fail(Error::xml_malformed);version=true;}
                else if(name=="encoding") {if(!EqualsNoCase(value,"UTF-8")&&!EqualsNoCase(value,"UTF8"))Fail(Error::xml_encoding);}
                else if(name=="standalone") {if(value!="yes"&&value!="no")Fail(Error::xml_malformed);}
                else Fail(Error::xml_malformed);
            }
            if(!version)Fail(Error::xml_malformed);
        }
    }
    void Comment() {
        const auto end=s_.find("-->",p_+4);
        if(end==std::string_view::npos)Fail(Error::xml_malformed);
        p_=end+3;
    }
    void Instruction() {
        p_+=2;
        const auto target=Name();
        if(EqualsNoCase(target,"xml"))Fail(Error::xml_malformed); // Declaration only at offset 0.
        const auto end=s_.find("?>",p_);
        if(end==std::string_view::npos)Fail(Error::xml_malformed);
        p_=end+2;
    }
    std::string_view Name() {
        const auto start=p_;
        if(p_>=s_.size()||!NameStart(std::uint8_t(s_[p_])))Fail(Error::xml_malformed);
        while(p_<s_.size()&&NameChar(std::uint8_t(s_[p_])))++p_;
        if(p_-start>limits_.maximumNameBytes)Fail(Error::xml_limit);
        return s_.substr(start,p_-start);
    }
    static void Split(std::string_view qname,std::string_view& prefix,std::string_view& local) {
        const auto colon=qname.find(':');
        if(colon==std::string_view::npos) {prefix={};local=qname;return;}
        prefix=qname.substr(0,colon);local=qname.substr(colon+1);
        if(prefix.empty()||local.empty()||local.find(':')!=std::string_view::npos)Fail(Error::xml_malformed);
    }
    std::uint32_t Intern(std::string_view uri) {
        const auto found=interned_.find(std::string(uri));
        if(found!=interned_.end())return found->second;
        const auto index=std::uint32_t(doc_.namespaces.size());
        doc_.namespaces.emplace_back(uri);
        interned_.emplace(std::string(uri),index);
        return index;
    }
    std::uint32_t Resolve(std::string_view prefix) const {
        for(auto i=bindings_.size();i-->0;)if(bindings_[i].prefix==prefix)return bindings_[i].ns;
        Fail(Error::xml_malformed); // Undeclared prefix.
    }
    void Node() {if(++nodes_>limits_.maximumNodes)Fail(Error::xml_limit);}
    void Charge(std::size_t bytes) {
        if(bytes>limits_.maximumTokenBytes)Fail(Error::xml_limit);
        if(bytes>limits_.maximumTextBytes-text_)Fail(Error::xml_limit);
        text_+=bytes;
    }
    // Literal line ends become LF (XML 1.0 section 2.11).
    std::string Normalize(std::string_view raw) {
        Charge(raw.size());
        std::string out;out.reserve(raw.size());
        for(std::size_t i=0;i<raw.size();++i) {
            if(raw[i]=='\r') {out.push_back('\n');if(i+1<raw.size()&&raw[i+1]=='\n')++i;}
            else out.push_back(raw[i]);
        }
        return out;
    }
    // Decodes references and normalizes line ends; attributes also map literal
    // whitespace to spaces (CDATA attribute-value normalization).
    std::string Decode(std::string_view raw,bool attribute) {
        Charge(raw.size());
        std::string out;out.reserve(raw.size());
        for(std::size_t i=0;i<raw.size();) {
            const char c=raw[i];
            if(c=='&') {
                const auto semi=raw.find(';',i);
                if(semi==std::string_view::npos||semi-i>12)Fail(Error::xml_malformed);
                const auto ref=raw.substr(i+1,semi-i-1);
                i=semi+1;
                if(ref=="lt")out.push_back('<');
                else if(ref=="gt")out.push_back('>');
                else if(ref=="amp")out.push_back('&');
                else if(ref=="apos")out.push_back('\'');
                else if(ref=="quot")out.push_back('"');
                else if(!ref.empty()&&ref[0]=='#') {
                    const bool hex=ref.size()>1&&ref[1]=='x';
                    const auto digits=ref.substr(hex?2:1);
                    if(digits.empty()||digits.size()>8)Fail(Error::xml_malformed);
                    char32_t value=0;
                    for(const char d:digits) {
                        unsigned v=0;
                        if(d>='0'&&d<='9')v=unsigned(d-'0');
                        else if(hex&&d>='a'&&d<='f')v=unsigned(d-'a'+10);
                        else if(hex&&d>='A'&&d<='F')v=unsigned(d-'A'+10);
                        else Fail(Error::xml_malformed);
                        value=value*(hex?16:10)+v;
                        if(value>0x10ffff)Fail(Error::xml_malformed);
                    }
                    if(!XmlChar(value)||(value>=0xd800&&value<=0xdfff))Fail(Error::xml_malformed);
                    AppendUtf8(out,value);
                } else {
                    Fail(Error::xml_entity);
                }
                continue;
            }
            if(attribute&&c=='<')Fail(Error::xml_malformed);
            if(c=='\r') {out.push_back(attribute?' ':'\n');i+=(i+1<raw.size()&&raw[i+1]=='\n')?2:1;continue;}
            if(attribute&&(c=='\n'||c=='\t')) {out.push_back(' ');++i;continue;}
            out.push_back(c);++i;
        }
        return out;
    }
    void AddText(std::string&& text) {
        if(text.empty())return;
        Node();
        const auto index=std::uint32_t(doc_.texts.size());
        doc_.texts.push_back(std::move(text));
        doc_.elements[open_.back().element].children.push_back({true,index});
    }
    void Text(std::string_view raw) {AddText(Decode(raw,false));}
    void StartTag() {
        ++p_;
        const auto qname=Name();
        struct Raw {std::string_view qname;std::string value;};
        std::vector<Raw> attributes;
        bool empty=false;
        for(;;) {
            const auto before=p_;
            SkipSpace();
            if(p_>=s_.size())Fail(Error::xml_malformed);
            if(s_[p_]=='>') {++p_;break;}
            if(Starts("/>")) {p_+=2;empty=true;break;}
            if(p_==before)Fail(Error::xml_malformed); // Attributes need separating whitespace.
            const auto name=Name();
            SkipSpace();Expect('=');SkipSpace();
            if(p_>=s_.size()||(s_[p_]!='"'&&s_[p_]!='\''))Fail(Error::xml_malformed);
            const auto quote=s_[p_++];
            const auto close=s_.find(quote,p_);
            if(close==std::string_view::npos)Fail(Error::xml_malformed);
            if(close-p_>limits_.maximumTokenBytes)Fail(Error::xml_limit);
            if(attributes.size()>=limits_.maximumAttributes)Fail(Error::xml_limit);
            for(const auto& a:attributes)if(a.qname==name)Fail(Error::xml_malformed);
            attributes.push_back({name,Decode(s_.substr(p_,close-p_),true)});
            p_=close+1;
        }
        if(open_.size()>=limits_.maximumDepth)Fail(Error::xml_limit);
        Node();
        const auto bindingMark=bindings_.size();
        for(const auto& a:attributes) {
            if(a.qname=="xmlns") {bindings_.push_back({"",a.value.empty()?0:Intern(a.value)});continue;}
            if(a.qname.substr(0,6)=="xmlns:") {
                const auto prefix=a.qname.substr(6);
                if(prefix.empty()||a.value.empty()||prefix=="xmlns")Fail(Error::xml_malformed);
                if(prefix=="xml"&&a.value!=xmlNamespace)Fail(Error::xml_malformed);
                bindings_.push_back({std::string(prefix),Intern(a.value)});
            }
        }
        XmlElement element;
        std::string_view prefix,local;
        Split(qname,prefix,local);
        element.ns=Resolve(prefix);
        element.local=std::string(local);
        for(auto& a:attributes) {
            if(a.qname=="xmlns"||a.qname.substr(0,6)=="xmlns:")continue;
            std::string_view ap,al;
            Split(a.qname,ap,al);
            XmlAttribute attribute;
            attribute.ns=ap.empty()?0:Resolve(ap);
            attribute.local=std::string(al);
            attribute.value=std::move(a.value);
            for(const auto& other:element.attributes)
                if(other.ns==attribute.ns&&other.local==attribute.local)Fail(Error::xml_malformed);
            element.attributes.push_back(std::move(attribute));
        }
        const auto index=std::uint32_t(doc_.elements.size());
        if(!open_.empty()) {
            element.parent=open_.back().element;
            doc_.elements[element.parent].children.push_back({false,index});
        }
        doc_.elements.push_back(std::move(element));
        if(empty) {bindings_.resize(bindingMark);return;}
        open_.push_back({index,std::string(qname),bindingMark});
    }
    void EndTag() {
        p_+=2;
        const auto qname=Name();
        SkipSpace();Expect('>');
        if(open_.empty()||open_.back().qname!=qname)Fail(Error::xml_malformed);
        bindings_.resize(open_.back().bindings);
        open_.pop_back();
    }

    std::string_view s_;
    const XmlLimits& limits_;
    std::size_t p_=0;
    std::uint64_t nodes_=0,text_=0;
    XmlDocument doc_;
    std::map<std::string,std::uint32_t> interned_;
    std::vector<Binding> bindings_;
    std::vector<Open> open_;
};
}

XmlDocument ParseXml(std::string_view bytes,const XmlLimits& limits) {
    Parser parser(bytes,limits);
    return parser.Run();
}
}
