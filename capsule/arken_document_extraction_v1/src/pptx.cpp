#include "text_documents_detail.hpp"
#include <cstring>
#include <set>

// PresentationML slide text extraction. Policy (see NOTES.md): slides in
// p:sldIdLst order; per slide a "Slide N" heading, the title placeholder as a
// sub-heading, then shape text in shape-tree order (groups recursed), tables,
// hyperlinks from the slide relationships and, on request, speaker notes.
// Masters, layouts, pictures, charts, SmartArt, OLE, ink, animations and
// date/footer/slide-number placeholders are not extracted.
namespace arken::document::v1::detail::textdocs {
namespace {
using E=TextDocumentError;
constexpr std::string_view pTransitional="http://schemas.openxmlformats.org/presentationml/2006/main";
constexpr std::string_view pStrict="http://purl.oclc.org/ooxml/presentationml/main";
constexpr std::string_view aTransitional="http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr std::string_view aStrict="http://purl.oclc.org/ooxml/drawingml/main";
constexpr std::string_view mcNamespace="http://schemas.openxmlformats.org/markup-compatibility/2006";
constexpr std::string_view presentationType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml";
constexpr std::string_view slideshowType="application/vnd.openxmlformats-officedocument.presentationml.slideshow.main+xml";
constexpr std::string_view templateType="application/vnd.openxmlformats-officedocument.presentationml.template.main+xml";
constexpr std::string_view slideType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml";
constexpr std::string_view notesType="application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml";
constexpr std::string_view vbaProjectType="application/vnd.ms-office.vbaProject";
constexpr std::string_view macroTypes[]={
    "application/vnd.ms-powerpoint.presentation.macroEnabled.main+xml",
    "application/vnd.ms-powerpoint.slideshow.macroEnabled.main+xml",
    "application/vnd.ms-powerpoint.template.macroEnabled.main+xml",
    "application/vnd.ms-powerpoint.addin.macroEnabled.main+xml"};

// Element helpers over the transitional and strict namespaces.
struct Dml {
    const XmlDocument& xml;
    bool P(const XmlElement& e,std::string_view local) const {
        if(e.local!=local)return false;
        const auto ns=xml.Namespace(e);
        return ns==pTransitional||ns==pStrict;
    }
    bool A(const XmlElement& e,std::string_view local) const {
        if(e.local!=local)return false;
        const auto ns=xml.Namespace(e);
        return ns==aTransitional||ns==aStrict;
    }
    bool Mc(const XmlElement& e,std::string_view local) const {return Is(xml,e,mcNamespace,local);}
    const XmlElement* PChild(const XmlElement& e,std::string_view local) const {
        for(const auto& c:e.children)if(!c.text&&P(xml.elements[c.index],local))return &xml.elements[c.index];
        return nullptr;
    }
    const XmlElement* AChild(const XmlElement& e,std::string_view local) const {
        for(const auto& c:e.children)if(!c.text&&A(xml.elements[c.index],local))return &xml.elements[c.index];
        return nullptr;
    }
    const std::string* Attr(const XmlElement* e,std::string_view local) const {return e?xml.Attribute(*e,"",local):nullptr;}
    const std::string* RelId(const XmlElement& e) const {
        if(const auto* v=xml.Attribute(e,rTransitional,"id"))return v;
        return xml.Attribute(e,rStrict,"id");
    }
    bool True(const XmlElement* e,std::string_view local) const {
        const auto* v=Attr(e,local);
        return v&&(*v=="1"||*v=="true"||*v=="on");
    }
};

class SlideWalker {
public:
    SlideWalker(const XmlDocument& xml,Flow& flow,Context& context,const std::map<std::string,std::string>& links)
        :d_{xml},xml_(xml),flow_(flow),context_(context),links_(links) {}

