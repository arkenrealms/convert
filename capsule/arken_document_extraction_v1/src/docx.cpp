#include "detail.hpp"
#include <cstring>
#include <map>
#include <optional>

// WordprocessingML main-story extraction. Policy (see NOTES.md): the body of
// the officeDocument part only; paragraphs, runs, tabs, breaks, headings via
// paragraph styles, numbering, tables, hyperlinks (relationship and HYPERLINK
// field) and field results. Headers, footers, notes, comments, text boxes,
// drawings, deleted revisions and field instructions are not extracted.
namespace arken::document::v1::detail {
namespace {
constexpr std::string_view wTransitional="http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr std::string_view wStrict="http://purl.oclc.org/ooxml/wordprocessingml/main";
constexpr std::string_view rTransitional="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view rStrict="http://purl.oclc.org/ooxml/officeDocument/relationships";
constexpr std::string_view contentTypesNamespace="http://schemas.openxmlformats.org/package/2006/content-types";
constexpr std::string_view relationshipsNamespace="http://schemas.openxmlformats.org/package/2006/relationships";
constexpr std::string_view documentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
constexpr std::string_view templateType="application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml";
constexpr std::string_view macroDocumentType="application/vnd.ms-word.document.macroEnabled.main+xml";
constexpr std::string_view macroTemplateType="application/vnd.ms-word.template.macroEnabledTemplate.main+xml";
constexpr std::string_view vbaProjectType="application/vnd.ms-office.vbaProject";
constexpr std::size_t maximumFieldInstruction=4096;
constexpr std::size_t maximumFieldDepth=64;

bool RelationshipType(std::string_view type,std::string_view name) {
    for(const std::string_view base:{rTransitional,rStrict})
        if(type.size()==base.size()+1+name.size()&&type.substr(0,base.size())==base
            &&type[base.size()]=='/'&&type.substr(base.size()+1)==name)return true;
    return false;
}
int ParseInt(const std::string* value,int fallback) {
    if(!value||value->empty()||value->size()>9)return fallback;
    int result=0;
    for(const char c:*value) {if(c<'0'||c>'9')return fallback;result=result*10+(c-'0');}
    return result;
}

struct Relationship {std::string id,type,target;bool external=false;};
std::vector<Relationship> ParseRelationships(const std::string& bytes,const XmlLimits& limits) {
    const auto xml=ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Relationships"||xml.Namespace(root)!=relationshipsNamespace)Fail(Error::docx_invalid_package);
    std::vector<Relationship> result;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(e.local!="Relationship"||xml.Namespace(e)!=relationshipsNamespace)Fail(Error::docx_invalid_package);
        const auto* id=xml.Attribute(e,"","Id");
        const auto* type=xml.Attribute(e,"","Type");
        const auto* target=xml.Attribute(e,"","Target");
        const auto* mode=xml.Attribute(e,"","TargetMode");
        if(!id||id->empty()||!type||type->empty()||!target)Fail(Error::docx_invalid_package);
        if(mode&&*mode!="External"&&*mode!="Internal")Fail(Error::docx_invalid_package);
        for(const auto& r:result)if(r.id==*id)Fail(Error::docx_invalid_package);
        result.push_back({*id,*type,*target,mode&&*mode=="External"});
    }
    return result;
}
// Resolves an internal relationship target to an absolute part name.
std::string ResolvePart(std::string_view sourceDirectory,std::string_view target) {
    const auto fragment=target.find('#');
    if(fragment!=std::string_view::npos)target=target.substr(0,fragment);
    if(target.empty()||target.find('\\')!=std::string_view::npos||target.find("://")!=std::string_view::npos)
        Fail(Error::docx_invalid_package);
    for(const char c:target)if(std::uint8_t(c)<0x20)Fail(Error::docx_invalid_package);
    const std::string path=target.front()=='/'?std::string(target):std::string(sourceDirectory)+std::string(target);
    std::vector<std::string_view> segments;
    const std::string_view view(path);
    for(std::size_t start=0;start<view.size();) {
        auto end=view.find('/',start);
        if(end==std::string_view::npos)end=view.size();
        const auto segment=view.substr(start,end-start);
        if(segment=="..") {if(segments.empty())Fail(Error::docx_invalid_package);segments.pop_back();}
        else if(!segment.empty()&&segment!=".")segments.push_back(segment);
        start=end+1;
    }
    if(segments.empty())Fail(Error::docx_invalid_package);
    std::string result;
    for(const auto segment:segments) {result.push_back('/');result.append(segment);}
    return result;
}
std::string DirectoryOf(std::string_view part) {return std::string(part.substr(0,part.rfind('/')+1));}
std::string RelationshipsPartFor(std::string_view part) {
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
ContentTypes ParseContentTypes(const std::string& bytes,const XmlLimits& limits) {
    const auto xml=ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Types"||xml.Namespace(root)!=contentTypesNamespace)Fail(Error::docx_invalid_package);
    ContentTypes types;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(xml.Namespace(e)!=contentTypesNamespace)Fail(Error::docx_invalid_package);
        const auto* type=xml.Attribute(e,"","ContentType");
        if(!type||type->empty())Fail(Error::docx_invalid_package);
        if(e.local=="Default") {
            const auto* extension=xml.Attribute(e,"","Extension");
            if(!extension||extension->empty()||!types.defaults.emplace(AsciiLower(*extension),*type).second)Fail(Error::docx_invalid_package);
        } else if(e.local=="Override") {
            const auto* part=xml.Attribute(e,"","PartName");
            if(!part||part->empty()||part->front()!='/'||!types.overrides.emplace(AsciiLower(*part),*type).second)Fail(Error::docx_invalid_package);
        } else {
            Fail(Error::docx_invalid_package);
        }
    }
    return types;
}

// Element helpers over both the transitional and strict WordprocessingML namespaces.
struct Word {
    const XmlDocument& xml;
    bool Is(const XmlElement& e,std::string_view local) const {
        if(e.local!=local)return false;
        const auto ns=xml.Namespace(e);
        return ns==wTransitional||ns==wStrict;
    }
    const std::string* Attr(const XmlElement& e,std::string_view local) const {
        if(const auto* v=xml.Attribute(e,wTransitional,local))return v;
        return xml.Attribute(e,wStrict,local);
    }
    const std::string* RelAttr(const XmlElement& e,std::string_view local) const {
        if(const auto* v=xml.Attribute(e,rTransitional,local))return v;
        return xml.Attribute(e,rStrict,local);
    }
    const XmlElement* Child(const XmlElement& e,std::string_view local) const {
        for(const auto& c:e.children)if(!c.text&&Is(xml.elements[c.index],local))return &xml.elements[c.index];
        return nullptr;
    }
    const std::string* ChildValue(const XmlElement& e,std::string_view local) const {
        const auto* c=Child(e,local);
        return c?Attr(*c,"val"):nullptr;
    }
    // On/off properties: present without w:val, or with a true value.
    bool Toggle(const XmlElement& e,std::string_view local) const {
        const auto* c=Child(e,local);
        if(!c)return false;
        const auto* v=Attr(*c,"val");
        return !v||!(*v=="0"||*v=="false"||*v=="off");
    }
    std::string Text(const XmlElement& e) const {
        std::string out;
        for(const auto& c:e.children)if(c.text)out+=xml.texts[c.index];
        return out;
    }
};

int HeadingFromName(std::string_view name) {
    const auto lower=AsciiLower(name);
    if(lower=="title")return 1;
    if(lower.size()==9&&lower.substr(0,8)=="heading "&&lower[8]>='1'&&lower[8]<='6')return lower[8]-'0';
    return 0;
}
int HeadingFromStyleId(std::string_view id) {
    const auto lower=AsciiLower(id);
    if(lower=="title")return 1;
    if(lower.size()==8&&lower.substr(0,7)=="heading"&&lower[7]>='1'&&lower[7]<='6')return lower[7]-'0';
    return 0;
}

struct Styles {
    struct Style {int heading=0;std::string basedOn;bool numbered=false;std::string numId;int ilvl=-1;};
    std::map<std::string,Style> byId;
    bool present=false;
    void Parse(const XmlDocument& xml) {
        const Word w{xml};
        if(!w.Is(xml.Root(),"styles"))Fail(Error::docx_invalid_package);
        present=true;
        for(const auto& c:xml.Root().children) {
            if(c.text)continue;
            const auto& e=xml.elements[c.index];
            if(!w.Is(e,"style"))continue;
            const auto* type=w.Attr(e,"type");
            const auto* id=w.Attr(e,"styleId");
            if(!id||(type&&*type!="paragraph"))continue;
            Style s;
            if(const auto* name=w.ChildValue(e,"name"))s.heading=HeadingFromName(*name);
            if(const auto* based=w.ChildValue(e,"basedOn"))s.basedOn=*based;
            if(const auto* ppr=w.Child(e,"pPr")) {
                const auto outline=ParseInt(w.ChildValue(*ppr,"outlineLvl"),-1);
                if(!s.heading&&outline>=0&&outline<=5)s.heading=outline+1;
                if(const auto* num=w.Child(*ppr,"numPr")) {
                    if(const auto* numId=w.ChildValue(*num,"numId")) {s.numbered=true;s.numId=*numId;}
                    s.ilvl=ParseInt(w.ChildValue(*num,"ilvl"),-1);
                }
            }
            byId.emplace(*id,std::move(s)); // First definition wins; later duplicates are ignored.
        }
    }
    // Resolves heading level and inherited numbering through basedOn (bounded chain).
    Style Resolve(const std::string& id) const {
        Style result;
        bool haveHeading=false,haveNumbering=false,haveLevel=false;
        std::string current=id;
        for(int step=0;step<16&&!current.empty();++step) {
            const auto found=byId.find(current);
            if(found==byId.end()) {
                if(!haveHeading&&step==0) {result.heading=HeadingFromStyleId(current);haveHeading=result.heading!=0;}
                break;
            }
            const auto& s=found->second;
            if(!haveHeading&&s.heading) {result.heading=s.heading;haveHeading=true;}
            if(!haveNumbering&&s.numbered) {result.numbered=true;result.numId=s.numId;haveNumbering=true;}
            if(!haveLevel&&s.ilvl>=0) {result.ilvl=s.ilvl;haveLevel=true;}
            current=s.basedOn;
        }
        return result;
    }
};

struct Numbering {
    std::map<std::string,std::string> abstractOf;
    std::map<std::pair<std::string,int>,std::string> abstractFormat,overrideFormat;
    void Parse(const XmlDocument& xml) {
        const Word w{xml};
        if(!w.Is(xml.Root(),"numbering"))Fail(Error::docx_invalid_package);
        const auto levels=[&](const XmlElement& owner,const std::string& key,auto& target) {
            for(const auto& c:owner.children) {
                if(c.text)continue;
                const auto& lvl=xml.elements[c.index];
                if(!w.Is(lvl,"lvl"))continue;
                const auto level=ParseInt(w.Attr(lvl,"ilvl"),-1);
                if(const auto* format=w.ChildValue(lvl,"numFmt");format&&level>=0)target.emplace(std::pair{key,level},*format);
            }
        };
        for(const auto& c:xml.Root().children) {
            if(c.text)continue;
            const auto& e=xml.elements[c.index];
            if(w.Is(e,"abstractNum")) {
                if(const auto* id=w.Attr(e,"abstractNumId"))levels(e,*id,abstractFormat);
            } else if(w.Is(e,"num")) {
                const auto* id=w.Attr(e,"numId");
                const auto* abstract=w.ChildValue(e,"abstractNumId");
                if(!id||!abstract)continue;
                abstractOf.emplace(*id,*abstract);
                for(const auto& o:e.children) {
                    if(o.text||!w.Is(xml.elements[o.index],"lvlOverride"))continue;
                    levels(xml.elements[o.index],*id,overrideFormat);
                }
            }
        }
    }
    // Ordered unless the level is a bullet, unnumbered, or unknown.
    bool Ordered(const std::string& numId,int level) const {
        auto format=overrideFormat.find({numId,level});
        if(format==overrideFormat.end()) {
            const auto abstract=abstractOf.find(numId);
            if(abstract==abstractOf.end())return false;
            format=abstractFormat.find({abstract->second,level});
            if(format==abstractFormat.end())return false;
        }
        return format->second!="bullet"&&format->second!="none";
    }
};

class Walker {
public:
    Walker(const XmlDocument& xml,ModelBuilder& builder,const std::map<std::string,std::string>& links,
        const Styles& styles,const Numbering* numbering)
        :w_{xml},xml_(xml),builder_(builder),links_(links),styles_(styles),numbering_(numbering) {}
    void Document() {
        const auto& root=xml_.Root();
        if(!w_.Is(root,"document"))Fail(Error::docx_not_wordprocessing);
        const auto* body=w_.Child(root,"body");
        if(!body)Fail(Error::docx_not_wordprocessing);
        BlockContent(*body,nullptr);
    }
private:
    struct Field {std::string instruction;std::string link;bool result=false;};

