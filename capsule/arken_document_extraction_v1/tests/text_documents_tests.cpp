#include "arken_document_extraction_v1/text_documents.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Qualification of text_documents.hpp: PPTX, ODT, ODP and EPUB fixtures are
// authored here (stored ZIP via test_support/docx_fixtures.hpp, independent of
// the capsule's reader) with their expected TXT and Markdown bytes.
namespace doc=arken::document::v1;
using doc::OutputFormat;
using doc::TextDocumentError;
using doc::TextDocumentFormat;
using Members=std::vector<std::pair<std::string,std::string>>;

namespace {
int failures=0;

std::string Visible(std::string_view s) {
    std::string out;
    for(const char c:s) {
        if(c=='\n')out+="\\n\n";
        else if(c=='\t')out+="\\t";
        else out.push_back(c);
    }
    return out;
}
void Expect(bool ok,std::string_view what) {
    if(ok)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n";
}
std::span<const std::byte> Span(const std::string& s) {return std::as_bytes(std::span<const char>(s.data(),s.size()));}
doc::TextDocumentResult Convert(TextDocumentFormat format,OutputFormat output,const std::string& bytes,const doc::TextDocumentOptions& options={}) {
    return doc::ConvertTextDocument(format,output,Span(bytes),options);
}
void ExpectText(const doc::TextDocumentResult& got,std::string_view want,std::string_view what) {
    if(!got) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<doc::ErrorCode(got.error)<<"\n";return;}
    if(got.text==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n--- got ---\n"<<Visible(got.text)<<"\n--- want ---\n"<<Visible(want)<<"\n";
}
void ExpectConvert(TextDocumentFormat format,const std::string& bytes,std::string_view txt,std::string_view markdown,
    std::string_view what,const doc::TextDocumentOptions& options={}) {
    ExpectText(Convert(format,OutputFormat::text,bytes,options),txt,std::string(what)+" (txt)");
    ExpectText(Convert(format,OutputFormat::markdown,bytes,options),markdown,std::string(what)+" (markdown)");
}
void ExpectError(TextDocumentFormat format,const std::string& bytes,TextDocumentError want,std::string_view what,
    const doc::TextDocumentOptions& options={}) {
    for(const auto output:{OutputFormat::text,OutputFormat::markdown}) {
        const auto got=Convert(format,output,bytes,options);
        if(got.error==want&&got.text.empty())continue;
        ++failures;
        std::cerr<<"FAIL: "<<what<<": got "<<doc::ErrorCode(got.error)<<", want "<<doc::ErrorCode(want)<<"\n";
        return;
    }
}
std::string Replace(std::string s,std::string_view from,std::string_view to) {
    const auto at=s.find(from);
    if(at==std::string::npos) {++failures;std::cerr<<"FAIL: fixture edit did not apply: "<<from<<"\n";return s;}
    return s.replace(at,from.size(),to);
}
std::string Zip(const Members& members) {return docx_fixture::StoredZip(members);}

// ---- PPTX authoring ----
constexpr std::string_view pNs="http://schemas.openxmlformats.org/presentationml/2006/main";
constexpr std::string_view aNs="http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr std::string_view rNs="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view relsNs="http://schemas.openxmlformats.org/package/2006/relationships";
constexpr std::string_view relBase="http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
constexpr std::string_view presentationType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml";
const std::string xmlDecl="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";

std::string Shape(std::string_view placeholder,std::string_view paragraphs) {
    return "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"s\"/><p:cNvSpPr/><p:nvPr>"+std::string(placeholder)
        +"</p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/>"+std::string(paragraphs)+"</p:txBody></p:sp>";
}
std::string Run(std::string_view text,std::string_view properties="<a:rPr lang=\"en-US\"/>") {
    return "<a:r>"+std::string(properties)+"<a:t>"+std::string(text)+"</a:t></a:r>";
}
std::string Para(std::string_view runs,std::string_view properties="") {return "<a:p>"+std::string(properties)+std::string(runs)+"</a:p>";}
std::string SpTree(std::string_view shapes) {
    return "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
        +std::string(shapes)+"</p:spTree></p:cSld>";
}
std::string Rel(std::string_view id,std::string_view type,std::string_view target,bool external=false) {
    return "<Relationship Id=\""+std::string(id)+"\" Type=\""+std::string(relBase)+std::string(type)+"\" Target=\""
        +std::string(target)+"\""+(external?" TargetMode=\"External\"":"")+"/>";
}
std::string Rels(std::string_view inner) {return xmlDecl+"<Relationships xmlns=\""+std::string(relsNs)+"\">"+std::string(inner)+"</Relationships>";}
struct PptxSlide {std::string shapes,rels,notes;};
struct PptxSpec {
    std::vector<PptxSlide> slides;
    std::vector<int> order; // 1-based slide file numbers in sldIdLst order; empty = file order.
    std::string mainType=std::string(presentationType);
    Members extra;
};
Members PptxMembers(const PptxSpec& spec) {
    const std::string ns=" xmlns:a=\""+std::string(aNs)+"\" xmlns:p=\""+std::string(pNs)+"\" xmlns:r=\""+std::string(rNs)+"\"";
    std::string types=xmlDecl+"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\""+spec.mainType+"\"/>";
    std::string presentationRels,ids;
    Members members;
    for(std::size_t i=1;i<=spec.slides.size();++i) {
        const auto n=std::to_string(i);
        types+="<Override PartName=\"/ppt/slides/slide"+n+".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
        presentationRels+=Rel("rIdS"+n,"slide","slides/slide"+n+".xml");
        const auto& slide=spec.slides[i-1];
        members.emplace_back("ppt/slides/slide"+n+".xml",xmlDecl+"<p:sld"+ns+">"+SpTree(slide.shapes)+"<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>");
        std::string rels=slide.rels+Rel("rIdL","slideLayout","../slideLayouts/slideLayout1.xml");
        if(!slide.notes.empty()) {
            types+="<Override PartName=\"/ppt/notesSlides/notesSlide"+n+".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml\"/>";
            rels+=Rel("rIdN","notesSlide","../notesSlides/notesSlide"+n+".xml");
            members.emplace_back("ppt/notesSlides/notesSlide"+n+".xml",xmlDecl+"<p:notes"+ns+">"+SpTree(
                Shape("<p:ph type=\"sldImg\"/>","")+Shape("<p:ph type=\"body\" idx=\"1\"/>",slide.notes))+"</p:notes>");
        }
        members.emplace_back("ppt/slides/_rels/slide"+n+".xml.rels",Rels(rels));
    }
    std::vector<int> order=spec.order;
    if(order.empty())for(std::size_t i=1;i<=spec.slides.size();++i)order.push_back(int(i));
    for(std::size_t k=0;k<order.size();++k)
        ids+="<p:sldId id=\""+std::to_string(256+k)+"\" r:id=\"rIdS"+std::to_string(order[k])+"\"/>";
    types+="</Types>";
    Members all{{"[Content_Types].xml",types},
        {"_rels/.rels",Rels(Rel("rId1","officeDocument","ppt/presentation.xml"))},
        {"ppt/presentation.xml",xmlDecl+"<p:presentation"+ns+"><p:sldIdLst>"+ids+"</p:sldIdLst><p:sldSz cx=\"9144000\" cy=\"6858000\"/></p:presentation>"},
        {"ppt/_rels/presentation.xml.rels",Rels(presentationRels+Rel("rIdM","slideMaster","slideMasters/slideMaster1.xml"))}};
    for(auto& m:members)all.push_back(std::move(m));
    for(const auto& m:spec.extra)all.push_back(m);
    return all;
}
std::string Pptx(const PptxSpec& spec) {return Zip(PptxMembers(spec));}

const std::string titleTypePh="<p:ph type=\"title\"/>";
PptxSpec ReferencePptx() {
    PptxSpec spec;
    // File slide1 is shown second, file slide2 first (sldIdLst order wins).
    PptxSlide results;
    results.shapes=Shape("<p:ph type=\"ctrTitle\"/>",Para(Run("Results")))
        +"<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"4\" name=\"t\"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr><p:xfrm/>"
         "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"><a:tbl><a:tblGrid/>"
         "<a:tr h=\"1\"><a:tc><a:txBody><a:bodyPr/>"+Para(Run("Name"))+"</a:txBody></a:tc><a:tc><a:txBody><a:bodyPr/>"+Para(Run("Score"))+"</a:txBody></a:tc></a:tr>"
         "<a:tr h=\"1\"><a:tc><a:txBody><a:bodyPr/>"+Para(Run("Ada"))+Para(Run("Lovelace"))+"</a:txBody></a:tc><a:tc><a:txBody><a:bodyPr/>"+Para(Run("9"))+"</a:txBody></a:tc></a:tr>"
         "</a:tbl></a:graphicData></a:graphic></p:graphicFrame>";
    results.notes=Para(Run("Remember to smile"))+Para(Run("and breathe"));
    PptxSlide agenda;
    agenda.shapes=Shape("<p:ph idx=\"1\"/>",
            Para(Run("First point"))+Para(Run("Sub point"),"<a:pPr lvl=\"1\"/>")+Para(Run("Plain body"),"<a:pPr><a:buNone/></a:pPr>"))
        +Shape("<p:ph type=\"sldNum\" idx=\"12\"/>",Para(Run("7")))
        +Shape(titleTypePh,Para(Run("Agenda")))
        +"<p:pic><p:nvPicPr><p:cNvPr id=\"5\" name=\"p\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill/><p:spPr/></p:pic>"
        +"<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"6\" name=\"c\"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr><p:xfrm/>"
         "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" r:id=\"rIdC\"/></a:graphicData></a:graphic></p:graphicFrame>"
        +Shape("",Para(Run("Step one"),"<a:pPr><a:buAutoNum type=\"arabicPeriod\"/></a:pPr>")+Para(Run("Step two"),"<a:pPr><a:buAutoNum type=\"arabicPeriod\"/></a:pPr>"))
        +"<p:grpSp><p:nvGrpSpPr><p:cNvPr id=\"7\" name=\"g\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
        +Shape("",Para(Run("Bold","<a:rPr b=\"1\"/>")+Run(" and ")+Run("italic","<a:rPr i=\"1\"/>")+"<a:br/>"+Run("next line")))
        +"</p:grpSp>"
        +Shape("",Para(Run("Visit ")+Run("site","<a:rPr><a:hlinkClick r:id=\"rIdH\"/></a:rPr>")+Run(" or ")+Run("slide 2","<a:rPr><a:hlinkClick r:id=\"rIdJ\" action=\"ppaction://hlinksldjump\"/></a:rPr>"),"<a:pPr><a:buChar char=\"x\"/></a:pPr>"));
    agenda.rels=Rel("rIdH","hyperlink","https://example.com/",true)+Rel("rIdJ","slide","slide1.xml");
    spec.slides={results,agenda};
    spec.order={2,1};
    return spec;
}
const std::string pptxText=
    "Slide 1\n\nAgenda\n\n- First point\n  - Sub point\n\nPlain body\n\n1. Step one\n2. Step two\n\n"
    "Bold and italic\nnext line\n\n- Visit site (https://example.com/) or slide 2\n\n"
    "Slide 2\n\nResults\n\nName\tScore\nAda Lovelace\t9\n";
const std::string pptxMarkdown=
    "# Slide 1\n\n## Agenda\n\n- First point\n  - Sub point\n\nPlain body\n\n1. Step one\n2. Step two\n\n"
    "**Bold** and *italic*\\\nnext line\n\n- Visit [site](https://example.com/) or slide 2\n\n"
    "# Slide 2\n\n## Results\n\n| Name | Score |\n| --- | --- |\n| Ada Lovelace | 9 |\n";

void PptxTests() {
    const auto bytes=Pptx(ReferencePptx());
    ExpectConvert(TextDocumentFormat::pptx,bytes,pptxText,pptxMarkdown,"PPTX reference");
    const auto result=Convert(TextDocumentFormat::pptx,OutputFormat::text,bytes);
    Expect(result.report.slides==2,"PPTX slide count");
    Expect(result.report.excluded.images==1&&result.report.excluded.graphics==1,"PPTX picture and chart excluded");
    Expect(result.report.excluded.placeholders==1,"PPTX slide-number placeholder excluded");
    Expect(result.report.excluded.notes==1&&result.report.notesIncluded==0,"PPTX notes off by default and reported");

    doc::TextDocumentOptions notes;
    notes.includeNotes=true;
    const auto withNotes=Convert(TextDocumentFormat::pptx,OutputFormat::text,bytes,notes);
    ExpectText(withNotes,pptxText+"\nNotes: Remember to smile\nand breathe\n","PPTX notes option (txt)");
    ExpectText(Convert(TextDocumentFormat::pptx,OutputFormat::markdown,bytes,notes),
        pptxMarkdown+"\n**Notes:** Remember to smile\\\nand breathe\n","PPTX notes option (markdown)");
    Expect(withNotes.report.notesIncluded==1&&withNotes.report.excluded.notes==0,"PPTX notes reported as included");

    // Merged table cells stay empty; fallback branches of mc:AlternateContent are walked.
    PptxSpec merged;
    merged.slides={{"<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"4\" name=\"t\"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr><p:xfrm/>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"><a:tbl>"
        "<a:tr><a:tc gridSpan=\"2\"><a:txBody>"+Para(Run("Wide"))+"</a:txBody></a:tc><a:tc hMerge=\"1\"><a:txBody>"+Para(Run("hidden"))+"</a:txBody></a:tc></a:tr>"
        "<a:tr><a:tc><a:txBody>"+Para(Run("L"))+"</a:txBody></a:tc><a:tc><a:txBody>"+Para(Run("R"))+"</a:txBody></a:tc></a:tr>"
        "</a:tbl></a:graphicData></a:graphic></p:graphicFrame>"
        "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"><mc:Choice Requires=\"p14\">"
        +Shape("",Para(Run("choice")))+"</mc:Choice><mc:Fallback>"+Shape("",Para(Run("fallback")))+"</mc:Fallback></mc:AlternateContent>",{},{}}};
    ExpectConvert(TextDocumentFormat::pptx,Pptx(merged),"Slide 1\n\nWide\t\nL\tR\n\nfallback\n",
        "# Slide 1\n\n| Wide |  |\n| --- | --- |\n| L | R |\n\nfallback\n","PPTX merged cells and AlternateContent");

    // A presentation without slides is an empty success.
    ExpectConvert(TextDocumentFormat::pptx,Pptx(PptxSpec{}),"","","PPTX without slides");

    // Package failures.
    auto spec=ReferencePptx();
    spec.mainType="application/vnd.ms-powerpoint.presentation.macroEnabled.main+xml";
    ExpectError(TextDocumentFormat::pptx,Pptx(spec),TextDocumentError::pptx_macro_enabled,"PPTX macro-enabled main part");
    spec=ReferencePptx();
    spec.extra={{"ppt/vbaProject.bin","x"}};
    ExpectError(TextDocumentFormat::pptx,Pptx(spec),TextDocumentError::pptx_macro_enabled,"PPTX VBA project member");
    ExpectError(TextDocumentFormat::pptx,docx_fixture::EncryptedDocx(),TextDocumentError::pptx_encrypted,"PPTX encrypted package");
    ExpectError(TextDocumentFormat::pptx,std::string("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1",8)+std::string(600,'\0'),
        TextDocumentError::pptx_encrypted,"PPTX OLE compound file");
    ExpectError(TextDocumentFormat::pptx,docx_fixture::Docx("<w:p><w:r><w:t>x</w:t></w:r></w:p>"),
        TextDocumentError::pptx_not_presentation,"DOCX given as PPTX");
    auto members=PptxMembers(ReferencePptx());
    std::erase_if(members,[](const auto& m) {return m.first=="ppt/slides/slide1.xml";});
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_missing_part,"PPTX missing slide part");
    members=PptxMembers(ReferencePptx());
    for(auto& [name,data]:members)if(name=="ppt/presentation.xml")data=Replace(data,"rIdS1","rIdX");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_invalid_package,"PPTX sldId without relationship");
    members=PptxMembers(ReferencePptx());
    for(auto& [name,data]:members)if(name=="ppt/presentation.xml")data=Replace(data,"rIdS1","rIdS2");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_invalid_package,"PPTX duplicate sldId relationship");
    members=PptxMembers(ReferencePptx());
    for(auto& [name,data]:members)if(name=="[Content_Types].xml")data=Replace(data,"/ppt/slides/slide1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml","/ppt/slides/slide1.xml\" ContentType=\"application/xml");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_invalid_package,"PPTX slide with wrong content type");
    members=PptxMembers(ReferencePptx());
    std::erase_if(members,[](const auto& m) {return m.first=="[Content_Types].xml";});
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_invalid_package,"PPTX without content types");
    members=PptxMembers(ReferencePptx());
    for(auto& [name,data]:members)if(name=="ppt/slides/slide2.xml")data=Replace(data,"</p:sld>","");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::xml_malformed,"PPTX malformed slide XML");
    members=PptxMembers(ReferencePptx());
    for(auto& [name,data]:members)if(name=="ppt/presentation.xml")data=Replace(data,"<p:presentation","<!DOCTYPE p [<!ENTITY x \"y\">]><p:presentation");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::xml_doctype,"PPTX DOCTYPE rejected");
    // Notes are only read (and so only validated) when requested.
    members=PptxMembers(ReferencePptx());
    std::erase_if(members,[](const auto& m) {return m.first=="ppt/notesSlides/notesSlide1.xml";});
    ExpectText(Convert(TextDocumentFormat::pptx,OutputFormat::text,Zip(members)),pptxText,"PPTX missing notes ignored when notes are off");
    ExpectError(TextDocumentFormat::pptx,Zip(members),TextDocumentError::pptx_missing_part,"PPTX missing notes part when requested",notes);
}

