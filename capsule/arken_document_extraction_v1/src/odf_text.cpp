#include "text_documents_detail.hpp"
#include <algorithm>

// OpenDocument text (office:text) and presentation (office:presentation)
// extraction. Policy (see NOTES.md): paragraphs, headings, nested lists
// (numbered/bullet from list styles), spans with bold/italic from styles,
// hyperlinks, spaces/tabs/line breaks, tables with repeats, sections and index
// bodies; tracked changes shown as accepted. Notes, annotations, frames,
// images and drawing shapes in text documents are excluded. ODP pages become
// slides like PPTX.
namespace arken::document::v1::detail::textdocs {
namespace {
using E=TextDocumentError;
constexpr std::string_view officeNs="urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr std::string_view textNs="urn:oasis:names:tc:opendocument:xmlns:text:1.0";
constexpr std::string_view tableNs="urn:oasis:names:tc:opendocument:xmlns:table:1.0";
constexpr std::string_view drawNs="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0";
constexpr std::string_view styleNs="urn:oasis:names:tc:opendocument:xmlns:style:1.0";
constexpr std::string_view foNs="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
constexpr std::string_view xlinkNs="http://www.w3.org/1999/xlink";
constexpr std::string_view presentationNs="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
constexpr std::string_view manifestNs="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
constexpr std::string_view odfPrefix="application/vnd.oasis.opendocument.";
constexpr int maximumRepeat=1<<30;

// Style properties this extractor understands: bold and italic (tri-state,
// -1 = not set) with parent-style inheritance, and list level kinds.
class Styles {
public:
    void Load(const XmlDocument& xml,const XmlElement& container,bool automatic) {
        ForChildren(xml,container,[&](const XmlElement& e) {
            const auto* name=xml.Attribute(e,styleNs,"name");
            if(!name)return;
            if(Is(xml,e,styleNs,"style")) {
                const auto* family=xml.Attribute(e,styleNs,"family");
                if(!family)return;
                TextStyle style;
                if(const auto* parent=xml.Attribute(e,styleNs,"parent-style-name"))style.parent=*parent;
                if(const auto* props=FirstChild(xml,e,styleNs,"text-properties")) {
                    if(const auto* weight=xml.Attribute(*props,foNs,"font-weight"))style.bold=Weight(*weight);
                    if(const auto* posture=xml.Attribute(*props,foNs,"font-style"))style.italic=*posture=="italic"||*posture=="oblique";
                }
                (automatic?automatic_:named_).emplace(*family+":"+*name,std::move(style)); // First definition wins.
            } else if(Is(xml,e,textNs,"list-style")) {
                std::map<int,bool> levels;
                ForChildren(xml,e,[&](const XmlElement& level) {
                    const int index=ParseCount(xml.Attribute(level,textNs,"level"),1,10);
                    if(Is(xml,level,textNs,"list-level-style-number")) {
                        const auto* format=xml.Attribute(level,styleNs,"num-format");
                        levels.emplace(index,format&&!format->empty()); // An empty format shows no number.
                    } else if(Is(xml,level,textNs,"list-level-style-bullet")||Is(xml,level,textNs,"list-level-style-image")) {
                        levels.emplace(index,false);
                    }
                });
                (automatic?automaticLists_:namedLists_).emplace(*name,std::move(levels));
            }
        });
    }
    Format Apply(Format base,std::string_view family,const std::string* name) const {
        if(!name)return base;
        int bold=-1,italic=-1;
        std::string key=std::string(family)+":"+*name;
        const TextStyle* style=nullptr;
        if(const auto a=automatic_.find(key);a!=automatic_.end())style=&a->second;
        else if(const auto n=named_.find(key);n!=named_.end())style=&n->second;
        for(int step=0;style&&step<16;++step) {
            if(bold<0)bold=style->bold;
            if(italic<0)italic=style->italic;
            if(style->parent.empty())break;
            const auto n=named_.find(std::string(family)+":"+style->parent);
            style=n==named_.end()?nullptr:&n->second;
        }
        if(bold>=0)base.bold=bold!=0;
        if(italic>=0)base.italic=italic!=0;
        return base;
    }
    // Unknown styles and levels are bullets.
    bool Ordered(const std::string& list,int level) const {
        const std::map<int,bool>* levels=nullptr;
        if(const auto a=automaticLists_.find(list);a!=automaticLists_.end())levels=&a->second;
        else if(const auto n=namedLists_.find(list);n!=namedLists_.end())levels=&n->second;
        if(!levels)return false;
        const auto found=levels->find(level>10?10:level);
        return found!=levels->end()&&found->second;
    }
private:
    struct TextStyle {int bold=-1,italic=-1;std::string parent;};
    static int Weight(const std::string& value) {
        if(value=="bold")return 1;
        if(value=="normal")return 0;
        const int numeric=ParseCount(&value,-1,1000);
        return numeric<0?-1:numeric>=600?1:0;
    }
    std::map<std::string,TextStyle> automatic_,named_;
    std::map<std::string,std::map<int,bool>> automaticLists_,namedLists_;
};

class Walker {
public:
    Walker(const XmlDocument& xml,Flow& flow,Context& context,const Styles& styles)
        :xml_(xml),flow_(flow),context_(context),styles_(styles) {}