    template<class F> void Elements(const XmlElement& e,F&& visit) {
        for(const auto& c:e.children)if(!c.text)visit(xml_.elements[c.index]);
    }
    void BlockContent(const XmlElement& container,Cell* cell) {
        Elements(container,[&](const XmlElement& e) {
            if(w_.Is(e,"p"))Paragraph(e,cell);
            else if(w_.Is(e,"tbl"))Table(e,cell);
            else if(w_.Is(e,"sdt")) {if(const auto* content=w_.Child(e,"sdtContent"))BlockContent(*content,cell);}
            else if(w_.Is(e,"customXml")||w_.Is(e,"ins")||w_.Is(e,"moveTo")||w_.Is(e,"smartTag"))BlockContent(e,cell);
            // w:del/w:moveFrom (deleted revisions), w:sectPr, bookmarks, w:altChunk
            // (imported external content) and unknown extensions are not extracted.
        });
    }
    void Paragraph(const XmlElement& p,Cell* cell) {
        std::vector<Run> runs;
        Inline(p,runs,{});
        if(cell) {builder_.AppendToCell(*cell,std::move(runs));return;}
        Block block;
        block.runs=std::move(runs);
        std::string styleId;
        const auto* ppr=w_.Child(p,"pPr");
        if(ppr)if(const auto* s=w_.ChildValue(*ppr,"pStyle"))styleId=*s;
        const auto style=styles_.Resolve(styleId); // Falls back to built-in IDs when unknown.
        int heading=style.heading;
        bool numbered=style.numbered;
        std::string numId=style.numId;
        int level=style.ilvl<0?0:style.ilvl;
        if(ppr) {
            const auto outline=ParseInt(w_.ChildValue(*ppr,"outlineLvl"),-1);
            if(!heading&&outline>=0&&outline<=5)heading=outline+1;
            if(const auto* num=w_.Child(*ppr,"numPr")) {
                if(const auto* id=w_.ChildValue(*num,"numId")) {numbered=true;numId=*id;}
                level=ParseInt(w_.ChildValue(*num,"ilvl"),level);
            }
        }
        if(heading) {
            block.kind=BlockKind::heading;block.level=std::uint8_t(heading);
        } else if(numbered&&numId!="0"&&!numId.empty()) {
            block.kind=BlockKind::list_item;
            block.level=std::uint8_t(level<0?0:level>8?8:level);
            block.ordered=numbering_&&numbering_->Ordered(numId,level);
        }
        builder_.Push(std::move(block));
    }
    void Table(const XmlElement& table,Cell* cell) {
        if(cell) { // Nested tables are flattened into the enclosing cell's text.
            Block nested;
            Rows(table,nested.rows);
            for(auto& row:nested.rows)for(auto& c:row)builder_.AppendToCell(*cell,std::move(c));
            return;
        }
        Block block;
        block.kind=BlockKind::table;
        Rows(table,block.rows);
        builder_.Push(std::move(block));
    }
    void Rows(const XmlElement& container,std::vector<Row>& rows) {
        Elements(container,[&](const XmlElement& e) {
            if(w_.Is(e,"tr")) {
                if(const auto* trPr=w_.Child(e,"trPr");trPr&&w_.Child(*trPr,"del"))return; // Deleted row revision.
                Row row;
                Cells(e,row);
                if(!row.empty())rows.push_back(std::move(row));
            } else if(w_.Is(e,"sdt")) {
                if(const auto* content=w_.Child(e,"sdtContent"))Rows(*content,rows);
            } else if(w_.Is(e,"customXml")||w_.Is(e,"ins")||w_.Is(e,"moveTo")) {
                Rows(e,rows);
            }
        });
    }
    void Cells(const XmlElement& container,Row& row) {
        Elements(container,[&](const XmlElement& e) {
            if(w_.Is(e,"tc")) {
                Cell cell;
                BlockContent(e,&cell);
                row.push_back(std::move(cell));
            } else if(w_.Is(e,"sdt")) {
                if(const auto* content=w_.Child(e,"sdtContent"))Cells(*content,row);
            } else if(w_.Is(e,"customXml")) {
                Cells(e,row);
            }
        });
    }
    void Inline(const XmlElement& container,std::vector<Run>& runs,const std::string& link) {
        Elements(container,[&](const XmlElement& e) {
            if(w_.Is(e,"r")) {RunContent(e,runs,link);}
            else if(w_.Is(e,"hyperlink")) {
                // r:id names an external relationship; w:anchor alone is an internal bookmark.
                std::string target=link;
                if(const auto* id=w_.RelAttr(e,"id")) {
                    const auto found=links_.find(*id);
                    if(found!=links_.end())target=found->second;
                }
                Inline(e,runs,target);
            } else if(w_.Is(e,"fldSimple")) {
                const auto* instruction=w_.Attr(e,"instr");
                const auto target=instruction?HyperlinkFieldTarget(*instruction):std::string();
                Inline(e,runs,target.empty()?link:target);
            } else if(w_.Is(e,"sdt")) {
                if(const auto* content=w_.Child(e,"sdtContent"))Inline(*content,runs,link);
            } else if(w_.Is(e,"ins")||w_.Is(e,"moveTo")||w_.Is(e,"smartTag")||w_.Is(e,"customXml")
                ||w_.Is(e,"dir")||w_.Is(e,"bdo")) {
                Inline(e,runs,link);
            }
            // w:del, w:moveFrom, bookmarks, proofing marks, comments ranges and
            // math (m:oMath) are not extracted.
        });
    }
    bool Suppressed() const {
        for(const auto& f:fields_)if(!f.result)return true;
        return false;
    }
    std::string_view FieldLink() const {
        for(auto i=fields_.size();i-->0;)if(fields_[i].result&&!fields_[i].link.empty())return fields_[i].link;
        return {};
    }
    bool Preserve(const XmlElement& t) const {
        for(const XmlElement* e=&t;;) {
            if(const auto* space=xml_.Attribute(*e,xmlNamespace,"space"))return *space=="preserve";
            if(e->parent==0xffffffffu)return false;
            e=&xml_.elements[e->parent];
        }
    }
    void RunContent(const XmlElement& r,std::vector<Run>& runs,const std::string& link) {
        bool bold=false,italic=false;
        if(const auto* rpr=w_.Child(r,"rPr")) {bold=w_.Toggle(*rpr,"b");italic=w_.Toggle(*rpr,"i");}
        const auto emit=[&](std::string_view text) {
            if(Suppressed())return;
            builder_.Append(runs,text,bold,italic,link.empty()?FieldLink():std::string_view(link));
        };
        Elements(r,[&](const XmlElement& e) {
            if(w_.Is(e,"t")) {
                auto text=w_.Text(e);
                if(!Preserve(e)) {
                    const auto first=text.find_first_not_of(" \t\n");
                    if(first==std::string::npos)text.clear();
                    else text=text.substr(first,text.find_last_not_of(" \t\n")-first+1);
                }
                for(auto& c:text)if(c=='\n')c=' ';
                emit(text);
            } else if(w_.Is(e,"tab")) {emit("\t");}
            else if(w_.Is(e,"br")||w_.Is(e,"cr")) {emit("\n");}
            else if(w_.Is(e,"noBreakHyphen")) {emit("\u2011");}
            else if(w_.Is(e,"fldChar")) {
                const auto* type=w_.Attr(e,"fldCharType");
                if(!type)return;
                if(*type=="begin") {
                    if(fields_.size()>=maximumFieldDepth)Fail(Error::xml_limit);
                    fields_.push_back({});
                } else if(*type=="separate"&&!fields_.empty()) {
                    fields_.back().result=true;
                    fields_.back().link=HyperlinkFieldTarget(fields_.back().instruction);
                } else if(*type=="end"&&!fields_.empty()) {
                    fields_.pop_back();
                }
            } else if(w_.Is(e,"instrText")) {
                if(fields_.empty()||fields_.back().result)return;
                auto& instruction=fields_.back().instruction;
                const auto text=w_.Text(e);
                if(instruction.size()+text.size()<=maximumFieldInstruction)instruction+=text;
            }
            // w:delText/w:delInstrText (deleted), w:softHyphen, w:sym, w:drawing,
            // w:pict, w:object, note/comment references and mc:AlternateContent are skipped.
        });
    }