// ---- OpenDocument authoring ----
const std::string odfNamespaces=
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
    " xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\"";
constexpr std::string_view odtType="application/vnd.oasis.opendocument.text";
constexpr std::string_view odpType="application/vnd.oasis.opendocument.presentation";
std::string OdfContent(std::string_view automaticStyles,std::string_view kind,std::string_view body) {
    return xmlDecl+"<office:document-content"+odfNamespaces+" office:version=\"1.3\"><office:automatic-styles>"+std::string(automaticStyles)
        +"</office:automatic-styles><office:body><office:"+std::string(kind)+">"+std::string(body)+"</office:"+std::string(kind)+"></office:body></office:document-content>";
}
std::string Manifest(std::string_view mimetype,std::string_view extra="") {
    return xmlDecl+"<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\""+std::string(mimetype)+"\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"+std::string(extra)+"</manifest:manifest>";
}
Members OdfMembers(std::string_view mimetype,const std::string& content,const std::string& styles="") {
    Members members{{"mimetype",std::string(mimetype)},{"META-INF/manifest.xml",Manifest(mimetype)},{"content.xml",content}};
    if(!styles.empty())members.emplace_back("styles.xml",styles);
    return members;
}
const std::string odtStyles=xmlDecl+"<office:document-styles"+odfNamespaces+"><office:styles>"
    "<style:style style:name=\"Emphasis\" style:family=\"text\"><style:text-properties fo:font-style=\"italic\"/></style:style>"
    "<text:list-style style:name=\"Numbering_20_123\"><text:list-level-style-number text:level=\"1\" style:num-format=\"1\"/></text:list-style>"
    "</office:styles></office:document-styles>";
