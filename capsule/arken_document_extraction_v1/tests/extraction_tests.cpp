#include "arken_document_extraction_v1/api.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace doc=arken::document::v1;
using doc::Error;
using doc::OutputFormat;
using doc::SourceFormat;

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
void ExpectText(const doc::TextResult& got,std::string_view want,std::string_view what) {
    if(!got) {++failures;std::cerr<<"FAIL: "<<what<<": error "<<doc::ErrorCode(got.error)<<"\n";return;}
    if(got.text==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<"\n--- got ---\n"<<Visible(got.text)<<"\n--- want ---\n"<<Visible(want)<<"\n";
}
void ExpectError(Error got,Error want,std::string_view what) {
    if(got==want)return;
    ++failures;
    std::cerr<<"FAIL: "<<what<<": got "<<doc::ErrorCode(got)<<", want "<<doc::ErrorCode(want)<<"\n";
}
std::span<const std::byte> Span(const std::string& s) {return std::as_bytes(std::span<const char>(s.data(),s.size()));}
doc::TextResult Convert(SourceFormat source,OutputFormat output,const std::string& bytes,const doc::ConvertOptions& options={}) {
    return doc::Convert(source,output,Span(bytes),options);
}

// ---- Minimal ZIP and raw-deflate authoring (test-only, independent of the reader) ----
std::uint32_t Crc(std::string_view data) {
    std::uint32_t c=0xffffffffu;
    for(const char ch:data) {
        c^=std::uint8_t(ch);
        for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));
    }
    return ~c;
}
void Le16(std::string& out,std::uint32_t v) {out.push_back(char(v&0xff));out.push_back(char((v>>8)&0xff));}
void Le32(std::string& out,std::uint32_t v) {Le16(out,v&0xffff);Le16(out,v>>16);}