    const XmlElement* ShapeTree(const XmlElement& root) const {
        const auto* cSld=d_.PChild(root,"cSld");
        return cSld?d_.PChild(*cSld,"spTree"):nullptr;
    }
    // The first title or centred-title placeholder in shape-tree order.
    const XmlElement* FindTitle(const XmlElement& tree,std::uint32_t depth=0) const {
        if(depth>context_.options.maximumNesting)FailText(E::nesting_limit);
        for(const auto& c:tree.children) {
            if(c.text)continue;
            const auto& e=xml_.elements[c.index];
            if(d_.P(e,"sp")) {
                const auto type=PlaceholderType(e);
                if((type=="title"||type=="ctrTitle")&&d_.PChild(e,"txBody"))return &e;
            } else if(d_.P(e,"grpSp")) {
                if(const auto* found=FindTitle(e,depth+1))return found;
            }
        }
        return nullptr;
    }
    // The first body placeholder (speaker notes text on a notes slide).
    const XmlElement* FindBody(const XmlElement& tree) const {
        for(const auto& c:tree.children) {
            if(c.text)continue;
            const auto& e=xml_.elements[c.index];
            if(d_.P(e,"sp")&&PlaceholderType(e)=="body")return &e;
        }
        return nullptr;
    }
    Cell Text(const XmlElement& shape,char separator) {
        return flow_.Capture([&] {if(const auto* body=d_.PChild(shape,"txBody"))Paragraphs(*body,false);},separator);
    }
    void Shapes(const XmlElement& tree,const XmlElement* title) {
        const Nest nest(context_);
        ForChildren(xml_,tree,[&](const XmlElement& e) {
            if(&e==title)return;
            if(d_.P(e,"sp")) {Shape(e);}
            else if(d_.P(e,"grpSp")) {Shapes(e,title);}
            else if(d_.P(e,"graphicFrame")) {GraphicFrame(e);}
            else if(d_.P(e,"pic")) {++context_.report.excluded.images;}
            else if(d_.P(e,"contentPart")) {++context_.report.excluded.graphics;}
            else if(d_.Mc(e,"AlternateContent")) {
                // Only the fallback branch is understood; it is walked as shapes.
                if(const auto* fallback=FirstChild(xml_,e,mcNamespace,"Fallback"))Shapes(*fallback,title);
                else ++context_.report.excluded.graphics;
            }
            // p:cxnSp (connectors), p:nvGrpSpPr, p:grpSpPr and extensions carry no text.
        });
    }
private:
    // ST_PlaceholderType; an untyped placeholder is "obj" (a content/body placeholder).
    std::string PlaceholderType(const XmlElement& sp) const {
        const auto* nv=d_.PChild(sp,"nvSpPr");
        const auto* nvPr=nv?d_.PChild(*nv,"nvPr"):nullptr;
        const auto* ph=nvPr?d_.PChild(*nvPr,"ph"):nullptr;
        if(!ph)return {};
        const auto* type=d_.Attr(ph,"type");
        return type?*type:std::string("obj");
    }
    void Shape(const XmlElement& sp) {
        const auto type=PlaceholderType(sp);
        if(type=="dt"||type=="ftr"||type=="sldNum"||type=="hdr") {++context_.report.excluded.placeholders;return;}
        const auto* body=d_.PChild(sp,"txBody");
        if(!body)return;
        // Body/content placeholders inherit bullets from the layout and master,
        // which are not read: their paragraphs default to bullets.
        Paragraphs(*body,type=="obj"||type=="body");
    }
    void GraphicFrame(const XmlElement& frame) {
        const auto* graphic=d_.AChild(frame,"graphic");
        const auto* data=graphic?d_.AChild(*graphic,"graphicData"):nullptr;
        const auto* table=data?d_.AChild(*data,"tbl"):nullptr;
        if(!table) {++context_.report.excluded.graphics;return;} // Charts, SmartArt, OLE objects, media.
        std::vector<Row> rows;
        ForChildren(xml_,*table,[&](const XmlElement& tr) {
            if(!d_.A(tr,"tr"))return;
            Row row;
            ForChildren(xml_,tr,[&](const XmlElement& tc) {
                if(!d_.A(tc,"tc"))return;
                context_.ChargeCells(1);
                // Merge continuation cells stay empty so the grid stays rectangular.
                if(d_.True(&tc,"hMerge")||d_.True(&tc,"vMerge")) {row.emplace_back();return;}
                row.push_back(flow_.Capture([&] {if(const auto* body=d_.AChild(tc,"txBody"))Paragraphs(*body,false);}));
            });
            if(!row.empty())rows.push_back(std::move(row));
        });
        flow_.Table(std::move(rows));
    }
    void Paragraphs(const XmlElement& body,bool bullets) {
        ForChildren(xml_,body,[&](const XmlElement& p) {
            if(!d_.A(p,"p"))return;
            const auto* pPr=d_.AChild(p,"pPr");
            const int level=ParseCount(d_.Attr(pPr,"lvl"),0,8);
            enum {none,bullet,ordered} kind=bullets?bullet:none;
            if(pPr) {
                if(d_.AChild(*pPr,"buNone"))kind=none;
                else if(d_.AChild(*pPr,"buAutoNum"))kind=ordered;
                else if(d_.AChild(*pPr,"buChar")||d_.AChild(*pPr,"buBlip"))kind=bullet;
            }
            const auto runs=[&] {Runs(p);};
            if(kind==none)flow_.Paragraph(runs);
            else flow_.ListItem(level,kind==ordered,runs);
        });
    }
    void Runs(const XmlElement& p) {
        ForChildren(xml_,p,[&](const XmlElement& e) {
            if(d_.A(e,"r")||d_.A(e,"fld")) {
                Format f;
                if(const auto* rPr=d_.AChild(e,"rPr")) {
                    f.bold=d_.True(rPr,"b");
                    f.italic=d_.True(rPr,"i");
                    if(const auto* click=d_.AChild(*rPr,"hlinkClick"))
                        if(const auto* id=d_.RelId(*click)) {
                            const auto found=links_.find(*id);
                            if(found!=links_.end())f.link=found->second;
                        }
                }
                if(const auto* t=d_.AChild(e,"t")) {
                    std::string text;
                    for(const auto& c:t->children)if(c.text)text+=xml_.texts[c.index];
                    for(auto& c:text)if(c=='\n')c=' ';
                    flow_.Literal(text,f);
                }
            } else if(d_.A(e,"br")) {
                flow_.Break({});
            }
            // a:pPr, a:endParaRPr and math (a14:m in mc:AlternateContent) carry no extracted text.
        });
    }

