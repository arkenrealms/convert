#include "arken_document_extraction_v1/tabular.hpp"
#include "arken_document_extraction_v1/test_support/docx_fixtures.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace tab=arken::document::v1::tabular;
using tab::CellKind;
using tab::Error;
using tab::TableFormat;

namespace {
int failures=0;
long mutationRounds=1500;

std::string Visible(std::string_view s) {
    std::string out;
    for(const char c:s) {
        if(c=='\n')out+="\\n\n";
        else if(c=='\r')out+="\\r";
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
void ExpectError(Error got,Error want,std::string_view what) {
    if(got==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<": got "<<tab::ErrorCode(got)<<", want "<<tab::ErrorCode(want)<<"\n";
}
void ExpectText(Error error,const std::string& got,std::string_view want,std::string_view what) {
    if(error!=Error::none) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<tab::ErrorCode(error)<<"\n";return;}
    if(got==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n--- got ---\n"<<Visible(got)<<"\n--- want ---\n"<<Visible(want)<<"\n";
}
std::span<const std::byte> Span(const std::string& s) {return std::as_bytes(std::span<const char>(s.data(),s.size()));}
tab::ReadResult Read(TableFormat format,const std::string& bytes,const tab::ReadOptions& options={}) {
    return tab::ReadTable(format,Span(bytes),options);
}
tab::ConvertResult Convert(TableFormat in,TableFormat out,const std::string& bytes,const tab::ConvertOptions& options={}) {
    return tab::ConvertTable(in,out,Span(bytes),options);
}
void ExpectConvert(TableFormat in,TableFormat out,const std::string& bytes,std::string_view want,std::string_view what,
    const tab::ConvertOptions& options={}) {
    const auto r=Convert(in,out,bytes,options);
    ExpectText(r.error,r.text,want,what);
}
Error ReadError(TableFormat format,const std::string& bytes,const tab::ReadOptions& options={}) {
    const auto r=Read(format,bytes,options);
    if(!r)Expect(r.table.rows.empty()&&r.report.sheets.empty(),"failure carries no partial table");
    return r.error;
}
const tab::TableCell& At(const tab::ReadResult& r,std::size_t row,std::size_t column) {
    static const tab::TableCell missing{CellKind::error,"<missing>",{}};
    if(row>=r.table.rows.size()||column>=r.table.rows[row].size())return missing;
    return r.table.rows[row][column];
}
void ExpectCell(const tab::ReadResult& r,std::size_t row,std::size_t column,CellKind kind,std::string_view value,
    std::string_view formatted,std::string_view what) {
    const auto& c=At(r,row,column);
    if(c.kind==kind&&c.value==value&&c.formatted==formatted)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<": cell("<<row<<","<<column<<") kind "<<int(c.kind)<<" value '"<<Visible(c.value)
             <<"' formatted '"<<Visible(c.formatted)<<"'\n";
}

// ---- ZIP authoring: stored members, or deflate with stored blocks (test-only) ----
struct Member {std::string name,data;bool deflate=false;};
std::string DeflateStored(std::string_view data) {
    std::string out;
    std::size_t p=0;
    do {
        const auto n=std::min<std::size_t>(65535,data.size()-p);
        out.push_back(char(p+n==data.size()?1:0));
        docx_fixture::Le16(out,std::uint32_t(n));docx_fixture::Le16(out,std::uint32_t(~n&0xffff));
        out.append(data.substr(p,n));
        p+=n;
    } while(p<data.size());
    return out;
}
std::string Zip(const std::vector<Member>& members) {
    using docx_fixture::Le16;using docx_fixture::Le32;
    std::string out,central;
    for(const auto& m:members) {
        const auto payload=m.deflate?DeflateStored(m.data):m.data;
        const auto crc=docx_fixture::Crc32(m.data);
        const auto offset=std::uint32_t(out.size());
        const std::uint32_t method=m.deflate?8:0;
        Le32(out,0x04034b50);Le16(out,20);Le16(out,0);Le16(out,method);Le16(out,0);Le16(out,0x21);
        Le32(out,crc);Le32(out,std::uint32_t(payload.size()));Le32(out,std::uint32_t(m.data.size()));
        Le16(out,std::uint32_t(m.name.size()));Le16(out,0);
        out+=m.name;out+=payload;
        Le32(central,0x02014b50);Le16(central,20);Le16(central,20);Le16(central,0);Le16(central,method);
        Le16(central,0);Le16(central,0x21);Le32(central,crc);Le32(central,std::uint32_t(payload.size()));
        Le32(central,std::uint32_t(m.data.size()));
        Le16(central,std::uint32_t(m.name.size()));Le16(central,0);Le16(central,0);Le16(central,0);Le16(central,0);
        Le32(central,0);Le32(central,offset);central+=m.name;
    }
    const auto centralOffset=std::uint32_t(out.size());
    out+=central;
    Le32(out,0x06054b50);Le16(out,0);Le16(out,0);
    Le16(out,std::uint32_t(members.size()));Le16(out,std::uint32_t(members.size()));
    Le32(out,std::uint32_t(central.size()));Le32(out,centralOffset);Le16(out,0);
    return out;
}

// ---- XLSX authoring ----
constexpr std::string_view sml="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr std::string_view relBase="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
struct Sheet {std::string name,state,content;std::string relType="worksheet";};
struct Xlsx {
    std::vector<Sheet> sheets;
    std::vector<std::string> strings; // <si> inner XML each.
    std::string styles;               // Inner XML of styleSheet.
    bool date1904=false;
    std::string mainType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
    bool deflate=false,includeContentTypes=true,includeSheets=true;
    std::vector<Member> extra;
};
std::string SheetXml(std::string_view inner) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\""+std::string(sml)+"\">"+std::string(inner)+"</worksheet>";
}
std::vector<Member> XlsxMembers(const Xlsx& x) {
    std::vector<Member> m;
    const auto add=[&](std::string name,std::string data) {m.push_back({std::move(name),std::move(data),x.deflate});};
    if(x.includeContentTypes)
        add("[Content_Types].xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/xl/workbook.xml\" ContentType=\""+x.mainType+"\"/></Types>");
    add("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\""+std::string(relBase)+"/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");
    std::string workbook="<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook xmlns=\""+std::string(sml)+"\" xmlns:r=\""+std::string(relBase)+"\">";
    if(x.date1904)workbook+="<workbookPr date1904=\"1\"/>";
    workbook+="<sheets>";
    std::string rels="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for(std::size_t i=0;i<x.sheets.size();++i) {
        const auto& s=x.sheets[i];
        const auto id="rId"+std::to_string(i+1);
        workbook+="<sheet name=\""+s.name+"\" sheetId=\""+std::to_string(i+1)+"\""+(s.state.empty()?"":" state=\""+s.state+"\"")+" r:id=\""+id+"\"/>";
        const auto part=(s.relType=="worksheet"?"worksheets/sheet":"chartsheets/sheet")+std::to_string(i+1)+".xml";
        rels+="<Relationship Id=\""+id+"\" Type=\""+std::string(relBase)+"/"+s.relType+"\" Target=\""+part+"\"/>";
        if(x.includeSheets&&s.relType=="worksheet")add("xl/"+part,SheetXml(s.content));
    }
    workbook+="</sheets></workbook>";
    if(!x.strings.empty()) {
        rels+="<Relationship Id=\"rIdS\" Type=\""+std::string(relBase)+"/sharedStrings\" Target=\"sharedStrings.xml\"/>";
        std::string sst="<sst xmlns=\""+std::string(sml)+"\" count=\""+std::to_string(x.strings.size())+"\">";
        for(const auto& si:x.strings)sst+="<si>"+si+"</si>";
        add("xl/sharedStrings.xml",sst+"</sst>");
    }
    if(!x.styles.empty()) {
        rels+="<Relationship Id=\"rIdT\" Type=\""+std::string(relBase)+"/styles\" Target=\"/xl/styles.xml\"/>";
        add("xl/styles.xml","<styleSheet xmlns=\""+std::string(sml)+"\">"+x.styles+"</styleSheet>");
    }
    add("xl/workbook.xml",workbook);
    add("xl/_rels/workbook.xml.rels",rels+"</Relationships>");
    for(const auto& e:x.extra)m.push_back(e);
    return m;
}
std::string Build(const Xlsx& x) {return Zip(XlsxMembers(x));}
std::string OneSheet(std::string_view content) {
    Xlsx x;
    x.sheets={{"S","",std::string(content)}};
    return Build(x);
}

const std::string referenceStyles=
    "<numFmts count=\"5\"><numFmt numFmtId=\"164\" formatCode=\"yyyy-mm-dd hh:mm\"/><numFmt numFmtId=\"165\" formatCode=\"0.00E+00\"/>"
    "<numFmt numFmtId=\"166\" formatCode=\"[Red]#,##0;[Blue]General\"/><numFmt numFmtId=\"167\" formatCode=\"&quot;Day&quot; dd\"/>"
    "<numFmt numFmtId=\"168\" formatCode=\"[h]:mm:ss\"/></numFmts>"
    "<cellXfs count=\"8\"><xf numFmtId=\"0\"/><xf numFmtId=\"14\"/><xf numFmtId=\"164\"/><xf numFmtId=\"20\"/>"
    "<xf numFmtId=\"165\"/><xf numFmtId=\"166\"/><xf numFmtId=\"167\"/><xf numFmtId=\"168\"/></cellXfs>";
const std::string referenceData=
    "<dimension ref=\"A1:F6\"/><sheetData>"
    "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"inlineStr\"><is><t>Value</t></is></c>"
    "<c r=\"C1\" t=\"s\"><v>2</v></c><c r=\"D1\" t=\"str\"><f>\"F\"&amp;\"x\"</f><v>Fx</v></c></row>"
    "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>1</v></c><c r=\"B2\"><v>42</v></c><c r=\"C2\" t=\"b\"><v>1</v></c>"
    "<c r=\"D2\" t=\"e\"><f>1/0</f><v>#DIV/0!</v></c><c r=\"E2\" s=\"1\"/></row>"
    "<row r=\"4\"><c r=\"A4\" s=\"1\"><v>44927</v></c><c r=\"B4\" s=\"2\"><v>44927.5</v></c><c r=\"C4\" s=\"3\"><v>0.75</v></c>"
    "<c r=\"D4\" s=\"4\"><v>1234.5</v></c></row>"
    "<row r=\"5\"><c r=\"A5\"><f>NOW()</f></c><c r=\"B5\" s=\"6\"><v>45000</v></c><c r=\"C5\" s=\"7\"><v>1.5</v></c>"
    "<c r=\"F5\" t=\"s\"><v>3</v></c></row>"
    "<row r=\"6\"><c r=\"A6\" s=\"1\"><v>60</v></c><c t=\"s\"><v>4</v></c><c t=\"s\"><v>5</v></c>"
    "<c r=\"D6\" t=\"d\"><v>2024-02-29T10:00:00</v></c><c r=\"E6\" s=\"5\"><v>-3</v></c></row>"
    "</sheetData><mergeCells count=\"1\"><mergeCell ref=\"A1:B1\"/></mergeCells>";
Xlsx ReferenceXlsx() {
    Xlsx x;
    x.sheets={{"Hidden","hidden","<sheetData><row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>secret</t></is></c></row></sheetData>"},
              {"Data","",referenceData},
              {"Chart","","","chartsheet"}};
    x.strings={"<t>Name</t>","<t xml:space=\"preserve\">  alpha  </t>",
               "<r><rPr><b/></rPr><t>Ri</t></r><r><t>ch</t></r><rPh sb=\"0\" eb=\"1\"><t>XX</t></rPh>",
               "<t>  trimmed  </t>","<t>a_x0009_b</t>","<t>x_x005F_x0041_</t>"};
    x.styles=referenceStyles;
    return x;
}
const std::string referenceCsv=
    "Name,Value,Rich,Fx,,\n"
    "  alpha  ,42,TRUE,#DIV/0!,,\n"
    ",,,,,\n"
    "2023-01-01,2023-01-01T12:00:00,18:00:00,1234.5,,\n"
    ",2023-03-15,1900-01-01T12:00:00,,,trimmed\n"
    "60,a\tb,x_x0041_,2024-02-29T10:00:00,-3,\n";

// ---- ODS authoring ----
constexpr std::string_view odsNamespaces=
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:calcext=\"urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0\"";
struct Ods {
    std::string tables,styles;
    std::string mimetype="application/vnd.oasis.opendocument.spreadsheet";
    std::string manifestExtra;
    bool includeMimetype=true,includeManifest=true,includeContent=true,deflate=false;
};
std::string Build(const Ods& o) {
    std::vector<Member> m;
    if(o.includeMimetype)m.push_back({"mimetype",o.mimetype,false});
    if(o.includeManifest)
        m.push_back({"META-INF/manifest.xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
            "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\""+o.mimetype+"\"/>"
            "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\">"+o.manifestExtra+"</manifest:file-entry></manifest:manifest>",o.deflate});
    if(o.includeContent)
        m.push_back({"content.xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><office:document-content"+std::string(odsNamespaces)+" office:version=\"1.2\">"
            "<office:automatic-styles>"+o.styles+"</office:automatic-styles><office:body><office:spreadsheet>"+o.tables
            +"</office:spreadsheet></office:body></office:document-content>",o.deflate});
    return Zip(m);
}
std::string OdsTable(std::string_view name,std::string_view rows) {
    return "<table:table table:name=\""+std::string(name)+"\">"+std::string(rows)+"</table:table>";
}
std::string OdsOne(std::string_view rows) {Ods o;o.tables=OdsTable("T",rows);return Build(o);}
const std::string referenceOdsRows=
    "<table:table-column table:number-columns-repeated=\"1024\"/>"
    "<table:table-header-rows><table:table-row>"
    "<table:table-cell office:value-type=\"string\"><text:p>Name</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"string\" table:number-columns-spanned=\"2\"><text:p>Merged</text:p></table:table-cell>"
    "<table:covered-table-cell/><table:table-cell table:number-columns-repeated=\"1021\"/>"
    "</table:table-row></table:table-header-rows>"
    "<table:table-row>"
    "<table:table-cell office:value-type=\"float\" office:value=\"1234.5\"><text:p>1,234.50</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"percentage\" office:value=\"0.5\"><text:p>50%</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"currency\" office:currency=\"USD\" office:value=\"3\"><text:p>$3.00</text:p></table:table-cell>"
    "<table:table-cell table:number-columns-repeated=\"1021\"/></table:table-row>"
    "<table:table-row>"
    "<table:table-cell office:value-type=\"date\" office:date-value=\"2023-01-01\"><text:p>01/01/23</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"time\" office:time-value=\"PT12H30M00S\"><text:p>12:30 PM</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"boolean\" office:boolean-value=\"true\"><text:p>TRUE</text:p></table:table-cell>"
    "</table:table-row>"
    "<table:table-row table:number-rows-repeated=\"2\">"
    "<table:table-cell office:value-type=\"string\" table:number-columns-repeated=\"2\"><text:p>x</text:p></table:table-cell>"
    "<table:table-cell table:formula=\"of:=1+1\" office:value-type=\"float\" office:value=\"2\"><text:p>2</text:p></table:table-cell>"
    "</table:table-row>"
    "<table:table-row>"
    "<table:table-cell office:value-type=\"string\"><office:annotation><text:p>note</text:p></office:annotation>"
    "<text:p>a<text:s text:c=\"2\"/>b<text:tab/>c</text:p><text:p>line <text:span>two</text:span></text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"string\" office:string-value=\"sv\"><text:p>display</text:p></table:table-cell>"
    "<table:table-cell office:value-type=\"float\" office:value=\"0\" calcext:value-type=\"error\"><text:p>#DIV/0!</text:p></table:table-cell>"
    "</table:table-row>"
    "<table:table-row table:number-rows-repeated=\"1048570\"><table:table-cell table:number-columns-repeated=\"1024\"/></table:table-row>";
Ods ReferenceOds() {
    Ods o;
    o.styles="<style:style style:name=\"ta2\" style:family=\"table\"><style:table-properties table:display=\"false\"/></style:style>";
    o.tables="<table:table table:name=\"Hidden\" table:style-name=\"ta2\"><table:table-row><table:table-cell office:value-type=\"string\"><text:p>secret</text:p></table:table-cell></table:table-row></table:table>"
        +OdsTable("Sheet1",referenceOdsRows)
        +OdsTable("Sheet3","<table:table-row><table:table-cell office:value-type=\"float\" office:value=\"7\"><text:p>7</text:p></table:table-cell></table:table-row>");
    return o;
}
const std::string referenceOdsCsv=
    "Name,Merged,\n"
    "1234.5,0.5,3\n"
    "2023-01-01,12:30:00,TRUE\n"
    "x,x,2\n"
    "x,x,2\n"
    "\"a  b\tc\nline two\",sv,#DIV/0!\n";

// ---------------------------------------------------------------------------
void CsvTests() {
    {
        const auto r=Read(TableFormat::csv,"name,qty,ok\r\nwidget,\"1,5\",true\r\n\"he said \"\"hi\"\"\",-2.5e3,FALSE\r\n007,,x\r\n");
        Expect(bool(r)&&r.table.rows.size()==4&&r.table.columns==3,"CSV shape");
        ExpectCell(r,1,1,CellKind::text,"1,5","","quoted delimiter");
        ExpectCell(r,1,2,CellKind::boolean,"true","","inferred boolean");
        ExpectCell(r,2,0,CellKind::text,"he said \"hi\"","","doubled quotes");
        ExpectCell(r,2,1,CellKind::number,"-2.5e3","","inferred number keeps lexical");
        ExpectCell(r,2,2,CellKind::boolean,"FALSE","","boolean case kept");
        ExpectCell(r,3,0,CellKind::text,"007","","leading zeros stay text");
        ExpectCell(r,3,1,CellKind::empty,"","","empty field");
        Expect(r.report.raggedRows==0&&!r.report.byteOrderMark,"no ragged rows, no BOM");
    }
    {   // Embedded newlines (CRLF kept verbatim inside quotes), BOM, lone CR, no final newline.
        const auto r=Read(TableFormat::csv,"\xef\xbb\xbf" "a,\"l1\r\nl2\"\rb,c");
        Expect(bool(r)&&r.report.byteOrderMark&&r.table.rows.size()==2,"BOM and lone CR");
        ExpectCell(r,0,0,CellKind::text,"a","","BOM stripped from first field");
        ExpectCell(r,0,1,CellKind::text,"l1\r\nl2","","embedded CRLF");
        ExpectCell(r,1,1,CellKind::text,"c","","final record without newline");
    }
    {   // Ragged rows are padded to the widest and reported; blank lines are one empty field.
        const auto r=Read(TableFormat::csv,"a,b,c\n1\n\n2,3\n");
        Expect(bool(r)&&r.table.rows.size()==4&&r.table.columns==3&&r.report.raggedRows==3,"ragged rows padded");
        Expect(r.table.rows[1].size()==3&&r.table.rows[2][0].kind==CellKind::empty,"padding cells are empty");
        const auto w=tab::WriteCsv(r.table);
        ExpectText(w.error,w.text,"a,b,c\n1,,\n,,\n2,3,\n","ragged CSV rewritten rectangular");
    }
    {
        const auto r=Read(TableFormat::csv,"a,\n,");
        Expect(bool(r)&&r.table.rows.size()==2&&r.table.columns==2,"trailing delimiter makes an empty field");
    }
    {   // Bare quote in an unquoted field is literal.
        const auto r=Read(TableFormat::csv,"5\" pipe,x\n");
        ExpectCell(r,0,0,CellKind::text,"5\" pipe","","bare quote literal");
    }
    {
        tab::ReadOptions o;o.delimiter=';';
        const auto r=Read(TableFormat::csv,"a;1,5;\"x;y\"\n",o);
        Expect(bool(r)&&r.table.columns==3,"semicolon delimiter");
        ExpectCell(r,0,1,CellKind::text,"1,5","","comma is data with ';'");
        ExpectCell(r,0,2,CellKind::text,"x;y","","quoted semicolon");
        o.inferTypes=false;
        const auto plain=Read(TableFormat::csv,"1;true\n",o);
        ExpectCell(plain,0,0,CellKind::text,"1","","inference off: number stays text");
        ExpectCell(plain,0,1,CellKind::text,"true","","inference off: boolean stays text");
    }
    {
        const auto r=Read(TableFormat::tsv,"a\tb,c\n\"t\tq\"\t2\n");
        Expect(bool(r)&&r.table.columns==2,"TSV reader");
        ExpectCell(r,0,1,CellKind::text,"b,c","","comma is data in TSV");
        ExpectCell(r,1,0,CellKind::text,"t\tq","","quoted tab in TSV");
    }
    {
        const auto r=Read(TableFormat::csv,"");
        Expect(bool(r)&&r.table.rows.empty()&&r.table.columns==0,"empty CSV");
        const auto w=tab::WriteCsv(r.table);
        Expect(bool(w)&&w.text.empty(),"empty table writes nothing");
        const auto j=tab::WriteJson(r.table);
        ExpectText(j.error,j.text,"[]\n","empty JSON");
    }
    ExpectError(ReadError(TableFormat::csv,"a,\"open\n"),Error::csv_malformed,"unterminated quote");
    ExpectError(ReadError(TableFormat::csv,"\"a\"b,c\n"),Error::csv_malformed,"text after closing quote");
    ExpectError(ReadError(TableFormat::csv,"a,\xff\n"),Error::csv_encoding,"invalid UTF-8");
    ExpectError(ReadError(TableFormat::csv,"a,\xc0\xaf\n"),Error::csv_encoding,"overlong UTF-8");
    {   // Writer quoting.
        tab::Table t;
        t.columns=4;
        t.rows={{{CellKind::text,"plain",{}},{CellKind::text,"a,b",{}},{CellKind::text,"say \"x\"",{}},{CellKind::text,"l1\nl2",{}}},
                {{CellKind::text,"cr\rx",{}},{CellKind::text,"tab\there",{}},{CellKind::empty,"",{}},{CellKind::text," lead",{}}}};
        auto w=tab::WriteCsv(t);
        ExpectText(w.error,w.text,"plain,\"a,b\",\"say \"\"x\"\"\",\"l1\nl2\"\n\"cr\rx\",tab\there,, lead\n","CSV quoting");
        w=tab::WriteTsv(t);
        ExpectText(w.error,w.text,"plain\ta,b\t\"say \"\"x\"\"\"\t\"l1\nl2\"\n\"cr\rx\"\t\"tab\there\"\t\t lead\n","TSV quoting");
        tab::WriteOptions o;o.byteOrderMark=true;o.crlf=true;o.delimiter=';';
        w=tab::WriteCsv(t,o);
        ExpectText(w.error,w.text,"\xef\xbb\xbfplain;a,b;\"say \"\"x\"\"\";\"l1\nl2\"\r\n\"cr\rx\";tab\there;; lead\r\n","BOM, CRLF and ';'");
        tab::Table single;single.columns=1;single.rows={{{CellKind::text,"a",{}}},{{}},{{CellKind::text,"b",{}}}};
        w=tab::WriteCsv(single);
        ExpectText(w.error,w.text,"a\n\"\"\nb\n","lone empty field is quoted");
        const auto back=Read(TableFormat::csv,w.text);
        Expect(bool(back)&&back.table.rows.size()==3&&back.table.rows[1][0].kind==CellKind::empty,"lone empty field reads back");
    }
    {   // CSV -> CSV identity for canonical input, and CSV <-> TSV.
        const std::string csv="h1,h2,h3\n\"a,b\",\"q\"\"\",\"x\ny\"\n,2,\n";
        ExpectConvert(TableFormat::csv,TableFormat::csv,csv,csv,"CSV identity");
        const auto tsv=Convert(TableFormat::csv,TableFormat::tsv,csv);
        ExpectText(tsv.error,tsv.text,"h1\th2\th3\na,b\t\"q\"\"\"\t\"x\ny\"\n\t2\t\n","CSV to TSV");
        ExpectConvert(TableFormat::tsv,TableFormat::csv,tsv.text,csv,"TSV back to CSV");
    }
}

void JsonTests() {
    {
        const auto r=Read(TableFormat::json," [ [\"a\", 1, true, null], [\"b\", 2.5e1] ] ");
        Expect(bool(r)&&r.table.rows.size()==2&&r.table.columns==4&&r.report.raggedRows==1,"JSON arrays");
        ExpectCell(r,0,1,CellKind::number,"1","","JSON number");
        ExpectCell(r,0,2,CellKind::boolean,"true","","JSON boolean");
        ExpectCell(r,0,3,CellKind::empty,"","","JSON null");
        ExpectCell(r,1,1,CellKind::number,"2.5e1","","JSON number lexical");
    }
    {   // Objects: header = union of keys in first-seen order.
        const auto r=Read(TableFormat::json,"[{\"name\":\"a\",\"n\":1},{\"n\":2,\"extra\":\"z\"},{}]");
        Expect(bool(r)&&r.table.rows.size()==4&&r.table.columns==3,"JSON objects");
        ExpectCell(r,0,0,CellKind::text,"name","","key 1");
        ExpectCell(r,0,2,CellKind::text,"extra","","key 3 (first seen in row 2)");
        ExpectCell(r,2,0,CellKind::empty,"","","missing key is empty");
        ExpectCell(r,2,2,CellKind::text,"z","","late key placed");
        tab::WriteOptions o;o.json=tab::JsonShape::objects;
        const auto w=tab::WriteJson(r.table,o);
        ExpectText(w.error,w.text,"[\n{\"name\":\"a\",\"n\":1,\"extra\":\"\"},\n{\"name\":\"\",\"n\":2,\"extra\":\"z\"},\n{\"name\":\"\",\"n\":\"\",\"extra\":\"\"}\n]\n","objects rewritten");
        o.emptyAsNull=true;
        const auto n=tab::WriteJson(r.table,o);
        ExpectText(n.error,n.text,"[\n{\"name\":\"a\",\"n\":1,\"extra\":null},\n{\"name\":null,\"n\":2,\"extra\":\"z\"},\n{\"name\":null,\"n\":null,\"extra\":null}\n]\n","empty as null");
    }
    {   // Strings: escapes, surrogate pairs, raw UTF-8.
        const auto r=Read(TableFormat::json,"[[\"q\\\"\\\\\\/\\b\\f\\n\\r\\t\\u00e9\\ud83d\\ude00\",\"\xc3\xa9\"]]");
        ExpectCell(r,0,0,CellKind::text,"q\"\\/\b\f\n\r\t\xc3\xa9\xf0\x9f\x98\x80","","JSON escapes");
        const auto w=tab::WriteJson(r.table);
        ExpectText(w.error,w.text,"[\n[\"q\\\"\\\\/\\b\\f\\n\\r\\t\xc3\xa9\xf0\x9f\x98\x80\",\"\xc3\xa9\"]\n]\n","JSON writer escaping");
        tab::Table t;t.columns=1;t.rows={{{CellKind::text,std::string("\x01\x1f\x7f",3),{}}}};
        const auto c=tab::WriteJson(t);
        ExpectText(c.error,c.text,"[\n[\"\\u0001\\u001f\x7f\"]\n]\n","control characters");
    }
    {   // Typing rules: exact binary64 round trip only.
        tab::Table t;
        const std::vector<std::string> numbers={"0","-0","1","0.1","1234.5","-3","1e+21","1e-07","1.50","1e3","007","12345678901234567890","1E5","abc","NaN"};
        t.columns=numbers.size()+4;
        t.rows.emplace_back();
        for(const auto& n:numbers)t.rows[0].push_back({CellKind::number,n,{}});
        t.rows[0].push_back({CellKind::boolean,"TRUE","1"});
        t.rows[0].push_back({CellKind::date,"2023-01-01","44927"});
        t.rows[0].push_back({CellKind::text,"42",{}});
        t.rows[0].push_back({CellKind::error,"#N/A",{}});
        auto w=tab::WriteJson(t);
        ExpectText(w.error,w.text,"[\n[0,-0,1,0.1,1234.5,-3,1e+21,1e-07,\"1.50\",\"1e3\",\"007\",\"12345678901234567890\",\"1E5\",\"abc\",\"NaN\",true,\"2023-01-01\",\"42\",\"#N/A\"]\n]\n","JSON typing");
        tab::WriteOptions o;o.formattedText=true;
        w=tab::WriteJson(t,o);
        Expect(bool(w)&&w.text.find("\"1\",\"44927\",\"42\"")!=std::string::npos,"formatted text written as strings");
    }
    {   // Object keys: empty and duplicate header names are renamed deterministically.
        const auto r=Read(TableFormat::csv,"id,,id,id_2\n1,2,3,4\n");
        tab::WriteOptions o;o.json=tab::JsonShape::objects;
        const auto w=tab::WriteJson(r.table,o);
        ExpectText(w.error,w.text,"[\n{\"id\":1,\"column_2\":2,\"id_2\":3,\"id_2_2\":4}\n]\n","renamed keys");
        Expect(w.renamedKeys==3,"renamed key count");
        const auto header=Read(TableFormat::csv,"a,b\n");
        const auto only=tab::WriteJson(header.table,o);
        ExpectText(only.error,only.text,"[]\n","header only: no objects");
    }
    {   // Round trips JSON -> CSV -> JSON (arrays and objects) and CSV -> JSON -> CSV.
        const std::string arrays="[\n[\"name\",\"n\",\"ok\"],\n[\"a\",1,true],\n[\"b,c\",2.5,false],\n[\"q\\\"x\\ny\",-3,true]\n]\n";
        const auto csv=Convert(TableFormat::json,TableFormat::csv,arrays);
        ExpectText(csv.error,csv.text,"name,n,ok\na,1,true\n\"b,c\",2.5,false\n\"q\"\"x\ny\",-3,true\n","JSON to CSV");
        ExpectConvert(TableFormat::csv,TableFormat::json,csv.text,arrays,"CSV back to identical JSON");
        const std::string objects="[\n{\"k\":\"v\",\"n\":0.25},\n{\"k\":\"w\",\"n\":1e+21}\n]\n";
        tab::ConvertOptions o;o.write.json=tab::JsonShape::objects;
        const auto viaCsv=Convert(TableFormat::json,TableFormat::csv,objects);
        ExpectText(viaCsv.error,viaCsv.text,"k,n\nv,0.25\nw,1e+21\n","objects to CSV");
        ExpectConvert(TableFormat::csv,TableFormat::json,viaCsv.text,objects,"objects round trip",o);
        const std::string csvIn="a,b\n1,x\n,true\n";
        const auto json=Convert(TableFormat::csv,TableFormat::json,csvIn);
        ExpectText(json.error,json.text,"[\n[\"a\",\"b\"],\n[1,\"x\"],\n[\"\",true]\n]\n","CSV to JSON");
        ExpectConvert(TableFormat::json,TableFormat::csv,json.text,csvIn,"CSV -> JSON -> CSV identity");
    }
    ExpectError(ReadError(TableFormat::json,"[]"),Error::none,"empty array");
    ExpectError(ReadError(TableFormat::json,"{\"a\":[1]}"),Error::json_not_table,"top-level object");
    ExpectError(ReadError(TableFormat::json,"[1,2]"),Error::json_not_table,"array of scalars");
    ExpectError(ReadError(TableFormat::json,"[[1,[2]]]"),Error::json_not_table,"nested array in a cell");
    ExpectError(ReadError(TableFormat::json,"[{\"a\":{\"b\":1}}]"),Error::json_not_table,"nested object in a cell");
    ExpectError(ReadError(TableFormat::json,"[[1],{\"a\":1}]"),Error::json_not_table,"mixed rows");
    ExpectError(ReadError(TableFormat::json,""),Error::json_malformed,"empty input");
    ExpectError(ReadError(TableFormat::json,"[[1,]]"),Error::json_malformed,"trailing comma");
    ExpectError(ReadError(TableFormat::json,"[[01]]"),Error::json_malformed,"leading zero");
    ExpectError(ReadError(TableFormat::json,"[[+1]]"),Error::json_malformed,"plus sign");
    ExpectError(ReadError(TableFormat::json,"[[\"a\tb\"]]"),Error::json_malformed,"raw control character");
    ExpectError(ReadError(TableFormat::json,"[[\"\\ud800\"]]"),Error::json_malformed,"lone high surrogate");
    ExpectError(ReadError(TableFormat::json,"[[\"\\udc00\"]]"),Error::json_malformed,"lone low surrogate");
    ExpectError(ReadError(TableFormat::json,"[[\"\\x\"]]"),Error::json_malformed,"bad escape");
    ExpectError(ReadError(TableFormat::json,"[{\"a\":1,\"a\":2}]"),Error::json_malformed,"duplicate key");
    ExpectError(ReadError(TableFormat::json,"[[1]] x"),Error::json_malformed,"trailing garbage");
    ExpectError(ReadError(TableFormat::json,"[[tru]]"),Error::json_malformed,"bad literal");
    ExpectError(ReadError(TableFormat::json,"[[\"\xff\"]]"),Error::json_encoding,"invalid UTF-8");
    ExpectError(ReadError(TableFormat::json,"{\"a\":1 x}"),Error::json_malformed,"malformed non-table is malformed");
    {
        std::string deep(100,'[');deep+=std::string(100,']');
        ExpectError(ReadError(TableFormat::json,deep),Error::json_malformed,"nesting over 64");
        std::string ok(40,'[');ok+=std::string(40,']');
        ExpectError(ReadError(TableFormat::json,ok),Error::json_not_table,"nesting within 64 is valid JSON");
    }
}

void XlsxTests() {
    const auto xlsx=Build(ReferenceXlsx());
    {
        const auto r=Read(TableFormat::xlsx,xlsx);
        Expect(bool(r),"reference XLSX reads");
        Expect(r.report.sheets.size()==3&&r.report.sheets[0].name=="Hidden"&&r.report.sheets[0].state==tab::SheetState::hidden
            &&r.report.sheets[1].name=="Data"&&r.report.sheets[2].kind==tab::SheetKind::chartsheet,"sheet list");
        Expect(r.report.selectedSheet==1,"default is the first visible worksheet");
        Expect(r.report.dimension=="A1:F6"&&r.report.rows==6&&r.report.columns==6,"dimension and extent");
        Expect(r.report.mergedRanges==std::vector<std::string>{"A1:B1"},"merged ranges reported");
        Expect(r.report.formulas==3&&r.report.formulasWithoutValue==1,"formulas counted, cached values used");
        ExpectCell(r,0,2,CellKind::text,"Rich","","rich text runs, phonetic skipped");
        ExpectCell(r,0,3,CellKind::text,"Fx","","str formula cached value");
        ExpectCell(r,1,0,CellKind::text,"  alpha  ","","xml:space preserve");
        ExpectCell(r,1,1,CellKind::number,"42","","number");
        ExpectCell(r,1,2,CellKind::boolean,"TRUE","1","boolean");
        ExpectCell(r,1,3,CellKind::error,"#DIV/0!","","error");
        ExpectCell(r,1,4,CellKind::empty,"","","styled empty cell");
        ExpectCell(r,3,0,CellKind::date,"2023-01-01","44927","built-in date format 14");
        ExpectCell(r,3,1,CellKind::date,"2023-01-01T12:00:00","44927.5","custom date-time format");
        ExpectCell(r,3,2,CellKind::date,"18:00:00","0.75","built-in time format 20");
        ExpectCell(r,3,3,CellKind::number,"1234.5","","scientific format is not a date");
        ExpectCell(r,4,0,CellKind::empty,"","","formula without cached value");
        ExpectCell(r,4,1,CellKind::date,"2023-03-15","45000","quoted literal does not hide the date token");
        ExpectCell(r,4,2,CellKind::date,"1900-01-01T12:00:00","1.5","elapsed-hours format");
        ExpectCell(r,4,5,CellKind::text,"trimmed","","edge whitespace trimmed without preserve");
        ExpectCell(r,5,0,CellKind::number,"60","","fictitious 1900-02-29 stays a number");
        ExpectCell(r,5,1,CellKind::text,"a\tb","","_x0009_ escape, implicit column");
        ExpectCell(r,5,2,CellKind::text,"x_x0041_","","_x005F_ escapes the underscore");
        ExpectCell(r,5,3,CellKind::date,"2024-02-29T10:00:00","","t=d ISO date");
        ExpectCell(r,5,4,CellKind::number,"-3","","colour/conditional format is not a date");
    }
    ExpectConvert(TableFormat::xlsx,TableFormat::csv,xlsx,referenceCsv,"XLSX to CSV");
    {
        tab::ConvertOptions o;o.write.formattedText=true;
        const auto r=Convert(TableFormat::xlsx,TableFormat::csv,xlsx,o);
        Expect(bool(r)&&r.text.find("\n44927,44927.5,0.75,1234.5,,\n")!=std::string::npos&&r.text.find(",1,#DIV/0!")!=std::string::npos,
            "formatted text exposes serials");
        const auto j=Convert(TableFormat::xlsx,TableFormat::json,xlsx);
        ExpectText(j.error,j.text,
            "[\n[\"Name\",\"Value\",\"Rich\",\"Fx\",\"\",\"\"],\n[\"  alpha  \",42,true,\"#DIV/0!\",\"\",\"\"],\n[\"\",\"\",\"\",\"\",\"\",\"\"],\n"
            "[\"2023-01-01\",\"2023-01-01T12:00:00\",\"18:00:00\",1234.5,\"\",\"\"],\n[\"\",\"2023-03-15\",\"1900-01-01T12:00:00\",\"\",\"\",\"trimmed\"],\n"
            "[60,\"a\\tb\",\"x_x0041_\",\"2024-02-29T10:00:00\",-3,\"\"]\n]\n","XLSX to JSON");
    }
    {   // Sheet selection.
        tab::ReadOptions o;
        o.sheet.by=tab::SheetSelection::name;o.sheet.name="Hidden";
        auto r=Read(TableFormat::xlsx,xlsx,o);
        Expect(bool(r)&&r.report.selectedSheet==0&&r.table.rows.size()==1,"select hidden sheet by name");
        ExpectCell(r,0,0,CellKind::text,"secret","","hidden sheet content");
        o.sheet.by=tab::SheetSelection::index;o.sheet.index=1;
        r=Read(TableFormat::xlsx,xlsx,o);
        Expect(bool(r)&&r.report.selectedSheet==1&&r.table.rows.size()==6,"select by index");
        o.sheet.index=2;
        ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::sheet_unsupported,"chartsheet");
        o.sheet.index=3;
        ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::sheet_not_found,"index out of range");
        o.sheet.by=tab::SheetSelection::name;o.sheet.name="data";
        ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::sheet_not_found,"names are case-sensitive");
    }
    {   // 1904 date system and day 0.
        Xlsx x;
        x.date1904=true;x.styles=referenceStyles;
        x.sheets={{"S","","<sheetData><row><c s=\"1\"><v>0</v></c><c s=\"1\"><v>1</v></c><c s=\"2\"><v>43461.25</v></c></row></sheetData>"}};
        const auto r=Read(TableFormat::xlsx,Build(x));
        ExpectCell(r,0,0,CellKind::date,"1904-01-01","0","1904 day 0");
        ExpectCell(r,0,1,CellKind::date,"1904-01-02","1","1904 day 1");
        ExpectCell(r,0,2,CellKind::date,"2022-12-28T06:00:00","43461.25","1904 date-time");
        x.date1904=false;
        const auto d=Read(TableFormat::xlsx,Build(x));
        ExpectCell(d,0,0,CellKind::number,"0","","1900 day 0 of a date format stays a number");
        ExpectCell(d,0,1,CellKind::date,"1900-01-01","1","1900 day 1");
        ExpectCell(d,0,2,CellKind::date,"2018-12-27T06:00:00","43461.25","1900 date-time");
    }
    {   // Deflated members, all hidden sheets, sparse placement far from A1.
        auto x=ReferenceXlsx();
        x.deflate=true;
        ExpectConvert(TableFormat::xlsx,TableFormat::csv,Build(x),referenceCsv,"deflated XLSX");
        Xlsx h;
        h.sheets={{"A","hidden","<sheetData><row r=\"3\"><c r=\"C3\"><v>1</v></c></row></sheetData>"}};
        ExpectConvert(TableFormat::xlsx,TableFormat::csv,Build(h),",,\n,,\n,,1\n","only hidden sheets: first worksheet; sparse cell");
        Xlsx empty;empty.sheets={{"E","","<sheetData/>"}};
        const auto e=Read(TableFormat::xlsx,Build(empty));
        Expect(bool(e)&&e.table.rows.empty(),"empty worksheet");
        Xlsx none;
        ExpectError(ReadError(TableFormat::xlsx,Build(none)),Error::sheet_not_found,"workbook without sheets");
    }
    // Package-level failures.
    {
        auto x=ReferenceXlsx();x.mainType="application/vnd.ms-excel.sheet.macroEnabled.main+xml";
        ExpectError(ReadError(TableFormat::xlsx,Build(x)),Error::package_macro_enabled,"xlsm main part");
        x=ReferenceXlsx();x.extra={{"xl/vbaProject.bin","x"}};
        ExpectError(ReadError(TableFormat::xlsx,Build(x)),Error::package_macro_enabled,"VBA project member");
        x=ReferenceXlsx();x.mainType="application/vnd.ms-excel.sheet.binary.macroEnabled.main";
        ExpectError(ReadError(TableFormat::xlsx,Build(x)),Error::not_spreadsheet,"XLSB main part");
        x=ReferenceXlsx();x.includeContentTypes=false;
        ExpectError(ReadError(TableFormat::xlsx,Build(x)),Error::package_invalid,"missing content types");
        x=ReferenceXlsx();x.includeSheets=false;
        ExpectError(ReadError(TableFormat::xlsx,Build(x)),Error::package_missing_part,"missing worksheet part");
        ExpectError(ReadError(TableFormat::xlsx,docx_fixture::Docx("<w:p/>")),Error::not_spreadsheet,"DOCX is not a spreadsheet");
        ExpectError(ReadError(TableFormat::xlsx,docx_fixture::EncryptedDocx()),Error::package_encrypted,"EncryptedPackage");
        ExpectError(ReadError(TableFormat::xlsx,std::string("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1",8)+std::string(600,'\0')),
            Error::ole_compound_file,"legacy XLS / OLE");
        ExpectError(ReadError(TableFormat::xlsx,"PK not really"),Error::zip_not_archive,"not a ZIP");
        auto corrupt=xlsx;
        const auto at=corrupt.find("44927.5");
        corrupt[at]='5';
        ExpectError(ReadError(TableFormat::xlsx,corrupt),Error::zip_crc_mismatch,"CRC mismatch");
    }
    // Worksheet-level failures.
    const auto sheet=[](std::string_view cells) {return OneSheet("<sheetData><row r=\"1\">"+std::string(cells)+"</row></sheetData>");};
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c r=\"A0\"><v>1</v></c>")),Error::xlsx_malformed,"row 0");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c r=\"XFE1\"><v>1</v></c>")),Error::xlsx_malformed,"column beyond XFD");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c r=\"B2\"><v>1</v></c>")),Error::xlsx_malformed,"cell outside its row");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c r=\"A1\"><v>1</v></c><c r=\"A1\"><v>2</v></c>")),Error::xlsx_malformed,"duplicate cell");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c t=\"s\"><v>0</v></c>")),Error::xlsx_malformed,"shared string without table");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c t=\"zz\"><v>0</v></c>")),Error::xlsx_malformed,"unknown cell type");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c><v>12abc</v></c>")),Error::xlsx_malformed,"non-numeric number");
    ExpectError(ReadError(TableFormat::xlsx,sheet("<c t=\"b\"><v>2</v></c>")),Error::xlsx_malformed,"bad boolean");
    ExpectError(ReadError(TableFormat::xlsx,OneSheet("<sheetData/><mergeCells><mergeCell ref=\"A1:\"/></mergeCells>")),Error::xlsx_malformed,"bad merge ref");
    ExpectError(ReadError(TableFormat::xlsx,OneSheet("<sheetData><row r=\"1048577\"/></sheetData>")),Error::xlsx_malformed,"row beyond 1048576");
    {
        auto x=ReferenceXlsx();
        x.sheets[1].content="<!DOCTYPE x [<!ENTITY a \"b\">]><sheetData/>";
        auto members=XlsxMembers(x);
        for(auto& m:members)if(m.name=="xl/worksheets/sheet2.xml")m.data="<!DOCTYPE worksheet><worksheet xmlns=\""+std::string(sml)+"\"/>";
        ExpectError(ReadError(TableFormat::xlsx,Zip(members)),Error::xml_doctype,"DOCTYPE in worksheet");
        for(auto& m:members)if(m.name=="xl/worksheets/sheet2.xml")m.data="<worksheet xmlns=\""+std::string(sml)+"\"><sheetData><row><c t=\"inlineStr\"><is><t>&bomb;</t></is></c></row></sheetData></worksheet>";
        ExpectError(ReadError(TableFormat::xlsx,Zip(members)),Error::xml_entity,"undefined entity");
    }
}