const std::string odtAutomatic=
    "<style:style style:name=\"T1\" style:family=\"text\"><style:text-properties fo:font-weight=\"bold\"/></style:style>"
    "<style:style style:name=\"T2\" style:family=\"text\" style:parent-style-name=\"Emphasis\"/>"
    "<style:style style:name=\"T3\" style:family=\"text\"><style:text-properties fo:font-weight=\"700\" fo:font-style=\"italic\"/></style:style>"
    "<style:style style:name=\"P1\" style:family=\"paragraph\"><style:text-properties fo:font-weight=\"bold\"/></style:style>"
    "<style:style style:name=\"T4\" style:family=\"text\"><style:text-properties fo:font-weight=\"normal\"/></style:style>"
    "<text:list-style style:name=\"L1\"><text:list-level-style-number text:level=\"1\" style:num-format=\"1\"/>"
    "<text:list-level-style-bullet text:level=\"2\" text:bullet-char=\"*\"/></text:list-style>"
    "<text:list-style style:name=\"L2\"><text:list-level-style-bullet text:level=\"1\" text:bullet-char=\"*\"/></text:list-style>";
const std::string odtBody=
    "<text:sequence-decls><text:sequence-decl text:display-outline-level=\"0\" text:name=\"Figure\"/></text:sequence-decls>"
    "<text:tracked-changes><text:changed-region text:id=\"c1\"><text:deletion><office:change-info/><text:p>removed words</text:p></text:deletion></text:changed-region>"
    "<text:changed-region text:id=\"c2\"><text:insertion><office:change-info/></text:insertion></text:changed-region></text:tracked-changes>"
    "<text:h text:outline-level=\"1\">Report  title</text:h>"
    "<text:p>\n  Lead   paragraph with<text:s text:c=\"3\"/>gaps,<text:tab/>tab and<text:line-break/>break.\n</text:p>"
    "<text:p>Styled <text:span text:style-name=\"T1\">bold</text:span> and <text:span text:style-name=\"T2\">italic</text:span> and "
    "<text:a xlink:type=\"simple\" xlink:href=\"https://example.org/x\">a link</text:a> and <text:a xlink:href=\"#bookmark\">internal</text:a>."
    "<text:note text:note-class=\"footnote\"><text:note-citation>1</text:note-citation><text:note-body><text:p>Footnote text</text:p></text:note-body></text:note></text:p>"
    "<text:p text:style-name=\"P1\">All bold <text:span text:style-name=\"T4\">except this</text:span></text:p>"
    "<text:p>Kept <text:change-start text:change-id=\"c2\"/>inserted<text:change-end text:change-id=\"c2\"/> text<text:change text:change-id=\"c1\"/>.</text:p>"
    "<text:list text:style-name=\"L1\"><text:list-item><text:p>One</text:p><text:list><text:list-item><text:p>Nested bullet</text:p></text:list-item></text:list></text:list-item>"
    "<text:list-item><text:p>Two</text:p><text:p>second line</text:p></text:list-item></text:list>"
    "<text:h text:outline-level=\"2\"><text:span text:style-name=\"T3\">Data</text:span></text:h>"
    "<table:table table:name=\"T\"><table:table-column table:number-columns-repeated=\"1024\"/>"
    "<table:table-header-rows><table:table-row><table:table-cell office:value-type=\"string\"><text:p>H1</text:p></table:table-cell>"
    "<table:table-cell><text:p>H2</text:p></table:table-cell><table:table-cell><text:p>H3</text:p></table:table-cell><table:table-cell table:number-columns-repeated=\"1021\"/></table:table-row></table:table-header-rows>"
    "<table:table-row table:number-rows-repeated=\"2\"><table:table-cell table:number-columns-repeated=\"2\"><text:p>x</text:p></table:table-cell>"
    "<table:covered-table-cell/><table:table-cell table:number-columns-repeated=\"1021\"/></table:table-row>"
    "<table:table-row><table:table-cell table:number-columns-repeated=\"1024\"/></table:table-row>"
    "<table:table-row><table:table-cell table:number-columns-repeated=\"2\"/><table:table-cell><text:p>z</text:p></table:table-cell></table:table-row>"
    "<table:table-row table:number-rows-repeated=\"1048000\"><table:table-cell table:number-columns-repeated=\"1024\"/></table:table-row></table:table>"
    "<text:list text:style-name=\"Numbering_20_123\"><text:list-item><text:p>Named style item</text:p></text:list-item></text:list>"
    "<text:p><draw:frame draw:name=\"img\"><draw:image xlink:href=\"Pictures/a.png\"/></draw:frame><office:annotation><text:p>comment</text:p></office:annotation></text:p>"
    "<text:section text:name=\"S\"><text:p>In a section<text:bookmark text:name=\"b\"/> with a <text:date>2026-11-04</text:date> field</text:p></text:section>";