class BitWriter {
public:
    void Bits(std::uint32_t value,int count) {
        for(int i=0;i<count;++i) {
            accumulator_|=((value>>i)&1u)<<used_;
            if(++used_==8)Flush();
        }
    }
    void Huffman(std::uint32_t code,int length) {for(int i=length-1;i>=0;--i)Bits((code>>i)&1u,1);}
    void Flush() {if(used_){out_.push_back(char(accumulator_));accumulator_=0;used_=0;}}
    std::string& Bytes() {return out_;}
private:
    std::string out_;
    std::uint32_t accumulator_=0;
    int used_=0;
};
void FixedLiteral(BitWriter& w,int symbol) {
    if(symbol<=143)w.Huffman(0x30+symbol,8);
    else if(symbol<=255)w.Huffman(0x190+symbol-144,9);
    else if(symbol<=279)w.Huffman(symbol-256,7);
    else w.Huffman(0xc0+symbol-280,8);
}
void FixedLength(BitWriter& w,int length) {
    static constexpr int base[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static constexpr int extra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    int i=28;
    while(base[i]>length)--i;
    FixedLiteral(w,257+i);
    w.Bits(std::uint32_t(length-base[i]),extra[i]);
}
// One fixed-Huffman block: literals plus distance-1 matches for byte runs, so
// both the literal and the length/distance paths of the inflater are exercised.
std::string DeflateFixed(std::string_view data) {
    BitWriter w;
    w.Bits(1,1);w.Bits(1,2);
    for(std::size_t i=0;i<data.size();) {
        std::size_t run=0;
        if(i>0)while(i+run<data.size()&&data[i+run]==data[i-1]&&run<258)++run;
        if(run>=3) {FixedLength(w,int(run));w.Huffman(0,5);i+=run;continue;}
        FixedLiteral(w,std::uint8_t(data[i]));++i;
    }
    FixedLiteral(w,256);
    w.Flush();
    return std::move(w.Bytes());
}
// Stored deflate blocks (BTYPE=00).
std::string DeflateStored(std::string_view data) {
    std::string out;
    std::size_t p=0;
    do {
        const auto n=std::min<std::size_t>(65535,data.size()-p);
        out.push_back(char(p+n==data.size()?1:0));
        Le16(out,std::uint32_t(n));Le16(out,std::uint32_t(~n&0xffff));
        out.append(data.substr(p,n));
        p+=n;
    } while(p<data.size());
    return out;
}

struct Member {
    Member()=default;
    Member(std::string n,std::string d,std::uint16_t m=0):name(std::move(n)),data(std::move(d)),method(m) {}
    std::string name,data;
    std::uint16_t method=0;
    std::optional<std::string> payload;   // Compressed bytes; default derived from method.
    std::optional<std::uint32_t> crc,size; // Overrides for the declared values.
    std::uint16_t flags=0;
    bool descriptorSignature=true;
    std::optional<std::uint32_t> descriptorCrc;
};
std::string Zip(const std::vector<Member>& members) {
    std::string out,central;
    for(const auto& m:members) {
        const auto payload=m.payload?*m.payload:m.method==8?DeflateFixed(m.data):m.data;
        const auto crc=m.crc?*m.crc:Crc(m.data);
        const auto size=m.size?*m.size:std::uint32_t(m.data.size());
        const bool descriptor=(m.flags&8)!=0;
        const auto offset=std::uint32_t(out.size());
        Le32(out,0x04034b50);Le16(out,20);Le16(out,m.flags);Le16(out,m.method);Le16(out,0);Le16(out,0x21);
        Le32(out,descriptor?0:crc);Le32(out,descriptor?0:std::uint32_t(payload.size()));Le32(out,descriptor?0:size);
        Le16(out,std::uint32_t(m.name.size()));Le16(out,0);
        out+=m.name;out+=payload;
        if(descriptor) {
            if(m.descriptorSignature)Le32(out,0x08074b50);
            Le32(out,m.descriptorCrc?*m.descriptorCrc:crc);Le32(out,std::uint32_t(payload.size()));Le32(out,size);
        }
        Le32(central,0x02014b50);Le16(central,20);Le16(central,20);Le16(central,m.flags);Le16(central,m.method);
        Le16(central,0);Le16(central,0x21);Le32(central,crc);Le32(central,std::uint32_t(payload.size()));Le32(central,size);
        Le16(central,std::uint32_t(m.name.size()));Le16(central,0);Le16(central,0);Le16(central,0);Le16(central,0);
        Le32(central,0);Le32(central,offset);
        central+=m.name;
    }
    const auto centralOffset=std::uint32_t(out.size());
    out+=central;
    Le32(out,0x06054b50);Le16(out,0);Le16(out,0);
    Le16(out,std::uint32_t(members.size()));Le16(out,std::uint32_t(members.size()));
    Le32(out,std::uint32_t(central.size()));Le32(out,centralOffset);Le16(out,0);
    return out;
}

constexpr std::string_view wNs="http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr std::string_view rNs="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view docxMain="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";

struct Docx {
    std::string body,styles,numbering,relationships;
    std::string mainType=std::string(docxMain);
    std::optional<std::string> document; // Full document.xml override.
    std::optional<std::string> contentTypes;
    std::uint16_t method=0;
    bool includeDocument=true;
    std::vector<Member> extra;
};
std::string DocumentXml(std::string_view body) {
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<w:document xmlns:w=\"")
        +std::string(wNs)+"\" xmlns:r=\""+std::string(rNs)+"\"><w:body>"+std::string(body)+"<w:sectPr/></w:body></w:document>";
}
std::vector<Member> DocxMembers(const Docx& d) {
    std::vector<Member> members;
    const auto add=[&](std::string name,std::string data) {
        Member m;m.name=std::move(name);m.data=std::move(data);m.method=d.method;
        members.push_back(std::move(m));
    };
    add("[Content_Types].xml",d.contentTypes?*d.contentTypes:
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\""+d.mainType+"\"/></Types>");
    add("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
    if(d.includeDocument)add("word/document.xml",d.document?*d.document:DocumentXml(d.body));
    std::string rels=d.relationships;
    if(!d.styles.empty()) {
        rels+="<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>";
        add("word/styles.xml","<w:styles xmlns:w=\""+std::string(wNs)+"\">"+d.styles+"</w:styles>");
    }
    if(!d.numbering.empty()) {
        rels+="<Relationship Id=\"rIdNumbering\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"./numbering.xml\"/>";
        add("word/numbering.xml","<w:numbering xmlns:w=\""+std::string(wNs)+"\">"+d.numbering+"</w:numbering>");
    }
    if(!rels.empty())
        add("word/_rels/document.xml.rels","<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"+rels+"</Relationships>");
    for(const auto& m:d.extra)members.push_back(m);
    return members;
}
std::string Build(const Docx& d) {return Zip(DocxMembers(d));}
std::string Paragraph(std::string_view text) {return "<w:p><w:r><w:t>"+std::string(text)+"</w:t></w:r></w:p>";}
std::string Link(std::string_view id,std::string_view target) {
    return "<Relationship Id=\""+std::string(id)+"\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\""
        +std::string(target)+"\" TargetMode=\"External\"/>";
}
Error DocxError(const std::string& bytes,const doc::ExtractOptions& options={}) {return doc::ExtractDocx(Span(bytes),options).error;}
Error RtfError(const std::string& bytes,const doc::ExtractOptions& options={}) {return doc::ExtractRtf(Span(bytes),options).error;}
doc::TextResult DocxText(const Docx& d) {return Convert(SourceFormat::docx,OutputFormat::text,Build(d));}
doc::TextResult DocxMarkdown(const Docx& d) {return Convert(SourceFormat::docx,OutputFormat::markdown,Build(d));}
doc::TextResult RtfText(const std::string& s) {return Convert(SourceFormat::rtf,OutputFormat::text,s);}
doc::TextResult RtfMarkdown(const std::string& s) {return Convert(SourceFormat::rtf,OutputFormat::markdown,s);}

// ---- Reference fixtures ----
Docx ReferenceDocx() {
    Docx d;
    d.styles=R"(<w:style w:type="paragraph" w:styleId="Normal"><w:name w:val="Normal"/></w:style>)"
             R"(<w:style w:type="paragraph" w:styleId="H1x"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/></w:style>)"
             R"(<w:style w:type="paragraph" w:styleId="Derived"><w:name w:val="Custom"/><w:basedOn w:val="H1x"/></w:style>)";
    d.numbering=R"(<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:numFmt w:val="decimal"/></w:lvl><w:lvl w:ilvl="1"><w:numFmt w:val="bullet"/></w:lvl></w:abstractNum>)"
                R"(<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>)";
    d.relationships=Link("rIdLink","https://example.com/a");
    d.body=
        R"(<w:p><w:pPr><w:pStyle w:val="Title"/></w:pPr><w:r><w:t>Report</w:t></w:r></w:p>)"
        R"(<w:p><w:pPr><w:pStyle w:val="Derived"/></w:pPr><w:r><w:t>Intro</w:t></w:r></w:p>)"
        R"(<w:p><w:r><w:t xml:space="preserve">Hel</w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>lo</w:t></w:r>)"
        "<w:r><w:t xml:space=\"preserve\"> w\xc3\xb6rld \xf0\x9f\x98\x80</w:t></w:r></w:p>"
        R"(<w:p/><w:p><w:r><w:t xml:space="preserve">   </w:t></w:r></w:p>)"
        R"(<w:p><w:r><w:t>a</w:t><w:tab/><w:t>b</w:t><w:br/><w:t>c</w:t></w:r></w:p>)"
        R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>one</w:t></w:r></w:p>)"
        R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="1"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>sub</w:t></w:r></w:p>)"
        R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>two</w:t></w:r></w:p>)"
        R"(<w:tbl><w:tblGrid/><w:tr><w:tc><w:p><w:r><w:t>H1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>H2</w:t></w:r></w:p></w:tc></w:tr>)"
        R"(<w:tr><w:tc><w:p><w:r><w:t>x|y</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>z</w:t></w:r></w:p></w:tc></w:tr></w:tbl>)"
        R"(<w:p><w:r><w:t xml:space="preserve">See </w:t></w:r><w:hyperlink r:id="rIdLink"><w:r><w:t>site</w:t></w:r></w:hyperlink><w:r><w:t>.</w:t></w:r></w:p>)";
    return d;
}
constexpr std::string_view referenceText=
    "Report\n\nIntro\n\nHello w\xc3\xb6rld \xf0\x9f\x98\x80\n\na\tb\nc\n\n1. one\n  - sub\n2. two\n\nH1\tH2\nx|y\tz\n\n"
    "See site (https://example.com/a).\n";
constexpr std::string_view referenceMarkdown=
    "# Report\n\n# Intro\n\nHel**lo** w\xc3\xb6rld \xf0\x9f\x98\x80\n\na\tb\\\nc\n\n1. one\n   - sub\n2. two\n\n"
    "| H1 | H2 |\n| --- | --- |\n| x\\|y | z |\n\nSee [site](https://example.com/a).\n";