void OdsTests() {
    const auto ods=Build(ReferenceOds());
    {
        const auto r=Read(TableFormat::ods,ods);
        Expect(bool(r),"reference ODS reads");
        Expect(r.report.sheets.size()==3&&r.report.sheets[0].state==tab::SheetState::hidden&&r.report.sheets[1].name=="Sheet1"
            &&r.report.sheets[2].state==tab::SheetState::visible,"ODS sheet list and hidden table style");
        Expect(r.report.selectedSheet==1&&r.report.rows==6&&r.report.columns==3,"first visible table, trimmed extent");
        Expect(r.report.mergedRanges==std::vector<std::string>{"B1:C1"},"ODS merged range");
        Expect(r.report.formulas==2,"formulas counted per materialized repeat");
        Expect(r.report.trimmedRepeatedRows==1048570&&r.report.trimmedRepeatedCells==2042,"trailing repeats trimmed and reported");
        ExpectCell(r,0,2,CellKind::empty,"","","covered cell is empty");
        ExpectCell(r,1,0,CellKind::number,"1234.5","1,234.50","float with display text");
        ExpectCell(r,1,1,CellKind::number,"0.5","50%","percentage");
        ExpectCell(r,1,2,CellKind::number,"3","$3.00","currency");
        ExpectCell(r,2,0,CellKind::date,"2023-01-01","01/01/23","date");
        ExpectCell(r,2,1,CellKind::date,"12:30:00","12:30 PM","time duration");
        ExpectCell(r,2,2,CellKind::boolean,"TRUE","","boolean (display equals value)");
        ExpectCell(r,4,1,CellKind::text,"x","","repeated columns in repeated rows");
        ExpectCell(r,5,0,CellKind::text,"a  b\tc\nline two","","text:s, text:tab, spans, paragraphs; annotation skipped");
        ExpectCell(r,5,1,CellKind::text,"sv","","office:string-value wins");
        ExpectCell(r,5,2,CellKind::error,"#DIV/0!","","calcext error");
    }
    ExpectConvert(TableFormat::ods,TableFormat::csv,ods,referenceOdsCsv,"ODS to CSV");
    {
        tab::ConvertOptions o;o.write.formattedText=true;
        ExpectConvert(TableFormat::ods,TableFormat::csv,ods,
            "Name,Merged,\n\"1,234.50\",50%,$3.00\n01/01/23,12:30 PM,TRUE\nx,x,2\nx,x,2\n\"a  b\tc\nline two\",sv,#DIV/0!\n",
            "ODS formatted text",o);
        ExpectConvert(TableFormat::ods,TableFormat::json,ods,
            "[\n[\"Name\",\"Merged\",\"\"],\n[1234.5,0.5,3],\n[\"2023-01-01\",\"12:30:00\",true],\n[\"x\",\"x\",2],\n[\"x\",\"x\",2],\n"
            "[\"a  b\\tc\\nline two\",\"sv\",\"#DIV/0!\"]\n]\n","ODS to JSON");
        auto d=ReferenceOds();d.deflate=true;
        ExpectConvert(TableFormat::ods,TableFormat::csv,Build(d),referenceOdsCsv,"deflated ODS");
    }
    {
        tab::ReadOptions o;o.sheet.by=tab::SheetSelection::name;o.sheet.name="Sheet3";
        ExpectConvert(TableFormat::ods,TableFormat::csv,ods,"7\n","select ODS sheet by name",{o,{}});
        o.sheet.by=tab::SheetSelection::index;o.sheet.index=0;
        ExpectConvert(TableFormat::ods,TableFormat::csv,ods,"secret\n","select hidden ODS sheet by index",{o,{}});
        o.sheet.index=5;
        ExpectError(ReadError(TableFormat::ods,ods,o),Error::sheet_not_found,"ODS index out of range");
    }
    {   // Leading empty repeats inside the data are materialized; time durations beyond a day.
        const auto r=Read(TableFormat::ods,OdsOne(
            "<table:table-row table:number-rows-repeated=\"2\"><table:table-cell/></table:table-row>"
            "<table:table-row><table:table-cell table:number-columns-repeated=\"2\"/>"
            "<table:table-cell office:value-type=\"time\" office:time-value=\"P1DT1H0M1.5S\"><text:p>x</text:p></table:table-cell></table:table-row>"));
        Expect(bool(r)&&r.table.rows.size()==3&&r.table.columns==3,"leading empty repeats kept");
        ExpectCell(r,2,2,CellKind::date,"25:00:01.500","x","duration with days and fraction");
        Expect(r.report.trimmedRepeatedRows==0&&r.report.trimmedRepeatedCells==0,"no trimming inside the data");
    }
    {   // Package failures.
        auto o=ReferenceOds();o.mimetype="application/vnd.oasis.opendocument.text";
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::not_spreadsheet,"ODT mimetype");
        o=ReferenceOds();o.includeMimetype=false;
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::package_invalid,"missing mimetype");
        o=ReferenceOds();o.includeManifest=false;
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::package_invalid,"missing manifest");
        o=ReferenceOds();o.includeContent=false;
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::package_missing_part,"missing content.xml");
        o=ReferenceOds();o.manifestExtra="<manifest:encryption-data manifest:checksum-type=\"SHA1\"/>";
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::package_encrypted,"encrypted ODS");
        o=ReferenceOds();o.mimetype="text/plain";
        ExpectError(ReadError(TableFormat::ods,Build(o)),Error::package_invalid,"foreign mimetype");
        ExpectError(ReadError(TableFormat::ods,Build(ReferenceXlsx())),Error::package_invalid,"XLSX read as ODS");
        Ods empty;
        ExpectError(ReadError(TableFormat::ods,Build(empty)),Error::sheet_not_found,"ODS without tables");
    }
    const auto row=[](std::string_view cells) {return OdsOne("<table:table-row>"+std::string(cells)+"</table:table-row>");};
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell table:number-columns-repeated=\"0\"/>")),Error::ods_malformed,"repeat 0");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell table:number-columns-repeated=\"x\"/>")),Error::ods_malformed,"repeat not a number");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell office:value-type=\"float\"><text:p>1</text:p></table:table-cell>")),Error::ods_malformed,"float without value");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell office:value-type=\"boolean\" office:boolean-value=\"yes\"/>")),Error::ods_malformed,"bad boolean");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell office:value-type=\"time\" office:time-value=\"12:30\"/>")),Error::ods_malformed,"bad duration");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell office:value-type=\"string\" table:number-columns-repeated=\"20000\"><text:p>x</text:p></table:table-cell>")),
        Error::column_limit,"repeated data columns beyond the limit");
    ExpectError(ReadError(TableFormat::ods,OdsOne("<table:table-row table:number-rows-repeated=\"2000000\"><table:table-cell office:value-type=\"string\"><text:p>x</text:p></table:table-cell></table:table-row>")),
        Error::row_limit,"repeated data rows beyond the limit");
    ExpectError(ReadError(TableFormat::ods,OdsOne("<table:table-row table:number-rows-repeated=\"1000000\"><table:table-cell office:value-type=\"string\" table:number-columns-repeated=\"3\"><text:p>x</text:p></table:table-cell></table:table-row>")),
        Error::cell_limit,"repeated data cells beyond the cell limit");
    ExpectError(ReadError(TableFormat::ods,row("<table:table-cell office:value-type=\"string\"><text:p><text:s text:c=\"2000000\"/></text:p></table:table-cell>")),
        Error::cell_size_limit,"text:s expansion bounded");
}

