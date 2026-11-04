#include "detail.hpp"

// SRT <-> WebVTT through one cue model: start/end in milliseconds and a token
// list (text, b/i/u open/close, colour open/close). Everything the target
// cannot carry is dropped and counted in the report.
namespace arken::markup::v1::detail {
std::string SrtToVttText(Bytes bytes,const SubtitleOptions& options,SubtitleReport& report);
std::string VttToSrtText(Bytes bytes,const SubtitleOptions& options,SubtitleReport& report);

namespace {
enum class TokenKind : std::uint8_t {text,open,close};
struct Token {
    TokenKind kind=TokenKind::text;
    std::string value;  // Text, or tag: "b", "i", "u", or a colour name for colour tags.
    bool colour=false;
};
struct Cue {
    std::uint64_t start=0,end=0;
    std::vector<Token> tokens;
};

bool Blank(std::string_view s) {return TrimAsciiSpace(s).empty();}
bool AllDigits(std::string_view s) {
    return !s.empty()&&std::all_of(s.begin(),s.end(),IsAsciiDigit);
}
bool IsWs(char c) {return c==' '||c=='\t';}
std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    for(std::size_t start=0;start<text.size();) {
        auto end=text.find('\n',start);
        if(end==std::string_view::npos)end=text.size();
        lines.push_back(text.substr(start,end-start));
        start=end+1;
    }
    return lines;
}

// The eight WebVTT default colour classes and their #RRGGBB values.
constexpr std::pair<std::string_view,std::string_view> colours[]={
    {"white","#ffffff"},{"lime","#00ff00"},{"cyan","#00ffff"},{"red","#ff0000"},
    {"yellow","#ffff00"},{"magenta","#ff00ff"},{"blue","#0000ff"},{"black","#000000"}};
std::string_view ColourName(std::string_view value) {
    const auto v=AsciiLower(TrimAsciiSpace(value));
    for(const auto& [name,hex]:colours)if(v==name||v==hex)return name;
    return {};
}

// Digits [min,max] at s[p]; returns the value or -1.
std::int64_t Digits(std::string_view s,std::size_t& p,std::size_t min,std::size_t max) {
    const auto start=p;
    std::int64_t v=0;
    while(p<s.size()&&IsAsciiDigit(s[p])&&p-start<max) {v=v*10+(s[p]-'0');++p;}
    if(p-start<min||(p<s.size()&&IsAsciiDigit(s[p])))return -1;
    return v;
}
// SRT: H{1,5}:MM:SS,mmm (a '.' separator is tolerated). WebVTT: [H{2,5}:]MM:SS.mmm.
bool Timestamp(std::string_view s,std::size_t& p,bool vtt,std::uint64_t& ms) {
    std::int64_t parts[3]={0,0,0};
    std::size_t count=0;
    std::size_t widths[3]={0,0,0};
    for(;;) {
        const auto start=p;
        const auto v=Digits(s,p,1,5);
        if(v<0||count==3)return false;
        widths[count]=p-start;
        parts[count++]=v;
        if(p<s.size()&&s[p]==':') {++p;continue;}
        break;
    }
    if(p>=s.size())return false;
    const char sep=s[p];
    if(!(sep=='.'||(!vtt&&sep==',')))return false;
    ++p;
    const auto millis=Digits(s,p,3,3);
    if(millis<0)return false;
    std::int64_t h=0,m=0,sec=0;
    if(count==3) {
        if(vtt&&widths[0]<2)return false;
        h=parts[0];m=parts[1];sec=parts[2];
        if(widths[1]!=2||widths[2]!=2)return false;
    } else if(count==2&&vtt) {
        m=parts[0];sec=parts[1];
        if(widths[0]!=2||widths[1]!=2)return false;
    } else {
        return false;
    }
    if(m>59||sec>59)return false;
    ms=std::uint64_t(((h*60+m)*60+sec)*1000+millis);
    return true;
}
// Parses "start --> end[ rest]". `rest` receives settings/coordinates.
bool Timing(std::string_view line,bool vtt,std::uint64_t& start,std::uint64_t& end,std::string_view& rest) {
    std::size_t p=0;
    while(p<line.size()&&IsWs(line[p]))++p;
    if(!Timestamp(line,p,vtt,start))return false;
    while(p<line.size()&&IsWs(line[p]))++p;
    if(line.substr(p,3)!="-->")return false;
    p+=3;
    while(p<line.size()&&IsWs(line[p]))++p;
    if(!Timestamp(line,p,vtt,end))return false;
    if(p<line.size()&&!IsWs(line[p]))return false;
    rest=TrimAsciiSpace(line.substr(p));
    return true;
}
std::string FormatTime(std::uint64_t ms,char separator) {
    const auto h=ms/3'600'000,m=ms/60'000%60,s=ms/1000%60,f=ms%1000;
    std::string out=std::to_string(h);
    if(out.size()<2)out.insert(0,"0");
    const auto two=[&](std::uint64_t v) {out.push_back(char('0'+v/10));out.push_back(char('0'+v%10));};
    out.push_back(':');two(m);out.push_back(':');two(s);out.push_back(separator);
    out.push_back(char('0'+f/100));out.push_back(char('0'+f/10%10));out.push_back(char('0'+f%10));
    return out;
}

class CueSink {
public:
    CueSink(const SubtitleOptions& options,SubtitleReport& report):options_(options),report_(report) {}
    void Check(Cue& cue,std::uint64_t line,std::size_t payloadBytes) {
        if(cue.end<cue.start)Fail(Error::subtitle_time_order,line);
        if(!cues.empty()&&cue.start<cues.back().start)Fail(Error::subtitle_non_monotonic,line);
        if(payloadBytes>options_.maximumPayloadBytes)Fail(Error::subtitle_payload_limit,line);
        if(cues.size()>=options_.maximumCues)Fail(Error::subtitle_cue_limit,line);
        cues.push_back(std::move(cue));
        ++report_.cues;
    }
    std::vector<Cue> cues;
private:
    const SubtitleOptions& options_;
    SubtitleReport& report_;
};

void AddText(std::vector<Token>& tokens,std::string_view text) {
    if(text.empty())return;
    if(!tokens.empty()&&tokens.back().kind==TokenKind::text) {tokens.back().value.append(text);return;}
    tokens.push_back(Token{TokenKind::text,std::string(text),false});
}

// ---- SRT payload: literal text, b/i/u/font tags, {\...} override codes ----
std::vector<Token> ParseSrtPayload(std::string_view s,const SubtitleOptions& options,SubtitleReport& report) {
    std::vector<Token> tokens;
    std::vector<bool> fontKept;
    for(std::size_t p=0;p<s.size();) {
        if(s[p]=='{'&&p+1<s.size()&&s[p+1]=='\\') {
            const auto close=s.find('}',p);
            if(close!=std::string_view::npos&&s.substr(p,close-p).find('\n')==std::string_view::npos) {
                ++report.overridesStripped;
                p=close+1;
                continue;
            }
        }
        if(s[p]=='<') {
            auto q=p+1;
            const bool closing=q<s.size()&&s[q]=='/';
            if(closing)++q;
            const auto nameStart=q;
            while(q<s.size()&&IsAsciiAlpha(s[q]))++q;
            const auto gt=s.find('>',q);
            const auto lt=s.find('<',q);
            const auto nl=s.find('\n',q);
            if(q>nameStart&&gt!=std::string_view::npos&&gt<lt&&gt<nl&&(q==gt||IsWs(s[q])||s[q]=='/')) {
                const auto name=AsciiLower(s.substr(nameStart,q-nameStart));
                const auto attrs=s.substr(q,gt-q);
                p=gt+1;
                if(name=="b"||name=="i"||name=="u") {
                    tokens.push_back(Token{closing?TokenKind::close:TokenKind::open,name,false});
                    continue;
                }
                if(name=="font") {
                    if(closing) {
                        const bool kept=!fontKept.empty()&&fontKept.back();
                        if(!fontKept.empty())fontKept.pop_back();
                        if(kept)tokens.push_back(Token{TokenKind::close,{},true});
                        continue;
                    }
                    std::string_view colour;
                    const auto lower=AsciiLower(attrs);
                    const auto at=lower.find("color");
                    if(at!=std::string::npos) {
                        auto v=attrs.substr(at+5);
                        while(!v.empty()&&(IsWs(v.front())||v.front()=='='))v.remove_prefix(1);
                        if(!v.empty()&&(v.front()=='"'||v.front()=='\'')) {
                            const char quote=v.front();v.remove_prefix(1);
                            v=v.substr(0,v.find(quote));
                        } else {
                            std::size_t e=0;
                            while(e<v.size()&&!IsWs(v[e])&&v[e]!='/')++e;
                            v=v.substr(0,e);
                        }
                        colour=ColourName(v);
                    }
                    const bool keep=options.keepFontColor&&!colour.empty();
                    fontKept.push_back(keep);
                    if(keep) tokens.push_back(Token{TokenKind::open,std::string(colour),true});
                    else ++report.tagsStripped;
                    continue;
                }
                if(!closing)++report.tagsStripped;
                continue;
            }
        }
        const auto stop=s.find_first_of("<{",p+1);
        const auto end=stop==std::string_view::npos?s.size():stop;
        AddText(tokens,s.substr(p,end-p));
        p=end;
    }
    return tokens;
}

// ---- WebVTT payload: escaped text and cue-text tags ----
std::vector<Token> ParseVttPayload(std::string_view s,const SubtitleOptions& options,SubtitleReport& report) {
    std::vector<Token> tokens;
    std::vector<bool> classKept;
    for(std::size_t p=0;p<s.size();) {
        if(s[p]=='<') {
            const auto gt=s.find('>',p);
            const auto end=gt==std::string_view::npos?s.size():gt;
            const auto body=s.substr(p+1,end-p-1);
            p=gt==std::string_view::npos?s.size():gt+1;
            if(!body.empty()&&IsAsciiDigit(body[0])) {++report.tagsStripped;continue;}  // Timestamp tag.
            const bool closing=!body.empty()&&body[0]=='/';
            auto rest=closing?body.substr(1):body;
            std::size_t n=0;
            while(n<rest.size()&&rest[n]!='.'&&!IsWs(rest[n])&&rest[n]!='\n')++n;
            const auto name=rest.substr(0,n);
            std::vector<std::string_view> classes;
            if(n<rest.size()&&rest[n]=='.') {
                auto c=rest.substr(n+1);
                c=c.substr(0,std::min(c.size(),c.find_first_of(" \t\n")));
                while(!c.empty()) {
                    const auto dot=c.find('.');
                    classes.push_back(c.substr(0,dot));
                    if(dot==std::string_view::npos)break;
                    c.remove_prefix(dot+1);
                }
            }
            if(name=="b"||name=="i"||name=="u") {
                tokens.push_back(Token{closing?TokenKind::close:TokenKind::open,std::string(name),false});
                continue;
            }
            if(name=="c") {
                if(closing) {
                    const bool kept=!classKept.empty()&&classKept.back();
                    if(!classKept.empty())classKept.pop_back();
                    if(kept)tokens.push_back(Token{TokenKind::close,{},true});
                    continue;
                }
                std::string_view colour;
                for(const auto c:classes)if(colour.empty()&&!c.empty())colour=ColourName(c)==c?c:std::string_view();
                const bool keep=options.keepFontColor&&!colour.empty();
                classKept.push_back(keep);
                if(keep) tokens.push_back(Token{TokenKind::open,std::string(colour),true});
                else ++report.tagsStripped;
                continue;
            }
            if(!closing)++report.tagsStripped;  // v, lang, ruby, rt and unknown tags.
            continue;
        }
        if(s[p]=='&') {
            std::string decoded;
            auto e=p+1;
            if(e<s.size()&&s[e]=='#') {
                ++e;
                const bool hex=e<s.size()&&(s[e]=='x'||s[e]=='X');
                if(hex)++e;
                const auto start=e;
                std::uint64_t v=0;
                while(e<s.size()&&(hex?IsHexDigit(s[e]):IsAsciiDigit(s[e]))&&e-start<8) {
                    v=v*(hex?16:10)+(IsAsciiDigit(s[e])?unsigned(s[e]-'0'):unsigned(AsciiLower(s[e])-'a'+10));
                    ++e;
                }
                if(e>start&&e<s.size()&&s[e]==';') {
                    AppendUtf8(decoded,NumericReference(v));
                    AddText(tokens,decoded);
                    p=e+1;
                    continue;
                }
            } else {
                while(e<s.size()&&IsAsciiAlnum(s[e])&&e-p<33)++e;
                if(e<s.size()&&s[e]==';') {
                    if(const auto v=LookupEntity(s.substr(p+1,e-p-1))) {
                        AppendUtf8(decoded,v);
                        AddText(tokens,decoded);
                        p=e+1;
                        continue;
                    }
                }
            }
            AddText(tokens,"&");
            ++p;
            continue;
        }
        const auto stop=s.find_first_of("<&",p);
        const auto end=stop==std::string_view::npos?s.size():stop;
        AddText(tokens,s.substr(p,end-p));
        p=end;
    }
    return tokens;
}

// Renders tokens; lines that end up empty are removed so a payload never
// contains the blank line that would end the cue.
std::string RenderPayload(const std::vector<Token>& tokens,bool vtt) {
    std::string out;
    for(const auto& t:tokens) {
        if(t.kind==TokenKind::text) {
            if(!vtt) {out+=t.value;continue;}
            for(const char c:t.value) {
                if(c=='&')out+="&amp;";
                else if(c=='<')out+="&lt;";
                else if(c=='>')out+="&gt;";
                else out.push_back(c);
            }
            continue;
        }
        const bool open=t.kind==TokenKind::open;
        if(t.colour) {
            if(vtt) out+=open?"<c."+t.value+">":"</c>";
            else out+=open?"<font color=\""+t.value+"\">":"</font>";
            continue;
        }
        out+=open?"<"+t.value+">":"</"+t.value+">";
    }
    std::string clean;
    for(const auto line:Lines(out)) {
        if(line.empty())continue;
        if(!clean.empty())clean.push_back('\n');
        clean.append(line);
    }
    return clean;
}

std::string Prepared(Bytes bytes) {return PrepareText(bytes);}

// ---- SRT reader ----
std::vector<Cue> ReadSrt(std::string_view text,const SubtitleOptions& options,SubtitleReport& report) {
    const auto lines=Lines(text);
    CueSink sink(options,report);
    std::size_t i=0;
    const auto isTiming=[](std::string_view l) {return l.find("-->")!=std::string_view::npos;};
    while(i<lines.size()) {
        if(Blank(lines[i])) {++i;continue;}
        std::size_t timing=0;
        if(AllDigits(TrimAsciiSpace(lines[i]))) {
            if(i+1>=lines.size()||!isTiming(lines[i+1]))Fail(Error::subtitle_malformed_block,i+1);
            timing=i+1;
        } else if(isTiming(lines[i])) {
            timing=i;
            ++report.missingIndices;
        } else {
            Fail(Error::subtitle_malformed_block,i+1);
        }
        Cue cue;
        std::string_view rest;
        if(!Timing(lines[timing],false,cue.start,cue.end,rest))Fail(Error::subtitle_malformed_timestamp,timing+1);
        if(!rest.empty())++report.settingsDropped;
        std::string payload;
        auto j=timing+1;
        // A missing blank line is tolerated when the next cue starts with a
        // valid timing line (optionally after an index); text that merely
        // contains "-->" stays payload.
        const auto validTiming=[&](std::string_view l) {
            std::uint64_t a=0,b=0;
            std::string_view r;
            return isTiming(l)&&Timing(l,false,a,b,r);
        };
        while(j<lines.size()&&!Blank(lines[j])) {
            if(validTiming(lines[j]))break;
            if(AllDigits(TrimAsciiSpace(lines[j]))&&j+1<lines.size()&&validTiming(lines[j+1]))break;
            if(!payload.empty())payload.push_back('\n');
            payload.append(lines[j]);
            if(payload.size()>options.maximumPayloadBytes)Fail(Error::subtitle_payload_limit,j+1);
            ++j;
        }
        cue.tokens=ParseSrtPayload(payload,options,report);
        sink.Check(cue,timing+1,payload.size());
        i=j;
    }
    return std::move(sink.cues);
}

// ---- WebVTT reader ----
std::vector<Cue> ReadVtt(std::string_view text,const SubtitleOptions& options,SubtitleReport& report) {
    const auto lines=Lines(text);
    if(lines.empty()||lines[0].substr(0,6)!="WEBVTT"||(lines[0].size()>6&&!IsWs(lines[0][6])))
        Fail(Error::subtitle_missing_header,1);
    if(!Blank(lines[0].substr(6)))++report.headerLinesDropped;
    const auto hasArrow=[](std::string_view l) {return l.find("-->")!=std::string_view::npos;};
    std::size_t i=1;
    while(i<lines.size()&&!lines[i].empty()&&!hasArrow(lines[i])) {++report.headerLinesDropped;++i;}
    const auto keyword=[](std::string_view l,std::string_view k) {
        return l.substr(0,k.size())==k&&(l.size()==k.size()||IsWs(l[k.size()]));
    };
    CueSink sink(options,report);
    std::size_t index=0;
    while(i<lines.size()) {
        if(Blank(lines[i])) {++i;continue;}
        const auto first=lines[i];
        const auto skipBlock=[&] {while(i<lines.size()&&!Blank(lines[i]))++i;};
        if(!hasArrow(first)) {
            if(keyword(first,"NOTE")) {++report.notesDropped;skipBlock();continue;}
            if(keyword(first,"STYLE")&&Blank(first.substr(5))) {++report.styleBlocksDropped;skipBlock();continue;}
            if(keyword(first,"REGION")&&Blank(first.substr(6))) {++report.regionBlocksDropped;skipBlock();continue;}
        }
        std::size_t timing=i;
        std::string_view identifier;
        if(!hasArrow(first)) {
            if(i+1>=lines.size()||!hasArrow(lines[i+1]))Fail(Error::subtitle_malformed_block,i+1);
            identifier=first;
            timing=i+1;
        }
        Cue cue;
        std::string_view settings;
        if(!Timing(lines[timing],true,cue.start,cue.end,settings))Fail(Error::subtitle_malformed_timestamp,timing+1);
        if(!settings.empty())++report.settingsDropped;
        ++index;
        if(!identifier.empty()&&TrimAsciiSpace(identifier)!=std::to_string(index))++report.identifiersDropped;
        std::string payload;
        auto j=timing+1;
        while(j<lines.size()&&!lines[j].empty()&&!hasArrow(lines[j])) {
            if(!payload.empty())payload.push_back('\n');
            payload.append(lines[j]);
            if(payload.size()>options.maximumPayloadBytes)Fail(Error::subtitle_payload_limit,j+1);
            ++j;
        }
        cue.tokens=ParseVttPayload(payload,options,report);
        sink.Check(cue,timing+1,payload.size());
        i=j;
    }
    return std::move(sink.cues);
}
}