const std::string referenceRtf=
    R"({\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fswiss Arial;}{\f1\fcharset204 Times;}})"
    R"({\colortbl;\red0\green0\blue0;}{\stylesheet{\s0 Normal;}{\s1 heading 1;}}{\info{\title Secret}})"
    "\r\n"
    R"({\header Header text\par}{\footnote Footnote text\par})"
    R"(\pard\s1 Title Here\par)"
    "\r\n"
    R"(\pard Plain {\b bold {\i both}} {\i it}\par)"
    R"(\pard caf\'e9 \u8364? {\f1 \'c0}\par)"
    R"(\pard a\tab b\line c\par)"
    R"(\pard{\pntext\'b7\tab}Item one\par)"
    R"(\pard\ls1\ilvl1 Nested\par)"
    R"(\pard {\field{\*\fldinst{HYPERLINK "https://example.org/x"}}{\fldrslt{link text}}} after\par)"
    R"(\trowd\cellx1000\cellx2000\pard\intbl A\cell B\cell\row)"
    R"(\trowd\cellx1000\cellx2000\pard\intbl C\cell D\cell\row)"
    R"(\pard Last{\*\unknown hidden}\par})";
constexpr std::string_view referenceRtfText=
    "Title Here\n\nPlain bold both it\n\ncaf\xc3\xa9 \xe2\x82\xac \xd0\x90\n\na\tb\nc\n\n- Item one\n  - Nested\n\n"
    "link text (https://example.org/x) after\n\nA\tB\nC\tD\n\nLast\n";
constexpr std::string_view referenceRtfMarkdown=
    "# Title Here\n\nPlain **bold *both*** *it*\n\ncaf\xc3\xa9 \xe2\x82\xac \xd0\x90\n\na\tb\\\nc\n\n- Item one\n  - Nested\n\n"
    "[link text](https://example.org/x) after\n\n| A | B |\n| --- | --- |\n| C | D |\n\nLast\n";

// ---- Groups ----
void ZipTests() {
    const auto reference=ReferenceDocx();
    ExpectText(DocxText(reference),referenceText,"stored reference TXT");
    auto deflated=reference;deflated.method=8;
    ExpectText(DocxText(deflated),referenceText,"fixed-Huffman deflate members");
    {
        auto members=DocxMembers(reference);
        for(auto& m:members) {m.method=8;m.payload=DeflateStored(m.data);}
        ExpectText(Convert(SourceFormat::docx,OutputFormat::text,Zip(members)),referenceText,"stored-block deflate members");
    }
    {   // Long repetitive document: matches up to 258 bytes and multi-block stored deflate.
        std::string body;
        for(int i=0;i<200;++i)body+=Paragraph(std::string(300,'z'));
        Docx d;d.body=body;d.method=8;
        const auto result=DocxText(d);
        Expect(result&&result.text.size()==200*301+199,"large deflated document");
        auto members=DocxMembers(d);
        for(auto& m:members) {m.payload=DeflateStored(m.data);}
        Expect(Convert(SourceFormat::docx,OutputFormat::text,Zip(members)).text==result.text,"multi-block stored deflate");
    }
    {   // Data descriptors (flag bit 3) with and without the optional signature.
        auto members=DocxMembers(reference);
        for(std::size_t i=0;i<members.size();++i) {members[i].flags=8;members[i].method=8;members[i].descriptorSignature=i%2==0;}
        ExpectText(Convert(SourceFormat::docx,OutputFormat::text,Zip(members)),referenceText,"data descriptors");
        members[2].descriptorCrc=0x12345678;
        ExpectError(DocxError(Zip(members)),Error::zip_malformed,"descriptor disagreeing with directory");
    }
    for(const std::string name:{"../evil.xml","/abs.xml","word\\x.xml","word/./x.xml","word//x.xml","C:/x.xml","bad\x01name","a/../b.xml","\xc3\x28.xml"}) {
        auto members=DocxMembers(reference);
        members.push_back({name,"x"});
        ExpectError(DocxError(Zip(members)),Error::zip_unsafe_name,"unsafe name "+name);
    }
    {
        auto members=DocxMembers(reference);
        members.push_back({"word/document.xml",DocumentXml(Paragraph("evil"))});
        ExpectError(DocxError(Zip(members)),Error::zip_duplicate_name,"duplicate member");
        members.back().name="WORD/Document.XML";
        ExpectError(DocxError(Zip(members)),Error::zip_duplicate_name,"case-insensitive duplicate member");
    }
    {
        auto members=DocxMembers(reference);
        members[2].crc=Crc(members[2].data)^1;
        ExpectError(DocxError(Zip(members)),Error::zip_crc_mismatch,"CRC mismatch (stored)");
        members[2].method=8;
        ExpectError(DocxError(Zip(members)),Error::zip_crc_mismatch,"CRC mismatch (deflate)");
    }
    {   // Oversized: declared expanded size above the per-entry limit.
        doc::ExtractOptions options;options.zip.maximumEntryExpandedBytes=1024;
        ExpectError(DocxError(Build(reference),options),Error::zip_size_limit,"per-entry expanded limit");
        options=doc::ExtractOptions{};options.zip.maximumEntryCompressedBytes=64;
        ExpectError(DocxError(Build(reference),options),Error::zip_size_limit,"per-entry compressed limit");
    }
    {   // Zip bomb: 1 MiB of 'A' in ~6 KiB. Declared honestly, it exceeds the total budget...
        const std::string bomb(1u<<20,'A');
        auto members=DocxMembers(reference);
        members.push_back({"word/media/bomb.xml",bomb,8});
        doc::ExtractOptions options;options.zip.maximumTotalExpandedBytes=256*1024;
        options.zip.maximumEntryExpandedBytes=2u<<20;
        // ...but it is never read: extraction only reads named parts.
        Expect(doc::ExtractDocx(Span(Zip(members)),options).error==Error::none,"unreferenced bomb is never inflated");
        // A main part that really expands beyond the total budget is rejected before inflation.
        members=DocxMembers(reference);
        members[2].data=DocumentXml(Paragraph(std::string(300000,'A')));members[2].method=8;
        ExpectError(DocxError(Zip(members),options),Error::zip_size_limit,"total expanded budget");
        // A lying header declaring 100 bytes for a 1 MiB stream stops at the declared size.
        members=DocxMembers(reference);
        members[2].method=8;members[2].payload=DeflateFixed(bomb);members[2].size=100;
        ExpectError(DocxError(Zip(members)),Error::zip_size_limit,"stream expanding past declared size");
    }
    {
        auto members=DocxMembers(reference);
        members[2].method=8;
        auto payload=DeflateFixed(members[2].data);
        members[2].payload=payload.substr(0,payload.size()/2);
        ExpectError(DocxError(Zip(members)),Error::zip_corrupt_data,"truncated deflate");
        members[2].payload=payload+"trailing";
        ExpectError(DocxError(Zip(members)),Error::zip_corrupt_data,"data after the deflate stream");
        members[2].payload=std::string("\x07",1)+payload; // BTYPE=11 reserved.
        ExpectError(DocxError(Zip(members)),Error::zip_corrupt_data,"reserved block type");
    }
    {
        doc::ExtractOptions options;options.zip.maximumEntries=3;
        ExpectError(DocxError(Build(reference),options),Error::zip_entry_limit,"entry count limit");
        auto members=DocxMembers(reference);members[0].flags=1;
        ExpectError(DocxError(Zip(members)),Error::zip_encrypted_entry,"encrypted entry flag");
        members=DocxMembers(reference);members[2].method=12;
        ExpectError(DocxError(Zip(members)),Error::zip_unsupported,"bzip2 method");
        auto zip64=Build(reference);
        zip64[zip64.size()-22+8]='\xff';zip64[zip64.size()-22+9]='\xff';
        zip64[zip64.size()-22+10]='\xff';zip64[zip64.size()-22+11]='\xff';
        ExpectError(DocxError(zip64),Error::zip_unsupported,"ZIP64 sentinel");
        ExpectError(DocxError("not a zip at all, definitely not"),Error::zip_not_archive,"not an archive");
        auto truncated=Build(reference);truncated.resize(truncated.size()-30);
        ExpectError(DocxError(truncated),Error::zip_not_archive,"truncated end record");
        auto shifted=Build(reference);
        shifted[shifted.size()-22+16]=char(shifted[shifted.size()-22+16]+1);
        ExpectError(DocxError(shifted),Error::zip_malformed,"directory offset inconsistent");
    }
}

