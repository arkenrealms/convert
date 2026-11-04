#pragma once
#include "../text_documents.hpp"
#include "detail.hpp"
#include <map>
#include <optional>

// Internal plumbing shared by pptx.cpp, odf_text.cpp and epub.cpp. It reuses
// the capsule's ZipArchive, ParseXml and ModelBuilder unchanged; the OPC
// helpers below duplicate the (file-local) ones in docx.cpp with a
// caller-chosen error, because docx.cpp is not shared.
namespace arken::document::v1::detail::textdocs {
struct TextFailure {TextDocumentError error;};
[[noreturn]] inline void FailText(TextDocumentError error) {throw TextFailure{error};}

struct Context {
    const TextDocumentOptions& options;
    TextDocumentReport& report;
    std::uint32_t depth=0;
    std::uint64_t cells=0;
    void ChargeCells(std::uint64_t n) {
        if(n>options.maximumTableCells-cells)FailText(TextDocumentError::table_limit);
        cells+=n;
    }
};
// Bounds structural recursion independently of the XML depth limit, which a
// caller may raise far beyond a safe native stack depth.
class Nest {
public:
    explicit Nest(Context& context):context_(context) {
        if(context_.depth>=context_.options.maximumNesting)FailText(TextDocumentError::nesting_limit);
        ++context_.depth;
    }
    ~Nest() {--context_.depth;}
    Nest(const Nest&)=delete;
    Nest& operator=(const Nest&)=delete;
private:
    Context& context_;
};

// ---- XML helpers ----
inline bool Is(const XmlDocument& xml,const XmlElement& e,std::string_view ns,std::string_view local) {
    return e.local==local&&xml.Namespace(e)==ns;
}
template<class F> void ForChildren(const XmlDocument& xml,const XmlElement& e,F&& visit) {
    for(const auto& c:e.children)if(!c.text)visit(xml.elements[c.index]);
}
inline const XmlElement* FirstChild(const XmlDocument& xml,const XmlElement& e,std::string_view ns,std::string_view local) {
    for(const auto& c:e.children)if(!c.text&&Is(xml,xml.elements[c.index],ns,local))return &xml.elements[c.index];
    return nullptr;
}
// Concatenated character data of e and its descendants (bounded by the XML limits).
inline void AllText(const XmlDocument& xml,const XmlElement& e,std::string& out,std::uint32_t depth=0) {
    if(depth>256)return;
    for(const auto& c:e.children) {
        if(c.text)out+=xml.texts[c.index];
        else AllText(xml,xml.elements[c.index],out,depth+1);
    }
}
inline std::string CollapseSpace(std::string_view s) {
    std::string out;
    bool space=true;
    for(const char c:s) {
        if(c==' '||c=='\t'||c=='\n'||c=='\r') {if(!space)out.push_back(' ');space=true;}
        else {out.push_back(c);space=false;}
    }
    if(!out.empty()&&out.back()==' ')out.pop_back();
    return out;
}
// Non-negative decimal attribute; values above `maximum` (of any length) clamp to it.
inline int ParseCount(const std::string* value,int fallback,int maximum) {
    if(!value||value->empty())return fallback;
    long long result=0;
    for(const char c:*value) {
        if(c<'0'||c>'9')return fallback;
        result=result*10+(c-'0');
        if(result>maximum)result=maximum;
    }
    return int(result);
}
inline bool StartsNoCase(std::string_view s,std::string_view prefix) {
    return s.size()>=prefix.size()&&EqualsNoCase(s.substr(0,prefix.size()),prefix);
}
// Links the serializers may render; everything else is kept as plain text.
inline bool ExternalLink(std::string_view url) {
    return StartsNoCase(url,"http://")||StartsNoCase(url,"https://")||StartsNoCase(url,"mailto:");
}

// ---- Package helpers ----
inline std::optional<std::string> ReadPart(ZipArchive& zip,std::string_view name) {
    if(!name.empty()&&name.front()=='/')name.remove_prefix(1);
    const auto* entry=zip.Find(name);
    if(!entry)return std::nullopt;
    return zip.Read(*entry);
}
// Resolves a relative reference against a directory ("/a/b/") to an absolute
// part name. Fragments are dropped; ".." may not escape the root.
inline std::string ResolvePart(std::string_view sourceDirectory,std::string_view target,TextDocumentError invalid) {
    const auto fragment=target.find('#');
    if(fragment!=std::string_view::npos)target=target.substr(0,fragment);
    if(target.empty()||target.find('\\')!=std::string_view::npos||target.find("://")!=std::string_view::npos)FailText(invalid);
    for(const char c:target)if(std::uint8_t(c)<0x20)FailText(invalid);
    const std::string path=target.front()=='/'?std::string(target):std::string(sourceDirectory)+std::string(target);
    std::vector<std::string_view> segments;
    const std::string_view view(path);
    for(std::size_t start=0;start<view.size();) {
        auto end=view.find('/',start);
        if(end==std::string_view::npos)end=view.size();
        const auto segment=view.substr(start,end-start);
        if(segment=="..") {if(segments.empty())FailText(invalid);segments.pop_back();}
        else if(!segment.empty()&&segment!=".")segments.push_back(segment);
        start=end+1;
    }
    if(segments.empty())FailText(invalid);
    std::string result;
    for(const auto segment:segments) {result.push_back('/');result.append(segment);}
    return result;
}
inline std::string DirectoryOf(std::string_view part) {return std::string(part.substr(0,part.rfind('/')+1));}

// Open Packaging Conventions (PPTX).
inline constexpr std::string_view relationshipsNamespace="http://schemas.openxmlformats.org/package/2006/relationships";
inline constexpr std::string_view contentTypesNamespace="http://schemas.openxmlformats.org/package/2006/content-types";
inline constexpr std::string_view rTransitional="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
inline constexpr std::string_view rStrict="http://purl.oclc.org/ooxml/officeDocument/relationships";
struct Relationship {std::string id,type,target;bool external=false;};
inline bool RelationshipType(std::string_view type,std::string_view name) {
    for(const std::string_view base:{rTransitional,rStrict})
        if(type.size()==base.size()+1+name.size()&&type.substr(0,base.size())==base
            &&type[base.size()]=='/'&&type.substr(base.size()+1)==name)return true;
    return false;
}
inline std::vector<Relationship> ParseRelationships(const std::string& bytes,const XmlLimits& limits,TextDocumentError invalid) {
    const auto xml=ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Relationships"||xml.Namespace(root)!=relationshipsNamespace)FailText(invalid);
    std::vector<Relationship> result;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(e.local!="Relationship"||xml.Namespace(e)!=relationshipsNamespace)FailText(invalid);
        const auto* id=xml.Attribute(e,"","Id");
        const auto* type=xml.Attribute(e,"","Type");
        const auto* target=xml.Attribute(e,"","Target");
        const auto* mode=xml.Attribute(e,"","TargetMode");
        if(!id||id->empty()||!type||type->empty()||!target)FailText(invalid);
        if(mode&&*mode!="External"&&*mode!="Internal")FailText(invalid);
        for(const auto& r:result)if(r.id==*id)FailText(invalid);
        result.push_back({*id,*type,*target,mode&&*mode=="External"});
    }
    return result;
}
inline std::string RelationshipsPartFor(std::string_view part) {
    const auto slash=part.rfind('/');
    return std::string(part.substr(0,slash+1))+"_rels/"+std::string(part.substr(slash+1))+".rels";
}
struct ContentTypes {
    std::map<std::string,std::string> defaults,overrides; // Lower-cased keys.
    std::string For(std::string_view part) const {
        const auto o=overrides.find(AsciiLower(part));
        if(o!=overrides.end())return o->second;
        const auto dot=part.rfind('.');
        if(dot==std::string_view::npos||dot<part.rfind('/'))return {};
        const auto d=defaults.find(AsciiLower(part.substr(dot+1)));
        return d==defaults.end()?std::string():d->second;
    }
};
inline ContentTypes ParseContentTypes(const std::string& bytes,const XmlLimits& limits,TextDocumentError invalid) {
    const auto xml=ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Types"||xml.Namespace(root)!=contentTypesNamespace)FailText(invalid);
    ContentTypes types;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(xml.Namespace(e)!=contentTypesNamespace)FailText(invalid);
        const auto* type=xml.Attribute(e,"","ContentType");
        if(!type||type->empty())FailText(invalid);
        if(e.local=="Default") {
            const auto* extension=xml.Attribute(e,"","Extension");
            if(!extension||extension->empty()||!types.defaults.emplace(AsciiLower(*extension),*type).second)FailText(invalid);
        } else if(e.local=="Override") {
            const auto* part=xml.Attribute(e,"","PartName");
            if(!part||part->empty()||part->front()!='/'||!types.overrides.emplace(AsciiLower(*part),*type).second)FailText(invalid);
        } else {
            FailText(invalid);
        }
    }
    return types;
}