    struct ListContext {int depth=-1;std::string style;};

    void BlockContent(const XmlElement& container) {BlockContent(container,ListContext{});}
    void BlockContent(const XmlElement& container,const ListContext& list) {
        const Nest nest(context_);
        ForChildren(xml_,container,[&](const XmlElement& e) {
            const auto ns=xml_.Namespace(e);
            if(ns==textNs) {
                if(e.local=="p") {
                    flow_.Paragraph([&] {Inline(e,styles_.Apply({},"paragraph",Attr(e,textNs,"style-name")));});
                } else if(e.local=="h") {
                    const int level=ParseCount(Attr(e,textNs,"outline-level"),1,10);
                    flow_.Heading(level,[&] {Inline(e,styles_.Apply({},"paragraph",Attr(e,textNs,"style-name")));});
                } else if(e.local=="list") {
                    List(e,list);
                } else if(e.local=="numbered-paragraph") {
                    const int level=ParseCount(Attr(e,textNs,"level"),1,10);
                    const auto* style=Attr(e,textNs,"style-name");
                    const bool ordered=style?styles_.Ordered(*style,level):true;
                    flow_.ListItem(level-1,ordered,[&] {BlockContent(e,ListContext{level-1,style?*style:std::string()});});
                } else if(e.local=="section"||e.local=="index-body"||e.local=="index-title") {
                    // text:index-title (inside index-body) holds the index's displayed
                    // heading, e.g. "Table of Contents" (LibreOffice fdo79358.odt).
                    BlockContent(e,list);
                } else if(e.local=="table-of-content"||e.local=="illustration-index"||e.local=="table-index"
                    ||e.local=="object-index"||e.local=="user-index"||e.local=="alphabetical-index"||e.local=="bibliography") {
                    if(const auto* body=FirstChild(xml_,e,textNs,"index-body"))BlockContent(*body,list);
                } else if(e.local=="tracked-changes") {
                    TrackedChanges(e);
                }
                // text:sequence-decls, variable/user-field declarations,
                // text:soft-page-break and text:number carry no body text.
            } else if(ns==tableNs&&e.local=="table") {
                Table(e);
            } else if(ns==drawNs) {
                Drawing(e);
            } else if(ns==officeNs&&e.local=="annotation") {
                ++context_.report.excluded.annotations;
            }
        });
    }
    void Inline(const XmlElement& container,const Format& format) {
        const Nest nest(context_);
        for(const auto& c:container.children) {
            if(c.text) {flow_.Collapsed(xml_.texts[c.index],format);continue;}
            const auto& e=xml_.elements[c.index];
            const auto ns=xml_.Namespace(e);
            if(ns==textNs) {
                const auto& n=e.local;
                if(n=="span") {Inline(e,styles_.Apply(format,"text",Attr(e,textNs,"style-name")));}
                else if(n=="a") {
                    auto linked=format;
                    if(const auto* href=Attr(e,xlinkNs,"href"))linked.link=*href;
                    Inline(e,linked);
                } else if(n=="s") {
                    flow_.Literal(std::string(std::size_t(ParseCount(Attr(e,textNs,"c"),1,10000)),' '),format);
                } else if(n=="tab") {flow_.Literal("\t",format);}
                else if(n=="line-break") {flow_.Break(format);}
                else if(n=="note") {++context_.report.excluded.notes;}
                else if(n=="ruby") {if(const auto* base=FirstChild(xml_,e,textNs,"ruby-base"))Inline(*base,format);}
                else if(n=="soft-page-break"||n=="bookmark"||n=="bookmark-start"||n=="bookmark-end"
                    ||n=="reference-mark"||n=="reference-mark-start"||n=="reference-mark-end"
                    ||n=="toc-mark"||n=="toc-mark-start"||n=="toc-mark-end"
                    ||n=="alphabetical-index-mark"||n=="alphabetical-index-mark-start"||n=="alphabetical-index-mark-end"
                    ||n=="user-index-mark"||n=="user-index-mark-start"||n=="user-index-mark-end"
                    ||n=="change"||n=="change-start"||n=="change-end"||n=="number"||n=="hidden-text"
                    ||n=="tracked-changes") {
                    // Markers, generated list labels and hidden text carry no visible body text.
                } else {
                    Inline(e,format); // Fields (dates, page numbers, references, ...) keep their displayed value.
                }
            } else if(ns==officeNs&&e.local=="annotation") {
                ++context_.report.excluded.annotations;
            } else if(ns==drawNs) {
                Drawing(e);
            }
        }
    }
    void Table(const XmlElement& table) {
        std::vector<Row> rows;
        std::uint64_t pendingRows=0;
        Rows(table,rows,pendingRows);
        std::size_t width=0;
        for(const auto& row:rows)width=std::max(width,row.size());
        if(!width)return;
        for(auto& row:rows) { // ODF tables are a full grid: restore trimmed trailing cells.
            context_.ChargeCells(width-row.size());
            row.resize(width);
        }
        flow_.Table(std::move(rows));
    }
    // ODP: the frames and shapes of one page (or group).
    void Shapes(const XmlElement& container,const XmlElement* title) {
        const Nest nest(context_);
        ForChildren(xml_,container,[&](const XmlElement& e) {
            if(&e==title||xml_.Namespace(e)!=drawNs)return;
            if(e.local=="frame") {
                const auto* cls=Attr(e,presentationNs,"class");
                if(cls&&(*cls=="header"||*cls=="footer"||*cls=="date-time"||*cls=="page-number")) {
                    ++context_.report.excluded.placeholders;return;
                }
                if(const auto* box=FirstChild(xml_,e,drawNs,"text-box"))BlockContent(*box);
                else if(const auto* t=FirstChild(xml_,e,tableNs,"table"))Table(*t);
                else Drawing(e);
            } else if(e.local=="g"||e.local=="a") {
                Shapes(e,title);
            } else if(e.local=="custom-shape"||e.local=="rect"||e.local=="ellipse"||e.local=="circle"
                ||e.local=="line"||e.local=="polyline"||e.local=="polygon"||e.local=="regular-polygon"
                ||e.local=="path"||e.local=="connector"||e.local=="caption"||e.local=="measure") {
                BlockContent(e); // Shape text (text:p / text:list children); geometry is skipped.
            } else {
                Drawing(e);
            }
        });
    }
    const XmlElement* FindFrame(const XmlElement& container,std::string_view cls,std::uint32_t depth=0) const {
        if(depth>context_.options.maximumNesting)FailText(E::nesting_limit);
        for(const auto& c:container.children) {
            if(c.text)continue;
            const auto& e=xml_.elements[c.index];
            if(Is(xml_,e,drawNs,"frame")) {
                const auto* value=Attr(e,presentationNs,"class");
                if(value&&*value==cls&&FirstChild(xml_,e,drawNs,"text-box"))return &e;
            } else if(Is(xml_,e,drawNs,"g")) {
                if(const auto* found=FindFrame(e,cls,depth+1))return found;
            }
        }
        return nullptr;
    }
    const std::string* Attr(const XmlElement& e,std::string_view ns,std::string_view local) const {return xml_.Attribute(e,ns,local);}
private:
    void List(const XmlElement& list,const ListContext& outer) {
        const auto* own=Attr(list,textNs,"style-name");
        const ListContext inner{outer.depth+1,own?*own:outer.style}; // Nested lists inherit the outer style.
        ForChildren(xml_,list,[&](const XmlElement& item) {
            if(!Is(xml_,item,textNs,"list-item")&&!Is(xml_,item,textNs,"list-header"))return;
            const bool ordered=item.local=="list-item"&&styles_.Ordered(inner.style,inner.depth+1);
            flow_.ListItem(inner.depth,ordered,[&] {BlockContent(item,inner);});
        });
    }
    void TrackedChanges(const XmlElement& changes) {
        // Deleted text lives only here; the body already reads as if every change
        // were accepted (insertions present, deletions absent).
        ForChildren(xml_,changes,[&](const XmlElement& region) {
            if(Is(xml_,region,textNs,"changed-region")&&FirstChild(xml_,region,textNs,"deletion"))
                ++context_.report.excluded.deletions;
        });
    }
    void Drawing(const XmlElement& e) {
        if(e.local=="frame") {
            if(FirstChild(xml_,e,drawNs,"image"))++context_.report.excluded.images;
            else ++context_.report.excluded.graphics; // Text boxes in text documents, objects, charts, plugins.
        } else if(e.local=="image") {
            ++context_.report.excluded.images;
        } else {
            static constexpr std::string_view drawn[]={"object","object-ole","plugin","applet","floating-frame",
                "custom-shape","rect","ellipse","circle","line","polyline","polygon","regular-polygon","path",
                "connector","caption","measure","g","a","control","page-thumbnail"};
            for(const auto name:drawn)if(e.local==name) {++context_.report.excluded.graphics;return;}
            // Shape geometry (draw:enhanced-geometry, glue points, contours) is not content.
        }
    }
    void Rows(const XmlElement& container,std::vector<Row>& rows,std::uint64_t& pendingRows) {
        const Nest nest(context_);
        ForChildren(xml_,container,[&](const XmlElement& e) {
            if(xml_.Namespace(e)!=tableNs)return;
            if(e.local=="table-row") {
                const auto repeat=std::uint64_t(ParseCount(Attr(e,tableNs,"number-rows-repeated"),1,maximumRepeat));
                Row row=Cells(e);
                if(row.empty()) {pendingRows+=repeat;return;} // Trailing empty rows are dropped.
                for(;pendingRows;--pendingRows) {context_.ChargeCells(1);rows.emplace_back();}
                context_.ChargeCells(row.size()*(repeat-1)); // The first copy was charged by Cells.
                for(std::uint64_t k=1;k<repeat;++k) {
                    Row copy(row.size());
                    for(std::size_t i=0;i<row.size();++i)flow_.AppendCopy(copy[i],row[i]);
                    rows.push_back(std::move(copy));
                }
                rows.push_back(std::move(row));
            } else if(e.local=="table-header-rows"||e.local=="table-rows"||e.local=="table-row-group") {
                Rows(e,rows,pendingRows);
            }
            // Columns, titles, descriptions and table shapes carry no cell text.
        });
    }
    Row Cells(const XmlElement& tr) {
        Row row;
        std::uint64_t pending=0;
        ForChildren(xml_,tr,[&](const XmlElement& e) {
            if(!Is(xml_,e,tableNs,"table-cell")&&!Is(xml_,e,tableNs,"covered-table-cell"))return;
            const auto repeat=std::uint64_t(ParseCount(Attr(e,tableNs,"number-columns-repeated"),1,maximumRepeat));
            auto cell=flow_.Capture([&] {BlockContent(e);});
            if(!ModelBuilder::HasText(cell)) {pending+=repeat;return;} // Trailing empty cells are dropped.
            context_.ChargeCells(pending+repeat);
            row.resize(row.size()+std::size_t(pending));
            pending=0;
            for(std::uint64_t k=1;k<repeat;++k) {
                Cell copy;
                flow_.AppendCopy(copy,cell);
                row.push_back(std::move(copy));
            }
            row.push_back(std::move(cell));
        });
        return row;
    }