void XmlTests() {
    Docx d;
    d.body=R"(<!-- comment --><?pi data?><w:p><w:r><w:t>&lt;a&gt; &amp; &quot;q&quot; &apos;s&apos; &#233;&#x1F600;</w:t></w:r></w:p>)"
           R"(<w:p><w:r><w:t><![CDATA[<raw> & stuff]]></w:t></w:r></w:p>)";
    ExpectText(DocxText(d),"<a> & \"q\" 's' \xc3\xa9\xf0\x9f\x98\x80\n\n<raw> & stuff\n","entities, references, CDATA");
    {
        Docx x;x.body=Paragraph("&nbsp;");
        ExpectError(DocxError(Build(x)),Error::xml_entity,"undefined entity");
        x.body=Paragraph("&#0;");
        ExpectError(DocxError(Build(x)),Error::xml_malformed,"NUL character reference");
        x.body=Paragraph("&#xD800;");
        ExpectError(DocxError(Build(x)),Error::xml_malformed,"surrogate character reference");
    }
    {
        Docx x;
        x.document="<?xml version=\"1.0\"?><!DOCTYPE w:document [<!ENTITY e SYSTEM \"file:///etc/passwd\">]>"+DocumentXml(Paragraph("&e;")).substr(38);
        ExpectError(DocxError(Build(x)),Error::xml_doctype,"DOCTYPE with external entity");
        x.document=std::nullopt;
        x.contentTypes="<!DOCTYPE Types><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>";
        ExpectError(DocxError(Build(x)),Error::xml_doctype,"DOCTYPE in content types");
    }
    {
        Docx x;x.body=Paragraph("x");
        x.document="<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>"+DocumentXml(Paragraph("x")).substr(55);
        ExpectError(DocxError(Build(x)),Error::xml_encoding,"ISO-8859-1 declaration");
        x.document=std::string("\xff\xfe<\0",4);
        ExpectError(DocxError(Build(x)),Error::xml_encoding,"UTF-16 BOM");
        x.document=DocumentXml(Paragraph("bad \xe9 byte"));
        ExpectError(DocxError(Build(x)),Error::xml_encoding,"invalid UTF-8");
        x.document="\xef\xbb\xbf"+DocumentXml(Paragraph("bom"));
        ExpectText(Convert(SourceFormat::docx,OutputFormat::text,Build(x)),"bom\n","UTF-8 BOM");
    }
    for(const std::string body:{"<w:p><w:r><w:t>x</w:r></w:t></w:p>","<w:p><q:r/></w:p>","<w:p a=\"1\" a=\"2\"/>","<w:p><w:r><w:t>x</w:t></w:r>",
            "<w:p a=\"<\"/>","<w:p><w:r><w:t>a</w:t></w:r></w:p><!-- open"}) {
        Docx x;x.body=body;
        ExpectError(DocxError(Build(x)),Error::xml_malformed,"malformed XML "+body);
    }
    {
        Docx x;x.document=DocumentXml(Paragraph("x"))+"<extra/>";
        ExpectError(DocxError(Build(x)),Error::xml_malformed,"second root element");
    }
    {   // Prefixes are resolved, not matched textually.
        Docx x;
        x.document="<doc:document xmlns:doc=\""+std::string(wNs)+"\"><doc:body><doc:p><doc:r><doc:t>prefixed</doc:t></doc:r></doc:p>"
            "<p xmlns=\""+std::string(wNs)+"\"><r><t>default</t></r></p><w:p xmlns:w=\"urn:other\"><w:r><w:t>foreign</w:t></w:r></w:p></doc:body></doc:document>";
        ExpectText(Convert(SourceFormat::docx,OutputFormat::text,Build(x)),"prefixed\n\ndefault\n","namespace resolution");
    }
    {
        Docx x;
        std::string body;
        for(int i=0;i<40;++i)body+="<w:sdt><w:sdtContent>";
        body+=Paragraph("deep");
        for(int i=0;i<40;++i)body+="</w:sdtContent></w:sdt>";
        x.body=body;
        ExpectText(DocxText(x),"deep\n","nested content controls");
        doc::ExtractOptions options;options.xml.maximumDepth=32;
        ExpectError(DocxError(Build(x),options),Error::xml_limit,"XML depth limit");
        options=doc::ExtractOptions{};options.xml.maximumAttributes=2;
        ExpectError(DocxError(Build(x),options),Error::xml_limit,"XML attribute limit");
        options=doc::ExtractOptions{};options.xml.maximumTokenBytes=16;
        x.body=Paragraph(std::string(64,'t'));
        ExpectError(DocxError(Build(x),options),Error::xml_limit,"XML token limit");
        options=doc::ExtractOptions{};options.xml.maximumNodes=5;
        ExpectError(DocxError(Build(x),options),Error::xml_limit,"XML node limit");
    }
}

