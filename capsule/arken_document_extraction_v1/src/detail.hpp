#pragma once
#include "../api.hpp"
#include <new>
#include <utility>

namespace arken::document::v1::detail {
using Bytes=std::span<const std::byte>;

// Internal failures unwind to the noexcept public boundary, which reports the code.
struct Failure {Error error;};
[[noreturn]] inline void Fail(Error error) {throw Failure{error};}

template<class F> Error Guard(F&& body) noexcept {
    try {body();return Error::none;}
    catch(const Failure& failure) {return failure.error;}
    catch(const std::bad_alloc&) {return Error::allocation_failed;}
    catch(...) {return Error::allocation_failed;}
}

inline std::uint8_t U8(Bytes b,std::size_t p) {return std::to_integer<std::uint8_t>(b[p]);}
inline std::uint16_t U16(Bytes b,std::size_t p) {return std::uint16_t(U8(b,p)|(U8(b,p+1)<<8));}
inline std::uint32_t U32(Bytes b,std::size_t p) {return std::uint32_t(U16(b,p))|(std::uint32_t(U16(b,p+2))<<16);}

inline void AppendUtf8(std::string& out,char32_t c) {
    if(c<0x80) {out.push_back(char(c));}
    else if(c<0x800) {out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&0x3f)));}
    else if(c<0x10000) {
        out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&0x3f)));out.push_back(char(0x80|(c&0x3f)));
    } else {
        out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&0x3f)));
        out.push_back(char(0x80|((c>>6)&0x3f)));out.push_back(char(0x80|(c&0x3f)));
    }
}
// Decodes one strict UTF-8 scalar (no overlongs, surrogates or values above
// U+10FFFF). Returns its length, or 0 when the sequence is invalid.
inline std::size_t DecodeUtf8(std::string_view s,std::size_t p,char32_t& c) {
    const auto b0=std::uint8_t(s[p]);
    if(b0<0x80) {c=b0;return 1;}
    std::size_t n=0;char32_t min=0;
    if(b0>=0xc2&&b0<=0xdf) {n=2;c=b0&0x1f;min=0x80;}
    else if(b0>=0xe0&&b0<=0xef) {n=3;c=b0&0x0f;min=0x800;}
    else if(b0>=0xf0&&b0<=0xf4) {n=4;c=b0&0x07;min=0x10000;}
    else return 0;
    if(s.size()-p<n)return 0;
    for(std::size_t i=1;i<n;++i) {
        const auto b=std::uint8_t(s[p+i]);
        if((b&0xc0)!=0x80)return 0;
        c=(c<<6)|(b&0x3f);
    }
    if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0;
    return n;
}
inline bool ValidUtf8(std::string_view s) {
    char32_t c=0;
    for(std::size_t p=0;p<s.size();) {const auto n=DecodeUtf8(s,p,c);if(!n)return false;p+=n;}
    return true;
}
inline char AsciiLower(char c) {return c>='A'&&c<='Z'?char(c-'A'+'a'):c;}
inline std::string AsciiLower(std::string_view s) {
    std::string out(s);
    for(auto& c:out)c=AsciiLower(c);
    return out;
}
inline bool EqualsNoCase(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(AsciiLower(a[i])!=AsciiLower(b[i]))return false;
    return true;
}

// Shared extraction budget so DOCX and RTF enforce the same model bounds.
class ModelBuilder {
public:
    ModelBuilder(Document& document,const ExtractOptions& options):document_(document),options_(options) {}
    // Appends text to a run list, merging with the previous run when the
    // formatting and link are identical. Empty text is ignored.
    void Append(std::vector<Run>& runs,std::string_view text,bool bold,bool italic,std::string_view link) {
        if(text.empty())return;
        Charge(text.size());
        if(!runs.empty()) {
            auto& last=runs.back();
            if(last.bold==bold&&last.italic==italic&&last.link==link) {last.text.append(text);return;}
        }
        Charge(link.size());
        runs.push_back(Run{std::string(text),std::string(link),bold,italic});
    }
    // Joins paragraph content inside one table cell with a single space.
    void AppendToCell(Cell& cell,std::vector<Run>&& runs) {
        if(!HasText(runs))return;
        if(HasText(cell))Append(cell," ",false,false,{});
        for(auto& run:runs)Append(cell,run.text,run.bold,run.italic,run.link);
    }
    // Whitespace-only paragraphs are layout, not content: they are dropped so
    // serializers keep exactly one blank line between blocks.
    void Push(Block&& block) {
        if(block.kind==BlockKind::table) {
            if(block.rows.empty())return;
        } else if(block.kind==BlockKind::thematic_break) {
            block.runs.clear();
        } else if(!HasText(block.runs)) {
            return;
        }
        if(++blocks_>options_.maximumBlocks)Fail(Error::text_limit);
        Charge(block.language.size());
        document_.blocks.push_back(std::move(block));
    }
    static bool HasText(const std::vector<Run>& runs) {
        for(const auto& run:runs)
            for(const char c:run.text)if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r')return true;
        return false;
    }
private:
    void Charge(std::uint64_t bytes) {
        if(bytes>options_.maximumTextBytes-text_)Fail(Error::text_limit);
        text_+=bytes;
    }
    Document& document_;
    const ExtractOptions& options_;
    std::uint64_t text_=0,blocks_=0;
};