std::string SrtToVttText(Bytes bytes,const SubtitleOptions& options,SubtitleReport& report) {
    const auto text=Prepared(bytes);
    const auto cues=ReadSrt(text,options,report);
    Output out(options.maximumOutputBytes);
    out.Put("WEBVTT\n\n");
    for(const auto& cue:cues) {
        out.Put(FormatTime(cue.start,'.'));out.Put(" --> ");out.Put(FormatTime(cue.end,'.'));out.Put('\n');
        const auto payload=RenderPayload(cue.tokens,true);
        if(!payload.empty()) {out.Put(payload);out.Put('\n');}
        out.Put('\n');
    }
    return out.Take();
}

std::string VttToSrtText(Bytes bytes,const SubtitleOptions& options,SubtitleReport& report) {
    const auto text=Prepared(bytes);
    const auto cues=ReadVtt(text,options,report);
    Output out(options.maximumOutputBytes);
    const std::string_view nl=options.crlf?"\r\n":"\n";
    for(std::size_t i=0;i<cues.size();++i) {
        const auto& cue=cues[i];
        out.Put(std::to_string(i+1));out.Put(nl);
        out.Put(FormatTime(cue.start,','));out.Put(" --> ");out.Put(FormatTime(cue.end,','));out.Put(nl);
        const auto payload=RenderPayload(cue.tokens,false);
        for(const auto line:Lines(payload)) {out.Put(line);out.Put(nl);}
        out.Put(nl);
    }
    return out.Take();
}
}