void DocxTests() {
    const auto reference=ReferenceDocx();
    ExpectText(DocxText(reference),referenceText,"reference TXT");
    ExpectText(DocxMarkdown(reference),referenceMarkdown,"reference Markdown");
    {   // xml:space: without preserve, edge whitespace of w:t is not content.
        Docx d;
        d.body=R"(<w:p><w:r><w:t> trimmed </w:t></w:r><w:r><w:t xml:space="preserve"> kept </w:t></w:r><w:r><w:t>end</w:t></w:r></w:p>)"
               R"(<w:p xml:space="preserve"><w:r><w:t> inherited</w:t></w:r></w:p>)";
        ExpectText(DocxText(d),"trimmed kept end\n\n inherited\n","xml:space handling");
    }
    {   // Headings without styles.xml use the built-in style identifiers.
        Docx d;
        d.body=R"(<w:p><w:pPr><w:pStyle w:val="Heading2"/></w:pPr><w:r><w:t>Two</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:pStyle w:val="heading6"/></w:pPr><w:r><w:t>Six</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:outlineLvl w:val="2"/></w:pPr><w:r><w:t>Outline</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:pStyle w:val="Heading7"/></w:pPr><w:r><w:t>Seven</w:t></w:r></w:p>)";
        ExpectText(DocxMarkdown(d),"## Two\n\n###### Six\n\n### Outline\n\nSeven\n","heading fallbacks");
    }
    {   // Numbering without numbering.xml degrades to bullets; numId 0 removes numbering.
        Docx d;
        d.body=R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="4"/></w:numPr></w:pPr><w:r><w:t>a</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="2"/><w:numId w:val="4"/></w:numPr></w:pPr><w:r><w:t>deep</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="0"/></w:numPr></w:pPr><w:r><w:t>plain</w:t></w:r></w:p>)";
        ExpectText(DocxText(d),"- a\n    - deep\n\nplain\n","bullets without numbering part");
        ExpectText(DocxMarkdown(d),"- a\n  - deep\n\nplain\n","Markdown clamps skipped list levels");
    }
    {   // Numbering inherited from a list style, with a level override.
        Docx d;
        d.styles=R"(<w:style w:type="paragraph" w:styleId="ListNumber"><w:name w:val="List Number"/><w:pPr><w:numPr><w:numId w:val="7"/></w:numPr></w:pPr></w:style>)";
        d.numbering=R"(<w:abstractNum w:abstractNumId="3"><w:lvl w:ilvl="0"><w:numFmt w:val="lowerLetter"/></w:lvl></w:abstractNum>)"
                    R"(<w:num w:numId="7"><w:abstractNumId w:val="3"/></w:num>)"
                    R"(<w:num w:numId="8"><w:abstractNumId w:val="3"/><w:lvlOverride w:ilvl="0"><w:lvl w:ilvl="0"><w:numFmt w:val="bullet"/></w:lvl></w:lvlOverride></w:num>)";
        d.body=R"(<w:p><w:pPr><w:pStyle w:val="ListNumber"/></w:pPr><w:r><w:t>first</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:pStyle w:val="ListNumber"/></w:pPr><w:r><w:t>second</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="8"/></w:numPr></w:pPr><w:r><w:t>overridden</w:t></w:r></w:p>)";
        ExpectText(DocxMarkdown(d),"1. first\n2. second\n- overridden\n","style numbering and level override");
    }
    {   // Tables: merged cells make rows ragged, so Markdown uses paragraphs.
        Docx d;
        d.body=R"(<w:tbl><w:tr><w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:r><w:t>wide</w:t></w:r></w:p></w:tc></w:tr>)"
               R"(<w:tr><w:tc><w:p><w:r><w:t>a</w:t></w:r></w:p><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>c</w:t><w:tab/><w:t>d</w:t></w:r></w:p>)"
               R"(<w:tbl><w:tr><w:tc><w:p><w:r><w:t>inner</w:t></w:r></w:p></w:tc></w:tr></w:tbl></w:tc></w:tr></w:tbl>)";
        ExpectText(DocxText(d),"wide\na b\tc d inner\n","table TXT with nested table and cell tab");
        ExpectText(DocxMarkdown(d),"wide\n\na b \\| c d inner\n","non-rectangular table Markdown");
    }
    {   // Hyperlinks: relationship, simple field, complex field; unsafe schemes stay text.
        Docx d;
        d.relationships=Link("rId5","javascript:alert(1)")+Link("rId6","mailto:a@example.com");
        d.body=R"(<w:p><w:hyperlink r:id="rId5"><w:r><w:t>bad</w:t></w:r></w:hyperlink><w:r><w:t xml:space="preserve"> </w:t></w:r>)"
               R"(<w:hyperlink r:id="rId6"><w:r><w:t>a@example.com</w:t></w:r></w:hyperlink><w:r><w:t xml:space="preserve"> </w:t></w:r>)"
               R"(<w:hyperlink w:anchor="_Toc1"><w:r><w:t>anchor</w:t></w:r></w:hyperlink><w:r><w:t xml:space="preserve"> </w:t></w:r>)"
               R"(<w:fldSimple w:instr=" HYPERLINK &quot;http://x.test/a b(c)&quot; \o &quot;tip&quot; "><w:r><w:t>simple</w:t></w:r></w:fldSimple></w:p>)"
               R"(<w:p><w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> HYPERLINK "https://complex.test/" </w:instrText></w:r>)"
               R"(<w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:rPr><w:i/></w:rPr><w:t>complex</w:t></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r>)"
               R"(<w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText> PAGE </w:instrText></w:r>)"
               R"(<w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:t>7</w:t></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r></w:p>)";
        ExpectText(DocxMarkdown(d),"bad [a@example.com](mailto:a@example.com) anchor [simple](http://x.test/a%20b%28c%29)\n\n"
            "[*complex*](https://complex.test/) and 7\n","hyperlink forms");
        ExpectText(DocxText(d),"bad a@example.com anchor simple (http://x.test/a b(c))\n\ncomplex (https://complex.test/) and 7\n","hyperlink TXT");
    }
    {   // Tracked changes and excluded stories.
        Docx d;
        d.body=R"(<w:p><w:r><w:t xml:space="preserve">keep </w:t></w:r><w:del w:id="1"><w:r><w:delText>gone </w:delText></w:r></w:del>)"
               R"(<w:ins w:id="2"><w:r><w:t xml:space="preserve">added </w:t></w:r></w:ins><w:moveFrom><w:r><w:t>moved-away</w:t></w:r></w:moveFrom>)"
               R"(<w:r><w:footnoteReference w:id="1"/><w:t>end</w:t></w:r>)"
               R"(<w:r><w:drawing><wp:inline xmlns:wp="urn:wp"><w:txbxContent><w:p><w:r><w:t>textbox</w:t></w:r></w:p></w:txbxContent></wp:inline></w:drawing></w:r></w:p>)"
               R"(<w:del w:id="3"><w:p><w:r><w:t>deleted paragraph</w:t></w:r></w:p></w:del>)";
        ExpectText(DocxText(d),"keep added end\n","tracked changes, notes and text boxes");
    }
    {   // Line breaks of every form, empty paragraphs and Unicode.
        Docx d;
        d.body="<w:p/><w:p><w:r><w:t>\xe4\xb8\xad\xe6\x96\x87</w:t><w:cr/><w:t>&#x10348;</w:t><w:br w:type=\"page\"/><w:t>x</w:t><w:noBreakHyphen/><w:t>y</w:t></w:r></w:p><w:p></w:p>";
        ExpectText(DocxText(d),"\xe4\xb8\xad\xe6\x96\x87\n\xf0\x90\x8d\x88\nx\xe2\x80\x91y\n","breaks and Unicode");
    }
    {   // Encrypted and macro-enabled packages, and non-documents.
        std::string cfb("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1",8);
        cfb.resize(4096,'\0');
        ExpectError(DocxError(cfb),Error::docx_encrypted,"OLE compound encrypted package");
        ExpectError(DocxError(Zip({{"EncryptionInfo","x"},{"EncryptedPackage","y"}})),Error::docx_encrypted,"EncryptedPackage members");
        auto macro=ReferenceDocx();macro.mainType="application/vnd.ms-word.document.macroEnabled.main+xml";
        ExpectError(DocxError(Build(macro)),Error::docx_macro_enabled,"docm main part");
        auto vba=ReferenceDocx();vba.extra.push_back({"word/vbaProject.bin","binary"});
        ExpectError(DocxError(Build(vba)),Error::docx_macro_enabled,"VBA project part");
        auto missing=ReferenceDocx();missing.includeDocument=false;
        ExpectError(DocxError(Build(missing)),Error::docx_missing_part,"missing main document part");
        auto sheet=ReferenceDocx();sheet.mainType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
        ExpectError(DocxError(Build(sheet)),Error::docx_not_wordprocessing,"spreadsheet main part");
        ExpectError(DocxError(Zip({{"[Content_Types].xml","<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"},
            {"_rels/.rels","<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>"}})),
            Error::docx_not_wordprocessing,"no officeDocument relationship");
        ExpectError(DocxError(Zip({{"word/document.xml",DocumentXml(Paragraph("x"))}})),Error::docx_invalid_package,"no content types");
        auto badRels=ReferenceDocx();
        badRels.relationships+=Link("rIdLink","https://dup.test/");
        ExpectError(DocxError(Build(badRels)),Error::docx_invalid_package,"duplicate relationship id");
        auto escape=ReferenceDocx();
        escape.styles.clear();escape.numbering.clear();
        escape.relationships="<Relationship Id=\"rS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"../../../etc/styles.xml\"/>";
        ExpectError(DocxError(Build(escape)),Error::docx_invalid_package,"relationship escaping the package root");
        Docx empty;
        const auto result=DocxText(empty);
        Expect(result&&result.text.empty(),"genuinely empty document is an empty success");
    }
}