// HYPERLINK field instruction (Word fields in both DOCX and RTF):
// HYPERLINK "target" [\l "anchor"] [\o "tooltip"] [\t frame] [\m] [\n].
// Returns the external target, or empty when the field is not a hyperlink or
// only references a bookmark.
inline std::string HyperlinkFieldTarget(std::string_view instruction) {
    std::vector<std::string> tokens;
    for(std::size_t p=0;p<instruction.size()&&tokens.size()<16;) {
        const char c=instruction[p];
        if(c==' '||c=='\t'||c=='\n'||c=='\r') {++p;continue;}
        std::string token;
        if(c=='"') {
            ++p;
            while(p<instruction.size()&&instruction[p]!='"') {
                if(instruction[p]=='\\'&&p+1<instruction.size()&&(instruction[p+1]=='"'||instruction[p+1]=='\\'))++p;
                token.push_back(instruction[p++]);
            }
            ++p;
        } else {
            while(p<instruction.size()&&instruction[p]!=' '&&instruction[p]!='\t'&&instruction[p]!='\n'&&instruction[p]!='\r'&&instruction[p]!='"')
                token.push_back(instruction[p++]);
        }
        tokens.push_back(std::move(token));
    }
    if(tokens.empty()||!EqualsNoCase(tokens[0],"HYPERLINK"))return {};
    for(std::size_t i=1;i<tokens.size();++i) {
        const auto& token=tokens[i];
        if(token.size()==2&&token[0]=='\\') {
            const char s=AsciiLower(token[1]);
            if(s=='l'||s=='o'||s=='t')++i; // Switches with an argument.
            continue;
        }
        return token;
    }
    return {};
}

// ZIP (zip.cpp).
struct ZipEntry {
    std::string name;
    std::uint16_t flags=0,method=0;
    std::uint32_t crc=0;
    std::uint64_t compressed=0,expanded=0,localOffset=0,dataOffset=0;
    bool directory=false;
};
class ZipArchive {
public:
    ZipArchive(Bytes bytes,const ZipLimits& limits);
    [[nodiscard]] const ZipEntry* Find(std::string_view name) const;
    [[nodiscard]] std::string Read(const ZipEntry& entry);
    [[nodiscard]] const std::vector<ZipEntry>& Entries() const noexcept {return entries_;}
private:
    Bytes bytes_;
    ZipLimits limits_;
    std::vector<ZipEntry> entries_;
    std::uint64_t expanded_=0;
};
enum class InflateStatus {ok,corrupt,overflow};
// Raw deflate (RFC 1951, no zlib header) into exactly `expected` bytes. The
// stream must end inside the last input byte and produce exactly that many bytes.
[[nodiscard]] InflateStatus InflateRaw(Bytes input,std::size_t expected,std::string& output);
[[nodiscard]] std::uint32_t Crc32(std::string_view bytes) noexcept;

// XML (xml.cpp). Namespace-resolved, DOM-shaped, bounded.
struct XmlAttribute {
    std::uint32_t ns=0; // Index into XmlDocument::namespaces; 0 is "no namespace".
    std::string local;
    std::string value;
};
struct XmlNode {
    bool text=false;
    std::uint32_t index=0;
};
struct XmlElement {
    std::uint32_t ns=0;
    std::string local;
    std::uint32_t parent=0xffffffffu;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
};
struct XmlDocument {
    std::vector<std::string> namespaces;
    std::vector<XmlElement> elements; // elements[0] is the root.
    std::vector<std::string> texts;
    [[nodiscard]] const XmlElement& Root() const {return elements.front();}
    [[nodiscard]] std::string_view Namespace(const XmlElement& e) const {return namespaces[e.ns];}
    [[nodiscard]] const std::string* Attribute(const XmlElement& e,std::string_view ns,std::string_view local) const {
        for(const auto& a:e.attributes)if(a.local==local&&namespaces[a.ns]==ns)return &a.value;
        return nullptr;
    }
};
inline constexpr std::string_view xmlNamespace="http://www.w3.org/XML/1998/namespace";
[[nodiscard]] XmlDocument ParseXml(std::string_view bytes,const XmlLimits& limits);

[[nodiscard]] Error ExtractDocxInto(Bytes bytes,const ExtractOptions& options,Document& document) noexcept;
[[nodiscard]] Error ExtractRtfInto(Bytes bytes,const ExtractOptions& options,Document& document) noexcept;
}
