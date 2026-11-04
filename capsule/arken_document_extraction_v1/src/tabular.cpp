#include "tabular_detail.hpp"
#include <limits>
#include <map>
#include <set>

// Delimited text (RFC 4180 CSV/TSV) and JSON (RFC 8259) for the Table model,
// plus the public entry points. Boundary: everything CSV/TSV/JSON is inside
// this file, behind TableFromDelimited, TableFromJson, DelimitedFromTable and
// JsonFromTable. The XLSX/ODS readers never call into it.
namespace arken::document::v1::tabular {
namespace detail {
namespace {
bool IsJsonNumber(std::string_view s) {
    std::size_t p=0;
    const auto digits=[&] {const auto start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;return p-start;};
    if(p<s.size()&&s[p]=='-')++p;
    if(p>=s.size())return false;
    if(s[p]=='0') {++p;}
    else if(s[p]>='1'&&s[p]<='9') {digits();}
    else return false;
    if(p<s.size()&&s[p]=='.') {++p;if(!digits())return false;}
    if(p<s.size()&&(s[p]=='e'||s[p]=='E')) {
        ++p;
        if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;
        if(!digits())return false;
    }
    return p==s.size();
}
// A JSON number whose text is exactly the shortest binary64 round-trip form,
// so any conforming consumer reads back the same value and text.
bool ExactJsonNumber(std::string_view s) {
    if(!IsJsonNumber(s))return false;
    double d=0;
    if(!ParseDouble(s,d))return false;
    char buffer[64];
    const auto [end,ec]=std::to_chars(buffer,buffer+sizeof buffer,d);
    return ec==std::errc()&&std::string_view(buffer,std::size_t(end-buffer))==s;
}
bool ValidDelimiter(char c) {return c==','||c==';'||c=='\t';}
std::string_view StripBom(std::string_view s,TableReport& report) {
    if(s.substr(0,3)=="\xef\xbb\xbf") {report.byteOrderMark=true;s.remove_prefix(3);}
    return s;
}

// Pads short rows, counts ragged rows and enforces the dense cell budget.
Table Rectangular(std::vector<std::vector<TableCell>>&& rows,const TableLimits& limits,TableReport& report) {
    std::uint64_t width=0;
    for(const auto& row:rows)width=std::max<std::uint64_t>(width,row.size());
    Table table;
    if(!width)return table;
    if(rows.size()>limits.maximumCells/width)Fail(Error::cell_limit);
    for(auto& row:rows) {
        if(row.size()!=width) {++report.raggedRows;row.resize(width);}
    }
    table.rows=std::move(rows);
    table.columns=width;
    return table;
}

class CellSink {
public:
    explicit CellSink(const TableLimits& limits):limits_(limits),budget_(limits) {}
    void Add(std::vector<TableCell>& row,TableCell&& cell) {
        if(row.size()>=limits_.maximumColumns)Fail(Error::column_limit);
        Charge(cell);
        row.push_back(std::move(cell));
    }
    // Counts and charges a cell that is stored elsewhere.
    void Charge(const TableCell& cell) {
        if(++cells_>limits_.maximumCells)Fail(Error::cell_limit);
        budget_.Charge(cell);
    }
    void Row(std::vector<std::vector<TableCell>>& rows,std::vector<TableCell>&& row) {
        if(rows.size()>=limits_.maximumRows)Fail(Error::row_limit);
        rows.push_back(std::move(row));
    }
private:
    const TableLimits& limits_;
    TextBudget budget_;
    std::uint64_t cells_=0;
};
}

// ---- Delimited text ----
Table TableFromDelimited(std::string_view s,char delimiter,bool inferTypes,const TableLimits& limits,TableReport& report) {
    s=StripBom(s,report);
    if(!doc::detail::ValidUtf8(s))Fail(Error::csv_encoding);
    std::vector<std::vector<TableCell>> rows;
    if(s.empty())return {};
    CellSink sink(limits);
    std::vector<TableCell> row;
    std::string field;
    const auto n=s.size();
    std::size_t p=0;
    const auto cell=[&] {
        if(field.size()>limits.maximumCellBytes)Fail(Error::cell_size_limit);
        TableCell c;
        if(!field.empty()) {
            c.kind=CellKind::text;
            if(inferTypes) {
                if(IsJsonNumber(field))c.kind=CellKind::number;
                else if(doc::detail::EqualsNoCase(field,"true")||doc::detail::EqualsNoCase(field,"false"))c.kind=CellKind::boolean;
            }
            c.value=std::move(field);
        }
        field.clear();
        sink.Add(row,std::move(c));
    };
    for(;;) {
        if(p<n&&s[p]=='"') {
            ++p;
            for(;;) {
                const auto quote=s.find('"',p);
                if(quote==std::string_view::npos)Fail(Error::csv_malformed); // Unterminated quoted field.
                if(field.size()+(quote-p)>limits.maximumCellBytes)Fail(Error::cell_size_limit);
                field.append(s.substr(p,quote-p));
                p=quote+1;
                if(p<n&&s[p]=='"') {field.push_back('"');++p;continue;}
                break;
            }
            if(p<n&&s[p]!=delimiter&&s[p]!='\r'&&s[p]!='\n')Fail(Error::csv_malformed);
        } else {
            const auto start=p;
            while(p<n&&s[p]!=delimiter&&s[p]!='\r'&&s[p]!='\n')++p;
            field.assign(s.substr(start,p-start)); // A bare '"' inside an unquoted field is literal.
        }
        cell();
        if(p>=n)break;
        if(s[p]==delimiter) {++p;continue;}
        if(s[p]=='\r'&&p+1<n&&s[p+1]=='\n')++p;
        ++p;
        sink.Row(rows,std::move(row));
        row.clear();
        if(p>=n)break;
    }
    if(!row.empty())sink.Row(rows,std::move(row));
    return Rectangular(std::move(rows),limits,report);
}

std::string DelimitedFromTable(const Table& table,char delimiter,const WriteOptions& options) {
    std::string out;
    if(options.byteOrderMark)out="\xef\xbb\xbf";
    const std::string_view eol=options.crlf?"\r\n":"\n";
    const std::string special{delimiter,'"','\r','\n'};
    for(const auto& row:table.rows) {
        for(std::size_t i=0;i<row.size();++i) {
            if(i)out.push_back(delimiter);
            const auto& text=options.formattedText?row[i].Display():row[i].value;
            const bool quote=text.find_first_of(special)!=std::string::npos
                ||(row.size()==1&&text.empty()); // A lone empty field must not read back as a blank line.
            if(!quote) {out+=text;continue;}
            out.push_back('"');
            for(const char c:text) {
                if(c=='"')out.push_back('"');
                out.push_back(c);
            }
            out.push_back('"');
        }
        out+=eol;
        if(out.size()>options.maximumOutputBytes)Fail(Error::output_limit);
    }
    if(out.size()>options.maximumOutputBytes)Fail(Error::output_limit);
    return out;
}

// ---- JSON ----
namespace {
constexpr int maximumJsonDepth=64;
class JsonReader {
public:
    JsonReader(std::string_view s,const TableLimits& limits):s_(s),limits_(limits),sink_(limits) {}
    std::vector<std::vector<TableCell>> Run(bool& notTable) {
        Space();
        if(Peek()=='[') {Rows();}
        else {notTable_=true;Skip(0);}
        Space();
        if(p_!=s_.size())Fail(Error::json_malformed);
        notTable=notTable_;
        if(objects_&&!notTable_) {
            // Header row: keys in first-seen order, then each object placed by key.
            std::vector<std::vector<TableCell>> rows;
            std::vector<TableCell> header;
            for(auto& key:keys_) {
                TableCell c;
                if(!key.empty()) {c.kind=CellKind::text;c.value=key;}
                sink_.Add(header,std::move(c));
            }
            sink_.Row(rows,std::move(header));
            for(auto& members:objectRows_) {
                std::vector<TableCell> row(keys_.size());
                for(auto& [index,cell]:members)row[index]=std::move(cell);
                sink_.Row(rows,std::move(row));
            }
            return rows;
        }
        return std::move(rows_);
    }
private:
    char Peek() const {return p_<s_.size()?s_[p_]:'\0';}
    void Space() {while(p_<s_.size()&&(s_[p_]==' '||s_[p_]=='\t'||s_[p_]=='\n'||s_[p_]=='\r'))++p_;}
    void Expect(char c) {if(Peek()!=c||p_>=s_.size())Fail(Error::json_malformed);++p_;}
    void Rows() {
        ++p_;Space();
        if(Peek()==']') {++p_;return;}
        for(;;) {
            Space();
            const char c=Peek();
            if(c=='['&&!objects_) {arrays_=true;ArrayRow();}
            else if(c=='{'&&!arrays_) {objects_=true;ObjectRow();}
            else {notTable_=true;Skip(1);}
            Space();
            if(Peek()==',') {++p_;continue;}
            Expect(']');
            return;
        }
    }
    void ArrayRow() {
        ++p_;Space();
        std::vector<TableCell> row;
        if(Peek()==']') {++p_;Commit(std::move(row));return;}
        for(;;) {
            Space();
            TableCell cell;
            if(Scalar(cell)) {if(!notTable_)sink_.Add(row,std::move(cell));}
            else {notTable_=true;Skip(2);}
            Space();
            if(Peek()==',') {++p_;continue;}
            Expect(']');
            break;
        }
        Commit(std::move(row));
    }
    void ObjectRow() {
        ++p_;Space();
        std::vector<std::pair<std::size_t,TableCell>> members;
        std::set<std::string> seen;
        if(Peek()=='}') {++p_;CommitObject(std::move(members));return;}
        for(;;) {
            Space();
            if(Peek()!='"')Fail(Error::json_malformed);
            auto key=String();
            if(!seen.insert(key).second)Fail(Error::json_malformed); // Duplicate member name.
            Space();Expect(':');Space();
            TableCell cell;
            if(Scalar(cell)) {
                if(!notTable_) {
                    auto found=keyIndex_.find(key);
                    if(found==keyIndex_.end()) {
                        if(keys_.size()>=limits_.maximumColumns)Fail(Error::column_limit);
                        found=keyIndex_.emplace(key,keys_.size()).first;
                        keys_.push_back(key);
                    }
                    sink_.Charge(cell);
                    members.emplace_back(found->second,std::move(cell));
                }
            } else {
                notTable_=true;Skip(2);
            }
            Space();
            if(Peek()==',') {++p_;continue;}
            Expect('}');
            break;
        }
        CommitObject(std::move(members));
    }
    void Commit(std::vector<TableCell>&& row) {if(!notTable_)sink_.Row(rows_,std::move(row));}
    void CommitObject(std::vector<std::pair<std::size_t,TableCell>>&& members) {
        if(notTable_)return;
        if(objectRows_.size()+1>=limits_.maximumRows)Fail(Error::row_limit); // The header takes one row.
        objectRows_.push_back(std::move(members));
    }
    // Parses a scalar into cell; returns false (consuming nothing) for arrays and objects.
    bool Scalar(TableCell& cell) {
        const char c=Peek();
        if(c=='['||c=='{')return false;
        if(c=='"') {
            cell.value=String();
            if(!cell.value.empty())cell.kind=CellKind::text;
        } else if(c=='t') {Literal("true");cell.kind=CellKind::boolean;cell.value="true";}
        else if(c=='f') {Literal("false");cell.kind=CellKind::boolean;cell.value="false";}
        else if(c=='n') {Literal("null");}
        else {cell.kind=CellKind::number;cell.value=std::string(Number());}
        return true;
    }
    void Literal(std::string_view word) {
        if(s_.substr(p_,word.size())!=word)Fail(Error::json_malformed);
        p_+=word.size();
    }
    std::string_view Number() {
        const auto start=p_;
        while(p_<s_.size()) {
            const char c=s_[p_];
            if((c>='0'&&c<='9')||c=='-'||c=='+'||c=='.'||c=='e'||c=='E')++p_;
            else break;
        }
        const auto text=s_.substr(start,p_-start);
        if(!IsJsonNumber(text))Fail(Error::json_malformed);
        if(text.size()>limits_.maximumCellBytes)Fail(Error::cell_size_limit);
        return text;
    }
    unsigned Hex4() {
        if(s_.size()-p_<4)Fail(Error::json_malformed);
        unsigned v=0;
        for(int i=0;i<4;++i) {
            const char d=s_[p_++];
            v<<=4;
            if(d>='0'&&d<='9')v|=unsigned(d-'0');
            else if(d>='a'&&d<='f')v|=unsigned(d-'a'+10);
            else if(d>='A'&&d<='F')v|=unsigned(d-'A'+10);
            else Fail(Error::json_malformed);
        }
        return v;
    }
    std::string String() {
        ++p_;
        std::string out;
        for(;;) {
            if(p_>=s_.size())Fail(Error::json_malformed);
            const char c=s_[p_];
            if(c=='"') {++p_;break;}
            if(std::uint8_t(c)<0x20)Fail(Error::json_malformed);
            if(c!='\\') {
                const auto start=p_;
                while(p_<s_.size()&&s_[p_]!='"'&&s_[p_]!='\\'&&std::uint8_t(s_[p_])>=0x20)++p_;
                out.append(s_.substr(start,p_-start));
            } else {
                ++p_;
                if(p_>=s_.size())Fail(Error::json_malformed);
                const char e=s_[p_++];
                switch(e) {
                    case '"': out.push_back('"');break;
                    case '\\': out.push_back('\\');break;
                    case '/': out.push_back('/');break;
                    case 'b': out.push_back('\b');break;
                    case 'f': out.push_back('\f');break;
                    case 'n': out.push_back('\n');break;
                    case 'r': out.push_back('\r');break;
                    case 't': out.push_back('\t');break;
                    case 'u': {
                        char32_t u=Hex4();
                        if(u>=0xdc00&&u<=0xdfff)Fail(Error::json_malformed); // Lone low surrogate.
                        if(u>=0xd800&&u<=0xdbff) {
                            if(s_.substr(p_,2)!="\\u")Fail(Error::json_malformed);
                            p_+=2;
                            const auto low=Hex4();
                            if(low<0xdc00||low>0xdfff)Fail(Error::json_malformed);
                            u=0x10000+((u-0xd800)<<10)+(low-0xdc00);
                        }
                        doc::detail::AppendUtf8(out,u);
                        break;
                    }
                    default: Fail(Error::json_malformed);
                }
            }
            if(out.size()>limits_.maximumCellBytes)Fail(Error::cell_size_limit);
        }
        return out;
    }
    // Validates and discards any value (used once the input is known not to be a table).
    void Skip(int depth) {
        if(depth>=maximumJsonDepth)Fail(Error::json_malformed);
        Space();
        const char c=Peek();
        if(c=='['||c=='{') {
            const char close=c=='['?']':'}';
            ++p_;Space();
            if(Peek()==close) {++p_;return;}
            for(;;) {
                Space();
                if(c=='{') {
                    if(Peek()!='"')Fail(Error::json_malformed);
                    (void)String();
                    Space();Expect(':');
                }
                Skip(depth+1);
                Space();
                if(Peek()==',') {++p_;continue;}
                Expect(close);
                return;
            }
        }
        TableCell ignored;
        (void)Scalar(ignored);
    }

