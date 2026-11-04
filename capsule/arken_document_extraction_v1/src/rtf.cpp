#include "detail.hpp"
#include <algorithm>
#include <map>

// Stateful RTF 1.x reader: group stack, control words/symbols, \uN with \ucN
// fallback skipping, \'hh through the active code page, \bin payloads skipped
// byte-exactly, ignorable and non-text destinations dropped, field results
// kept (HYPERLINK targets attached), simple tables and degraded lists.
namespace arken::document::v1::detail {
namespace {
#include "code_pages.inc"

constexpr std::size_t maximumFieldInstruction=4096;
constexpr std::size_t maximumStyleName=256;
constexpr int symbolCodePage=-2;

// Destinations whose content is never document text.
constexpr std::string_view skippedDestinations[]={
    "aftncn","aftnsep","aftnsepc","annotation","atnauthor","atndate","atnicn","atnid","atnparent","atnref",
    "atntime","atrfend","atrfstart","author","background","bkmkend","bkmkstart","buptim","category",
    "colortbl","comment","company","creatim","datafield","datastore","do","doccomm","docvar","dptxbxtext",
    "falt","ffname","filetbl","fldtype","footer","footerf","footerl","footerr","footnote","formfield",
    "ftncn","ftnsep","ftnsepc","generator","header","headerf","headerl","headerr","hlinkbase","info",
    "keywords","latentstyles","listoverridetable","listpicture","listtable","manager","mmathPr","nonshppict",
    "object","objdata","operator","panose","pgdsctbl","pict","printim","private","revtbl","revtim","rsidtbl",
    "rxe","shp","shpinst","shppict","shprslt","shptxt","subject","tc","tcn","template","themedata","title",
    "txe","userprops","colorschememapping","xe","xmlnstbl","xmlopen","wgrffmtfilter","pn","pnseclvl",
    "pntxta","pntxtb","result","sv","sn","blipuid","protusertbl","mhtmltag","htmltag","fchars","lchars",
    "pgptbl","passwordhash","ud","upr","ebcstart","ebcend","oldcprops","oldpprops","oldsprops","oldtprops",
};

int CodePageForCharset(int charset,int documentCodePage) {
    switch(charset) {
        case 0: return 1252;
        case 1: return documentCodePage;
        case 2: return symbolCodePage;
        case 77: return 10000;
        case 128: return 932;
        case 129: return 949;
        case 130: return 1361;
        case 134: return 936;
        case 136: return 950;
        case 161: return 1253;
        case 162: return 1254;
        case 163: return 1258;
        case 177: return 1255;
        case 178: return 1256;
        case 186: return 1257;
        case 204: return 1251;
        case 222: return 874;
        case 238: return 1250;
        case 254: return 437;
        case 255: return 850;
        default: return documentCodePage;
    }
}
const std::uint16_t* CodePageTable(int codePage) {
    switch(codePage) {
        case 1252: return cp1252;
        case 1250: return cp1250;
        case 1251: return cp1251;
        case 1253: return cp1253;
        case 1254: return cp1254;
        case 1257: return cp1257;
        default: return nullptr;
    }
}
int HeadingFromStyleName(std::string_view name) {
    while(!name.empty()&&(name.front()==' '))name.remove_prefix(1);
    while(!name.empty()&&(name.back()==' '))name.remove_suffix(1);
    const auto lower=AsciiLower(name);
    if(lower=="title")return 1;
    if(lower.size()==9&&lower.substr(0,8)=="heading "&&lower[8]>='1'&&lower[8]<='6')return lower[8]-'0';
    return 0;
}

class Parser {
public:
    Parser(Bytes bytes,const ExtractOptions& options,ModelBuilder& builder)
        :s_(reinterpret_cast<const char*>(bytes.data()),bytes.size()),options_(options),builder_(builder) {}
    void Parse() {
        if(s_.substr(0,5)!="{\\rtf")Fail(Error::rtf_not_rtf);
        bool closed=false;
        while(p_<s_.size()) {
            if(++tokens_>options_.rtf.maximumTokens)Fail(Error::rtf_limit);
            const char c=s_[p_];
            if(c=='{') {
                ++p_;pending_=0;
                if(stack_.size()>=options_.rtf.maximumDepth)Fail(Error::rtf_limit);
                stack_.push_back(stack_.empty()?State{}:stack_.back());
                if(stack_.back().dest==Dest::stylesheet) {styleNumber_=0;styleName_.clear();}
                groupStart_=true;starred_=false;
                continue;
            }
            if(c=='}') {
                ++p_;pending_=0;
                if(stack_.empty())Fail(Error::rtf_malformed);
                stack_.pop_back();
                groupStart_=false;starred_=false;
                if(stack_.empty()) {closed=true;break;}
                continue;
            }
            if(stack_.empty())Fail(Error::rtf_malformed);
            if(c=='\\') {Control();continue;}
            ++p_;
            groupStart_=false;
            if(c=='\r'||c=='\n')continue;
            if(pending_>0) {--pending_;continue;}
            Byte(std::uint8_t(c));
        }
        if(!closed)Fail(Error::rtf_malformed);
        for(;p_<s_.size();++p_) {
            const char c=s_[p_];
            if(c!='\r'&&c!='\n'&&c!=' '&&c!='\t'&&c!='\0')Fail(Error::rtf_malformed);
        }
        Finish();
    }
private:
    enum class Dest : std::uint8_t {text,skip,fonttbl,stylesheet,fldinst};
    struct State {
        Dest dest=Dest::text;
        int uc=1;
        bool bold=false,italic=false,hidden=false;
        int font=-1;
        int field=-1;     // Enclosing \field.
        int linkField=-1; // \fldrslt whose field is a HYPERLINK.
    };
    struct Paragraph {
        bool intbl=false,marker=false;
        int list=-1,level=0,outline=-1,style=0;
    };
    struct Font {int charset=-1,codePage=0;};
    struct Field {std::string instruction,link;};

