#pragma once
#include "../api.hpp"
#include <algorithm>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace arken::markup::v1::detail {
using Bytes=std::span<const std::byte>;

// Internal failures unwind to the noexcept public boundary, which reports the code.
struct Failure {Error error;std::uint64_t line=0;};
[[noreturn]] inline void Fail(Error error,std::uint64_t line=0) {throw Failure{error,line};}

template<class F> Error Guard(F&& body,std::uint64_t* line=nullptr) noexcept {
    try {body();return Error::none;}
    catch(const Failure& failure) {if(line)*line=failure.line;return failure.error;}
    catch(const std::bad_alloc&) {return Error::allocation_failed;}
    catch(...) {return Error::allocation_failed;}
}

inline void AppendUtf8(std::string& out,char32_t c) {
    if(c<0x80) {
        out.push_back(char(c));
    } else if(c<0x800) {
        out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&0x3f)));
    } else if(c<0x10000) {
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
inline bool StartsWithNoCase(std::string_view s,std::string_view prefix) {
    return s.size()>=prefix.size()&&EqualsNoCase(s.substr(0,prefix.size()),prefix);
}
inline bool IsAsciiAlpha(char c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
inline bool IsAsciiDigit(char c) {return c>='0'&&c<='9';}
inline bool IsAsciiAlnum(char c) {return IsAsciiAlpha(c)||IsAsciiDigit(c);}
inline bool IsHexDigit(char c) {return IsAsciiDigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
inline bool IsAsciiPunctuation(char c) {
    return (c>=0x21&&c<=0x2f)||(c>=0x3a&&c<=0x40)||(c>=0x5b&&c<=0x60)||(c>=0x7b&&c<=0x7e);
}

// Strips a UTF-8 BOM, validates strict UTF-8, normalizes CRLF and lone CR to
// LF and replaces U+0000 with U+FFFD. Fails with invalid_utf8.
inline std::string PrepareText(Bytes bytes) {
    std::string_view s(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(s.size()>=3&&s.substr(0,3)=="\xEF\xBB\xBF")s.remove_prefix(3);
    std::string out;
    out.reserve(s.size());
    char32_t c=0;
    for(std::size_t p=0;p<s.size();) {
        const auto n=DecodeUtf8(s,p,c);
        if(!n)Fail(Error::invalid_utf8);
        if(c==0) {
            out+="\xEF\xBF\xBD";
        } else if(c=='\r') {
            out.push_back('\n');
            if(p+1<s.size()&&s[p+1]=='\n')++p;
        } else {
            out.append(s.substr(p,n));
        }
        p+=n;
    }
    return out;
}

// Append-only output with a hard byte budget.
class Output {
public:
    explicit Output(std::uint64_t limit):limit_(limit) {}
    void Put(std::string_view s) {
        if(s.size()>limit_-std::min<std::uint64_t>(limit_,text_.size()))Fail(Error::output_limit);
        text_.append(s);
    }
    void Put(char c) {Put(std::string_view(&c,1));}
    [[nodiscard]] const std::string& Text() const {return text_;}
    std::string Take() {return std::move(text_);}
private:
    std::uint64_t limit_;
    std::string text_;
};

// Named character references (entities.cpp). Returns the scalar, or 0 when the
// name (without '&' and ';') is not in the supported set. `legacy` is set when
// HTML also recognizes the name without a terminating semicolon.
char32_t LookupEntity(std::string_view name,bool* legacy=nullptr) noexcept;
// Longest supported legacy (semicolon-optional) name that prefixes `s`; 0 if none.
std::size_t LongestLegacyEntity(std::string_view s,char32_t& value) noexcept;
// Numeric reference value after the HTML5 replacement rules: 0, surrogates and
// values above U+10FFFF become U+FFFD; 0x80-0x9F map through windows-1252; other
// C0 controls except TAB/LF/FF and DEL become U+FFFD.
char32_t NumericReference(std::uint64_t value) noexcept;

// URL policy shared by both directions. The scheme is whatever precedes the
// first ':' when that ':' comes before any '/', '?' or '#'.
inline std::string_view TrimAsciiSpace(std::string_view s) {
    while(!s.empty()&&(s.front()==' '||s.front()=='\t'||s.front()=='\n'||s.front()=='\f'||s.front()=='\r'))s.remove_prefix(1);
    while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\n'||s.back()=='\f'||s.back()=='\r'))s.remove_suffix(1);
    return s;
}
inline bool HasScheme(std::string_view url,std::string_view& scheme) {
    const auto stop=url.find_first_of(":/?#");
    if(stop==std::string_view::npos||url[stop]!=':')return false;
    scheme=url.substr(0,stop);
    return true;
}
// Absolute http(s) (and mailto when allowed) with a non-empty remainder and
// no control characters. Spaces are allowed (serializers percent-encode them).
inline bool AbsoluteAllowed(std::string_view url,bool allowMailto) {
    for(const char c:url)if(std::uint8_t(c)<0x20||c==0x7f)return false;
    std::string_view scheme;
    if(!HasScheme(url,scheme))return false;
    if(url.size()<=scheme.size()+1)return false;
    if(EqualsNoCase(scheme,"http")||EqualsNoCase(scheme,"https"))return url.substr(scheme.size()+1,2)=="//"&&url.size()>scheme.size()+3;
    return allowMailto&&EqualsNoCase(scheme,"mailto");
}
}