    std::string_view s_;
    const TableLimits& limits_;
    CellSink sink_;
    std::size_t p_=0;
    bool notTable_=false,arrays_=false,objects_=false;
    std::vector<std::vector<TableCell>> rows_;
    std::vector<std::vector<std::pair<std::size_t,TableCell>>> objectRows_;
    std::vector<std::string> keys_;
    std::map<std::string,std::size_t> keyIndex_;
};

void JsonString(std::string& out,std::string_view s) {
    static constexpr char hex[]="0123456789abcdef";
    out.push_back('"');
    for(const char c:s) {
        const auto u=std::uint8_t(c);
        switch(c) {
            case '"': out+="\\\"";break;
            case '\\': out+="\\\\";break;
            case '\b': out+="\\b";break;
            case '\f': out+="\\f";break;
            case '\n': out+="\\n";break;
            case '\r': out+="\\r";break;
            case '\t': out+="\\t";break;
            default:
                if(u<0x20) {out+="\\u00";out.push_back(hex[u>>4]);out.push_back(hex[u&15]);}
                else out.push_back(c);
        }
    }
    out.push_back('"');
}
void JsonCell(std::string& out,const TableCell& cell,const WriteOptions& options) {
    if(options.formattedText&&!cell.formatted.empty()) {JsonString(out,cell.formatted);return;}
    switch(cell.kind) {
        case CellKind::empty:
            out+=options.emptyAsNull?"null":"\"\"";
            return;
        case CellKind::boolean:
            if(doc::detail::EqualsNoCase(cell.value,"true")) {out+="true";return;}
            if(doc::detail::EqualsNoCase(cell.value,"false")) {out+="false";return;}
            break;
        case CellKind::number:
            if(ExactJsonNumber(cell.value)) {out+=cell.value;return;}
            break;
        default: break;
    }
    JsonString(out,cell.value);
}
}

Table TableFromJson(std::string_view s,const TableLimits& limits,TableReport& report) {
    s=StripBom(s,report);
    if(!doc::detail::ValidUtf8(s))Fail(Error::json_encoding);
    JsonReader reader(s,limits);
    bool notTable=false;
    auto rows=reader.Run(notTable);
    if(notTable)Fail(Error::json_not_table);
    return Rectangular(std::move(rows),limits,report);
}

std::string JsonFromTable(const Table& table,const WriteOptions& options,std::uint64_t& renamed) {
    std::string out="[";
    const auto check=[&] {if(out.size()>options.maximumOutputBytes)Fail(Error::output_limit);};
    bool first=true;
    if(options.json==JsonShape::arrays) {
        for(const auto& row:table.rows) {
            out+=first?"\n[":",\n[";first=false;
            for(std::size_t i=0;i<row.size();++i) {
                if(i)out.push_back(',');
                JsonCell(out,row[i],options);
            }
            out.push_back(']');
            check();
        }
    } else if(!table.rows.empty()) {
        // Header names become keys in column order; empty names become column_N and
        // duplicates gain _2, _3... so every object has unique, deterministic keys.
        std::vector<std::string> keys;
        std::set<std::string> used;
        const auto& header=table.rows.front();
        for(std::size_t i=0;i<header.size();++i) {
            std::string key=options.formattedText?header[i].Display():header[i].value;
            if(key.empty()) {key="column_"+std::to_string(i+1);++renamed;}
            if(used.count(key)) {
                ++renamed;
                for(std::uint64_t n=2;;++n) {
                    auto candidate=key+"_"+std::to_string(n);
                    if(!used.count(candidate)) {key=std::move(candidate);break;}
                }
            }
            used.insert(key);
            std::string encoded;
            JsonString(encoded,key);
            keys.push_back(std::move(encoded));
        }
        for(std::size_t r=1;r<table.rows.size();++r) {
            out+=first?"\n{":",\n{";first=false;
            const auto& row=table.rows[r];
            for(std::size_t i=0;i<row.size();++i) {
                if(i)out.push_back(',');
                out+=keys[i];out.push_back(':');
                JsonCell(out,row[i],options);
            }
            out.push_back('}');
            check();
        }
    }
    out+=first?"]\n":"\n]\n";
    check();
    return out;
}
}

// ---- Public API ----
namespace {
using detail::Fail;
bool ValidLimits(const TableLimits& t) noexcept {
    return t.maximumRows&&t.maximumColumns&&t.maximumCells&&t.maximumCellBytes&&t.maximumTextBytes&&t.maximumSharedStrings;
}
bool Valid(const ReadOptions& o) noexcept {
    return o.maximumSourceBytes&&ValidLimits(o.table)&&detail::ValidDelimiter(o.delimiter)
        &&o.zip.maximumEntries&&o.zip.maximumNameBytes&&o.zip.maximumTotalExpandedBytes
        &&o.zip.maximumEntryExpandedBytes<=std::uint64_t(std::numeric_limits<int>::max())-1024
        &&o.xml.maximumDepth&&o.xml.maximumAttributes&&o.xml.maximumNameBytes&&o.xml.maximumTokenBytes
        &&o.xml.maximumNodes&&o.xml.maximumTextBytes
        &&(o.sheet.by!=SheetSelection::name||!o.sheet.name.empty())
        &&std::uint8_t(o.sheet.by)<=std::uint8_t(SheetSelection::name);
}
bool Valid(const WriteOptions& o) noexcept {
    return o.maximumOutputBytes&&detail::ValidDelimiter(o.delimiter)&&std::uint8_t(o.json)<=std::uint8_t(JsonShape::objects);
}
bool Readable(TableFormat f) noexcept {return f>=TableFormat::csv&&f<=TableFormat::ods;}
bool Writable(TableFormat f) noexcept {return f==TableFormat::csv||f==TableFormat::tsv||f==TableFormat::json;}
std::string_view Text(std::span<const std::byte> bytes) {return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
}

ReadResult ReadTable(TableFormat format,std::span<const std::byte> bytes,const ReadOptions& options) noexcept {
    ReadResult result;
    if(!Readable(format)||!Valid(options)) {result.error=Error::invalid_options;return result;}
    if(bytes.size()>options.maximumSourceBytes) {result.error=Error::source_limit;return result;}
    result.error=detail::Guard([&] {
        switch(format) {
            case TableFormat::csv:
                result.table=detail::TableFromDelimited(Text(bytes),options.delimiter,options.inferTypes,options.table,result.report);
                break;
            case TableFormat::tsv:
                result.table=detail::TableFromDelimited(Text(bytes),'\t',options.inferTypes,options.table,result.report);
                break;
            case TableFormat::json:
                result.table=detail::TableFromJson(Text(bytes),options.table,result.report);
                break;
            case TableFormat::xlsx:
                result.table=detail::XlsxTable(bytes,options,result.report);
                break;
            case TableFormat::ods:
                result.table=detail::OdsTable(bytes,options,result.report);
                break;
        }
        result.report.rows=result.table.rows.size();
        result.report.columns=result.table.columns;
    });
    if(result.error!=Error::none) {result.table={};result.report={};}
    return result;
}
ReadResult ReadCsv(std::span<const std::byte> b,const ReadOptions& o) noexcept {return ReadTable(TableFormat::csv,b,o);}
ReadResult ReadTsv(std::span<const std::byte> b,const ReadOptions& o) noexcept {return ReadTable(TableFormat::tsv,b,o);}
ReadResult ReadJson(std::span<const std::byte> b,const ReadOptions& o) noexcept {return ReadTable(TableFormat::json,b,o);}
ReadResult ReadXlsx(std::span<const std::byte> b,const ReadOptions& o) noexcept {return ReadTable(TableFormat::xlsx,b,o);}
ReadResult ReadOds(std::span<const std::byte> b,const ReadOptions& o) noexcept {return ReadTable(TableFormat::ods,b,o);}

WriteResult WriteTable(TableFormat format,const Table& table,const WriteOptions& options) noexcept {
    WriteResult result;
    if(!Writable(format)||!Valid(options)) {result.error=Error::invalid_options;return result;}
    for(const auto& row:table.rows)
        if(row.size()!=table.columns) {result.error=Error::invalid_options;return result;} // Not rectangular.
    result.error=detail::Guard([&] {
        if(format==TableFormat::json)result.text=detail::JsonFromTable(table,options,result.renamedKeys);
        else result.text=detail::DelimitedFromTable(table,format==TableFormat::tsv?'\t':options.delimiter,options);
    });
    if(result.error!=Error::none) {result.text.clear();result.renamedKeys=0;}
    return result;
}
WriteResult WriteCsv(const Table& t,const WriteOptions& o) noexcept {return WriteTable(TableFormat::csv,t,o);}
WriteResult WriteTsv(const Table& t,const WriteOptions& o) noexcept {return WriteTable(TableFormat::tsv,t,o);}
WriteResult WriteJson(const Table& t,const WriteOptions& o) noexcept {return WriteTable(TableFormat::json,t,o);}

ConvertResult ConvertTable(TableFormat input,TableFormat output,std::span<const std::byte> bytes,const ConvertOptions& options) noexcept {
    ConvertResult result;
    if(!Writable(output)||!Valid(options.write)) {result.error=Error::invalid_options;return result;}
    auto read=ReadTable(input,bytes,options.read);
    if(!read) {result.error=read.error;return result;}
    auto written=WriteTable(output,read.table,options.write);
    if(!written) {result.error=written.error;return result;}
    result.text=std::move(written.text);
    result.renamedKeys=written.renamedKeys;
    result.report=std::move(read.report);
    return result;
}

std::string_view ErrorCode(Error error) noexcept {
    switch(error) {
        case Error::none: return "none";
        case Error::invalid_options: return "invalid_options";
        case Error::source_limit: return "source_limit";
        case Error::output_limit: return "output_limit";
        case Error::allocation_failed: return "allocation_failed";
        case Error::row_limit: return "row_limit";
        case Error::column_limit: return "column_limit";
        case Error::cell_limit: return "cell_limit";
        case Error::cell_size_limit: return "cell_size_limit";
        case Error::text_limit: return "text_limit";
        case Error::csv_encoding: return "csv_encoding";
        case Error::csv_malformed: return "csv_malformed";
        case Error::json_encoding: return "json_encoding";
        case Error::json_malformed: return "json_malformed";
        case Error::json_not_table: return "json_not_table";
        case Error::zip_not_archive: return "zip_not_archive";
        case Error::zip_malformed: return "zip_malformed";
        case Error::zip_unsupported: return "zip_unsupported";
        case Error::zip_encrypted_entry: return "zip_encrypted_entry";
        case Error::zip_unsafe_name: return "zip_unsafe_name";
        case Error::zip_duplicate_name: return "zip_duplicate_name";
        case Error::zip_entry_limit: return "zip_entry_limit";
        case Error::zip_size_limit: return "zip_size_limit";
        case Error::zip_crc_mismatch: return "zip_crc_mismatch";
        case Error::zip_corrupt_data: return "zip_corrupt_data";
        case Error::xml_malformed: return "xml_malformed";
        case Error::xml_doctype: return "xml_doctype";
        case Error::xml_encoding: return "xml_encoding";
        case Error::xml_entity: return "xml_entity";
        case Error::xml_limit: return "xml_limit";
        case Error::package_encrypted: return "package_encrypted";
        case Error::package_macro_enabled: return "package_macro_enabled";
        case Error::package_invalid: return "package_invalid";
        case Error::package_missing_part: return "package_missing_part";
        case Error::not_spreadsheet: return "not_spreadsheet";
        case Error::xlsx_malformed: return "xlsx_malformed";
        case Error::ods_malformed: return "ods_malformed";
        case Error::sheet_not_found: return "sheet_not_found";
        case Error::sheet_unsupported: return "sheet_unsupported";
        case Error::ole_compound_file: return "ole_compound_file";
    }
    return "unknown";
}

std::string_view ErrorMessage(Error error) noexcept {
    switch(error) {
        case Error::none: return "No error.";
        case Error::invalid_options: return "The table conversion options or formats are invalid.";
        case Error::source_limit: return "The file exceeds the source size limit.";
        case Error::output_limit: return "The converted table exceeds the output size limit.";
        case Error::allocation_failed: return "Memory could not be allocated for the table.";
        case Error::row_limit: return "The table has more rows than the limit allows.";
        case Error::column_limit: return "The table has more columns than the limit allows.";
        case Error::cell_limit: return "The table has more cells than the limit allows.";
        case Error::cell_size_limit: return "A cell holds more text than the per-cell limit allows.";
        case Error::text_limit: return "The table holds more text than the limit allows.";
        case Error::csv_encoding: return "The delimited text is not valid UTF-8; re-save it as UTF-8.";
        case Error::csv_malformed: return "The delimited text has an unterminated quoted field or text after a closing quote.";
        case Error::json_encoding: return "The JSON is not valid UTF-8.";
        case Error::json_malformed: return "The file is not well-formed JSON (or has duplicate keys or nesting deeper than 64).";
        case Error::json_not_table: return "The JSON is not a table: expected an array of arrays or an array of flat objects with scalar values.";
        case Error::zip_not_archive: return "The file is not a ZIP-based spreadsheet package.";
        case Error::zip_malformed: return "The spreadsheet package has an inconsistent ZIP structure.";
        case Error::zip_unsupported: return "The spreadsheet package uses an unsupported ZIP feature (ZIP64, spanning or an unsupported compression method).";
        case Error::zip_encrypted_entry: return "The spreadsheet package contains encrypted ZIP entries.";
        case Error::zip_unsafe_name: return "The spreadsheet package contains an unsafe member name.";
        case Error::zip_duplicate_name: return "The spreadsheet package contains duplicate member names.";
        case Error::zip_entry_limit: return "The spreadsheet package contains too many members.";
        case Error::zip_size_limit: return "A spreadsheet package member expands beyond the size limit or its declared size.";
        case Error::zip_crc_mismatch: return "A spreadsheet package member failed its CRC-32 check.";
        case Error::zip_corrupt_data: return "A spreadsheet package member has corrupt compressed data.";
        case Error::xml_malformed: return "A spreadsheet part is not well-formed XML.";
        case Error::xml_doctype: return "A spreadsheet part contains a DOCTYPE declaration, which is not accepted.";
        case Error::xml_encoding: return "A spreadsheet part is not UTF-8 encoded.";
        case Error::xml_entity: return "A spreadsheet part references an undefined XML entity.";
        case Error::xml_limit: return "A spreadsheet part exceeds the XML nesting, size or node limits.";
        case Error::package_encrypted: return "The spreadsheet is password-protected or encrypted; remove the protection and try again.";
        case Error::package_macro_enabled: return "Macro-enabled workbooks are not accepted; save the workbook as .xlsx without macros.";
        case Error::package_invalid: return "The spreadsheet package has an invalid content-type list, relationships, mimetype or manifest.";
        case Error::package_missing_part: return "The spreadsheet package is missing a part it refers to.";
        case Error::not_spreadsheet: return "The package is not an XLSX/ODS spreadsheet (XLSB and other documents are not supported).";
        case Error::xlsx_malformed: return "The XLSX worksheet has an invalid cell reference, cell type, shared-string index or value.";
        case Error::ods_malformed: return "The ODS table has an invalid repeat count or typed value.";
        case Error::sheet_not_found: return "The requested sheet does not exist in the workbook.";
        case Error::sheet_unsupported: return "The requested sheet is a chart, dialog or macro sheet, not a worksheet.";
        case Error::ole_compound_file: return "The file is a legacy binary workbook (.xls) or a password-protected Office file; save it as an unprotected .xlsx and try again.";
    }
    return "Unknown table conversion error.";
}

std::string_view MediaType(TableFormat format) noexcept {
    switch(format) {
        case TableFormat::csv: return "text/csv; charset=utf-8";
        case TableFormat::tsv: return "text/tab-separated-values";
        case TableFormat::json: return "application/json";
        default: return {};
    }
}
}
