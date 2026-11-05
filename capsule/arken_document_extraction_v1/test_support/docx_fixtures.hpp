#pragma once
// Test-only DOCX authoring for consumers of arken_document_extraction_v1 (the
// conversion owner and the standalone MCP proof). It writes a minimal stored
// (method 0) ZIP with the three parts a WordprocessingML package needs. It is
// independent of the capsule's reader and is never linked into a product.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace docx_fixture {
inline std::uint32_t Crc32(std::string_view data) {
    std::uint32_t c=0xffffffffu;
    for(const char ch:data) {
        c^=std::uint8_t(ch);
        for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));
    }
    return ~c;
}
inline void Le16(std::string& out,std::uint32_t v) {out.push_back(char(v&0xff));out.push_back(char((v>>8)&0xff));}
inline void Le32(std::string& out,std::uint32_t v) {Le16(out,v&0xffff);Le16(out,v>>16);}
// Stored ZIP of (name, data) members in the given order; no data descriptors, no extras.
inline std::string StoredZip(const std::vector<std::pair<std::string,std::string>>& members) {
    std::string out,central;
    for(const auto& [name,data]:members) {
        const auto crc=Crc32(data);const auto size=std::uint32_t(data.size());const auto offset=std::uint32_t(out.size());
        Le32(out,0x04034b50);Le16(out,20);Le16(out,0);Le16(out,0);Le16(out,0);Le16(out,0x21);
        Le32(out,crc);Le32(out,size);Le32(out,size);Le16(out,std::uint32_t(name.size()));Le16(out,0);
        out+=name;out+=data;
        Le32(central,0x02014b50);Le16(central,20);Le16(central,20);Le16(central,0);Le16(central,0);
        Le16(central,0);Le16(central,0x21);Le32(central,crc);Le32(central,size);Le32(central,size);
        Le16(central,std::uint32_t(name.size()));Le16(central,0);Le16(central,0);Le16(central,0);Le16(central,0);
        Le32(central,0);Le32(central,offset);central+=name;
    }
    const auto centralOffset=std::uint32_t(out.size());
    out+=central;
    Le32(out,0x06054b50);Le16(out,0);Le16(out,0);
    Le16(out,std::uint32_t(members.size()));Le16(out,std::uint32_t(members.size()));
    Le32(out,std::uint32_t(central.size()));Le32(out,centralOffset);Le16(out,0);
    return out;
}
inline constexpr std::string_view wordNamespace="http://schemas.openxmlformats.org/wordprocessingml/2006/main";
// A package whose main story is `body` (WordprocessingML inside w:body). Built-in
// heading style IDs (Heading1..6, Title) work without a styles part.
inline std::string Docx(std::string_view body,std::vector<std::pair<std::string,std::string>> extra={}) {
    std::vector<std::pair<std::string,std::string>> members{
        {"[Content_Types].xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
         "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
         "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>"},
        {"_rels/.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>"},
        {"word/document.xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\""+std::string(wordNamespace)
         +"\"><w:body>"+std::string(body)+"<w:sectPr/></w:body></w:document>"}};
    for(auto& member:extra)members.push_back(std::move(member));
    return StoredZip(members);
}
// The ZIP form of a password-protected Office file: EncryptionInfo + EncryptedPackage.
inline std::string EncryptedDocx() {return StoredZip({{"EncryptionInfo","x"},{"EncryptedPackage","y"}});}
inline std::vector<std::byte> Bytes(std::string_view text) {
    const auto* begin=reinterpret_cast<const std::byte*>(text.data());
    return {begin,begin+text.size()};
}
}