std::string ReferenceOdt() {return Zip(OdfMembers(odtType,OdfContent(odtAutomatic,"text",odtBody),odtStyles));}
const std::string odtText=
    "Report title\n\nLead paragraph with   gaps,\ttab and\nbreak.\n\n"
    "Styled bold and italic and a link (https://example.org/x) and internal.\n\nAll bold except this\n\nKept inserted text.\n\n"
    "1. One\n  - Nested bullet\n2. Two\n   second line\n\nData\n\nH1\tH2\tH3\nx\tx\t\nx\tx\t\n\t\t\n\t\tz\n\n"
    "1. Named style item\n\nIn a section with a 2026-11-04 field\n";
const std::string odtMarkdown=
    "# Report title\n\nLead paragraph with   gaps,\ttab and\\\nbreak.\n\n"
    "Styled **bold** and *italic* and [a link](https://example.org/x) and internal.\n\n**All bold** except this\n\nKept inserted text.\n\n"
    "1. One\n   - Nested bullet\n2. Two\\\n   second line\n\n## ***Data***\n\n| H1 | H2 | H3 |\n| --- | --- | --- |\n| x | x |  |\n| x | x |  |\n|  |  |  |\n|  |  | z |\n\n"
    "1. Named style item\n\nIn a section with a 2026-11-04 field\n";

void OdtTests() {
    const auto bytes=ReferenceOdt();
    ExpectConvert(TextDocumentFormat::odt,bytes,odtText,odtMarkdown,"ODT reference");
    const auto result=Convert(TextDocumentFormat::odt,OutputFormat::text,bytes);
    const auto& x=result.report.excluded;
    Expect(x.notes==1&&x.annotations==1&&x.deletions==1&&x.images==1,"ODT exclusions reported");

    // Without styles.xml, the named list style is unknown: a bullet, as documented.
    ExpectText(Convert(TextDocumentFormat::odt,OutputFormat::text,Zip(OdfMembers(odtType,OdfContent(odtAutomatic,"text",
        "<text:list text:style-name=\"Numbering_20_123\"><text:list-item><text:p>Named style item</text:p></text:list-item></text:list>")))),
        "- Named style item\n","ODT unknown list style is a bullet");
    // A template is accepted; whitespace-only and empty documents are empty successes.
    ExpectText(Convert(TextDocumentFormat::odt,OutputFormat::text,Zip(OdfMembers("application/vnd.oasis.opendocument.text-template",
        OdfContent("","text","<text:p>T</text:p>")))),"T\n","ODT template");
    ExpectText(Convert(TextDocumentFormat::odt,OutputFormat::text,Zip(OdfMembers(odtType,OdfContent("","text","<text:p>  <text:s/> </text:p>")))),
        "","ODT whitespace-only paragraph");

    ExpectError(TextDocumentFormat::odt,Zip(OdfMembers("application/vnd.oasis.opendocument.spreadsheet",OdfContent("","spreadsheet",""))),
        TextDocumentError::odf_wrong_kind,"ODS given as ODT");
    ExpectError(TextDocumentFormat::odt,Zip(OdfMembers(odpType,OdfContent("","presentation",""))),
        TextDocumentError::odf_wrong_kind,"ODP given as ODT");
    ExpectError(TextDocumentFormat::odt,Zip(OdfMembers("application/zip",OdfContent("","text",""))),
        TextDocumentError::odf_not_opendocument,"ODT with a foreign mimetype");
    auto members=OdfMembers(odtType,OdfContent("","text","<text:p>x</text:p>"));
    members.erase(members.begin());
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_not_opendocument,"ODT without mimetype");
    members=OdfMembers(odtType,OdfContent("","text","<text:p>x</text:p>"));
    members.erase(members.begin()+2);
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_missing_part,"ODT without content.xml");
    members=OdfMembers(odtType,OdfContent("","text","<text:p>x</text:p>"));
    members.erase(members.begin()+1);
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_missing_part,"ODT without manifest");
    members=OdfMembers(odtType,"<x/>");
    members[1].second=Manifest(odtType,"<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\">"
        "<manifest:encryption-data manifest:checksum-type=\"SHA1/1K\" manifest:checksum=\"AA==\"><manifest:algorithm manifest:algorithm-name=\"Blowfish CFB\" manifest:initialisation-vector=\"AA==\"/>"
        "</manifest:encryption-data></manifest:file-entry>");
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_encrypted,"ODT password-protected");
    members=OdfMembers(odtType,OdfContent("","text","<text:p>x</text:p>"));
    members[1].second=Manifest(odpType);
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_invalid_package,"ODT manifest media type mismatch");
    members=OdfMembers(odtType,"<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"/>");
    ExpectError(TextDocumentFormat::odt,Zip(members),TextDocumentError::odf_invalid_package,"ODT without office:body");
    ExpectError(TextDocumentFormat::odt,Zip(OdfMembers(odtType,OdfContent("","text","<text:p>x</text:q>"))),
        TextDocumentError::xml_malformed,"ODT malformed content");
    ExpectError(TextDocumentFormat::odt,Pptx(ReferencePptx()),
        TextDocumentError::odf_not_opendocument,"PPTX given as ODT");
}