void RtfTests() {
    ExpectText(RtfText(referenceRtf),referenceRtfText,"reference RTF TXT");
    ExpectText(RtfMarkdown(referenceRtf),referenceRtfMarkdown,"reference RTF Markdown");
    // Nested groups restore formatting and Unicode skip counts.
    ExpectText(RtfMarkdown(R"({\rtf1 a{\b b{\i c}d}e\par})"),"a**b*c*d**e\n","nested group formatting");
    ExpectText(RtfText(R"({\rtf1\uc2 x\u233\'e9\'e9y{\uc0\u8364}\u8364??z\par})"),"x\xc3\xa9y\xe2\x82\xac\xe2\x82\xacz\n","\\uc skip counts");
    ExpectText(RtfText(R"({\rtf1 \u-10179?\u-8704?\par})"),"\xf0\x9f\x98\x80\n","surrogate pair via \\u");
    ExpectText(RtfText(R"({\rtf1 \u-10179?x\par})"),"\xef\xbf\xbdx\n","lone high surrogate");
    ExpectText(RtfText(R"({\rtf1 \u65?\u66 B\par})"),"AB\n","\\u fallback consumes one character");
    // Code pages.
    ExpectText(RtfText(R"({\rtf1\ansi\'80\'93q\'94\par})"),"\xe2\x82\xac\xe2\x80\x9cq\xe2\x80\x9d\n","Windows-1252");
    ExpectText(RtfText(R"({\rtf1\ansi\ansicpg1251 \'cf\'f0\'e8\par})"),"\xd0\x9f\xd1\x80\xd0\xb8\n","Windows-1251 document code page");
    ExpectText(RtfText(R"({\rtf1\ansi{\fonttbl{\f0 A;}{\f1\fcharset238 B;}{\f2\fcharset161 C;}{\f3\fcharset162 D;}{\f4\fcharset186 E;}})"
        R"(\f1\'8a\f2\'e1\f3\'fe\f4\'e0\f0\'e0\par})"),"\xc5\xa0\xce\xb1\xc5\x9f\xc4\x85\xc3\xa0\n","font charsets 1250/1253/1254/1257/1252");
    ExpectError(RtfError(R"({\rtf1\ansi\ansicpg932 \'82\'a0\par})"),Error::rtf_unsupported_code_page,"Shift-JIS bytes");
    ExpectText(RtfText(R"({\rtf1\ansi\ansicpg932\uc2 ascii \u12354\'82\'a0\par})"),"ascii \xe3\x81\x82\n","unsupported code page only as \\u fallback");
    ExpectError(RtfError(R"({\rtf1\mac \'8a\par})"),Error::rtf_unsupported_code_page,"Mac Roman bytes");
    // \bin payloads may contain braces and backslashes and are never decoded.
    {
        std::string s=R"({\rtf1 a{\*\blipuid x}{\pict\bin6 )";
        s+=std::string("}{\\\x00\xff",6);
        s+=R"(}b{\object{\objdata 0102}{\result c}}\bin3 }}\d\par})";
        ExpectText(RtfText(s),"abd\n","\\bin skipping");
        ExpectError(RtfError(R"({\rtf1 {\pict\bin99 x}})"),Error::rtf_malformed,"\\bin beyond end");
    }
    // Hidden text, special characters, control symbols.
    ExpectText(RtfText(R"({\rtf1 a{\v hidden}b\~c\_d\-e\emdash\bullet\{\}\\\lquote q\rquote\par})"),
        "ab\xc2\xa0" "c\xe2\x80\x91" "de\xe2\x80\x94\xe2\x80\xa2{}\\\xe2\x80\x98q\xe2\x80\x99\n","symbols and hidden text");
    ExpectText(RtfText("{\\rtf1 one\\\r\ntwo\\page three\\sect four}"),"one\n\ntwo\n\nthree\n\nfour\n","paragraph breaks");
    // Tables: ragged rows degrade in Markdown; paragraphs inside cells join.
    ExpectText(RtfMarkdown(R"({\rtf1\trowd\cellx1\cellx2\intbl a\par b\cell c\cell\row\trowd\cellx1\intbl wide\cell\row\pard after\par})"),
        "a b \\| c\n\nwide\n\nafter\n","RTF ragged table");
    ExpectText(RtfText(R"({\rtf1\trowd\intbl x\cell\cell y\cell\row})"),"x\t\ty\n","RTF empty cell and unterminated document table");
    // Headings via \outlinelevel.
    ExpectText(RtfMarkdown(R"({\rtf1\pard\outlinelevel2 Third\par\pard body\par})"),"### Third\n\nbody\n","\\outlinelevel heading");
    // Malformed inputs.
    ExpectError(RtfError("plain text"),Error::rtf_not_rtf,"not RTF");
    ExpectError(RtfError(R"({\rtf1 unclosed)"),Error::rtf_malformed,"missing closing brace");
    ExpectError(RtfError(R"({\rtf1 x}})"),Error::rtf_malformed,"extra closing brace");
    ExpectError(RtfError(R"({\rtf1 x} trailing)"),Error::rtf_malformed,"text after the document");
    ExpectError(RtfError(R"({\rtf1 \'zz})"),Error::rtf_malformed,"bad hex escape");
    ExpectError(RtfError(R"({\rtf1 \u})"),Error::rtf_malformed,"\\u without parameter");
    ExpectError(RtfError(R"({\rtf1 \b12345678901})"),Error::rtf_malformed,"oversized parameter");
    ExpectError(RtfError("{\\rtf1 \\"),Error::rtf_malformed,"dangling backslash");
    ExpectError(RtfError(R"({\rtf1 \abcdefghijklmnopqrstuvwxyzabcdefgh x})"),Error::rtf_limit,"control word length limit");
    Expect(RtfText("{\\rtf1 ok}\r\n\0").text=="ok\n","trailing whitespace allowed");
}

