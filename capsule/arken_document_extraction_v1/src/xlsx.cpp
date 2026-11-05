#include "tabular_detail.hpp"
#include <cstring>
#include <map>
#include <optional>

// SpreadsheetML (XLSX/XLTX) cell values. Policy (see NOTES.md "Tabular"): one
// worksheet, cached values only (formulas are never evaluated), shared and
// inline strings with rich-text runs, booleans, errors, ISO dates, and numbers
// whose cell style is a date/time format rendered as ISO 8601. Formatting,
// charts, drawings, comments, pivot caches and external links are ignored.
namespace arken::document::v1::tabular::detail {
namespace {
using doc::detail::XmlDocument;
using doc::detail::XmlElement;
constexpr std::string_view smlTransitional="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr std::string_view smlStrict="http://purl.oclc.org/ooxml/spreadsheetml/main";
constexpr std::string_view rTransitional="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view rStrict="http://purl.oclc.org/ooxml/officeDocument/relationships";
constexpr std::string_view contentTypesNamespace="http://schemas.openxmlformats.org/package/2006/content-types";
constexpr std::string_view relationshipsNamespace="http://schemas.openxmlformats.org/package/2006/relationships";
constexpr std::string_view workbookType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
constexpr std::string_view templateType="application/vnd.openxmlformats-officedocument.spreadsheetml.template.main+xml";
constexpr std::string_view macroTypes[]={
    "application/vnd.ms-excel.sheet.macroEnabled.main+xml",
    "application/vnd.ms-excel.template.macroEnabled.main+xml",
    "application/vnd.ms-excel.addin.macroEnabled.main+xml"};
constexpr std::string_view vbaProjectType="application/vnd.ms-office.vbaProject";

// ---- Open Packaging Conventions (independent of the DOCX reader's copy) ----
bool RelationshipType(std::string_view type,std::string_view name) {
    for(const std::string_view base:{rTransitional,rStrict})
        if(type.size()==base.size()+1+name.size()&&type.substr(0,base.size())==base
            &&type[base.size()]=='/'&&type.substr(base.size()+1)==name)return true;
    return false;
}
struct Relationship {std::string id,type,target;bool external=false;};
std::vector<Relationship> ParseRelationships(const std::string& bytes,const XmlLimits& limits) {
    const auto xml=doc::detail::ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Relationships"||xml.Namespace(root)!=relationshipsNamespace)Fail(Error::package_invalid);
    std::vector<Relationship> result;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(e.local!="Relationship"||xml.Namespace(e)!=relationshipsNamespace)Fail(Error::package_invalid);
        const auto* id=xml.Attribute(e,"","Id");
        const auto* type=xml.Attribute(e,"","Type");
        const auto* target=xml.Attribute(e,"","Target");
        const auto* mode=xml.Attribute(e,"","TargetMode");
        if(!id||id->empty()||!type||type->empty()||!target)Fail(Error::package_invalid);
        if(mode&&*mode!="External"&&*mode!="Internal")Fail(Error::package_invalid);
        for(const auto& r:result)if(r.id==*id)Fail(Error::package_invalid);
        result.push_back({*id,*type,*target,mode&&*mode=="External"});
    }
    return result;
}
std::string ResolvePart(std::string_view sourceDirectory,std::string_view target) {
    const auto fragment=target.find('#');
    if(fragment!=std::string_view::npos)target=target.substr(0,fragment);
    if(target.empty()||target.find('\\')!=std::string_view::npos||target.find("://")!=std::string_view::npos)
        Fail(Error::package_invalid);
    for(const char c:target)if(std::uint8_t(c)<0x20)Fail(Error::package_invalid);
    const std::string path=target.front()=='/'?std::string(target):std::string(sourceDirectory)+std::string(target);
    std::vector<std::string_view> segments;
    const std::string_view view(path);
    for(std::size_t start=0;start<view.size();) {
        auto end=view.find('/',start);
        if(end==std::string_view::npos)end=view.size();
        const auto segment=view.substr(start,end-start);
        if(segment=="..") {if(segments.empty())Fail(Error::package_invalid);segments.pop_back();}
        else if(!segment.empty()&&segment!=".")segments.push_back(segment);
        start=end+1;
    }
    if(segments.empty())Fail(Error::package_invalid);
    std::string result;
    for(const auto segment:segments) {result.push_back('/');result.append(segment);}
    return result;
}
std::string DirectoryOf(std::string_view part) {return std::string(part.substr(0,part.rfind('/')+1));}
std::string RelationshipsPartFor(std::string_view part) {
    const auto slash=part.rfind('/');
    return std::string(part.substr(0,slash+1))+"_rels/"+std::string(part.substr(slash+1))+".rels";
}
struct ContentTypes {
    std::map<std::string,std::string> defaults,overrides; // Lower-cased keys.
    std::string For(std::string_view part) const {
        const auto o=overrides.find(doc::detail::AsciiLower(part));
        if(o!=overrides.end())return o->second;
        const auto dot=part.rfind('.');
        if(dot==std::string_view::npos||dot<part.rfind('/'))return {};
        const auto d=defaults.find(doc::detail::AsciiLower(part.substr(dot+1)));
        return d==defaults.end()?std::string():d->second;
    }
};
ContentTypes ParseContentTypes(const std::string& bytes,const XmlLimits& limits) {
    const auto xml=doc::detail::ParseXml(bytes,limits);
    const auto& root=xml.Root();
    if(root.local!="Types"||xml.Namespace(root)!=contentTypesNamespace)Fail(Error::package_invalid);
    ContentTypes types;
    for(const auto& child:root.children) {
        if(child.text)continue;
        const auto& e=xml.elements[child.index];
        if(xml.Namespace(e)!=contentTypesNamespace)Fail(Error::package_invalid);
        const auto* type=xml.Attribute(e,"","ContentType");
        if(!type||type->empty())Fail(Error::package_invalid);
        if(e.local=="Default") {
            const auto* extension=xml.Attribute(e,"","Extension");
            if(!extension||extension->empty()||!types.defaults.emplace(doc::detail::AsciiLower(*extension),*type).second)
                Fail(Error::package_invalid);
        } else if(e.local=="Override") {
            const auto* part=xml.Attribute(e,"","PartName");
            if(!part||part->empty()||part->front()!='/'||!types.overrides.emplace(doc::detail::AsciiLower(*part),*type).second)
                Fail(Error::package_invalid);
        } else {
            Fail(Error::package_invalid);
        }
    }
    return types;
}

// ---- SpreadsheetML helpers ----
struct Sml {
    const XmlDocument& xml;
    bool Is(const XmlElement& e,std::string_view local) const {
        if(e.local!=local)return false;
        const auto ns=xml.Namespace(e);
        return ns==smlTransitional||ns==smlStrict;
    }
    const XmlElement* Child(const XmlElement& e,std::string_view local) const {
        for(const auto& c:e.children)if(!c.text&&Is(xml.elements[c.index],local))return &xml.elements[c.index];
        return nullptr;
    }
    template<class F> void Each(const XmlElement& e,std::string_view local,F&& visit) const {
        for(const auto& c:e.children)if(!c.text&&Is(xml.elements[c.index],local))visit(xml.elements[c.index]);
    }
    const std::string* Attr(const XmlElement& e,std::string_view local) const {return xml.Attribute(e,"",local);}
    const std::string* RelId(const XmlElement& e) const {
        if(const auto* v=xml.Attribute(e,rTransitional,"id"))return v;
        return xml.Attribute(e,rStrict,"id");
    }
    std::string Text(const XmlElement& e) const {
        std::string out;
        for(const auto& c:e.children)if(c.text)out+=xml.texts[c.index];
        return out;
    }
    bool Preserve(const XmlElement& t) const {
        for(const XmlElement* e=&t;;) {
            if(const auto* space=xml.Attribute(*e,doc::detail::xmlNamespace,"space"))return *space=="preserve";
            if(e->parent==0xffffffffu)return false;
            e=&xml.elements[e->parent];
        }
    }
};
bool Hex(char c,unsigned& v) {
    if(c>='0'&&c<='9') {v=unsigned(c-'0');return true;}
    if(c>='a'&&c<='f') {v=unsigned(c-'a'+10);return true;}
    if(c>='A'&&c<='F') {v=unsigned(c-'A'+10);return true;}
    return false;
}
// ST_Xstring escapes: _xHHHH_ is the UTF-16 code unit HHHH (used for control
// characters); _x005F_ escapes a literal underscore. Invalid forms stay literal.
std::string DecodeXstring(std::string_view s) {
    if(s.find("_x")==std::string_view::npos)return std::string(s);
    std::string out;
    for(std::size_t i=0;i<s.size();) {
        if(s.size()-i>=7&&s[i]=='_'&&s[i+1]=='x'&&s[i+6]=='_') {
            char32_t c=0;bool ok=true;
            for(int k=2;k<6;++k) {unsigned v=0;if(!Hex(s[i+std::size_t(k)],v)) {ok=false;break;}c=c*16+v;}
            const bool legal=c==0x9||c==0xa||c==0xd||(c>=0x20&&c<0xd800)||(c>=0xe000&&c<=0xfffd);
            if(ok&&legal) {doc::detail::AppendUtf8(out,c);i+=7;continue;}
        }
        out.push_back(s[i++]);
    }
    return out;
}
std::string TextElement(const Sml& sml,const XmlElement& t) {
    auto text=sml.Text(t);
    if(!sml.Preserve(t)) {
        const auto first=text.find_first_not_of(" \t\n");
        text=first==std::string::npos?std::string():text.substr(first,text.find_last_not_of(" \t\n")-first+1);
    }
    return DecodeXstring(text);
}
// CT_Rst: a plain <t>, or rich-text runs <r><t/></r>; phonetic runs (rPh) are skipped.
std::string RichText(const Sml& sml,const XmlElement& si) {
    std::string out;
    for(const auto& c:si.children) {
        if(c.text)continue;
        const auto& e=sml.xml.elements[c.index];
        if(sml.Is(e,"t"))out+=TextElement(sml,e);
        else if(sml.Is(e,"r"))if(const auto* t=sml.Child(e,"t"))out+=TextElement(sml,*t);
    }
    return out;
}

// ---- Number formats ----
enum class DateClass : std::uint8_t {none, date, time, datetime};
DateClass BuiltinClass(int id) {
    if(id>=14&&id<=17)return DateClass::date;
    if(id==22)return DateClass::datetime;
    if((id>=18&&id<=21)||(id>=45&&id<=47))return DateClass::time;
    return DateClass::none;
}
DateClass ClassifyFormat(std::string_view code) {
    bool date=false,time=false,month=false;
    for(std::size_t i=0;i<code.size();) {
        const char c=doc::detail::AsciiLower(code[i]);
        if(c==';')break; // Only the first (positive) section decides.
        if(c=='"') {const auto end=code.find('"',i+1);i=end==std::string_view::npos?code.size():end+1;continue;}
        if(c=='\\'||c=='_'||c=='*') {i+=2;continue;}
        if(c=='[') {
            const auto end=code.find(']',i+1);
            const auto inner=doc::detail::AsciiLower(code.substr(i+1,(end==std::string_view::npos?code.size():end)-i-1));
            if(!inner.empty()&&inner.find_first_not_of(inner[0])==std::string::npos&&(inner[0]=='h'||inner[0]=='m'||inner[0]=='s'))
                time=true; // Elapsed [h] [mm] [ss].
            i=end==std::string_view::npos?code.size():end+1;
            continue;
        }
        if(doc::detail::EqualsNoCase(code.substr(i,7),"general")) {i+=7;continue;}
        if(doc::detail::EqualsNoCase(code.substr(i,5),"am/pm")) {time=true;i+=5;continue;}
        if(doc::detail::EqualsNoCase(code.substr(i,3),"a/p")) {time=true;i+=3;continue;}
        if(c=='e'&&i+1<code.size()&&(code[i+1]=='+'||code[i+1]=='-')) {i+=2;continue;} // Scientific.
        if(c=='y'||c=='d'||c=='e'||c=='g')date=true;
        else if(c=='h'||c=='s')time=true;
        else if(c=='m')month=true;
        ++i;
    }
    if(month&&!time)date=true; // A lone m is a month; next to h or s it is minutes.
    if(date&&time)return DateClass::datetime;
    if(date)return DateClass::date;
    if(time)return DateClass::time;
    return DateClass::none;
}
int ParseId(const std::string* v) {
    if(!v||v->empty()||v->size()>9)return -1;
    int r=0;
    for(const char c:*v) {if(c<'0'||c>'9')return -1;r=r*10+(c-'0');}
    return r;
}
std::vector<DateClass> ParseStyles(const XmlDocument& xml) {
    const Sml sml{xml};
    if(!sml.Is(xml.Root(),"styleSheet"))Fail(Error::package_invalid);
    std::map<int,DateClass> custom;
    if(const auto* formats=sml.Child(xml.Root(),"numFmts"))
        sml.Each(*formats,"numFmt",[&](const XmlElement& f) {
            const auto id=ParseId(sml.Attr(f,"numFmtId"));
            const auto* code=sml.Attr(f,"formatCode");
            if(id>=0&&code)custom[id]=ClassifyFormat(*code);
        });
    std::vector<DateClass> xfs;
    if(const auto* cellXfs=sml.Child(xml.Root(),"cellXfs"))
        sml.Each(*cellXfs,"xf",[&](const XmlElement& xf) {
            const auto id=ParseId(sml.Attr(xf,"numFmtId"));
            const auto found=custom.find(id);
            xfs.push_back(found!=custom.end()?found->second:BuiltinClass(id));
        });
    return xfs;
}

// Serial date to ISO 8601. Returns false (keep the number) for negative serials,
// the fictitious 1900-02-29 (serial 60), day 0 of a date format, and years past 9999.
bool SerialToIso(double serial,DateClass cls,bool date1904,std::string& out) {
    if(!(serial>=0)||serial>2958466.0)return false;
    const auto ms=std::uint64_t(std::llround(serial*86400000.0));
    const auto day=ms/86400000,inDay=ms%86400000;
    out.clear();
    if(cls==DateClass::time&&day==0) {AppendClock(out,inDay/3600000,inDay);return true;}
    std::int64_t days1970=0;
    if(date1904) {
        days1970=-24107+std::int64_t(day);
    } else {
        if(day==0||day==60)return false;
        days1970=(day<60?-25568:-25569)+std::int64_t(day);
    }
    std::int64_t y=0;unsigned m=0,d=0;
    CivilFromDays(days1970,y,m,d);
    if(y>9999)return false;
    Pad(out,std::uint64_t(y),4);out.push_back('-');Pad(out,m,2);out.push_back('-');Pad(out,d,2);
    if(cls!=DateClass::date||inDay) {out.push_back('T');AppendClock(out,inDay/3600000,inDay);}
    return true;
}

struct SheetEntry {SheetInfo info;std::string part;};

class Workbook {
public:
    Workbook(Bytes bytes,const ReadOptions& options):zip_(bytes,options.zip),options_(options) {}
    Table Read(TableReport& report) {
        for(const auto& e:zip_.Entries()) {
            const auto lower=doc::detail::AsciiLower(e.name);
            if(lower=="encryptedpackage"||lower=="encryptioninfo")Fail(Error::package_encrypted);
            const auto slash=lower.rfind('/');
            const auto base=slash==std::string::npos?lower:lower.substr(slash+1);
            if(base=="vbaproject.bin"||base=="vbadata.xml")Fail(Error::package_macro_enabled);
        }
        const auto typesBytes=Part("/[Content_Types].xml");
        if(!typesBytes)Fail(Error::package_invalid);
        const auto types=ParseContentTypes(*typesBytes,options_.xml);
        for(const auto& [name,type]:types.overrides)if(type==vbaProjectType)Fail(Error::package_macro_enabled);
        const auto packageRels=Part("/_rels/.rels");
        if(!packageRels)Fail(Error::package_invalid);
        std::string main;
        for(const auto& r:ParseRelationships(*packageRels,options_.xml)) {
            if(!RelationshipType(r.type,"officeDocument"))continue;
            if(!main.empty()||r.external)Fail(Error::package_invalid);
            main=ResolvePart("/",r.target);
        }
        if(main.empty())Fail(Error::not_spreadsheet);
        const auto mainType=types.For(main);
        for(const auto macro:macroTypes)if(mainType==macro)Fail(Error::package_macro_enabled);
        if(mainType!=workbookType&&mainType!=templateType)Fail(Error::not_spreadsheet);
        const auto workbookBytes=Part(main);
        if(!workbookBytes)Fail(Error::package_missing_part);
        const auto relsBytes=Part(RelationshipsPartFor(main));
        if(!relsBytes)Fail(Error::package_invalid); // Sheets are only reachable through it.
        std::map<std::string,Relationship> rels;
        std::string sharedStringsPart,stylesPart;
        for(auto& r:ParseRelationships(*relsBytes,options_.xml)) {
            if(r.external)continue;
            if(RelationshipType(r.type,"sharedStrings"))sharedStringsPart=ResolvePart(DirectoryOf(main),r.target);
            else if(RelationshipType(r.type,"styles"))stylesPart=ResolvePart(DirectoryOf(main),r.target);
            rels.emplace(r.id,std::move(r));
        }

        bool date1904=false;
        std::vector<SheetEntry> sheets;
        {
            const auto xml=doc::detail::ParseXml(*workbookBytes,options_.xml);
            const Sml sml{xml};
            if(!sml.Is(xml.Root(),"workbook"))Fail(Error::not_spreadsheet);
            if(const auto* pr=sml.Child(xml.Root(),"workbookPr"))
                if(const auto* v=sml.Attr(*pr,"date1904"))date1904=*v=="1"||*v=="true";
            if(const auto* list=sml.Child(xml.Root(),"sheets"))
                sml.Each(*list,"sheet",[&](const XmlElement& s) {
                    const auto* name=sml.Attr(s,"name");
                    const auto* id=sml.RelId(s);
                    if(!name||!id)Fail(Error::package_invalid);
                    SheetEntry entry;
                    entry.info.name=*name;
                    if(const auto* state=sml.Attr(s,"state")) {
                        if(*state=="hidden")entry.info.state=SheetState::hidden;
                        else if(*state=="veryHidden")entry.info.state=SheetState::very_hidden;
                        else if(*state!="visible")Fail(Error::package_invalid);
                    }
                    const auto rel=rels.find(*id);
                    if(rel==rels.end())Fail(Error::package_invalid);
                    if(RelationshipType(rel->second.type,"worksheet"))entry.info.kind=SheetKind::worksheet;
                    else if(RelationshipType(rel->second.type,"chartsheet"))entry.info.kind=SheetKind::chartsheet;
                    else entry.info.kind=SheetKind::other;
                    entry.part=ResolvePart(DirectoryOf(main),rel->second.target);
                    sheets.push_back(std::move(entry));
                });
        }
        for(const auto& s:sheets)report.sheets.push_back(s.info);
        const auto selected=Select(sheets);
        report.selectedSheet=std::uint32_t(selected);
        if(sheets[selected].info.kind!=SheetKind::worksheet)Fail(Error::sheet_unsupported);

        std::vector<std::string> strings;
        if(!sharedStringsPart.empty())if(const auto bytes=Part(sharedStringsPart)) {
            const auto xml=doc::detail::ParseXml(*bytes,options_.xml);
            const Sml sml{xml};
            if(!sml.Is(xml.Root(),"sst"))Fail(Error::package_invalid);
            sml.Each(xml.Root(),"si",[&](const XmlElement& si) {
                if(strings.size()>=options_.table.maximumSharedStrings)Fail(Error::cell_limit);
                strings.push_back(RichText(sml,si));
            });
        }
        std::vector<DateClass> styles;
        if(!stylesPart.empty())if(const auto bytes=Part(stylesPart))styles=ParseStyles(doc::detail::ParseXml(*bytes,options_.xml));

        const auto sheetBytes=Part(sheets[selected].part);
        if(!sheetBytes)Fail(Error::package_missing_part);
        const auto xml=doc::detail::ParseXml(*sheetBytes,options_.xml);
        return Worksheet(xml,strings,styles,date1904,report);
    }
private:
    std::optional<std::string> Part(std::string_view name) {
        const auto* entry=zip_.Find(name.substr(name.front()=='/'?1:0));
        if(!entry)return std::nullopt;
        return zip_.Read(*entry);
    }
    std::size_t Select(const std::vector<SheetEntry>& sheets) const {
        const auto& s=options_.sheet;
        if(sheets.empty())Fail(Error::sheet_not_found);
        if(s.by==SheetSelection::index) {
            if(s.index>=sheets.size())Fail(Error::sheet_not_found);
            return s.index;
        }
        if(s.by==SheetSelection::name) {
            for(std::size_t i=0;i<sheets.size();++i)if(sheets[i].info.name==s.name)return i;
            Fail(Error::sheet_not_found);
        }
        for(std::size_t i=0;i<sheets.size();++i)
            if(sheets[i].info.state==SheetState::visible&&sheets[i].info.kind==SheetKind::worksheet)return i;
        for(std::size_t i=0;i<sheets.size();++i)if(sheets[i].info.kind==SheetKind::worksheet)return i;
        return 0; // Only chart/dialog sheets: reported as sheet_unsupported.
    }
    Table Worksheet(const XmlDocument& xml,const std::vector<std::string>& strings,const std::vector<DateClass>& styles,
        bool date1904,TableReport& report) {
        const Sml sml{xml};
        const auto& root=xml.Root();
        if(!sml.Is(root,"worksheet"))Fail(Error::not_spreadsheet);
        if(const auto* dimension=sml.Child(root,"dimension"))
            if(const auto* ref=sml.Attr(*dimension,"ref");ref&&ValidRange(*ref))report.dimension=*ref;
        SparseTable cells(options_.table);
        if(const auto* data=sml.Child(root,"sheetData")) {
            std::uint64_t nextRow=0;
            sml.Each(*data,"row",[&](const XmlElement& row) {
                std::uint64_t rowIndex=nextRow;
                if(const auto* r=sml.Attr(row,"r")) {
                    const auto n=ParseId(r);
                    if(n<1||n>1'048'576)Fail(Error::xlsx_malformed);
                    rowIndex=std::uint64_t(n-1);
                }
                nextRow=rowIndex+1;
                std::uint64_t nextColumn=0;
                sml.Each(row,"c",[&](const XmlElement& c) {
                    std::uint64_t r=rowIndex,column=nextColumn;
                    if(const auto* ref=sml.Attr(c,"r")) {
                        if(!ParseCellReference(*ref,r,column)||r!=rowIndex)Fail(Error::xlsx_malformed);
                    }
                    if(column>=16384)Fail(Error::xlsx_malformed);
                    nextColumn=column+1;
                    cells.Put(rowIndex,column,Cell(sml,c,strings,styles,date1904,report));
                });
            });
        }
        if(const auto* merges=sml.Child(root,"mergeCells"))
            sml.Each(*merges,"mergeCell",[&](const XmlElement& m) {
                const auto* ref=sml.Attr(m,"ref");
                if(!ref||!ValidRange(*ref))Fail(Error::xlsx_malformed);
                if(report.mergedRanges.size()>=options_.table.maximumCells)Fail(Error::cell_limit);
                report.mergedRanges.push_back(*ref);
            });
        return cells.Densify(Error::xlsx_malformed);
    }
    TableCell Cell(const Sml& sml,const XmlElement& c,const std::vector<std::string>& strings,
        const std::vector<DateClass>& styles,bool date1904,TableReport& report) {
        const auto* typeAttr=sml.Attr(c,"t");
        const std::string_view type=typeAttr?std::string_view(*typeAttr):"n";
        const auto* v=sml.Child(c,"v");
        const bool formula=sml.Child(c,"f")!=nullptr;
        TableCell cell;
        if(formula) {
            ++report.formulas;
            if(!v&&type!="inlineStr")++report.formulasWithoutValue;
        }
        const auto text=[&](std::string s) {
            if(!s.empty()) {cell.kind=CellKind::text;cell.value=std::move(s);}
        };
        if(type=="inlineStr") {
            if(const auto* is=sml.Child(c,"is"))text(RichText(sml,*is));
            return cell;
        }
        if(!v) {
            if(type!="n"&&type!="s"&&type!="str"&&type!="b"&&type!="e"&&type!="d")Fail(Error::xlsx_malformed);
            return cell; // Styled or formula cell without a value.
        }
        auto value=sml.Text(*v);
        if(type=="n"||type=="s"||type=="b") {
            // xsd:double, xsd:unsignedInt and xsd:boolean collapse XML whitespace, and
            // real writers emit `<v>2 </v>` (poi bug69769.xlsx) or an empty `<v/>`
            // (poi 56702/64508/64667.xlsx); an empty value is a blank cell, as in Excel.
            const auto first=value.find_first_not_of(" \t\r\n");
            if(first==std::string::npos)return cell;
            value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
        }
        if(type=="n") {
            double number=0;
            if(!ParseDouble(value,number))Fail(Error::xlsx_malformed);
            cell.kind=CellKind::number;cell.value=value;
            const auto style=ParseId(sml.Attr(c,"s"));
            const auto cls=style>=0&&std::size_t(style)<styles.size()?styles[std::size_t(style)]:DateClass::none;
            std::string iso;
            if(cls!=DateClass::none&&SerialToIso(number,cls,date1904,iso)) {
                cell.kind=CellKind::date;cell.formatted=value;cell.value=std::move(iso);
            }
        } else if(type=="s") {
            const auto index=ParseId(&value);
            if(index<0||std::size_t(index)>=strings.size())Fail(Error::xlsx_malformed);
            text(strings[std::size_t(index)]);
        } else if(type=="str") {
            text(DecodeXstring(value));
        } else if(type=="b") {
            if(value!="0"&&value!="1"&&value!="true"&&value!="false")Fail(Error::xlsx_malformed);
            cell.kind=CellKind::boolean;
            cell.value=(value=="1"||value=="true")?"TRUE":"FALSE";
            cell.formatted=value;
        } else if(type=="e") {
            if(value.empty())Fail(Error::xlsx_malformed);
            cell.kind=CellKind::error;cell.value=value;
        } else if(type=="d") {
            if(value.empty()||value[0]<'0'||value[0]>'9')Fail(Error::xlsx_malformed);
            cell.kind=CellKind::date;cell.value=value;
        } else {
            Fail(Error::xlsx_malformed);
        }
        return cell;
    }

    doc::detail::ZipArchive zip_;
    const ReadOptions& options_;
};
}

Table XlsxTable(Bytes bytes,const ReadOptions& options,TableReport& report) {
    static constexpr unsigned char compoundFile[8]={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    if(bytes.size()>=8&&std::memcmp(bytes.data(),compoundFile,8)==0)Fail(Error::ole_compound_file);
    Workbook workbook(bytes,options);
    return workbook.Read(report);
}
}