// ---- Flow: block assembly shared by the three extractors ----
//
// Text accumulates in one pending block. In body mode a block boundary emits
// it; inside a list item a boundary becomes a line break within the item (the
// model has no multi-paragraph items); inside a table cell (or a capture) it
// becomes the capture's separator, matching DOCX cells.
struct Format {bool bold=false,italic=false;std::string link;};
class Flow {
public:
    Flow(ModelBuilder& builder,Context& context):builder_(builder),context_(context) {}

    // Whitespace-collapsing text (ODF and HTML rules): runs of space, tab, CR
    // and LF become one space; leading space at a block or line start is dropped.
    void Collapsed(std::string_view text,const Format& f) {
        std::string out;
        bool space=lastSpace_||AtStart();
        for(const char c:text) {
            if(c==' '||c=='\t'||c=='\n'||c=='\r') {if(!space) {out.push_back(' ');space=true;}}
            else {out.push_back(c);space=false;}
        }
        if(out.empty())return;
        Put(out,f);
        lastSpace_=out.back()==' ';
    }
    // Text kept exactly (ODF text:s, tabs, DrawingML runs, preformatted text).
    void Literal(std::string_view text,const Format& f) {
        if(text.empty())return;
        Put(text,f);
        lastSpace_=false;
    }
    void Break(const Format& f) {
        TrimSpace();
        if(separator_||!ModelBuilder::HasText(runs_))return; // No leading breaks.
        Put("\n",f);
        lastSpace_=false;
    }
    void Boundary() {
        if(mode_==Mode::body) {Emit();return;}
        TrimSpace();TrimBreaks();
        if(ModelBuilder::HasText(runs_))separator_=true;
        lastSpace_=false;
    }
    template<class F> void Paragraph(F&& body) {Boundary();body();Boundary();}
    template<class F> void Heading(int level,F&& body) {
        if(mode_!=Mode::body) {Paragraph(body);return;}
        Emit();
        const auto saved=meta_;
        meta_=Block{};meta_.kind=BlockKind::heading;meta_.level=std::uint8_t(level<1?1:level>6?6:level);
        body();
        Emit();
        meta_=saved;
    }
    template<class F> void ListItem(int level,bool ordered,F&& body) {
        if(mode_==Mode::cell) {Paragraph(body);return;}
        Emit();
        const auto savedMeta=meta_;const auto savedMode=mode_;
        meta_=Block{};meta_.kind=BlockKind::list_item;meta_.level=std::uint8_t(level<0?0:level>8?8:level);meta_.ordered=ordered;
        mode_=Mode::item;
        body();
        Emit();
        meta_=savedMeta;mode_=savedMode;
    }
    // A quotation: paragraphs inside become block_quote blocks. Headings, lists and
    // tables inside keep their own kind (the model has no nested containers). In a list
    // item or table cell it is ordinary paragraph text.
    template<class F> void Quote(F&& body) {
        if(mode_!=Mode::body) {Paragraph(body);return;}
        Emit();
        const auto saved=meta_;
        meta_=Block{};meta_.kind=BlockKind::block_quote;
        body();
        Emit();
        meta_=saved;
    }
    // Preformatted text kept exactly; body should use Literal and Break. In a list item
    // or table cell it is ordinary paragraph text with its line breaks.
    template<class F> void Code(std::string_view language,F&& body) {
        if(mode_!=Mode::body) {Paragraph(body);return;}
        Emit();
        const auto saved=meta_;
        meta_=Block{};meta_.kind=BlockKind::code_block;
        if(language.size()<=32)meta_.language=std::string(language);
        body();
        Emit();
        meta_=saved;
    }
    // A thematic break between sections; a paragraph boundary inside items and cells.
    void Rule() {
        if(mode_!=Mode::body) {Boundary();return;}
        Emit();
        Block block;
        block.kind=BlockKind::thematic_break;
        builder_.Push(std::move(block));
    }
    // Runs body with a fresh pending run list and returns it; boundaries
    // inside become `separator`. Used for table cells, titles and notes.
    template<class F> Cell Capture(F&& body,char separator=' ') {
        auto runs=std::exchange(runs_,{});
        const auto mode=std::exchange(mode_,Mode::cell);
        const auto sep=std::exchange(separatorChar_,separator);
        const auto pending=std::exchange(separator_,false);
        const auto space=std::exchange(lastSpace_,false);
        body();
        TrimSpace();TrimBreaks();
        Cell cell=std::exchange(runs_,std::move(runs));
        mode_=mode;separatorChar_=sep;separator_=pending;lastSpace_=space;
        return cell;
    }
    void Table(std::vector<Row>&& rows) {
        if(rows.empty())return;
        if(mode_==Mode::cell) { // Nested tables are flattened into the enclosing cell's text.
            for(const auto& row:rows)for(const auto& cell:row) {
                if(!ModelBuilder::HasText(cell))continue;
                if(ModelBuilder::HasText(runs_))separator_=true;
                for(const auto& run:cell)Put(run.text,Format{run.bold,run.italic,run.link});
                lastSpace_=false;
            }
            return;
        }
        Emit();
        Block block;
        block.kind=BlockKind::table;
        block.rows=std::move(rows);
        builder_.Push(std::move(block));
    }
    // Pushes a ready block (used for slide headings and notes).
    void Push(Block&& block) {Emit();builder_.Push(std::move(block));}
    void AppendCopy(std::vector<Run>& target,const std::vector<Run>& source) {
        for(const auto& run:source)builder_.Append(target,run.text,run.bold,run.italic,run.link);
    }
    void Append(std::vector<Run>& target,std::string_view text,bool bold,bool italic,std::string_view link={}) {
        builder_.Append(target,text,bold,italic,link);
    }
    void Finish() {Emit();}
    Context& Ctx() {return context_;}
private:
    enum class Mode {body,item,cell};
    bool AtStart() const {
        if(separator_||runs_.empty())return true;
        const auto& t=runs_.back().text;
        return t.empty()||t.back()=='\n';
    }
    void Put(std::string_view text,const Format& f) {
        if(separator_) {
            separator_=false;
            const char sep=mode_==Mode::item?'\n':separatorChar_;
            builder_.Append(runs_,std::string_view(&sep,1),false,false,{});
        }
        builder_.Append(runs_,text,f.bold,f.italic,f.link);
    }
    void TrimSpace() {
        if(lastSpace_&&!runs_.empty()&&!runs_.back().text.empty()&&runs_.back().text.back()==' ') {
            runs_.back().text.pop_back();
            if(runs_.back().text.empty())runs_.pop_back();
        }
        lastSpace_=false;
    }
    void TrimBreaks() {
        while(!runs_.empty()) {
            auto& t=runs_.back().text;
            while(!t.empty()&&t.back()=='\n')t.pop_back();
            if(!t.empty())break;
            runs_.pop_back();
        }
    }
    void Emit() {
        TrimSpace();TrimBreaks();
        if(mode_!=Mode::cell) {
            Block block=meta_;
            block.runs=std::move(runs_);
            builder_.Push(std::move(block));
        }
        runs_.clear();
        separator_=false;lastSpace_=false;
    }

    ModelBuilder& builder_;
    Context& context_;
    std::vector<Run> runs_;
    Block meta_;
    Mode mode_=Mode::body;
    char separatorChar_=' ';
    bool separator_=false,lastSpace_=false;
};

// Extractors (each throws Failure or TextFailure; the API guards them).
void ExtractPptx(Bytes bytes,Context& context,Document& document);
void ExtractOdf(Bytes bytes,bool presentation,Context& context,Document& document);
void ExtractEpub(Bytes bytes,Context& context,Document& document);
}