    State& Top() {return stack_.back();}

    void Control() {
        ++p_;
        if(p_>=s_.size())Fail(Error::rtf_malformed);
        const char c=s_[p_];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')) {
            const auto start=p_;
            while(p_<s_.size()&&((s_[p_]>='a'&&s_[p_]<='z')||(s_[p_]>='A'&&s_[p_]<='Z'))) {
                if(p_-start>=options_.rtf.maximumControlWordBytes)Fail(Error::rtf_limit);
                ++p_;
            }
            const auto word=s_.substr(start,p_-start);
            bool hasParameter=false,negative=false;
            long long parameter=0;
            if(p_<s_.size()&&s_[p_]=='-') {negative=true;++p_;}
            const auto digits=p_;
            while(p_<s_.size()&&s_[p_]>='0'&&s_[p_]<='9') {
                if(p_-digits>=10)Fail(Error::rtf_malformed);
                parameter=parameter*10+(s_[p_]-'0');
                ++p_;
            }
            if(p_>digits)hasParameter=true;
            else if(negative)Fail(Error::rtf_malformed);
            if(negative)parameter=-parameter;
            if(parameter<-2147483648LL||parameter>2147483647LL)Fail(Error::rtf_malformed);
            if(p_<s_.size()&&s_[p_]==' ')++p_;
            Word(word,hasParameter,int(parameter));
            return;
        }
        ++p_;
        if(c=='\'') {
            if(s_.size()-p_<2)Fail(Error::rtf_malformed);
            const auto hex=[](char h)->int {
                if(h>='0'&&h<='9')return h-'0';
                if(h>='a'&&h<='f')return h-'a'+10;
                if(h>='A'&&h<='F')return h-'A'+10;
                return -1;
            };
            const auto hi=hex(s_[p_]),lo=hex(s_[p_+1]);
            if(hi<0||lo<0)Fail(Error::rtf_malformed);
            p_+=2;
            groupStart_=false;
            if(pending_>0) {--pending_;return;}
            Byte(std::uint8_t(hi*16+lo));
            return;
        }
        if(c=='*') {if(groupStart_)starred_=true;return;}
        groupStart_=false;
        if(pending_>0) {--pending_;return;}
        switch(c) {
            case '\\': case '{': case '}': Byte(std::uint8_t(c));break;
            case '~': Unicode(0xa0);break;
            case '_': Unicode(0x2011);break;
            case '\r': case '\n': EndParagraph();break;
            case '\t': Unicode('\t');break;
            default: break; // \- optional hyphen, \: index subentry, \| formula and unknown symbols.
        }
    }