std::string OdpFrame(std::string_view cls,std::string_view inner) {
    return "<draw:frame presentation:class=\""+std::string(cls)+"\" svg:x=\"0cm\"><draw:text-box>"+std::string(inner)+"</draw:text-box></draw:frame>";
}
const std::string odpBody=
    "<draw:page draw:name=\"p1\" draw:master-page-name=\"Default\">"
    +OdpFrame("outline","<text:list text:style-name=\"L1\"><text:list-item><text:p>Alpha</text:p></text:list-item>"
        "<text:list-item><text:p>Beta</text:p><text:list><text:list-item><text:p>Beta one</text:p></text:list-item></text:list></text:list-item></text:list>")
    +OdpFrame("title","<text:p>Welcome<text:line-break/>everyone</text:p>")
    +"<draw:frame presentation:class=\"page-number\"><draw:text-box><text:p>3</text:p></draw:text-box></draw:frame>"
    "<draw:frame draw:name=\"pic\"><draw:image xlink:href=\"Pictures/p.png\"/></draw:frame>"
    "<draw:frame draw:name=\"chart\"><draw:object xlink:href=\"./Object 1\"/></draw:frame>"
    "<draw:custom-shape><draw:enhanced-geometry svg:viewBox=\"0 0 1 1\"/><text:p>Shape text</text:p></draw:custom-shape>"
    "<presentation:notes><draw:page-thumbnail draw:page-number=\"1\"/>"+OdpFrame("notes","<text:p>Speaker hint</text:p>")+"</presentation:notes>"
    "</draw:page>"
    "<draw:page draw:name=\"p2\"><draw:frame><table:table><table:table-row><table:table-cell><text:p>A</text:p></table:table-cell>"
    "<table:table-cell><text:p>B</text:p></table:table-cell></table:table-row></table:table></draw:frame>"
    "<draw:g>"+OdpFrame("subtitle","<text:p>Grouped</text:p>")+"</draw:g></draw:page>"
    "<presentation:settings/>";
std::string ReferenceOdp() {return Zip(OdfMembers(odpType,OdfContent(odtAutomatic,"presentation",odpBody)));}
const std::string odpText="Slide 1\n\nWelcome everyone\n\n1. Alpha\n2. Beta\n  - Beta one\n\nShape text\n\nSlide 2\n\nA\tB\n\nGrouped\n";
const std::string odpMarkdown="# Slide 1\n\n## Welcome everyone\n\n1. Alpha\n2. Beta\n   - Beta one\n\nShape text\n\n# Slide 2\n\n| A | B |\n| --- | --- |\n\nGrouped\n";

void OdpTests() {
    const auto bytes=ReferenceOdp();
    ExpectConvert(TextDocumentFormat::odp,bytes,odpText,odpMarkdown,"ODP reference");
    const auto result=Convert(TextDocumentFormat::odp,OutputFormat::text,bytes);
    Expect(result.report.slides==2,"ODP page count");
    Expect(result.report.excluded.images==1&&result.report.excluded.graphics==1,"ODP image and object excluded");
    Expect(result.report.excluded.placeholders==1&&result.report.excluded.notes==1,"ODP page number and notes excluded");
    doc::TextDocumentOptions notes;
    notes.includeNotes=true;
    ExpectText(Convert(TextDocumentFormat::odp,OutputFormat::text,bytes,notes),
        "Slide 1\n\nWelcome everyone\n\n1. Alpha\n2. Beta\n  - Beta one\n\nShape text\n\nNotes: Speaker hint\n\nSlide 2\n\nA\tB\n\nGrouped\n","ODP notes option");
    ExpectError(TextDocumentFormat::odp,ReferenceOdt(),TextDocumentError::odf_wrong_kind,"ODT given as ODP");
    doc::TextDocumentOptions one;
    one.maximumItems=1;
    ExpectError(TextDocumentFormat::odp,bytes,TextDocumentError::item_limit,"ODP page limit",one);
}

// ---- EPUB authoring ----
std::string Container(std::string_view opf="OEBPS/content.opf") {
    return xmlDecl+"<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
        "<rootfile full-path=\""+std::string(opf)+"\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
}
std::string Opf(std::string_view version,std::string_view manifest,std::string_view spine,std::string_view spineAttributes="") {
    return xmlDecl+"<package xmlns=\"http://www.idpf.org/2007/opf\" version=\""+std::string(version)+"\" unique-identifier=\"id\">"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"id\">urn:uuid:1</dc:identifier><dc:title>T</dc:title></metadata>"
        "<manifest>"+std::string(manifest)+"</manifest><spine"+std::string(spineAttributes)+">"+std::string(spine)+"</spine></package>";
}
std::string Xhtml(std::string_view body,std::string_view doctype="<!DOCTYPE html>") {
    return xmlDecl+std::string(doctype)+"<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
        "<head><title>t</title><link rel=\"stylesheet\" href=\"../Styles/s.css\"/><style>p{}</style></head><body>"+std::string(body)+"</body></html>";
}
Members EpubMembers(const std::string& opf,Members files) {
    Members members{{"mimetype","application/epub+zip"},{"META-INF/container.xml",Container()},{"OEBPS/content.opf",opf}};
    for(auto& f:files)members.push_back(std::move(f));
    return members;
}
const std::string epub3Manifest=
    "<item id=\"c2\" href=\"Text/ch%202.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
    "<item id=\"c1\" href=\"Text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"cover\" href=\"Text/cover.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"svg\" href=\"Images/plate.svg\" media-type=\"image/svg+xml\"/>"
    "<item id=\"css\" href=\"Styles/s.css\" media-type=\"text/css\"/>"
    "<item id=\"img\" href=\"Images/i.png\" media-type=\"image/png\"/>";
const std::string epub3Spine=
    "<itemref idref=\"cover\" linear=\"no\"/><itemref idref=\"nav\"/><itemref idref=\"c1\"/><itemref idref=\"svg\"/><itemref idref=\"c2\"/>";
const std::string chapterOne=Xhtml(
    "<section epub:type=\"chapter\"><h1>Beginning</h1>"
    "<p>Some  <em>emphasis</em>\n and <strong>strength</strong>.<br/>New line.</p>"
    "<blockquote><p>Quoted text</p></blockquote>"
    "<ul><li>Item A<ul><li>Inner <b>bold</b></li></ul></li><li><p>Para one</p><p>Para two</p></li></ul>"
    "<ol><li>First</li><li>Second</li></ol>"
    "<table><caption>Data</caption><thead><tr><th>K</th><th>V</th></tr></thead>"
    "<tbody><tr><td>a</td><td><table><tr><td>n1</td><td>n2</td></tr></table></td></tr></tbody></table>"
    "<p>Links: <a href=\"https://example.net/\">web</a>, <a href=\"../Text/ch%202.xhtml#s\">chapter two</a>, "
    "<a href=\"mailto:a@b.c\">mail</a>, <a href=\"javascript:alert(1)\">bad</a>.</p>"
    "<pre>\n  code line 1\n    indented</pre>"
    "<img src=\"../Images/i.png\" alt=\"pic\"/><svg xmlns=\"http://www.w3.org/2000/svg\"><text>svg text</text></svg>"
    "<script>var x=1;</script><nav epub:type=\"landmarks\"><p>in-nav</p></nav>"
    "<div>Div <span>text</span><p hidden=\"hidden\">hidden</p></div><hr/><p>After rule</p></section>");