    Dml d_;
    const XmlDocument& xml_;
    Flow& flow_;
    Context& context_;
    const std::map<std::string,std::string>& links_;
};

Block TextBlock(BlockKind kind,std::uint8_t level,std::vector<Run>&& runs) {
    Block block;
    block.kind=kind;block.level=level;block.runs=std::move(runs);
    return block;
}
}

void ExtractPptx(Bytes bytes,Context& context,Document& document) {
    const auto& options=context.options.extract;
    static constexpr unsigned char compoundFile[8]={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    if(bytes.size()>=8&&std::memcmp(bytes.data(),compoundFile,8)==0)FailText(E::pptx_encrypted);
    ZipArchive zip(bytes,options.zip);
    for(const auto& e:zip.Entries()) {
        const auto lower=AsciiLower(e.name);
        if(lower=="encryptedpackage"||lower=="encryptioninfo")FailText(E::pptx_encrypted);
        const auto slash=lower.rfind('/');
        const auto base=slash==std::string::npos?lower:lower.substr(slash+1);
        if(base=="vbaproject.bin"||base=="vbadata.xml")FailText(E::pptx_macro_enabled);
    }
    const auto contentTypesBytes=ReadPart(zip,"[Content_Types].xml");
    if(!contentTypesBytes)FailText(E::pptx_invalid_package);
    const auto contentTypes=ParseContentTypes(*contentTypesBytes,options.xml,E::pptx_invalid_package);
    for(const auto& [name,type]:contentTypes.overrides)if(type==vbaProjectType)FailText(E::pptx_macro_enabled);
    const auto packageRelationships=ReadPart(zip,"_rels/.rels");
    if(!packageRelationships)FailText(E::pptx_invalid_package);
    std::string main;
    for(const auto& r:ParseRelationships(*packageRelationships,options.xml,E::pptx_invalid_package)) {
        if(!RelationshipType(r.type,"officeDocument"))continue;
        if(!main.empty()||r.external)FailText(E::pptx_invalid_package);
        main=ResolvePart("/",r.target,E::pptx_invalid_package);
    }
    if(main.empty())FailText(E::pptx_not_presentation);
    const auto mainType=contentTypes.For(main);
    for(const auto macro:macroTypes)if(mainType==macro)FailText(E::pptx_macro_enabled);
    if(mainType!=presentationType&&mainType!=slideshowType&&mainType!=templateType)FailText(E::pptx_not_presentation);
    const auto mainBytes=ReadPart(zip,main);
    if(!mainBytes)FailText(E::pptx_missing_part);
    std::map<std::string,Relationship> mainRelationships;
    if(const auto rels=ReadPart(zip,RelationshipsPartFor(main)))
        for(auto& r:ParseRelationships(*rels,options.xml,E::pptx_invalid_package))mainRelationships.emplace(r.id,r);

    const auto presentation=ParseXml(*mainBytes,options.xml);
    const Dml d{presentation};
    if(!d.P(presentation.Root(),"presentation"))FailText(E::pptx_invalid_package);
    std::vector<std::string> slideParts;
    std::set<std::string> seenIds;
    if(const auto* list=d.PChild(presentation.Root(),"sldIdLst")) {
        ForChildren(presentation,*list,[&](const XmlElement& sldId) {
            if(!d.P(sldId,"sldId"))return;
            const auto* id=d.RelId(sldId);
            if(!id||!seenIds.insert(*id).second)FailText(E::pptx_invalid_package);
            if(slideParts.size()>=context.options.maximumItems)FailText(E::item_limit);
            const auto found=mainRelationships.find(*id);
            if(found==mainRelationships.end()||found->second.external||!RelationshipType(found->second.type,"slide"))
                FailText(E::pptx_invalid_package);
            auto part=ResolvePart(DirectoryOf(main),found->second.target,E::pptx_invalid_package);
            if(contentTypes.For(part)!=slideType)FailText(E::pptx_invalid_package);
            slideParts.push_back(std::move(part));
        });
    }

    ModelBuilder builder(document,options);
    Flow flow(builder,context);
    for(std::size_t index=0;index<slideParts.size();++index) {
        const auto& part=slideParts[index];
        const auto slideBytes=ReadPart(zip,part);
        if(!slideBytes)FailText(E::pptx_missing_part);
        std::map<std::string,std::string> links;
        std::string notesPart;
        if(const auto rels=ReadPart(zip,RelationshipsPartFor(part))) {
            for(const auto& r:ParseRelationships(*rels,options.xml,E::pptx_invalid_package)) {
                if(RelationshipType(r.type,"hyperlink")) {if(r.external)links.emplace(r.id,r.target);}
                else if(RelationshipType(r.type,"notesSlide")&&!r.external&&notesPart.empty())
                    notesPart=ResolvePart(DirectoryOf(part),r.target,E::pptx_invalid_package);
            }
        }
        const auto slide=ParseXml(*slideBytes,options.xml);
        const Dml sd{slide};
        if(!sd.P(slide.Root(),"sld"))FailText(E::pptx_invalid_package);
        ++context.report.slides;
        std::vector<Run> heading;
        flow.Append(heading,"Slide "+std::to_string(index+1),false,false);
        flow.Push(TextBlock(BlockKind::heading,1,std::move(heading)));
        SlideWalker walker(slide,flow,context,links);
        const auto* tree=walker.ShapeTree(slide.Root());
        if(!tree)continue;
        const auto* title=walker.FindTitle(*tree);
        if(title) {
            auto runs=walker.Text(*title,' ');
            for(auto& run:runs)for(auto& c:run.text)if(c=='\n')c=' ';
            flow.Push(TextBlock(BlockKind::heading,2,std::move(runs)));
        }
        walker.Shapes(*tree,title);
        flow.Finish();
        if(notesPart.empty())continue;
        if(!context.options.includeNotes) {++context.report.excluded.notes;continue;}
        if(contentTypes.For(notesPart)!=notesType)FailText(E::pptx_invalid_package);
        const auto notesBytes=ReadPart(zip,notesPart);
        if(!notesBytes)FailText(E::pptx_missing_part);
        const auto notes=ParseXml(*notesBytes,options.xml);
        const Dml nd{notes};
        if(!nd.P(notes.Root(),"notes"))FailText(E::pptx_invalid_package);
        const std::map<std::string,std::string> noLinks;
        SlideWalker notesWalker(notes,flow,context,noLinks);
        const auto* notesTree=notesWalker.ShapeTree(notes.Root());
        const auto* body=notesTree?notesWalker.FindBody(*notesTree):nullptr;
        if(!body)continue;
        auto text=notesWalker.Text(*body,'\n');
        if(!ModelBuilder::HasText(text))continue;
        std::vector<Run> runs;
        flow.Append(runs,"Notes:",true,false);
        flow.Append(runs," ",false,false);
        flow.AppendCopy(runs,text);
        flow.Push(TextBlock(BlockKind::paragraph,0,std::move(runs)));
        ++context.report.notesIncluded;
    }
    flow.Finish();
}
}
