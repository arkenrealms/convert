#include "text_documents_detail.hpp"
#include <algorithm>
#include <set>

// EPUB 2/3 extraction. Policy (see NOTES.md): mimetype, container.xml, OPF
// manifest and spine; each linear XHTML spine item is parsed with the capsule's
// XML tokenizer (well-formed XML only; no HTML parser) and mapped to headings,
// paragraphs, lists, tables, links, breaks and emphasis. Navigation documents,
// NCX, images, SVG, MathML, media, scripts and styles are excluded. A chapter
// without its own h1-h6 receives a heading from the table of contents.
namespace arken::document::v1::detail::textdocs {
namespace {
using E=TextDocumentError;
constexpr std::string_view xhtmlNs="http://www.w3.org/1999/xhtml";
constexpr std::string_view opfNs="http://www.idpf.org/2007/opf";
constexpr std::string_view containerNs="urn:oasis:names:tc:opendocument:xmlns:container";
constexpr std::string_view opsNs="http://www.idpf.org/2007/ops";
constexpr std::string_view ncxNs="http://www.daisy.org/z3986/2005/ncx/";
constexpr std::string_view xmlEncNs="http://www.w3.org/2001/04/xmlenc#";
constexpr std::string_view svgNs="http://www.w3.org/2000/svg";
constexpr std::string_view mathNs="http://www.w3.org/1998/Math/MathML";
// Font obfuscation (EPUB OCF and Adobe) is not DRM: it only scrambles embedded fonts, which are never read.
constexpr std::string_view obfuscationAlgorithms[]={"http://www.idpf.org/2008/embedding","http://ns.adobe.com/pdf/enc#RC"};

bool HasToken(const std::string* list,std::string_view token) {
    if(!list)return false;
    const std::string_view s(*list);
    for(std::size_t p=0;p<s.size();) {
        while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r'))++p;
        auto q=p;
        while(q<s.size()&&s[q]!=' '&&s[q]!='\t'&&s[q]!='\n'&&s[q]!='\r')++q;
        if(q>p&&s.substr(p,q-p)==token)return true;
        p=q;
    }
    return false;
}
int HexDigit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool HasScheme(std::string_view href) {
    for(std::size_t i=0;i<href.size();++i) {
        const char c=href[i];
        if(c==':')return i>0;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(i>0&&((c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'))))return false;
    }
    return false;
}
// Resolves a package-relative IRI reference against a directory ("/OEBPS/")
// to a part name: fragment and query dropped, percent-decoded, ".." may not
// escape the root. Returns nothing for absolute IRIs, pure fragments and
// anything unsafe.
std::optional<std::string> ResolveHref(std::string_view directory,std::string_view href) {
    if(HasScheme(href))return std::nullopt;
    href=href.substr(0,std::min(href.find('#'),href.find('?')));
    if(href.empty())return std::nullopt;
    std::string decoded;
    for(std::size_t i=0;i<href.size();++i) {
        if(href[i]=='%') {
            if(i+2>=href.size())return std::nullopt;
            const int hi=HexDigit(href[i+1]),lo=HexDigit(href[i+2]);
            if(hi<0||lo<0)return std::nullopt;
            decoded.push_back(char(hi*16+lo));
            i+=2;
        } else {
            decoded.push_back(href[i]);
        }
    }
    if(!ValidUtf8(decoded)||decoded.find('\\')!=std::string::npos)return std::nullopt;
    for(const char c:decoded)if(std::uint8_t(c)<0x20)return std::nullopt;
    const std::string path=decoded.front()=='/'?decoded:std::string(directory)+decoded;
    std::vector<std::string_view> segments;
    const std::string_view view(path);
    for(std::size_t start=0;start<view.size();) {
        auto end=view.find('/',start);
        if(end==std::string_view::npos)end=view.size();
        const auto segment=view.substr(start,end-start);
        if(segment=="..") {if(segments.empty())return std::nullopt;segments.pop_back();}
        else if(!segment.empty()&&segment!=".")segments.push_back(segment);
        start=end+1;
    }
    if(segments.empty())return std::nullopt;
    std::string result;
    for(const auto segment:segments) {result.push_back('/');result.append(segment);}
    return result;
}

// ---- XHTML adapter (the XML tokenizer stays unchanged) ----
// Common XHTML 1.x named entities, rewritten as numeric references only when
// the document declares an XHTML DTD (which is what defines them).
struct NamedEntity {std::string_view name;std::uint32_t code;};
constexpr NamedEntity xhtmlEntities[]={
    {"nbsp",160},{"iexcl",161},{"cent",162},{"pound",163},{"curren",164},{"yen",165},{"brvbar",166},{"sect",167},
    {"uml",168},{"copy",169},{"ordf",170},{"laquo",171},{"not",172},{"shy",173},{"reg",174},{"macr",175},{"deg",176},
    {"plusmn",177},{"sup2",178},{"sup3",179},{"acute",180},{"micro",181},{"para",182},{"middot",183},{"cedil",184},
    {"sup1",185},{"ordm",186},{"raquo",187},{"frac14",188},{"frac12",189},{"frac34",190},{"iquest",191},
    {"Agrave",192},{"Aacute",193},{"Acirc",194},{"Atilde",195},{"Auml",196},{"Aring",197},{"AElig",198},{"Ccedil",199},
    {"Egrave",200},{"Eacute",201},{"Ecirc",202},{"Euml",203},{"Igrave",204},{"Iacute",205},{"Icirc",206},{"Iuml",207},
    {"ETH",208},{"Ntilde",209},{"Ograve",210},{"Oacute",211},{"Ocirc",212},{"Otilde",213},{"Ouml",214},{"times",215},
    {"Oslash",216},{"Ugrave",217},{"Uacute",218},{"Ucirc",219},{"Uuml",220},{"Yacute",221},{"THORN",222},{"szlig",223},
    {"agrave",224},{"aacute",225},{"acirc",226},{"atilde",227},{"auml",228},{"aring",229},{"aelig",230},{"ccedil",231},
    {"egrave",232},{"eacute",233},{"ecirc",234},{"euml",235},{"igrave",236},{"iacute",237},{"icirc",238},{"iuml",239},
    {"eth",240},{"ntilde",241},{"ograve",242},{"oacute",243},{"ocirc",244},{"otilde",245},{"ouml",246},{"divide",247},
    {"oslash",248},{"ugrave",249},{"uacute",250},{"ucirc",251},{"uuml",252},{"yacute",253},{"thorn",254},{"yuml",255},
    {"OElig",338},{"oelig",339},{"Scaron",352},{"scaron",353},{"Yuml",376},{"fnof",402},{"circ",710},{"tilde",732},
    {"ensp",8194},{"emsp",8195},{"thinsp",8201},{"zwnj",8204},{"zwj",8205},{"lrm",8206},{"rlm",8207},
    {"ndash",8211},{"mdash",8212},{"lsquo",8216},{"rsquo",8217},{"sbquo",8218},{"ldquo",8220},{"rdquo",8221},
    {"bdquo",8222},{"dagger",8224},{"Dagger",8225},{"bull",8226},{"hellip",8230},{"permil",8240},{"prime",8242},
    {"Prime",8243},{"lsaquo",8249},{"rsaquo",8250},{"oline",8254},{"frasl",8260},{"euro",8364},{"trade",8482},
    {"larr",8592},{"uarr",8593},{"rarr",8594},{"darr",8595},{"harr",8596},{"minus",8722},{"le",8804},{"ge",8805},
    {"ne",8800},{"infin",8734},
};
bool Space(char c) {return c==' '||c=='\t'||c=='\n'||c=='\r';}
// Removes a DOCTYPE without an internal subset from the prolog (HTML5
// "<!DOCTYPE html>" or the XHTML 1.x public identifiers); a DOCTYPE with an
// internal subset is left for the tokenizer to reject.
std::string PrepareXhtml(std::string text) {
    std::size_t p=text.compare(0,3,"\xef\xbb\xbf")==0?3:0;
    std::size_t doctype=std::string::npos;
    for(int guard=0;guard<64&&p<text.size();++guard) {
        while(p<text.size()&&Space(text[p]))++p;
        if(text.compare(p,2,"<?")==0) {const auto end=text.find("?>",p);if(end==std::string::npos)return text;p=end+2;continue;}
        if(text.compare(p,4,"<!--")==0) {const auto end=text.find("-->",p+4);if(end==std::string::npos)return text;p=end+3;continue;}
        if(p+9<=text.size()&&EqualsNoCase(std::string_view(text).substr(p,9),"<!DOCTYPE"))doctype=p;
        break;
    }
    if(doctype==std::string::npos)return text;
    const auto end=text.find('>',doctype);
    if(end==std::string::npos||end-doctype>2048)return text;
    const auto declaration=std::string_view(text).substr(doctype,end-doctype+1);
    if(declaration.find('[')!=std::string_view::npos)return text;
    auto q=std::size_t(9);
    while(q<declaration.size()&&Space(declaration[q]))++q;
    if(!EqualsNoCase(declaration.substr(q,4),"html")||(q+4<declaration.size()&&!Space(declaration[q+4])&&declaration[q+4]!='>'))return text;
    const bool xhtmlDtd=declaration.find("-//W3C//DTD XHTML")!=std::string_view::npos;
    std::fill(text.begin()+std::ptrdiff_t(doctype),text.begin()+std::ptrdiff_t(end+1),' ');
    if(!xhtmlDtd)return text;
    std::string out;
    out.reserve(text.size());
    for(std::size_t i=0;i<text.size();++i) {
        if(text[i]=='&') {
            auto j=i+1;
            while(j<text.size()&&j-i<=8&&((text[j]>='a'&&text[j]<='z')||(text[j]>='A'&&text[j]<='Z')||(text[j]>='0'&&text[j]<='9')))++j;
            if(j<text.size()&&text[j]==';'&&j>i+1) {
                const auto name=std::string_view(text).substr(i+1,j-i-1);
                bool replaced=false;
                for(const auto& entity:xhtmlEntities)
                    if(entity.name==name) {out+="&#"+std::to_string(entity.code)+";";replaced=true;break;}
                if(replaced) {i=j;continue;}
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

class XhtmlWalker {
public:
    XhtmlWalker(const XmlDocument& xml,Flow& flow,Context& context):xml_(xml),flow_(flow),context_(context) {}
    void Body() {
        const auto* body=FirstChild(xml_,xml_.Root(),xhtmlNs,"body");
        if(body)flow_.Paragraph([&] {Children(*body,{},false);});
    }
private:
    static int HeadingLevel(std::string_view local) {
        return local.size()==2&&local[0]=='h'&&local[1]>='1'&&local[1]<='6'?local[1]-'0':0;
    }
    void Children(const XmlElement& e,const Format& f,bool pre) {
        const Nest nest(context_);
        bool first=true;
        for(const auto& c:e.children) {
            if(c.text) {
                std::string_view text=xml_.texts[c.index];
                if(!pre) {flow_.Collapsed(text,f);continue;}
                if(first&&!text.empty()&&text.front()=='\n')text.remove_prefix(1); // HTML drops a newline right after <pre>.
                flow_.Literal(text,f);
            } else {
                Element(xml_.elements[c.index],f,pre);
            }
            first=false;
        }
    }
    void Element(const XmlElement& e,const Format& f,bool pre) {
        const auto ns=xml_.Namespace(e);
        if(ns==svgNs) {++context_.report.excluded.images;return;}
        if(ns==mathNs) {++context_.report.excluded.graphics;return;}
        if(ns!=xhtmlNs)return; // epub:switch, epub:trigger and foreign markup are not body text.
        if(xml_.Attribute(e,"","hidden"))return;
        const auto& n=e.local;
        auto& excluded=context_.report.excluded;
        if(n=="script"||n=="style"||n=="template"||n=="noscript") {++excluded.scripts;return;}
        if(n=="nav") {++excluded.navigation;return;}
        if(n=="img"||n=="picture") {++excluded.images;return;}
        if(n=="video"||n=="audio"||n=="object"||n=="embed"||n=="iframe"||n=="canvas"||n=="map") {++excluded.graphics;return;}
        if(n=="head"||n=="title"||n=="meta"||n=="link"||n=="base"||n=="rt"||n=="rp"||n=="colgroup"||n=="col")return;
        if(n=="br") {flow_.Break(f);return;}
        if(n=="hr") {flow_.Boundary();return;}
        if(const int level=HeadingLevel(n)) {flow_.Heading(level,[&] {Children(e,f,pre);});return;}
        if(n=="ul"||n=="ol") {List(e,n=="ol",f);return;}
        if(n=="table") {Table(e,f);return;}
        if(n=="pre") {flow_.Paragraph([&] {Children(e,f,true);});return;}
        if(n=="p"||n=="div"||n=="blockquote"||n=="section"||n=="article"||n=="header"||n=="footer"||n=="main"
            ||n=="aside"||n=="figure"||n=="figcaption"||n=="address"||n=="center"||n=="details"||n=="summary"
            ||n=="dl"||n=="dt"||n=="dd"||n=="hgroup"||n=="form"||n=="fieldset"||n=="legend"||n=="li"
            ||n=="caption"||n=="body") {
            // Block quotes are plain paragraphs: the model has no quote block.
            flow_.Paragraph([&] {Children(e,f,pre);});
            return;
        }
        auto inner=f;
        if(n=="a") {
            // Only external http/https/mailto targets stay links; relative
            // references name parts of this package and become plain text.
            inner.link.clear();
            if(const auto* href=xml_.Attribute(e,"","href");href&&ExternalLink(*href))inner.link=*href;
        } else if(n=="strong"||n=="b") {inner.bold=true;}
        else if(n=="em"||n=="i") {inner.italic=true;}
        Children(e,inner,pre); // span, code, small, sub, sup, abbr, q, cite, ... keep their text.
    }
    void List(const XmlElement& list,bool ordered,const Format& f) {
        const Nest nest(context_);
        ++depth_;
        for(const auto& c:list.children) {
            if(c.text)continue;
            const auto& e=xml_.elements[c.index];
            if(Is(xml_,e,xhtmlNs,"li")) flow_.ListItem(depth_,ordered,[&] {Children(e,f,false);});
            else Element(e,f,false); // Lists nested directly in a list, or stray markup.
        }
        --depth_;
    }
    void Table(const XmlElement& table,const Format& f) {
        std::vector<Row> rows;
        Rows(table,rows,f);
        flow_.Table(std::move(rows));
    }
    void Rows(const XmlElement& container,std::vector<Row>& rows,const Format& f) {
        const Nest nest(context_);
        ForChildren(xml_,container,[&](const XmlElement& e) {
            if(xml_.Namespace(e)!=xhtmlNs)return;
            if(e.local=="thead"||e.local=="tbody"||e.local=="tfoot") {Rows(e,rows,f);}
            else if(e.local=="caption") {flow_.Paragraph([&] {Children(e,f,false);});}
            else if(e.local=="tr") {
                Row row;
                ForChildren(xml_,e,[&](const XmlElement& cell) {
                    if(!Is(xml_,cell,xhtmlNs,"td")&&!Is(xml_,cell,xhtmlNs,"th"))return;
                    context_.ChargeCells(1);
                    row.push_back(flow_.Capture([&] {Children(cell,f,false);}));
                });
                if(!row.empty())rows.push_back(std::move(row));
            }
        });
    }

    const XmlDocument& xml_;
    Flow& flow_;
    Context& context_;
    int depth_=-1;
};

struct ManifestItem {std::string path,mediaType,properties;};

XmlDocument ParseContent(const std::string& bytes,const XmlLimits& limits) {
    try {
        auto xml=ParseXml(PrepareXhtml(bytes),limits);
        if(!Is(xml,xml.Root(),xhtmlNs,"html"))FailText(E::epub_content_not_xhtml);
        return xml;
    } catch(const Failure& failure) {
        if(failure.error==Error::xml_malformed)FailText(E::epub_content_not_xhtml);
        throw;
    }
}

// Table-of-contents titles by target part: the EPUB 3 navigation document's
// toc nav, else the EPUB 2 NCX navMap. The first entry for a part wins.
void NavigationTitles(const XmlDocument& nav,std::string_view directory,std::map<std::string,std::string>& titles) {
    std::vector<bool> inToc(nav.elements.size(),false);
    for(std::size_t i=0;i<nav.elements.size();++i) { // Elements are stored in document order, parents first.
        const auto& e=nav.elements[i];
        const bool parent=e.parent!=0xffffffffu&&inToc[e.parent];
        inToc[i]=parent||(Is(nav,e,xhtmlNs,"nav")&&HasToken(nav.Attribute(e,opsNs,"type"),"toc"));
        if(!inToc[i]||!Is(nav,e,xhtmlNs,"a"))continue;
        const auto* href=nav.Attribute(e,"","href");
        if(!href)continue;
        const auto target=ResolveHref(directory,*href);
        std::string text;
        AllText(nav,e,text);
        auto title=CollapseSpace(text);
        if(target&&!title.empty())titles.emplace(*target,std::move(title));
    }
}
void NcxTitles(const XmlDocument& ncx,std::string_view directory,std::map<std::string,std::string>& titles) {
    if(!Is(ncx,ncx.Root(),ncxNs,"ncx"))FailText(E::epub_invalid_package);
    for(const auto& e:ncx.elements) {
        if(!Is(ncx,e,ncxNs,"navPoint"))continue;
        const auto* label=FirstChild(ncx,e,ncxNs,"navLabel");
        const auto* text=label?FirstChild(ncx,*label,ncxNs,"text"):nullptr;
        const auto* content=FirstChild(ncx,e,ncxNs,"content");
        const auto* src=content?ncx.Attribute(*content,"","src"):nullptr;
        if(!text||!src)continue;
        std::string raw;
        AllText(ncx,*text,raw);
        auto title=CollapseSpace(raw);
        const auto target=ResolveHref(directory,*src);
        if(target&&!title.empty())titles.emplace(*target,std::move(title));
    }
}
}

void ExtractEpub(Bytes bytes,Context& context,Document& document) {
    const auto& options=context.options.extract;
    ZipArchive zip(bytes,options.zip);
    auto mimetype=ReadPart(zip,"mimetype");
    if(mimetype)while(!mimetype->empty()&&Space(mimetype->back()))mimetype->pop_back();
    if(!mimetype||*mimetype!="application/epub+zip")FailText(E::epub_not_epub);
    if(zip.Find("META-INF/rights.xml"))FailText(E::epub_drm);
    if(const auto encryption=ReadPart(zip,"META-INF/encryption.xml")) {
        const auto xml=ParseXml(*encryption,options.xml);
        if(!Is(xml,xml.Root(),containerNs,"encryption"))FailText(E::epub_drm);
        for(const auto& e:xml.elements) {
            if(!Is(xml,e,xmlEncNs,"EncryptedData"))continue;
            const auto* method=FirstChild(xml,e,xmlEncNs,"EncryptionMethod");
            const auto* algorithm=method?xml.Attribute(*method,"","Algorithm"):nullptr;
            bool obfuscation=false;
            for(const auto a:obfuscationAlgorithms)if(algorithm&&*algorithm==a)obfuscation=true;
            if(!obfuscation)FailText(E::epub_drm);
        }
    }
    const auto containerBytes=ReadPart(zip,"META-INF/container.xml");
    if(!containerBytes)FailText(E::epub_invalid_package);
    std::string packagePath;
    {
        const auto container=ParseXml(*containerBytes,options.xml);
        if(!Is(container,container.Root(),containerNs,"container"))FailText(E::epub_invalid_package);
        const auto* rootfiles=FirstChild(container,container.Root(),containerNs,"rootfiles");
        if(!rootfiles)FailText(E::epub_invalid_package);
        ForChildren(container,*rootfiles,[&](const XmlElement& e) {
            if(!packagePath.empty()||!Is(container,e,containerNs,"rootfile"))return;
            const auto* type=container.Attribute(e,"","media-type");
            const auto* path=container.Attribute(e,"","full-path");
            if(!type||*type!="application/oebps-package+xml")return;
            if(!path)FailText(E::epub_invalid_package);
            const auto resolved=ResolveHref("/",*path);
            if(!resolved)FailText(E::epub_invalid_package);
            packagePath=*resolved;
        });
    }
    if(packagePath.empty())FailText(E::epub_invalid_package);
    const auto packageBytes=ReadPart(zip,packagePath);
    if(!packageBytes)FailText(E::epub_missing_part);
    const auto opf=ParseXml(*packageBytes,options.xml);
    const auto& package=opf.Root();
    if(!Is(opf,package,opfNs,"package"))FailText(E::epub_invalid_package);
    const auto* version=opf.Attribute(package,"","version");
    if(!version||(version->rfind("2.",0)!=0&&version->rfind("3.",0)!=0))FailText(E::epub_invalid_package);
    const auto* manifest=FirstChild(opf,package,opfNs,"manifest");
    const auto* spine=FirstChild(opf,package,opfNs,"spine");
    if(!manifest||!spine)FailText(E::epub_invalid_package);
    const auto packageDirectory=DirectoryOf(packagePath);
    std::map<std::string,ManifestItem> items;
    ForChildren(opf,*manifest,[&](const XmlElement& e) {
        if(!Is(opf,e,opfNs,"item"))return;
        const auto* id=opf.Attribute(e,"","id");
        const auto* href=opf.Attribute(e,"","href");
        const auto* type=opf.Attribute(e,"","media-type");
        if(!id||id->empty()||!href||!type)FailText(E::epub_invalid_package);
        ManifestItem item;
        if(!HasScheme(*href)) { // Remote resources (EPUB 3 audio/video) have no part.
            const auto path=ResolveHref(packageDirectory,*href);
            if(!path)FailText(E::epub_invalid_package);
            item.path=*path;
        }
        item.mediaType=*type;
        if(const auto* properties=opf.Attribute(e,"","properties"))item.properties=*properties;
        if(!items.emplace(*id,std::move(item)).second)FailText(E::epub_invalid_package);
    });
    std::vector<std::pair<const ManifestItem*,bool>> order; // Item, linear.
    ForChildren(opf,*spine,[&](const XmlElement& e) {
        if(!Is(opf,e,opfNs,"itemref"))return;
        const auto* idref=opf.Attribute(e,"","idref");
        if(!idref)FailText(E::epub_invalid_package);
        const auto found=items.find(*idref);
        if(found==items.end())FailText(E::epub_invalid_package);
        if(order.size()>=context.options.maximumItems)FailText(E::item_limit);
        const auto* linear=opf.Attribute(e,"","linear");
        order.emplace_back(&found->second,!(linear&&*linear=="no"));
    });

    std::map<std::string,std::string> titles;
    for(const auto& [id,item]:items) {
        if(item.path.empty()||!HasToken(&item.properties,"nav"))continue;
        if(const auto bytes=ReadPart(zip,item.path))NavigationTitles(ParseContent(*bytes,options.xml),DirectoryOf(item.path),titles);
        break;
    }
    if(titles.empty())
        if(const auto* toc=opf.Attribute(*spine,"","toc"))
            if(const auto found=items.find(*toc);found!=items.end()&&!found->second.path.empty())
                if(const auto bytes=ReadPart(zip,found->second.path))
                    NcxTitles(ParseXml(*bytes,options.xml),DirectoryOf(found->second.path),titles);

    ModelBuilder builder(document,options);
    Flow flow(builder,context);
    auto& excluded=context.report.excluded;
    for(const auto& [item,linear]:order) {
        if(!linear) {++excluded.nonLinear;continue;}
        if(HasToken(&item->properties,"nav")) {++excluded.navigation;continue;}
        if(item->mediaType=="text/html")FailText(E::epub_content_not_xhtml);
        if(item->mediaType=="application/x-dtbncx+xml") {++excluded.navigation;continue;}
        if(item->mediaType!="application/xhtml+xml"||item->path.empty()) {++excluded.unsupportedItems;continue;}
        const auto bytes=ReadPart(zip,item->path);
        if(!bytes)FailText(E::epub_missing_part);
        const auto xml=ParseContent(*bytes,options.xml);
        ++context.report.chapters;
        const bool hasHeading=std::any_of(xml.elements.begin(),xml.elements.end(),[&](const XmlElement& e) {
            return xml.Namespace(e)==xhtmlNs&&e.local.size()==2&&e.local[0]=='h'&&e.local[1]>='1'&&e.local[1]<='6';
        });
        if(!hasHeading)
            if(const auto title=titles.find(item->path);title!=titles.end()) {
                Block heading;
                heading.kind=BlockKind::heading;heading.level=1;
                flow.Append(heading.runs,title->second,false,false);
                flow.Push(std::move(heading));
                ++context.report.tocHeadings;
            }
        XhtmlWalker walker(xml,flow,context);
        walker.Body();
        flow.Finish();
    }
}
}