const std::string chapterTwo=Xhtml("<p id=\"s\">Second chapter body.</p>");
const std::string navDocument=Xhtml(
    "<nav epub:type=\"toc\"><h1>Contents</h1><ol><li><a href=\"Text/ch1.xhtml\">Chapter One</a></li>"
    "<li><a href=\"Text/ch%202.xhtml#start\">The  Second\nChapter</a></li></ol></nav>");
Members Epub3Members() {
    return EpubMembers(Opf("3.0",epub3Manifest,epub3Spine),{
        {"OEBPS/Text/ch 2.xhtml",chapterTwo},{"OEBPS/nav.xhtml",navDocument},{"OEBPS/Text/ch1.xhtml",chapterOne},
        {"OEBPS/Text/cover.xhtml",Xhtml("<p>Cover</p>")},{"OEBPS/Images/plate.svg","<svg xmlns=\"http://www.w3.org/2000/svg\"/>"},
        {"OEBPS/Styles/s.css","p{}"},{"OEBPS/Images/i.png","\x89PNG"}});
}
const std::string epubText=
    "Beginning\n\nSome emphasis and strength.\nNew line.\n\nQuoted text\n\n"
    "- Item A\n  - Inner bold\n- Para one\n  Para two\n1. First\n2. Second\n\n"
    "Data\n\nK\tV\na\tn1 n2\n\n"
    "Links: web (https://example.net/), chapter two, mail (mailto:a@b.c), bad.\n\n"
    "  code line 1\n    indented\n\nDiv text\n\n\nAfter rule\n\n"
    "The Second Chapter\n\nSecond chapter body.\n";
const std::string epubMarkdown=
    "# Beginning\n\nSome *emphasis* and **strength**.\\\nNew line.\n\n> Quoted text\n\n"
    "- Item A\n  - Inner **bold**\n- Para one\\\n  Para two\n1. First\n2. Second\n\n"
    "Data\n\n| K | V |\n| --- | --- |\n| a | n1 n2 |\n\n"
    "Links: [web](https://example.net/), chapter two, [mail](mailto:a@b.c), bad.\n\n"
    "```\n  code line 1\n    indented\n```\n\nDiv text\n\n---\n\nAfter rule\n\n"
    "# The Second Chapter\n\nSecond chapter body.\n";

const std::string xhtml11="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">";
Members Epub2Members() {
    const auto ncx=xmlDecl+"<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head/><docTitle><text>T</text></docTitle><navMap>"
        "<navPoint id=\"n1\" playOrder=\"1\"><navLabel><text>Opening</text></navLabel><content src=\"a.html\"/></navPoint>"
        "<navPoint id=\"n2\" playOrder=\"2\"><navLabel><text>Middle part</text></navLabel><content src=\"b.html#top\"/>"
        "<navPoint id=\"n3\" playOrder=\"3\"><navLabel><text>Inner</text></navLabel><content src=\"b.html#inner\"/></navPoint></navPoint></navMap></ncx>";
    return EpubMembers(Opf("2.0",
        "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
        "<item id=\"b\" href=\"b.html\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"a\" href=\"a.html\" media-type=\"application/xhtml+xml\"/>",
        "<itemref idref=\"a\"/><itemref idref=\"b\"/>"," toc=\"ncx\""),{
        {"OEBPS/toc.ncx",ncx},
        {"OEBPS/a.html",Xhtml("<h2>Start&nbsp;here</h2><p>Dash &mdash; &copy; &#169; &amp;</p>",xhtml11)},
        {"OEBPS/b.html",Xhtml("<div><p>No heading here.</p></div>",xhtml11)}});
}

void EpubTests() {
    const auto bytes=Zip(Epub3Members());
    ExpectConvert(TextDocumentFormat::epub,bytes,epubText,epubMarkdown,"EPUB 3 reference");
    const auto result=Convert(TextDocumentFormat::epub,OutputFormat::text,bytes);
    const auto& r=result.report;
    Expect(r.chapters==2&&r.tocHeadings==1,"EPUB chapters and TOC heading");
    Expect(r.excluded.nonLinear==1&&r.excluded.navigation==2&&r.excluded.unsupportedItems==1,"EPUB non-linear, nav and SVG spine items excluded");
    Expect(r.excluded.images==2&&r.excluded.scripts==1,"EPUB img/svg and script/style excluded");

    ExpectConvert(TextDocumentFormat::epub,Zip(Epub2Members()),
        "Start\u00a0here\n\nDash \u2014 \u00a9 \u00a9 &\n\nMiddle part\n\nNo heading here.\n",
        "## Start\u00a0here\n\nDash \u2014 \u00a9 \u00a9 \\&\n\n# Middle part\n\nNo heading here.\n","EPUB 2 with NCX and XHTML 1.1 entities");

    // Block quotes, thematic breaks and code blocks (r2 model): multi-paragraph quotes stay
    // one quotation, a heading inside a quote stays a heading, the code language comes from
    // a language- class, and inside a list item each degrades to text of that item.
    {
        auto quoted=Epub3Members();
        for(auto& [name,data]:quoted)if(name=="OEBPS/Text/ch1.xhtml")data=Xhtml(
            "<h1>Quotes</h1><hr/><blockquote><p>One <em>a</em></p><p>Two<br/>three</p><h2>Inner</h2></blockquote>"
            "<pre class=\"x\"><code class=\"language-python other\">print(\"`x`\")\n  pass\n</code></pre>"
            "<pre class=\"lang-sh\">ls</pre><hr/><hr/><ul><li>Item<pre>a\n b</pre><hr/><blockquote><p>q</p></blockquote></li></ul><hr/>");
        const auto quotedBytes=Zip(quoted);
        ExpectConvert(TextDocumentFormat::epub,quotedBytes,
            "Quotes\n\n\nOne a\n\nTwo\nthree\n\nInner\n\nprint(\"`x`\")\n  pass\n\nls\n\n\n- Item\n  a\n   b\n  q\n\n\n"
            "The Second Chapter\n\nSecond chapter body.\n",
            "# Quotes\n\n---\n\n> One *a*\n>\n> Two\\\n> three\n\n## Inner\n\n```python\nprint(\"`x`\")\n  pass\n```\n\n```sh\nls\n```\n\n---\n\n"
            "- Item\\\n  a\\\n  b\\\n  q\n\n---\n\n# The Second Chapter\n\nSecond chapter body.\n","EPUB quotes, breaks and code");
        const auto extracted=doc::ExtractTextDocument(TextDocumentFormat::epub,std::as_bytes(std::span<const char>(quotedBytes.data(),quotedBytes.size())));
        std::size_t quotes=0,breaks=0,code=0;
        for(const auto& block:extracted.document.blocks) {
            quotes+=block.kind==doc::BlockKind::block_quote;breaks+=block.kind==doc::BlockKind::thematic_break;code+=block.kind==doc::BlockKind::code_block;
        }
        Expect(extracted&&quotes==2&&breaks==4&&code==2,"EPUB model holds quotes, breaks and code blocks");
    }

    // Without a navigation document or NCX, chapters are only separated by the usual blank line.
    auto members=Epub2Members();
    members[2].second=Replace(members[2].second," toc=\"ncx\"","");
    ExpectText(Convert(TextDocumentFormat::epub,OutputFormat::text,Zip(members)),
        "Start\u00a0here\n\nDash \u2014 \u00a9 \u00a9 &\n\nNo heading here.\n","EPUB without table of contents");

    // Font obfuscation is not DRM.
    members=Epub3Members();
    members.emplace_back("META-INF/encryption.xml",xmlDecl+"<encryption xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" xmlns:enc=\"http://www.w3.org/2001/04/xmlenc#\">"
        "<enc:EncryptedData><enc:EncryptionMethod Algorithm=\"http://www.idpf.org/2008/embedding\"/><enc:CipherData><enc:CipherReference URI=\"OEBPS/Fonts/f.otf\"/></enc:CipherData></enc:EncryptedData></encryption>");
    ExpectText(Convert(TextDocumentFormat::epub,OutputFormat::text,Zip(members)),epubText,"EPUB font obfuscation accepted");
    members.back().second=Replace(members.back().second,"http://www.idpf.org/2008/embedding","http://www.w3.org/2001/04/xmlenc#aes128-cbc");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_drm,"EPUB encrypted content (DRM)");
    members=Epub3Members();
    members.emplace_back("META-INF/rights.xml","<rights/>");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_drm,"EPUB rights.xml (Adobe DRM)");

    members=Epub3Members();
    members[0].second="application/zip";
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_not_epub,"EPUB wrong mimetype");
    members=Epub3Members();
    members.erase(members.begin()+1);
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_invalid_package,"EPUB without container.xml");
    members=Epub3Members();
    members[1].second=Container("OEBPS/missing.opf");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_missing_part,"EPUB without package document");
    members=Epub3Members();
    members[1].second=Container("../escape.opf");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_invalid_package,"EPUB package path escaping the root");
    members=Epub3Members();
    members[2].second=Replace(members[2].second,"idref=\"c2\"","idref=\"nope\"");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_invalid_package,"EPUB spine idref without manifest item");
    members=Epub3Members();
    members[2].second=Replace(members[2].second,"version=\"3.0\"","version=\"1.0\"");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_invalid_package,"EPUB unknown package version");
    members=Epub3Members();
    std::erase_if(members,[](const auto& m) {return m.first=="OEBPS/Text/ch1.xhtml";});
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_missing_part,"EPUB missing spine item");
    members=Epub3Members();
    for(auto& [name,data]:members)if(name=="OEBPS/Text/ch1.xhtml")data=Xhtml("<p>Unclosed<br></p>");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_content_not_xhtml,"EPUB HTML that is not XML");
    members=Epub3Members();
    members[2].second=Replace(members[2].second,"href=\"Text/ch1.xhtml\" media-type=\"application/xhtml+xml\"","href=\"Text/ch1.xhtml\" media-type=\"text/html\"");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_content_not_xhtml,"EPUB text/html spine item");
    members=Epub3Members();
    for(auto& [name,data]:members)if(name=="OEBPS/Text/ch1.xhtml")data=Xhtml("<p>&nbsp;</p>");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::xml_entity,"EPUB HTML entity without an XHTML DTD");
    members=Epub3Members();
    for(auto& [name,data]:members)if(name=="OEBPS/Text/ch1.xhtml")data=Xhtml("<p>x</p>","<!DOCTYPE html [<!ENTITY e \"boom\">]>");
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::xml_doctype,"EPUB DOCTYPE internal subset");
    members=Epub3Members();
    for(auto& [name,data]:members)if(name=="OEBPS/Text/ch1.xhtml")data=xmlDecl+"<html xmlns=\"urn:other\"><body><p>x</p></body></html>";
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::epub_content_not_xhtml,"EPUB item that is not XHTML");
    ExpectError(TextDocumentFormat::epub,ReferenceOdt(),TextDocumentError::epub_not_epub,"ODT given as EPUB");
    doc::TextDocumentOptions two;
    two.maximumItems=4;
    ExpectError(TextDocumentFormat::epub,bytes,TextDocumentError::item_limit,"EPUB spine item limit",two);
}