    const XmlDocument& xml_;
    Flow& flow_;
    Context& context_;
    const Styles& styles_;
};

bool HasVisibleText(const XmlDocument& xml,const XmlElement& e) {
    std::string text;
    AllText(xml,e,text);
    return !CollapseSpace(text).empty();
}
}

void ExtractOdf(Bytes bytes,bool presentation,Context& context,Document& document) {
    const auto& options=context.options.extract;
    ZipArchive zip(bytes,options.zip);
    const auto mimetype=ReadPart(zip,"mimetype");
    if(!mimetype||mimetype->substr(0,odfPrefix.size())!=odfPrefix)FailText(E::odf_not_opendocument);
    const auto kind=std::string_view(*mimetype).substr(odfPrefix.size());
    const bool expected=presentation?(kind=="presentation"||kind=="presentation-template")
        :(kind=="text"||kind=="text-template");
    if(!expected)FailText(E::odf_wrong_kind);

    const auto manifestBytes=ReadPart(zip,"META-INF/manifest.xml");
    if(!manifestBytes)FailText(E::odf_missing_part);
    {
        const auto manifest=ParseXml(*manifestBytes,options.xml);
        if(!Is(manifest,manifest.Root(),manifestNs,"manifest"))FailText(E::odf_invalid_package);
        ForChildren(manifest,manifest.Root(),[&](const XmlElement& entry) {
            if(!Is(manifest,entry,manifestNs,"file-entry"))return;
            const auto* path=manifest.Attribute(entry,manifestNs,"full-path");
            if(!path)FailText(E::odf_invalid_package);
            if(FirstChild(manifest,entry,manifestNs,"encryption-data"))FailText(E::odf_encrypted);
            if(*path=="/")
                if(const auto* type=manifest.Attribute(entry,manifestNs,"media-type");type&&!type->empty()&&*type!=*mimetype)
                    FailText(E::odf_invalid_package);
        });
    }
    const auto contentBytes=ReadPart(zip,"content.xml");
    if(!contentBytes)FailText(E::odf_missing_part);
    Styles styles;
    std::optional<XmlDocument> stylesXml;
    if(const auto stylesBytes=ReadPart(zip,"styles.xml")) {
        stylesXml.emplace(ParseXml(*stylesBytes,options.xml));
        if(!Is(*stylesXml,stylesXml->Root(),officeNs,"document-styles"))FailText(E::odf_invalid_package);
        if(const auto* named=FirstChild(*stylesXml,stylesXml->Root(),officeNs,"styles"))styles.Load(*stylesXml,*named,false);
        stylesXml.reset();
    }
    const auto content=ParseXml(*contentBytes,options.xml);
    const auto& root=content.Root();
    if(!Is(content,root,officeNs,"document-content"))FailText(E::odf_invalid_package);
    if(const auto* automatic=FirstChild(content,root,officeNs,"automatic-styles"))styles.Load(content,*automatic,true);
    const auto* body=FirstChild(content,root,officeNs,"body");
    if(!body)FailText(E::odf_invalid_package);
    const auto* office=FirstChild(content,*body,officeNs,presentation?"presentation":"text");
    if(!office) {
        bool other=false;
        ForChildren(content,*body,[&](const XmlElement& e) {if(content.Namespace(e)==officeNs)other=true;});
        FailText(other?E::odf_wrong_kind:E::odf_invalid_package);
    }

    ModelBuilder builder(document,options);
    Flow flow(builder,context);
    Walker walker(content,flow,context,styles);
    if(!presentation) {
        walker.BlockContent(*office);
        flow.Finish();
        return;
    }
    std::uint32_t index=0;
    ForChildren(content,*office,[&](const XmlElement& page) {
        if(!Is(content,page,drawNs,"page"))return;
        if(index>=context.options.maximumItems)FailText(E::item_limit);
        ++index;
        ++context.report.slides;
        Block heading;
        heading.kind=BlockKind::heading;heading.level=1;
        flow.Append(heading.runs,"Slide "+std::to_string(index),false,false);
        flow.Push(std::move(heading));
        const auto* title=walker.FindFrame(page,"title");
        if(title) {
            Block sub;
            sub.kind=BlockKind::heading;sub.level=2;
            sub.runs=flow.Capture([&] {walker.BlockContent(*FirstChild(content,*title,drawNs,"text-box"));});
            for(auto& run:sub.runs)for(auto& c:run.text)if(c=='\n')c=' ';
            flow.Push(std::move(sub));
        }
        walker.Shapes(page,title);
        flow.Finish();
        const auto* notes=FirstChild(content,page,presentationNs,"notes");
        const auto* frame=notes?walker.FindFrame(*notes,"notes"):nullptr;
        if(!frame||!HasVisibleText(content,*frame))return;
        if(!context.options.includeNotes) {++context.report.excluded.notes;return;}
        auto text=flow.Capture([&] {walker.BlockContent(*FirstChild(content,*frame,drawNs,"text-box"));},'\n');
        Block block;
        flow.Append(block.runs,"Notes:",true,false);
        flow.Append(block.runs," ",false,false);
        flow.AppendCopy(block.runs,text);
        flow.Push(std::move(block));
        ++context.report.notesIncluded;
    });
    flow.Finish();
}
}