void LimitTests() {
    const auto xlsx=Build(ReferenceXlsx());
    tab::ReadOptions o;
    o.maximumSourceBytes=100;
    ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::source_limit,"source limit");
    o=tab::ReadOptions{};o.table.maximumRows=3;
    ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::row_limit,"XLSX row limit");
    ExpectError(ReadError(TableFormat::csv,"1\n2\n3\n4\n",o),Error::row_limit,"CSV row limit");
    o=tab::ReadOptions{};o.table.maximumColumns=2;
    ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::column_limit,"XLSX column limit");
    ExpectError(ReadError(TableFormat::csv,"1,2,3\n",o),Error::column_limit,"CSV column limit");
    ExpectError(ReadError(TableFormat::json,"[{\"a\":1,\"b\":2,\"c\":3}]",o),Error::column_limit,"JSON key limit");
    o=tab::ReadOptions{};o.table.maximumSharedStrings=2;
    ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::cell_limit,"shared-string table limit");
    o=tab::ReadOptions{};o.table.maximumCellBytes=4;
    ExpectError(ReadError(TableFormat::csv,"abcde\n",o),Error::cell_size_limit,"CSV cell size");
    ExpectError(ReadError(TableFormat::csv,"\"ab\"\"cd\"\n",o),Error::cell_size_limit,"quoted CSV cell size");
    ExpectError(ReadError(TableFormat::json,"[[\"abcde\"]]",o),Error::cell_size_limit,"JSON cell size");
    ExpectError(ReadError(TableFormat::xlsx,xlsx,o),Error::cell_size_limit,"XLSX cell size");
    o=tab::ReadOptions{};o.table.maximumTextBytes=20;
    ExpectError(ReadError(TableFormat::csv,"aaaaaaaaaa,bbbbbbbbbb,c\n",o),Error::text_limit,"total text");
    o=tab::ReadOptions{};o.table.maximumCells=5;
    ExpectError(ReadError(TableFormat::csv,"1,2,3\n4,5,6\n",o),Error::cell_limit,"CSV cells");
    ExpectError(ReadError(TableFormat::csv,"1,2,3\n4\n",o),Error::cell_limit,"padding counts against cells");
    {   // A single cell at XFD1048576 would densify to 17 billion cells.
        const auto far=OneSheet("<sheetData><row r=\"1048576\"><c r=\"XFD1048576\"><v>1</v></c></row></sheetData>");
        ExpectError(ReadError(TableFormat::xlsx,far),Error::cell_limit,"sparse bomb");
    }
    {
        tab::ReadOptions bad;bad.delimiter='|';
        ExpectError(ReadError(TableFormat::csv,"a",bad),Error::invalid_options,"delimiter");
        bad=tab::ReadOptions{};bad.table.maximumCells=0;
        ExpectError(ReadError(TableFormat::csv,"a",bad),Error::invalid_options,"zero limit");
        bad=tab::ReadOptions{};bad.sheet.by=tab::SheetSelection::name;
        ExpectError(ReadError(TableFormat::xlsx,xlsx,bad),Error::invalid_options,"empty sheet name");
        bad=tab::ReadOptions{};bad.zip.maximumEntryExpandedBytes=1ull<<40;
        ExpectError(ReadError(TableFormat::xlsx,xlsx,bad),Error::invalid_options,"inflater-incompatible limit");
        ExpectError(ReadError(TableFormat(9),"a"),Error::invalid_options,"unknown input format");
        ExpectError(Convert(TableFormat::csv,TableFormat::xlsx,"a").error,Error::invalid_options,"XLSX is input-only");
        ExpectError(tab::WriteTable(TableFormat::ods,{}).error,Error::invalid_options,"ODS is input-only");
        tab::Table ragged;ragged.columns=2;ragged.rows={{{}}};
        ExpectError(tab::WriteCsv(ragged).error,Error::invalid_options,"non-rectangular table rejected by writers");
        tab::WriteOptions w;w.delimiter=':';
        ExpectError(tab::WriteCsv({},w).error,Error::invalid_options,"writer delimiter");
    }
    {   // Output limit, exact boundary.
        tab::ConvertOptions c;c.write.maximumOutputBytes=referenceCsv.size();
        ExpectConvert(TableFormat::xlsx,TableFormat::csv,xlsx,referenceCsv,"output exactly at limit",c);
        c.write.maximumOutputBytes=referenceCsv.size()-1;
        const auto r=Convert(TableFormat::xlsx,TableFormat::csv,xlsx,c);
        Expect(r.error==Error::output_limit&&r.text.empty(),"output limit, no partial text");
        c.write.maximumOutputBytes=10;
        ExpectError(Convert(TableFormat::xlsx,TableFormat::json,xlsx,c).error,Error::output_limit,"JSON output limit");
    }
    {   // XML limits apply to spreadsheet parts.
        tab::ReadOptions x;x.xml.maximumNodes=20;
        ExpectError(ReadError(TableFormat::xlsx,xlsx,x),Error::xml_limit,"XML node limit");
        x=tab::ReadOptions{};x.zip.maximumTotalExpandedBytes=200;
        ExpectError(ReadError(TableFormat::ods,Build(ReferenceOds()),x),Error::zip_size_limit,"ZIP expansion budget");
    }
    {   // A large but legal CSV stays linear: 200,000 rows x 5 columns.
        std::string big;
        for(int i=0;i<200000;++i)big+=std::to_string(i)+",text "+std::to_string(i)+",\"q,"+std::to_string(i)+"\",true,\n";
        const auto r=Read(TableFormat::csv,big);
        Expect(bool(r)&&r.report.rows==200000&&r.report.columns==5,"large CSV");
        const auto j=tab::WriteJson(r.table);
        Expect(bool(j)&&j.text.size()>big.size(),"large JSON");
    }
}