void MarkdownTests() {
    {
        Docx d;
        d.body=Paragraph(R"(*not* _x_ [l](y) &lt;b&gt;html&lt;/b&gt; # h | p `c` &amp;amp; \ ~s~ !)")
              +Paragraph("- dash")+Paragraph("+ plus")+Paragraph("12. num")+Paragraph("3) paren")+Paragraph("&gt; quote")
              +Paragraph("===")+R"(<w:p><w:r><w:t xml:space="preserve">    indented code?</w:t></w:r></w:p>)"
              +Paragraph("trailing spaces")+"<w:p><w:r><w:t xml:space=\"preserve\">a  </w:t><w:br/><w:t>-b</w:t></w:r></w:p>";
        ExpectText(DocxMarkdown(d),
            "\\*not\\* \\_x\\_ \\[l\\](y) \\<b\\>html\\</b\\> \\# h \\| p \\`c\\` \\&amp; \\\\ \\~s\\~ !\n\n"
            "\\- dash\n\n\\+ plus\n\n12\\. num\n\n3\\) paren\n\n\\> quote\n\n\\===\n\nindented code?\n\ntrailing spaces\n\na\\\n\\-b\n",
            "Markdown escaping");
    }
    {   // Emphasis transitions stay properly nested and flanking.
        Docx d;
        d.body=R"(<w:p><w:r><w:rPr><w:b/></w:rPr><w:t xml:space="preserve">bold </w:t></w:r><w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>both</w:t></w:r>)"
               R"(<w:r><w:rPr><w:i/></w:rPr><w:t xml:space="preserve"> ital </w:t></w:r><w:r><w:rPr><w:b w:val="0"/></w:rPr><w:t>plain</w:t></w:r></w:p>)"
               R"(<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Head</w:t><w:br/><w:t>#line</w:t></w:r></w:p>)";
        ExpectText(DocxMarkdown(d),"**bold *both*** *ital* plain\n\n# Head \\#line\n","emphasis and headings");
    }
    {   // Ordered numbering restarts after an interrupting paragraph; deep nesting indents by content column.
        doc::Document document;
        const auto item=[&](std::uint8_t level,bool ordered,std::string text) {
            doc::Block b;b.kind=doc::BlockKind::list_item;b.level=level;b.ordered=ordered;b.runs.push_back({std::move(text),{},false,false});
            document.blocks.push_back(std::move(b));
        };
        for(int i=0;i<10;++i)item(0,true,"n"+std::to_string(i));
        item(1,true,"child");item(2,false,"grand");item(1,true,"child2");
        doc::Block p;p.runs.push_back({"para",{},false,false});document.blocks.push_back(p);
        item(0,true,"again");
        const auto md=doc::SerializeMarkdown(document);
        Expect(md&&md.text.find("10. n9\n    1. child\n       - grand\n    2. child2\n\npara\n\n1. again\n")!=std::string::npos,"ordered list numbering and indentation");
        const auto txt=doc::SerializeText(document);
        Expect(txt&&txt.text.find("10. n9\n  1. child\n    - grand\n  2. child2\n\npara\n\n1. again\n")!=std::string::npos,"TXT list numbering");
    }
    {   // Links: only http/https/mailto become links; destinations are percent-encoded; never HTML.
        doc::Document document;
        doc::Block b;
        for(const auto& [text,link]:std::vector<std::pair<std::string,std::string>>{
                {"a","https://e.test/x y)"},{" ",""},{"b","javascript:alert(1)"},{" ",""},{"c","file:///etc/passwd"},
                {" ",""},{"d","HTTP://UP.test/<script>"},{" ",""},{"e","https://"},{" ",""},{"","https://empty.test/"}}) {
            b.runs.push_back({text,link,false,false});
        }
        document.blocks.push_back(b);
        ExpectText(doc::SerializeMarkdown(document),"[a](https://e.test/x%20y%29) b c [d](HTTP://UP.test/%3Cscript%3E) e\n","link scheme policy");
    }
    {   // Pipe tables escape cell content and flatten breaks.
        doc::Document document;
        doc::Block t;t.kind=doc::BlockKind::table;
        t.rows={{{{"a|b\nc",{},false,false}},{{"- x",{},true,false}}},{{},{{"<i>",{},false,false}}}};
        document.blocks.push_back(t);
        ExpectText(doc::SerializeMarkdown(document),"| a\\|b c | **- x** |\n| --- | --- |\n|  | \\<i\\> |\n","pipe table escaping");
        ExpectText(doc::SerializeText(document),"a|b c\t- x\n\t<i>\n","TXT table");
    }
}