// ---- Shared error, limit and robustness checks ----
void ErrorTests() {
    for(std::uint32_t e=0;e<=std::uint32_t(TextDocumentError::epub_content_not_xhtml);++e) {
        const auto error=TextDocumentError(e);
        Expect(doc::ErrorCode(error)!="unknown"&&doc::ErrorMessage(error).find("Unknown")==std::string_view::npos,"error text "+std::to_string(e));
        Expect(doc::ErrorMessage(error).find("DOCX")==std::string_view::npos,"format-neutral message "+std::to_string(e));
    }
    Expect(doc::FormatName(TextDocumentFormat::odp)=="odp","format name");
    ExpectError(TextDocumentFormat::pptx,"not a zip at all, just some bytes that are long enough",TextDocumentError::zip_not_archive,"not a ZIP");
    ExpectError(TextDocumentFormat(9),ReferenceOdt(),TextDocumentError::invalid_options,"unknown format");
    const auto bad=doc::ConvertTextDocument(TextDocumentFormat::odt,OutputFormat(7),Span(ReferenceOdt()));
    Expect(bad.error==TextDocumentError::invalid_options,"unknown output format");
    auto corrupt=ReferenceOdt();
    corrupt[corrupt.find("Report")]^=0x20;
    ExpectError(TextDocumentFormat::odt,corrupt,TextDocumentError::zip_crc_mismatch,"CRC mismatch");
    // Failures never carry a partial document or report.
    const auto failed=doc::ExtractTextDocument(TextDocumentFormat::odt,Span(corrupt));
    Expect(!failed&&failed.document.blocks.empty()&&failed.report.slides==0&&failed.report.excluded.deletions==0,"failure carries nothing");
    const auto extracted=doc::ExtractTextDocument(TextDocumentFormat::pptx,Span(Pptx(ReferencePptx())));
    Expect(extracted&&extracted.report.blocks==extracted.document.blocks.size()&&extracted.document.blocks.size()==12,"extract result and block count");
}