    Word w_;
    const XmlDocument& xml_;
    ModelBuilder& builder_;
    const std::map<std::string,std::string>& links_;
    const Styles& styles_;
    const Numbering* numbering_;
    std::vector<Field> fields_;
};

void Extract(Bytes bytes,const ExtractOptions& options,Document& document) {
    static constexpr unsigned char compoundFile[8]={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    if(bytes.size()>=8&&std::memcmp(bytes.data(),compoundFile,8)==0)Fail(Error::docx_encrypted);
    ZipArchive zip(bytes,options.zip);
    for(const auto& e:zip.Entries()) {
        const auto lower=AsciiLower(e.name);
        if(lower=="encryptedpackage"||lower=="encryptioninfo")Fail(Error::docx_encrypted);
        const auto slash=lower.rfind('/');
        const auto base=slash==std::string::npos?lower:lower.substr(slash+1);
        if(base=="vbaproject.bin"||base=="vbadata.xml")Fail(Error::docx_macro_enabled);
    }
    const auto part=[&](std::string_view name)->std::optional<std::string> {
        const auto* entry=zip.Find(name.substr(name.front()=='/'?1:0));
        if(!entry)return std::nullopt;
        return zip.Read(*entry);
    };
    const auto contentTypesBytes=part("[Content_Types].xml");
    if(!contentTypesBytes)Fail(Error::docx_invalid_package);
    const auto contentTypes=ParseContentTypes(*contentTypesBytes,options.xml);
    for(const auto& [name,type]:contentTypes.overrides)if(type==vbaProjectType)Fail(Error::docx_macro_enabled);
    const auto packageRelationships=part("_rels/.rels");
    if(!packageRelationships)Fail(Error::docx_invalid_package);
    std::string main;
    for(const auto& r:ParseRelationships(*packageRelationships,options.xml)) {
        if(!RelationshipType(r.type,"officeDocument"))continue;
        if(!main.empty()||r.external)Fail(Error::docx_invalid_package);
        main=ResolvePart("/",r.target);
    }
    if(main.empty())Fail(Error::docx_not_wordprocessing);
    const auto mainType=contentTypes.For(main);
    if(mainType==macroDocumentType||mainType==macroTemplateType)Fail(Error::docx_macro_enabled);
    if(mainType!=documentType&&mainType!=templateType)Fail(Error::docx_not_wordprocessing);
    const auto mainBytes=part(main);
    if(!mainBytes)Fail(Error::docx_missing_part);

    std::map<std::string,std::string> links;
    Styles styles;
    std::optional<Numbering> numbering;
    if(const auto rels=part(RelationshipsPartFor(main))) {
        std::string stylesPart,numberingPart;
        for(const auto& r:ParseRelationships(*rels,options.xml)) {
            if(RelationshipType(r.type,"hyperlink")) {if(r.external)links.emplace(r.id,r.target);}
            else if(r.external) {continue;} // Never followed.
            else if(RelationshipType(r.type,"styles")) {stylesPart=ResolvePart(DirectoryOf(main),r.target);}
            else if(RelationshipType(r.type,"numbering")) {numberingPart=ResolvePart(DirectoryOf(main),r.target);}
        }
        if(!stylesPart.empty())if(const auto bytes=part(stylesPart))styles.Parse(ParseXml(*bytes,options.xml));
        if(!numberingPart.empty())if(const auto bytes=part(numberingPart)) {
            numbering.emplace();
            numbering->Parse(ParseXml(*bytes,options.xml));
        }
    }
    const auto xml=ParseXml(*mainBytes,options.xml);
    ModelBuilder builder(document,options);
    Walker walker(xml,builder,links,styles,numbering?&*numbering:nullptr);
    walker.Document();
}
}

Error ExtractDocxInto(Bytes bytes,const ExtractOptions& options,Document& document) noexcept {
    return Guard([&] {Extract(bytes,options,document);});
}
}