    bool Destination(std::string_view word) {
        auto& s=Top();
        if(s.dest==Dest::skip)return true;
        if(word=="fonttbl") {s.dest=Dest::fonttbl;return true;}
        if(word=="stylesheet") {s.dest=Dest::stylesheet;return true;}
        if(word=="field") {
            if(fields_.size()>=65536)Fail(Error::rtf_limit);
            s.field=int(fields_.size());fields_.emplace_back();
            return true;
        }
        if(word=="fldinst") {s.dest=s.field>=0?Dest::fldinst:Dest::skip;return true;}
        if(word=="fldrslt") {
            s.dest=Dest::text;
            if(s.field>=0) {
                auto& field=fields_[std::size_t(s.field)];
                field.link=HyperlinkFieldTarget(field.instruction);
                s.linkField=field.link.empty()?-1:s.field;
            }
            return true;
        }
        if(word=="pntext"||word=="listtext") {s.dest=Dest::skip;paragraph_.marker=true;return true;}
        if(std::find(std::begin(skippedDestinations),std::end(skippedDestinations),word)!=std::end(skippedDestinations)) {
            s.dest=Dest::skip;return true;
        }
        if(starred_) {s.dest=Dest::skip;return true;} // Unknown ignorable destination.
        return false;
    }

    void Word(std::string_view word,bool has,int value) {
        if(word=="bin") {
            // Binary payload: skipped exactly, in every destination, never decoded.
            if(!has||value<0||std::size_t(value)>s_.size()-p_)Fail(Error::rtf_malformed);
            p_+=std::size_t(value);
            groupStart_=false;
            if(pending_>0)--pending_;
            return;
        }
        if(pending_>0) {--pending_;groupStart_=false;return;}
        if(groupStart_) {
            const bool destination=Destination(word);
            groupStart_=false;
            if(destination)return;
        }
        auto& s=Top();
        if(word=="uc") {s.uc=std::clamp(has?value:1,0,64);return;}
        if(word=="u") {
            if(!has)Fail(Error::rtf_malformed);
            auto code=value;
            if(code<0)code+=65536;
            if(code<0||code>0xffff)Fail(Error::rtf_malformed);
            Unicode(char32_t(code));
            pending_=std::size_t(s.uc);
            return;
        }
        if(s.dest==Dest::skip)return;
        if(s.dest==Dest::fonttbl) {
            if(word=="f"&&has)font_=value;
            else if(word=="fcharset"&&has)fonts_[font_].charset=value;
            else if(word=="cpg"&&has)fonts_[font_].codePage=value;
            return;
        }
        if(s.dest==Dest::stylesheet) {
            if(word=="s"&&has)styleNumber_=value;
            else if(word=="cs"||word=="ds"||word=="ts"||word=="tsrowd")styleNumber_=-1;
            return;
        }
        if(word=="ansicpg"&&has) {codePage_=value;return;}
        if(word=="ansi") {codePage_=1252;return;}
        if(word=="mac") {codePage_=10000;return;}
        if(word=="pc") {codePage_=437;return;}
        if(word=="pca") {codePage_=850;return;}
        if(word=="deff"&&has) {defaultFont_=value;return;}
        if(word=="f"&&has) {s.font=value;return;}
        if(word=="b") {s.bold=!has||value!=0;return;}
        if(word=="i") {s.italic=!has||value!=0;return;}
        if(word=="v") {s.hidden=!has||value!=0;return;}
        if(word=="plain") {s.bold=s.italic=s.hidden=false;s.font=-1;return;}
        if(s.dest==Dest::fldinst)return; // Formatting only from here on.
        if(word=="par"||word=="sect"||word=="page") {EndParagraph();return;}
        if(word=="pard") {paragraph_=Paragraph{};return;}
        if(word=="line") {Unicode('\n');return;}
        if(word=="tab") {Unicode('\t');return;}
        if(word=="cell") {EndCell();return;}
        if(word=="row") {EndRow();return;}
        if(word=="nestcell"||word=="nestrow") {Unicode(' ');return;} // Nested tables flattened.
        if(word=="intbl") {paragraph_.intbl=true;return;}
        if(word=="itap"&&has) {paragraph_.intbl=value>0;return;}
        if(word=="ls"&&has) {paragraph_.list=value;return;}
        if(word=="ilvl"&&has) {paragraph_.level=value;return;}
        if(word=="outlinelevel"&&has) {paragraph_.outline=value;return;}
        if(word=="s"&&has) {paragraph_.style=value;return;}
        static constexpr std::pair<std::string_view,char32_t> symbols[]={
            {"emdash",0x2014},{"endash",0x2013},{"bullet",0x2022},{"lquote",0x2018},{"rquote",0x2019},
            {"ldblquote",0x201c},{"rdblquote",0x201d},{"emspace",0x2003},{"enspace",0x2002},
            {"qmspace",0x2005},{"zwj",0x200d},{"zwnj",0x200c},
        };
        for(const auto& [name,code]:symbols)if(word==name) {Unicode(code);return;}
    }