void LimitTests() {
    const auto pptx=Pptx(ReferencePptx());
    const auto odt=ReferenceOdt();
    const auto epub=Zip(Epub3Members());
    doc::TextDocumentOptions o;
    o.maximumItems=0;
    ExpectError(TextDocumentFormat::odt,odt,TextDocumentError::invalid_options,"zero item limit",o);
    o={};o.extract.maximumSourceBytes=100;
    ExpectError(TextDocumentFormat::epub,epub,TextDocumentError::source_limit,"source limit",o);
    o={};o.extract.maximumTextBytes=20;
    ExpectError(TextDocumentFormat::pptx,pptx,TextDocumentError::text_limit,"text limit",o);
    o={};o.extract.maximumBlocks=3;
    ExpectError(TextDocumentFormat::epub,epub,TextDocumentError::text_limit,"block limit",o);
    o={};o.serialize.maximumOutputBytes=40;
    ExpectError(TextDocumentFormat::odt,odt,TextDocumentError::output_limit,"output limit",o);
    o={};o.extract.zip.maximumTotalExpandedBytes=2000;
    ExpectError(TextDocumentFormat::odt,odt,TextDocumentError::zip_size_limit,"total expansion limit",o);
    o={};o.extract.zip.maximumEntries=3;
    ExpectError(TextDocumentFormat::epub,epub,TextDocumentError::zip_entry_limit,"ZIP entry limit",o);
    o={};o.maximumItems=1;
    ExpectError(TextDocumentFormat::pptx,pptx,TextDocumentError::item_limit,"slide limit",o);
    o={};o.extract.xml.maximumDepth=6;
    ExpectError(TextDocumentFormat::odt,odt,TextDocumentError::xml_limit,"XML depth limit",o);
    // The reference ODT charges 16 cells (15 materialized plus one per interior empty row); one less fails.
    o={};o.maximumTableCells=16;
    ExpectText(Convert(TextDocumentFormat::odt,OutputFormat::text,odt,o),odtText,"cell budget met exactly");
    o.maximumTableCells=15;
    ExpectError(TextDocumentFormat::odt,odt,TextDocumentError::table_limit,"cell budget exceeded",o);
    const auto bomb=Zip(OdfMembers(odtType,OdfContent("","text",
        "<table:table><table:table-row table:number-rows-repeated=\"1073741824\"><table:table-cell table:number-columns-repeated=\"1073741824\"><text:p>x</text:p></table:table-cell></table:table-row></table:table>")));
    ExpectError(TextDocumentFormat::odt,bomb,TextDocumentError::table_limit,"repeat bomb");
    const auto emptyRows=Zip(OdfMembers(odtType,OdfContent("","text",
        "<table:table><table:table-row table:number-rows-repeated=\"1073741824\"><table:table-cell table:number-columns-repeated=\"1073741824\"/></table:table-row>"
        "<table:table-row><table:table-cell><text:p>x</text:p></table:table-cell></table:table-row></table:table>")));
    ExpectError(TextDocumentFormat::odt,emptyRows,TextDocumentError::table_limit,"empty-row repeat bomb before content");
    // Structural nesting is bounded independently of the XML depth limit.
    std::string lists;
    for(int i=0;i<40;++i)lists+="<text:list><text:list-item>";
    lists+="<text:p>deep</text:p>";
    for(int i=0;i<40;++i)lists+="</text:list-item></text:list>";
    const auto deep=Zip(OdfMembers(odtType,OdfContent("","text",lists)));
    o={};o.maximumNesting=30;
    ExpectError(TextDocumentFormat::odt,deep,TextDocumentError::nesting_limit,"ODT nesting limit",o);
    ExpectText(Convert(TextDocumentFormat::odt,OutputFormat::text,deep),"                - deep\n","deep list clamps to level 8");
    std::string divs;
    for(int i=0;i<60;++i)divs+="<div>";
    divs+="x";
    for(int i=0;i<60;++i)divs+="</div>";
    auto members=Epub3Members();
    for(auto& [name,data]:members)if(name=="OEBPS/Text/ch1.xhtml")data=Xhtml(divs);
    o={};o.maximumNesting=50;
    ExpectError(TextDocumentFormat::epub,Zip(members),TextDocumentError::nesting_limit,"EPUB nesting limit",o);
}

void DeterminismTests() {
    const std::vector<std::pair<TextDocumentFormat,std::string>> inputs={
        {TextDocumentFormat::pptx,Pptx(ReferencePptx())},{TextDocumentFormat::odt,ReferenceOdt()},
        {TextDocumentFormat::odp,ReferenceOdp()},{TextDocumentFormat::epub,Zip(Epub3Members())},{TextDocumentFormat::epub,Zip(Epub2Members())}};
    for(const auto& [format,bytes]:inputs)
        for(const auto output:{OutputFormat::text,OutputFormat::markdown}) {
            const auto first=Convert(format,output,bytes);
            for(int i=0;i<3;++i) {
                const auto again=Convert(format,output,bytes);
                Expect(first&&again&&first.text==again.text,"deterministic output");
            }
        }
}

bool ValidUtf8(std::string_view s) {
    for(std::size_t i=0;i<s.size();) {
        const auto b=std::uint8_t(s[i]);
        std::size_t n=b<0x80?1:(b>>5)==6?2:(b>>4)==14?3:(b>>3)==30?4:0;
        if(!n||s.size()-i<n)return false;
        for(std::size_t k=1;k<n;++k)if((std::uint8_t(s[i+k])&0xc0)!=0x80)return false;
        i+=n;
    }
    return true;
}
// Seeded mutations: raw package bytes (mostly caught by CRC) and the main XML
// part re-zipped with a valid CRC (reaching the parsers and walkers). Every
// outcome must be a typed error or valid UTF-8 ending in LF.
long mutationRounds=300;
void MutationTests() {
    std::uint64_t state=0x2545f4914f6cdd1dull;
    const auto next=[&] {state^=state<<13;state^=state>>7;state^=state<<17;return state;};
    struct Seed {TextDocumentFormat format;Members members;std::string part;};
    const std::vector<Seed> seeds={
        {TextDocumentFormat::pptx,PptxMembers(ReferencePptx()),"ppt/slides/slide2.xml"},
        {TextDocumentFormat::odt,OdfMembers(odtType,OdfContent(odtAutomatic,"text",odtBody),odtStyles),"content.xml"},
        {TextDocumentFormat::odp,OdfMembers(odpType,OdfContent(odtAutomatic,"presentation",odpBody)),"content.xml"},
        {TextDocumentFormat::epub,Epub3Members(),"OEBPS/Text/ch1.xhtml"},
        {TextDocumentFormat::epub,Epub3Members(),"OEBPS/content.opf"}};
    std::size_t successes=0,errors=0;
    doc::TextDocumentOptions options;
    for(const auto& seed:seeds) {
        for(long round=0;round<mutationRounds;++round) {
            const bool raw=round%4==0;
            auto members=seed.members;
            std::string* target=nullptr;
            for(auto& [name,data]:members)if(name==seed.part)target=&data;
            std::string packaged;
            std::string& bytes=raw?packaged:*target;
            if(raw)packaged=Zip(members);
            const auto edits=1+next()%4;
            for(std::uint64_t e=0;e<edits&&!bytes.empty();++e) {
                const auto at=std::size_t(next()%bytes.size());
                switch(next()%5) {
                    case 0: bytes[at]=char(next());break;
                    case 1: bytes[at]=char(bytes[at]^(1<<(next()%8)));break;
                    case 2: bytes.erase(at,std::size_t(next()%16));break;
                    case 3: bytes.insert(bytes.begin()+std::ptrdiff_t(at),char(next()));break;
                    default: bytes[at]="<>/&;\"=:x \x80\xff"[next()%13];break;
                }
            }
            if(!raw)packaged=Zip(members);
            options.includeNotes=round%2==1;
            for(const auto output:{OutputFormat::text,OutputFormat::markdown}) {
                const auto result=Convert(seed.format,output,packaged,options);
                if(!result) {++errors;Expect(result.text.empty(),"error carries no text");continue;}
                ++successes;
                Expect(ValidUtf8(result.text),"mutated input produced invalid UTF-8");
                Expect(result.text.empty()||result.text.back()=='\n',"mutated output ends with LF");
            }
        }
    }
    Expect(successes>0&&errors>0,"mutations exercise both outcomes");
}
}

int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version") {
        std::cout<<doc::textDocumentsRevision<<"\n"<<doc::revision<<"\n";
        return 0;
    }
    const std::map<std::string,std::function<void()>> groups={
        {"pptx",PptxTests},{"odt",OdtTests},{"odp",OdpTests},{"epub",EpubTests},{"errors",ErrorTests},
        {"limits",LimitTests},{"determinism",DeterminismTests},{"mutation",MutationTests}};
    if(argc>1) {
        const auto found=groups.find(argv[1]);
        if(found==groups.end()) {std::cerr<<"unknown group "<<argv[1]<<"\n";return 2;}
        if(argc>2)mutationRounds=std::max(1L,std::atol(argv[2]));
        found->second();
    } else {
        for(const auto& [name,run]:groups)run();
    }
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::cout<<"ok\n";
    return 0;
}