void DeterminismTests() {
    const auto xlsx=Build(ReferenceXlsx());
    const auto ods=Build(ReferenceOds());
    for(const auto out:{TableFormat::csv,TableFormat::tsv,TableFormat::json}) {
        for(const auto& [in,bytes]:std::vector<std::pair<TableFormat,std::string>>{{TableFormat::xlsx,xlsx},{TableFormat::ods,ods}}) {
            const auto a=Convert(in,out,bytes),b=Convert(in,out,bytes);
            Expect(a&&b&&a.text==b.text,"repeatable conversion");
            const auto read=Read(in,bytes);
            const auto direct=tab::WriteTable(out,read.table);
            Expect(direct.text==a.text,"ConvertTable equals ReadTable + WriteTable");
            Expect(!a.text.empty()&&a.text.back()=='\n'&&a.text.find('\r')==std::string::npos,"LF only");
        }
    }
    {
        const auto a=Read(TableFormat::xlsx,xlsx),b=Read(TableFormat::xlsx,xlsx);
        Expect(a.table.rows==b.table.rows,"identical tables");
    }
    Expect(tab::MediaType(TableFormat::csv)=="text/csv; charset=utf-8","CSV media type");
    Expect(tab::MediaType(TableFormat::tsv)=="text/tab-separated-values","TSV media type");
    Expect(tab::MediaType(TableFormat::json)=="application/json","JSON media type");
    Expect(tab::MediaType(TableFormat::xlsx).empty()&&tab::MediaType(TableFormat::ods).empty(),"input-only formats have no media type");
    for(std::uint32_t e=0;e<=std::uint32_t(Error::ole_compound_file);++e)
        Expect(tab::ErrorCode(Error(e))!="unknown"&&tab::ErrorMessage(Error(e)).find("Unknown")==std::string_view::npos,"error text "+std::to_string(e));
    Expect(tab::ErrorCode(Error(999))=="unknown","unknown error code");
}