    int CurrentCodePage() const {
        const auto font=stack_.back().font>=0?stack_.back().font:defaultFont_;
        const auto found=fonts_.find(font);
        if(found!=fonts_.end()) {
            if(found->second.codePage>0)return found->second.codePage;
            if(found->second.charset>=0)return CodePageForCharset(found->second.charset,codePage_);
        }
        return codePage_;
    }
    // Decodes a byte of the active code page. Only text that is actually
    // emitted can fail; other destinations substitute U+FFFD.
    char32_t Decode(std::uint8_t b,bool strict) const {
        if(b<0x80)return b;
        const auto codePage=CurrentCodePage();
        if(codePage==symbolCodePage)return 0xfffd;
        const auto* table=CodePageTable(codePage);
        if(!table) {
            if(strict)Fail(Error::rtf_unsupported_code_page);
            return 0xfffd;
        }
        return table[b-0x80];
    }
    void Byte(std::uint8_t b) {
        auto& s=Top();
        switch(s.dest) {
            case Dest::skip: case Dest::fonttbl: return;
            case Dest::stylesheet:
                if(b==';') {
                    if(styleNumber_>=0&&!styleHeadings_.count(styleNumber_))styleHeadings_[styleNumber_]=HeadingFromStyleName(styleName_);
                    styleName_.clear();
                } else if(b>=0x20&&b<0x80&&styleName_.size()<maximumStyleName) {
                    styleName_.push_back(char(b));
                }
                return;
            case Dest::fldinst: Unicode(Decode(b,false));return;
            case Dest::text:
                if(b=='\t') {Unicode('\t');return;}
                if(b<0x20)return;
                if(s.hidden)return;
                Unicode(Decode(b,true));
                return;
        }
    }
    void Unicode(char32_t c) {
        auto& s=Top();
        if(c>=0xd800&&c<=0xdbff) {FlushSurrogate();high_=c;return;}
        if(c>=0xdc00&&c<=0xdfff) {
            if(high_) {c=0x10000+((high_-0xd800)<<10)+(c-0xdc00);high_=0;}
            else c=0xfffd;
        } else {
            FlushSurrogate();
        }
        if(s.dest==Dest::fldinst) {
            auto& instruction=fields_[std::size_t(s.field)].instruction;
            if(instruction.size()<maximumFieldInstruction)AppendUtf8(instruction,c);
            return;
        }
        if(s.dest!=Dest::text||s.hidden)return;
        if(c<0x20&&c!='\t'&&c!='\n')return;
        std::string utf8;
        AppendUtf8(utf8,c);
        const std::string_view link=s.linkField>=0?std::string_view(fields_[std::size_t(s.linkField)].link):std::string_view();
        builder_.Append(runs_,utf8,s.bold,s.italic,link);
    }
    void FlushSurrogate() {
        if(!high_)return;
        high_=0;
        Unicode(0xfffd);
    }
    int StyleHeading(int style) const {
        const auto found=styleHeadings_.find(style);
        return found==styleHeadings_.end()?0:found->second;
    }
    void EndParagraph() {
        FlushSurrogate();
        if(paragraph_.intbl) {
            builder_.AppendToCell(cell_,std::move(runs_));
            runs_.clear();
            paragraph_.marker=false;
            return;
        }
        FlushTable();
        Block block;
        block.runs=std::move(runs_);
        runs_.clear();
        int heading=paragraph_.outline>=0&&paragraph_.outline<=5?paragraph_.outline+1:StyleHeading(paragraph_.style);
        if(heading) {
            block.kind=BlockKind::heading;block.level=std::uint8_t(heading);
        } else if(paragraph_.list>=0||paragraph_.marker) {
            // List numbering formats live in \listtable; this profile degrades every list to bullets.
            block.kind=BlockKind::list_item;
            block.level=std::uint8_t(std::clamp(paragraph_.level,0,8));
        }
        paragraph_.marker=false;
        builder_.Push(std::move(block));
    }
    void EndCell() {
        FlushSurrogate();
        builder_.AppendToCell(cell_,std::move(runs_));
        runs_.clear();
        row_.push_back(std::move(cell_));
        cell_.clear();
    }
    void EndRow() {
        if(ModelBuilder::HasText(runs_)||!cell_.empty())EndCell();
        runs_.clear();
        if(!row_.empty())rows_.push_back(std::move(row_));
        row_.clear();
    }
    void FlushTable() {
        if(!row_.empty()||!cell_.empty())EndRow();
        if(rows_.empty())return;
        Block table;
        table.kind=BlockKind::table;
        table.rows=std::move(rows_);
        rows_.clear();
        builder_.Push(std::move(table));
    }
    void Finish() {
        if(paragraph_.intbl) {
            if(ModelBuilder::HasText(runs_)||!cell_.empty())EndCell();
            FlushTable();
            return;
        }
        if(ModelBuilder::HasText(runs_))EndParagraph();
        FlushTable();
    }

    std::string_view s_;
    const ExtractOptions& options_;
    ModelBuilder& builder_;
    std::size_t p_=0;
    std::uint64_t tokens_=0;
    std::vector<State> stack_;
    bool groupStart_=false,starred_=false;
    std::size_t pending_=0; // \uN fallback characters still to skip.
    char32_t high_=0;
    int codePage_=1252,defaultFont_=0,font_=0;
    std::map<int,Font> fonts_;
    int styleNumber_=0;
    std::string styleName_;
    std::map<int,int> styleHeadings_;
    std::vector<Field> fields_;
    Paragraph paragraph_;
    std::vector<Run> runs_;
    Cell cell_;
    Row row_;
    std::vector<Row> rows_;
};
}

Error ExtractRtfInto(Bytes bytes,const ExtractOptions& options,Document& document) noexcept {
    return Guard([&] {
        ModelBuilder builder(document,options);
        Parser parser(bytes,options,builder);
        parser.Parse();
    });
}
}
