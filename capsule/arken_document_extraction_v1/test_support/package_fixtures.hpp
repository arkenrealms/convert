#pragma once
// Test-only authoring of minimal XLSX, ODS, PPTX, ODT, ODP and EPUB packages for
// consumers of arken_document_extraction_v1 (the conversion owner and the standalone
// MCP proof). Every package is a stored ZIP written by docx_fixtures.hpp; nothing here
// uses the capsule's reader, and nothing here is linked into a product.
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"

namespace package_fixture {
using Members=std::vector<std::pair<std::string,std::string>>;
inline const std::string xmlDecl="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
inline constexpr std::string_view relsNs="http://schemas.openxmlformats.org/package/2006/relationships";
inline constexpr std::string_view relBase="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
inline constexpr std::string_view typesHead=
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
inline std::string Rel(std::string_view id,std::string_view type,std::string_view target,bool external=false) {
    return "<Relationship Id=\""+std::string(id)+"\" Type=\""+std::string(relBase)+"/"+std::string(type)+"\" Target=\""
        +std::string(target)+"\""+(external?" TargetMode=\"External\"":"")+"/>";
}
inline std::string Rels(std::string_view inner) {
    return xmlDecl+"<Relationships xmlns=\""+std::string(relsNs)+"\">"+std::string(inner)+"</Relationships>";
}

// ---- XLSX: one worksheet part per sheet; `data` is the inner XML of <worksheet>. ----
struct Sheet {std::string name,state,data;};
inline std::string Xlsx(const std::vector<Sheet>& sheets,const std::vector<std::string>& sharedStrings={},
                        std::string_view mainType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml") {
    constexpr std::string_view sml="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
    std::string workbook=xmlDecl+"<workbook xmlns=\""+std::string(sml)+"\" xmlns:r=\""+std::string(relBase)+"\"><sheets>";
    std::string rels;
    Members parts;
    for(std::size_t i=0;i<sheets.size();++i) {
        const auto n=std::to_string(i+1);
        workbook+="<sheet name=\""+sheets[i].name+"\" sheetId=\""+n+"\""+(sheets[i].state.empty()?"":" state=\""+sheets[i].state+"\"")+" r:id=\"rId"+n+"\"/>";
        rels+=Rel("rId"+n,"worksheet","worksheets/sheet"+n+".xml");
        parts.emplace_back("xl/worksheets/sheet"+n+".xml",xmlDecl+"<worksheet xmlns=\""+std::string(sml)+"\">"+sheets[i].data+"</worksheet>");
    }
    if(!sharedStrings.empty()) {
        rels+=Rel("rIdS","sharedStrings","sharedStrings.xml");
        std::string sst=xmlDecl+"<sst xmlns=\""+std::string(sml)+"\" count=\""+std::to_string(sharedStrings.size())+"\">";
        for(const auto& s:sharedStrings)sst+="<si><t>"+s+"</t></si>";
        parts.emplace_back("xl/sharedStrings.xml",sst+"</sst>");
    }
    Members members{
        {"[Content_Types].xml",xmlDecl+std::string(typesHead)+"<Override PartName=\"/xl/workbook.xml\" ContentType=\""+std::string(mainType)+"\"/></Types>"},
        {"_rels/.rels",Rels(Rel("rId1","officeDocument","xl/workbook.xml"))},
        {"xl/workbook.xml",workbook+"</sheets></workbook>"},
        {"xl/_rels/workbook.xml.rels",Rels(rels)}};
    for(auto& part:parts)members.push_back(std::move(part));
    return docx_fixture::StoredZip(members);
}

// ---- OpenDocument ----
inline const std::string odfNamespaces=
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
    " xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\"";
inline constexpr std::string_view odtType="application/vnd.oasis.opendocument.text";
inline constexpr std::string_view odpType="application/vnd.oasis.opendocument.presentation";
inline constexpr std::string_view odsType="application/vnd.oasis.opendocument.spreadsheet";
inline std::string OdfManifest(std::string_view mimetype,std::string_view contentEntryInner="") {
    return xmlDecl+"<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\""+std::string(mimetype)+"\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\">"+std::string(contentEntryInner)
        +"</manifest:file-entry></manifest:manifest>";
}
// `kind` is text, presentation or spreadsheet; `body` is the inner XML of office:<kind>.
inline std::string Odf(std::string_view mimetype,std::string_view kind,std::string_view body,std::string_view automaticStyles="") {
    const auto content=xmlDecl+"<office:document-content"+odfNamespaces+" office:version=\"1.3\"><office:automatic-styles>"
        +std::string(automaticStyles)+"</office:automatic-styles><office:body><office:"+std::string(kind)+">"+std::string(body)
        +"</office:"+std::string(kind)+"></office:body></office:document-content>";
    return docx_fixture::StoredZip({{"mimetype",std::string(mimetype)},{"META-INF/manifest.xml",OdfManifest(mimetype)},{"content.xml",content}});
}
inline std::string Odt(std::string_view body,std::string_view styles="") {return Odf(odtType,"text",body,styles);}
inline std::string Odp(std::string_view pages) {return Odf(odpType,"presentation",pages);}
inline std::string Ods(std::string_view tables,std::string_view styles="") {return Odf(odsType,"spreadsheet",tables,styles);}
// A password-protected ODT: the manifest declares encryption data for content.xml.
inline std::string EncryptedOdt() {
    return docx_fixture::StoredZip({{"mimetype",std::string(odtType)},
        {"META-INF/manifest.xml",OdfManifest(odtType,
            "<manifest:encryption-data manifest:checksum-type=\"SHA1/1K\" manifest:checksum=\"AA==\">"
            "<manifest:algorithm manifest:algorithm-name=\"Blowfish CFB\" manifest:initialisation-vector=\"AA==\"/></manifest:encryption-data>")},
        {"content.xml","<x/>"}});
}

// ---- PPTX: `shapes` is the inner XML of p:spTree after the group properties. ----
inline constexpr std::string_view pNs="http://schemas.openxmlformats.org/presentationml/2006/main";
inline constexpr std::string_view aNs="http://schemas.openxmlformats.org/drawingml/2006/main";
inline std::string PptxShape(std::string_view placeholder,std::string_view paragraphs) {
    return "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"s\"/><p:cNvSpPr/><p:nvPr>"+std::string(placeholder)
        +"</p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/>"+std::string(paragraphs)+"</p:txBody></p:sp>";
}
inline std::string PptxPara(std::string_view text,std::string_view properties="") {
    return "<a:p>"+std::string(properties)+"<a:r><a:rPr lang=\"en-US\"/><a:t>"+std::string(text)+"</a:t></a:r></a:p>";
}
struct Slide {std::string shapes,notes;};
inline std::string Pptx(const std::vector<Slide>& slides) {
    const std::string ns=" xmlns:a=\""+std::string(aNs)+"\" xmlns:p=\""+std::string(pNs)+"\" xmlns:r=\""+std::string(relBase)+"\"";
    const auto tree=[](std::string_view shapes) {
        return "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
            +std::string(shapes)+"</p:spTree></p:cSld>";
    };
    std::string types=xmlDecl+std::string(typesHead)
        +"<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>";
    std::string presentationRels,ids;
    Members parts;
    for(std::size_t i=1;i<=slides.size();++i) {
        const auto n=std::to_string(i);
        types+="<Override PartName=\"/ppt/slides/slide"+n+".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
        presentationRels+=Rel("rIdS"+n,"slide","slides/slide"+n+".xml");
        ids+="<p:sldId id=\""+std::to_string(255+i)+"\" r:id=\"rIdS"+n+"\"/>";
        parts.emplace_back("ppt/slides/slide"+n+".xml",xmlDecl+"<p:sld"+ns+">"+tree(slides[i-1].shapes)+"</p:sld>");
        std::string rels;
        if(!slides[i-1].notes.empty()) {
            types+="<Override PartName=\"/ppt/notesSlides/notesSlide"+n+".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml\"/>";
            rels+=Rel("rIdN","notesSlide","../notesSlides/notesSlide"+n+".xml");
            parts.emplace_back("ppt/notesSlides/notesSlide"+n+".xml",xmlDecl+"<p:notes"+ns+">"
                +tree(PptxShape("<p:ph type=\"sldImg\"/>","")+PptxShape("<p:ph type=\"body\" idx=\"1\"/>",slides[i-1].notes))+"</p:notes>");
        }
        parts.emplace_back("ppt/slides/_rels/slide"+n+".xml.rels",Rels(rels));
    }
    Members members{{"[Content_Types].xml",types+"</Types>"},
        {"_rels/.rels",Rels(Rel("rId1","officeDocument","ppt/presentation.xml"))},
        {"ppt/presentation.xml",xmlDecl+"<p:presentation"+ns+"><p:sldIdLst>"+ids+"</p:sldIdLst></p:presentation>"},
        {"ppt/_rels/presentation.xml.rels",Rels(presentationRels)}};
    for(auto& part:parts)members.push_back(std::move(part));
    return docx_fixture::StoredZip(members);
}

// ---- EPUB 3: chapters in spine order, each the inner XML of <body>. ----
inline std::string Xhtml(std::string_view body) {
    return xmlDecl+"<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
        "<head><title>t</title></head><body>"+std::string(body)+"</body></html>";
}
inline Members EpubMembers(const std::vector<std::string>& chapters) {
    std::string manifest,spine;
    Members chapterParts;
    for(std::size_t i=1;i<=chapters.size();++i) {
        const auto n=std::to_string(i);
        manifest+="<item id=\"c"+n+"\" href=\"c"+n+".xhtml\" media-type=\"application/xhtml+xml\"/>";
        spine+="<itemref idref=\"c"+n+"\"/>";
        chapterParts.emplace_back("OEBPS/c"+n+".xhtml",Xhtml(chapters[i-1]));
    }
    Members members{{"mimetype","application/epub+zip"},
        {"META-INF/container.xml",xmlDecl+"<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
            "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>"},
        {"OEBPS/content.opf",xmlDecl+"<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"id\">"
            "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"id\">urn:uuid:1</dc:identifier><dc:title>T</dc:title></metadata>"
            "<manifest>"+manifest+"</manifest><spine>"+spine+"</spine></package>"}};
    for(auto& part:chapterParts)members.push_back(std::move(part));
    return members;
}
inline std::string Epub(const std::vector<std::string>& chapters) {return docx_fixture::StoredZip(EpubMembers(chapters));}
// Adobe-style DRM: META-INF/rights.xml is present.
inline std::string DrmEpub() {
    auto members=EpubMembers({"<p>Locked</p>"});
    members.emplace_back("META-INF/rights.xml","<rights/>");
    return docx_fixture::StoredZip(members);
}
}