void LimitTests() {
    const auto docx=Build(ReferenceDocx());
    doc::ConvertOptions options;
    options.extract.maximumSourceBytes=100;
    ExpectError(Convert(SourceFormat::docx,OutputFormat::text,docx,options).error,Error::source_limit,"source limit");
    options=doc::ConvertOptions{};options.extract.maximumTextBytes=10;
    ExpectError(Convert(SourceFormat::docx,OutputFormat::text,docx,options).error,Error::text_limit,"DOCX text limit");
    ExpectError(Convert(SourceFormat::rtf,OutputFormat::text,referenceRtf,options).error,Error::text_limit,"RTF text limit");
    options=doc::ConvertOptions{};options.extract.maximumBlocks=3;
    ExpectError(Convert(SourceFormat::docx,OutputFormat::text,docx,options).error,Error::text_limit,"block limit");
    options=doc::ConvertOptions{};options.serialize.maximumOutputBytes=20;
    ExpectError(Convert(SourceFormat::docx,OutputFormat::markdown,docx,options).error,Error::output_limit,"output limit");
    options=doc::ConvertOptions{};options.serialize.maximumOutputBytes=referenceText.size();
    ExpectText(Convert(SourceFormat::docx,OutputFormat::text,docx,options),referenceText,"output exactly at limit");
    options=doc::ConvertOptions{};options.extract.zip.maximumEntryExpandedBytes=1ull<<40;
    ExpectError(Convert(SourceFormat::docx,OutputFormat::text,docx,options).error,Error::invalid_options,"inflater-incompatible limit");
    ExpectError(doc::Convert(SourceFormat(9),OutputFormat::text,{}).error,Error::invalid_options,"unknown source format");
    {
        std::string deep="{\\rtf1 ";
        for(int i=0;i<300;++i)deep+="{";
        deep+="x";
        for(int i=0;i<300;++i)deep+="}";
        deep+="}";
        ExpectError(RtfError(deep),Error::rtf_limit,"RTF depth limit");
        doc::ExtractOptions extract;extract.rtf.maximumDepth=400;
        Expect(doc::ExtractRtf(Span(deep),extract).error==Error::none,"RTF depth within raised limit");
        extract=doc::ExtractOptions{};extract.rtf.maximumTokens=50;
        ExpectError(RtfError(referenceRtf,extract),Error::rtf_limit,"RTF token limit");
    }
    {   // A 2 MiB RTF of repeated \u escapes stays within budgets and is linear.
        std::string big="{\\rtf1 ";
        for(int i=0;i<100000;++i)big+="\\u8364?";
        big+="}";
        const auto result=RtfText(big);
        Expect(result&&result.text.size()==300001,"large RTF");
    }
}

void DeterminismTests() {
    const auto docx=Build(ReferenceDocx());
    for(const auto format:{OutputFormat::text,OutputFormat::markdown}) {
        const auto a=Convert(SourceFormat::docx,format,docx),b=Convert(SourceFormat::docx,format,docx);
        Expect(a&&b&&a.text==b.text,"repeatable DOCX conversion");
        const auto extracted=doc::ExtractDocx(Span(docx));
        const auto direct=format==OutputFormat::text?doc::SerializeText(extracted.document):doc::SerializeMarkdown(extracted.document);
        Expect(direct.text==a.text,"Convert equals Extract+Serialize");
        const auto r1=Convert(SourceFormat::rtf,format,referenceRtf),r2=Convert(SourceFormat::rtf,format,referenceRtf);
        Expect(r1&&r2&&r1.text==r2.text,"repeatable RTF conversion");
        Expect(!a.text.empty()&&a.text.back()=='\n'&&a.text.find('\r')==std::string::npos,"LF line endings");
    }
    Expect(doc::MediaType(OutputFormat::text)=="text/plain; charset=utf-8","TXT media type");
    Expect(doc::MediaType(OutputFormat::markdown)=="text/markdown; charset=utf-8","Markdown media type");
    for(std::uint32_t e=0;e<=std::uint32_t(Error::rtf_limit);++e) {
        Expect(doc::ErrorCode(Error(e))!="unknown"&&doc::ErrorMessage(Error(e)).find("Unknown")==std::string_view::npos,"error text "+std::to_string(e));
    }
    // Failures never return partial documents.
    auto broken=Build(ReferenceDocx());
    broken[40]^=0x55;
    const auto failed=doc::ExtractDocx(Span(broken));
    Expect(!failed&&failed.document.blocks.empty(),"failure carries no partial document");
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
// Seeded byte mutations of the reference inputs: every outcome must be a typed
// error or valid UTF-8 output, without crashes, hangs or sanitizer reports.
long mutationRounds=1500; // Per seed; `mutation N` runs a longer local campaign.
void MutationTests() {
    std::uint64_t state=0x9e3779b97f4a7c15ull;
    const auto next=[&] {state^=state<<13;state^=state>>7;state^=state<<17;return state;};
    auto deflated=ReferenceDocx();deflated.method=8;
    const std::vector<std::pair<SourceFormat,std::string>> seeds={
        {SourceFormat::docx,Build(ReferenceDocx())},{SourceFormat::docx,Build(deflated)},{SourceFormat::rtf,referenceRtf}};
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
                    default: bytes[at]="{}\\<>&;'\"\x80\xff"[next()%11];break;
                }
            }
            for(const auto output:{OutputFormat::text,OutputFormat::markdown}) {
                const auto result=Convert(format,output,bytes);
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
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--version") {std::cout<<doc::revision<<"\n";return 0;}
    const std::map<std::string,std::function<void()>> groups={
        {"zip",ZipTests},{"xml",XmlTests},{"docx",DocxTests},{"rtf",RtfTests},
        {"markdown",MarkdownTests},{"limits",LimitTests},{"determinism",DeterminismTests},{"mutation",MutationTests}};
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
