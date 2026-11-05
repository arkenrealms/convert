#include "tabular_detail.hpp"
#include <cstring>
#include <optional>
#include <set>

// OpenDocument spreadsheet (ODS/OTS) cell values from content.xml. Policy (see
// NOTES.md "Tabular"): one table, typed office:value-type values, text:p display
// text, repeated rows/columns expanded only where they hold data (trailing
// empty repeats are counted, never materialized), covered cells as empty
// placeholders, cached values only (table:formula is never evaluated).
namespace arken::document::v1::tabular::detail {
namespace {
using doc::detail::XmlDocument;
using doc::detail::XmlElement;
constexpr std::string_view officeNs="urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr std::string_view tableNs="urn:oasis:names:tc:opendocument:xmlns:table:1.0";
constexpr std::string_view textNs="urn:oasis:names:tc:opendocument:xmlns:text:1.0";
constexpr std::string_view styleNs="urn:oasis:names:tc:opendocument:xmlns:style:1.0";
constexpr std::string_view manifestNs="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
constexpr std::string_view calcextNs="urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0";
constexpr std::string_view spreadsheetType="application/vnd.oasis.opendocument.spreadsheet";
constexpr std::string_view spreadsheetTemplateType="application/vnd.oasis.opendocument.spreadsheet-template";
constexpr std::uint64_t maximumRepeat=1ull<<31;
constexpr std::uint64_t saturation=1ull<<62;

struct Odf {
    const XmlDocument& xml;
    bool Is(const XmlElement& e,std::string_view ns,std::string_view local) const {return e.local==local&&xml.Namespace(e)==ns;}
    const XmlElement* Child(const XmlElement& e,std::string_view ns,std::string_view local) const {
        for(const auto& c:e.children)if(!c.text&&Is(xml.elements[c.index],ns,local))return &xml.elements[c.index];
        return nullptr;
    }
    template<class F> void Elements(const XmlElement& e,F&& visit) const {
        for(const auto& c:e.children)if(!c.text)visit(xml.elements[c.index]);
    }
    const std::string* Attr(const XmlElement& e,std::string_view ns,std::string_view local) const {return xml.Attribute(e,ns,local);}
};

std::uint64_t Repeat(const std::string* v) {
    if(!v)return 1;
    if(v->empty()||v->size()>10)Fail(Error::ods_malformed);
    std::uint64_t n=0;
    for(const char c:*v) {
        if(c<'0'||c>'9')Fail(Error::ods_malformed);
        n=n*10+std::uint64_t(c-'0');
    }
    if(n<1||n>maximumRepeat)Fail(Error::ods_malformed);
    return n;
}
std::uint64_t Advance(std::uint64_t position,std::uint64_t count) {return std::min(saturation,position+count);}

// ISO 8601 duration (PnDTnHnMn.nS) as an hh:mm:ss[.fff] clock with total hours.
bool DurationToClock(std::string_view s,std::string& out) {
    std::size_t p=0;
    bool negative=false;
    if(p<s.size()&&s[p]=='-') {negative=true;++p;}
    if(p>=s.size()||s[p++]!='P'||negative)return false;
    long double total=0;
    bool inTime=false,any=false;
    while(p<s.size()) {
        if(s[p]=='T') {if(inTime)return false;inTime=true;++p;continue;}
        const auto start=p;
        while(p<s.size()&&((s[p]>='0'&&s[p]<='9')||s[p]=='.'))++p;
        if(p==start||p>=s.size()||p-start>20)return false;
        double v=0;
        if(!ParseDouble(s.substr(start,p-start),v))return false;
        const char unit=s[p++];
        if(!inTime&&unit=='D')total+=v*86400.0L;
        else if(inTime&&unit=='H')total+=v*3600.0L;
        else if(inTime&&unit=='M')total+=v*60.0L;
        else if(inTime&&unit=='S')total+=v;
        else return false;
        any=true;
    }
    if(!any||total>1e12L)return false;
    const auto ms=std::uint64_t(std::llround(double(total*1000.0L)));
    out.clear();
    AppendClock(out,ms/3600000,ms);
    return true;
}

class Spreadsheet {
public:
    Spreadsheet(const XmlDocument& xml,const ReadOptions& options,TableReport& report)
        :odf_{xml},xml_(xml),options_(options),report_(report),cells_(options.table) {}
    Table Read() {
        const auto& root=xml_.Root();
        if(!odf_.Is(root,officeNs,"document-content"))Fail(Error::not_spreadsheet);
        std::set<std::string> hiddenStyles;
        if(const auto* styles=odf_.Child(root,officeNs,"automatic-styles"))
            odf_.Elements(*styles,[&](const XmlElement& s) {
                if(!odf_.Is(s,styleNs,"style"))return;
                const auto* family=odf_.Attr(s,styleNs,"family");
                const auto* name=odf_.Attr(s,styleNs,"name");
                const auto* props=odf_.Child(s,styleNs,"table-properties");
                if(family&&*family=="table"&&name&&props)
                    if(const auto* display=odf_.Attr(*props,tableNs,"display");display&&*display=="false")hiddenStyles.insert(*name);
            });
        const auto* body=odf_.Child(root,officeNs,"body");
        const auto* spreadsheet=body?odf_.Child(*body,officeNs,"spreadsheet"):nullptr;
        if(!spreadsheet)Fail(Error::not_spreadsheet);
        std::vector<const XmlElement*> tables;
        odf_.Elements(*spreadsheet,[&](const XmlElement& t) {
            if(!odf_.Is(t,tableNs,"table"))return;
            SheetInfo info;
            if(const auto* name=odf_.Attr(t,tableNs,"name"))info.name=*name;
            if(const auto* style=odf_.Attr(t,tableNs,"style-name");style&&hiddenStyles.count(*style))info.state=SheetState::hidden;
            report_.sheets.push_back(std::move(info));
            tables.push_back(&t);
        });
        const auto selected=Select();
        report_.selectedSheet=std::uint32_t(selected);
        Rows(*tables[selected]);
        auto table=cells_.Densify(Error::ods_malformed);
        const auto height=table.rows.size();
        const auto width=table.columns;
        for(const auto& [start,count]:emptyRowRuns_)
            report_.trimmedRepeatedRows+=start+count-std::max<std::uint64_t>(start,std::min<std::uint64_t>(start+count,height));
        for(const auto& run:emptyCellRuns_) {
            const auto kept=std::max<std::uint64_t>(run.start,std::min<std::uint64_t>(run.start+run.count,width));
            const auto trimmed=run.start+run.count-kept;
            report_.trimmedRepeatedCells=std::min(saturation,report_.trimmedRepeatedCells+trimmed*run.rows);
        }
        return table;
    }
private:
    struct CellRun {std::uint64_t start,count,rows;};
    struct Pending {std::uint64_t column,count;TableCell cell;};