bool ValidUtf8(std::string_view s) {
    for(std::size_t i=0;i<s.size();) {
        const auto b=std::uint8_t(s[i]);
        std::size_t n=b<0x80?1:b>=0xc2&&b<=0xdf?2:b>=0xe0&&b<=0xef?3:b>=0xf0&&b<=0xf4?4:0;
        if(!n||s.size()-i<n)return false;
        for(std::size_t k=1;k<n;++k)if((std::uint8_t(s[i+k])&0xc0)!=0x80)return false;
        i+=n;
    }
    return true;
}
void MutationTests() {
    std::uint64_t state=0x9e3779b97f4a7c15ull;
    const auto next=[&] {state^=state<<13;state^=state>>7;state^=state<<17;return state;};
    const std::vector<std::pair<TableFormat,std::string>> seeds={
        {TableFormat::xlsx,Build(ReferenceXlsx())},{TableFormat::ods,Build(ReferenceOds())},
        {TableFormat::csv,"a,\"b\"\"c\",d\r\n1,\"x\ny\",true\n"},
        {TableFormat::json,"[{\"a\":1,\"b\":\"x\\u00e9\"},{\"c\":[1]},{\"a\":null}]"}};
    std::size_t successes=0,errors=0;
    for(const auto& [format,seed]:seeds) {
        for(long round=0;round<mutationRounds;++round) {
            auto bytes=seed;
            const auto edits=1+next()%4;
            for(std::uint64_t e=0;e<edits&&!bytes.empty();++e) {
                const auto at=std::size_t(next()%bytes.size());
                switch(next()%5) {
                    case 0: bytes[at]=char(next());break;
                    case 1: bytes[at]=char(bytes[at]^(1<<(next()%8)));break;
                    case 2: bytes.resize(at);break;
                    case 3: bytes.insert(bytes.begin()+std::ptrdiff_t(at),char(next()));break;
                    default: bytes[at]="\",[]{}<>&;'\n\x80\xff"[next()%14];break;
                }
            }
            for(const auto out:{TableFormat::csv,TableFormat::json}) {
                const auto r=Convert(format,out,bytes);
                if(!r) {++errors;Expect(r.text.empty(),"error carries no text");continue;}
                ++successes;
                Expect(ValidUtf8(r.text),"mutated input produced invalid UTF-8");
            }
        }
    }
    Expect(successes>0&&errors>0,"mutations exercise both outcomes");
}
}

int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version") {std::cout<<tab::revision<<"\n";return 0;}
    const std::map<std::string,std::function<void()>> groups={
        {"csv",CsvTests},{"json",JsonTests},{"xlsx",XlsxTests},{"ods",OdsTests},
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