    std::size_t Select() const {
        const auto& sheets=report_.sheets;
        const auto& s=options_.sheet;
        if(sheets.empty())Fail(Error::sheet_not_found);
        if(s.by==SheetSelection::index) {
            if(s.index>=sheets.size())Fail(Error::sheet_not_found);
            return s.index;
        }
        if(s.by==SheetSelection::name) {
            for(std::size_t i=0;i<sheets.size();++i)if(sheets[i].name==s.name)return i;
            Fail(Error::sheet_not_found);
        }
        for(std::size_t i=0;i<sheets.size();++i)if(sheets[i].state==SheetState::visible)return i;
        return 0;
    }
    void Rows(const XmlElement& container) {
        odf_.Elements(container,[&](const XmlElement& e) {
            if(odf_.Is(e,tableNs,"table-row"))Row(e);
            else if(odf_.Is(e,tableNs,"table-header-rows")||odf_.Is(e,tableNs,"table-rows")||odf_.Is(e,tableNs,"table-row-group"))Rows(e);
        });
    }
    void Row(const XmlElement& row) {
        const auto rowRepeat=Repeat(odf_.Attr(row,tableNs,"number-rows-repeated"));
        std::vector<Pending> pending;
        std::vector<CellRun> emptyRuns;
        std::uint64_t column=0,cellsInRow=0,formulas=0;
        odf_.Elements(row,[&](const XmlElement& c) {
            const bool covered=odf_.Is(c,tableNs,"covered-table-cell");
            if(!covered&&!odf_.Is(c,tableNs,"table-cell"))return;
            const auto repeat=Repeat(odf_.Attr(c,tableNs,"number-columns-repeated"));
            if(!covered) {
                if(odf_.Attr(c,tableNs,"formula"))formulas+=repeat;
                const auto columns=Repeat(odf_.Attr(c,tableNs,"number-columns-spanned"));
                const auto rows=Repeat(odf_.Attr(c,tableNs,"number-rows-spanned"));
                if((columns>1||rows>1)&&row_<1'048'576&&column<16'384) {
                    if(report_.mergedRanges.size()>=options_.table.maximumCells)Fail(Error::cell_limit);
                    report_.mergedRanges.push_back(CellName(row_,column)+":"
                        +CellName(std::min<std::uint64_t>(row_+rows-1,1'048'575),std::min<std::uint64_t>(column+columns-1,16'383)));
                }
            }
            auto cell=covered?TableCell{}:Cell(c);
            if(cell.kind==CellKind::empty) {
                if(repeat>1)emptyRuns.push_back({column,repeat,0});
            } else {
                if(column>=options_.table.maximumColumns)Fail(Error::column_limit);
                cellsInRow=std::min(saturation,cellsInRow+repeat);
                if(cellsInRow>options_.table.maximumCells)Fail(Error::cell_limit);
                pending.push_back({column,repeat,std::move(cell)});
            }
            column=Advance(column,repeat);
        });
        report_.formulas=std::min(saturation,report_.formulas+formulas*rowRepeat);
        if(pending.empty()) {
            if(rowRepeat>1)emptyRowRuns_.emplace_back(row_,rowRepeat);
            row_=Advance(row_,rowRepeat);
            return;
        }
        // Repeated rows that hold data are materialized; bound them before expanding.
        if(row_+rowRepeat>options_.table.maximumRows)Fail(Error::row_limit);
        const auto remaining=options_.table.maximumCells-std::min(cells_.Count(),options_.table.maximumCells);
        if(cellsInRow>remaining/rowRepeat)Fail(Error::cell_limit);
        for(auto& run:emptyRuns) {run.rows=rowRepeat;emptyCellRuns_.push_back(run);}
        for(std::uint64_t r=0;r<rowRepeat;++r)
            for(const auto& p:pending)
                for(std::uint64_t k=0;k<p.count;++k)cells_.Put(row_+r,p.column+k,TableCell(p.cell));
        row_+=rowRepeat;
    }
    // text:p/text:h paragraphs joined by LF; spans, links and fields contribute their text.
    void Inline(const XmlElement& e,std::string& out) const {
        for(const auto& c:e.children) {
            if(c.text) {Append(out,xml_.texts[c.index]);continue;}
            const auto& child=xml_.elements[c.index];
            if(odf_.Is(child,textNs,"s")) {
                const auto count=Repeat(odf_.Attr(child,textNs,"c"));
                if(count>options_.table.maximumCellBytes-std::min<std::uint64_t>(out.size(),options_.table.maximumCellBytes))
                    Fail(Error::cell_size_limit);
                out.append(std::size_t(count),' ');
            }
            else if(odf_.Is(child,textNs,"tab"))Append(out,"\t");
            else if(odf_.Is(child,textNs,"line-break"))Append(out,"\n");
            else if(odf_.Is(child,officeNs,"annotation")||odf_.Is(child,textNs,"note"))continue;
            else Inline(child,out);
        }
    }
    void Append(std::string& out,std::string_view text) const {
        if(out.size()+text.size()>options_.table.maximumCellBytes)Fail(Error::cell_size_limit);
        out.append(text);
    }
    std::string DisplayText(const XmlElement& cell) const {
        std::string out;
        bool first=true;
        odf_.Elements(cell,[&](const XmlElement& p) {
            if(!odf_.Is(p,textNs,"p")&&!odf_.Is(p,textNs,"h"))return;
            if(!first)Append(out,"\n");
            first=false;
            Inline(p,out);
        });
        return out;
    }
    TableCell Cell(const XmlElement& c) const {
        TableCell cell;
        auto display=DisplayText(c);
        const auto* type=odf_.Attr(c,officeNs,"value-type");
        const auto* extType=odf_.Attr(c,calcextNs,"value-type");
        const auto value=[&](std::string_view local)->const std::string& {
            const auto* v=odf_.Attr(c,officeNs,local);
            if(!v)Fail(Error::ods_malformed);
            return *v;
        };
        if(extType&&*extType=="error") {
            cell.kind=CellKind::error;
            cell.value=display.empty()?std::string("#ERR"):display;
            return cell;
        }
        const std::string_view t=type?std::string_view(*type):"";
        if(t=="float"||t=="percentage"||t=="currency") {
            double number=0;
            if(!ParseDouble(value("value"),number))Fail(Error::ods_malformed);
            cell.kind=CellKind::number;cell.value=value("value");cell.formatted=std::move(display);
        } else if(t=="date") {
            const auto& v=value("date-value");
            if(v.empty()||((v[0]<'0'||v[0]>'9')&&v[0]!='-'))Fail(Error::ods_malformed);
            cell.kind=CellKind::date;cell.value=v;cell.formatted=std::move(display);
        } else if(t=="time") {
            std::string clock;
            if(!DurationToClock(value("time-value"),clock))Fail(Error::ods_malformed);
            cell.kind=CellKind::date;cell.value=std::move(clock);cell.formatted=std::move(display);
        } else if(t=="boolean") {
            const auto& v=value("boolean-value");
            if(v!="true"&&v!="false")Fail(Error::ods_malformed);
            cell.kind=CellKind::boolean;cell.value=v=="true"?"TRUE":"FALSE";cell.formatted=std::move(display);
        } else {
            // string, void, or no type: the string value if given, else the paragraphs.
            const auto* s=t=="string"?odf_.Attr(c,officeNs,"string-value"):nullptr;
            std::string text=s?*s:std::move(display);
            if(!text.empty()) {cell.kind=CellKind::text;cell.value=std::move(text);}
        }
        if(cell.formatted==cell.value)cell.formatted.clear();
        return cell;
    }

    Odf odf_;
    const XmlDocument& xml_;
    const ReadOptions& options_;
    TableReport& report_;
    SparseTable cells_;
    std::uint64_t row_=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> emptyRowRuns_;
    std::vector<CellRun> emptyCellRuns_;
};

void CheckManifest(const XmlDocument& xml,std::string_view mimetype) {
    const Odf odf{xml};
    const auto& root=xml.Root();
    if(!odf.Is(root,manifestNs,"manifest"))Fail(Error::package_invalid);
    bool rootEntry=false;
    odf.Elements(root,[&](const XmlElement& entry) {
        if(!odf.Is(entry,manifestNs,"file-entry"))return;
        const auto* path=odf.Attr(entry,manifestNs,"full-path");
        const auto* type=odf.Attr(entry,manifestNs,"media-type");
        if(!path||!type)Fail(Error::package_invalid);
        if(odf.Child(entry,manifestNs,"encryption-data"))Fail(Error::package_encrypted);
        if(*path=="/") {
            if(rootEntry||*type!=mimetype)Fail(Error::package_invalid);
            rootEntry=true;
        }
    });
    if(!rootEntry)Fail(Error::package_invalid);
}
}

Table OdsTable(Bytes bytes,const ReadOptions& options,TableReport& report) {
    static constexpr unsigned char compoundFile[8]={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    if(bytes.size()>=8&&std::memcmp(bytes.data(),compoundFile,8)==0)Fail(Error::ole_compound_file);
    doc::detail::ZipArchive zip(bytes,options.zip);
    const auto read=[&](std::string_view name)->std::optional<std::string> {
        const auto* entry=zip.Find(name);
        if(!entry)return std::nullopt;
        return zip.Read(*entry);
    };
    const auto mimetype=read("mimetype");
    if(!mimetype)Fail(Error::package_invalid);
    if(*mimetype!=spreadsheetType&&*mimetype!=spreadsheetTemplateType) {
        if(mimetype->rfind("application/vnd.oasis.opendocument.",0)==0)Fail(Error::not_spreadsheet);
        Fail(Error::package_invalid);
    }
    const auto manifest=read("META-INF/manifest.xml");
    if(!manifest)Fail(Error::package_invalid);
    CheckManifest(doc::detail::ParseXml(*manifest,options.xml),*mimetype);
    const auto content=read("content.xml");
    if(!content)Fail(Error::package_missing_part);
    const auto xml=doc::detail::ParseXml(*content,options.xml);
    Spreadsheet sheet(xml,options,report);
    return sheet.Read();
}
}
