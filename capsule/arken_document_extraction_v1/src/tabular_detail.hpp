#pragma once
#include "../tabular.hpp"
#include "detail.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

// Internals shared by the spreadsheet readers (xlsx.cpp, ods.cpp) and the
// public entry points (tabular.cpp). Boundary: the spreadsheet readers depend
// only on the Table model, this sparse-cell builder and the package helpers
// in detail.hpp; they never call the CSV/TSV/JSON code, which lives entirely
// in tabular.cpp behind TableFromDelimited/TableFromJson/DelimitedFromTable/
// JsonFromTable so it can be re-pointed at another owner without touching them.
namespace arken::document::v1::tabular::detail {
using Bytes=std::span<const std::byte>;
namespace doc=arken::document::v1;

struct Failure {Error error;};
[[noreturn]] inline void Fail(Error error) {throw Failure{error};}

inline Error FromDocument(doc::Error e) noexcept {
    switch(e) {
        case doc::Error::zip_not_archive: return Error::zip_not_archive;
        case doc::Error::zip_malformed: return Error::zip_malformed;
        case doc::Error::zip_unsupported: return Error::zip_unsupported;
        case doc::Error::zip_encrypted_entry: return Error::zip_encrypted_entry;
        case doc::Error::zip_unsafe_name: return Error::zip_unsafe_name;
        case doc::Error::zip_duplicate_name: return Error::zip_duplicate_name;
        case doc::Error::zip_entry_limit: return Error::zip_entry_limit;
        case doc::Error::zip_size_limit: return Error::zip_size_limit;
        case doc::Error::zip_crc_mismatch: return Error::zip_crc_mismatch;
        case doc::Error::zip_corrupt_data: return Error::zip_corrupt_data;
        case doc::Error::xml_malformed: return Error::xml_malformed;
        case doc::Error::xml_doctype: return Error::xml_doctype;
        case doc::Error::xml_encoding: return Error::xml_encoding;
        case doc::Error::xml_entity: return Error::xml_entity;
        case doc::Error::xml_limit: return Error::xml_limit;
        case doc::Error::allocation_failed: return Error::allocation_failed;
        default: return Error::package_invalid;
    }
}
template<class F> Error Guard(F&& body) noexcept {
    try {body();return Error::none;}
    catch(const Failure& failure) {return failure.error;}
    catch(const doc::detail::Failure& failure) {return FromDocument(failure.error);}
    catch(const std::bad_alloc&) {return Error::allocation_failed;}
    catch(...) {return Error::allocation_failed;}
}

// Per-table text budget: one cell and the running total.
class TextBudget {
public:
    explicit TextBudget(const TableLimits& limits):limits_(limits) {}
    void Charge(const TableCell& cell) {
        const std::uint64_t bytes=cell.value.size()+cell.formatted.size();
        if(bytes>limits_.maximumCellBytes)Fail(Error::cell_size_limit);
        if(bytes>limits_.maximumTextBytes-total_)Fail(Error::text_limit);
        total_+=bytes;
    }
private:
    const TableLimits& limits_;
    std::uint64_t total_=0;
};

// Non-empty cells placed by (row, column); densified at the end so trailing
// empty space is never materialized. Positions are zero-based.
class SparseTable {
public:
    explicit SparseTable(const TableLimits& limits):limits_(limits),budget_(limits) {}
    void Put(std::uint64_t row,std::uint64_t column,TableCell&& cell) {
        if(cell.kind==CellKind::empty)return;
        if(row>=limits_.maximumRows)Fail(Error::row_limit);
        if(column>=limits_.maximumColumns)Fail(Error::column_limit);
        if(entries_.size()>=limits_.maximumCells)Fail(Error::cell_limit);
        if(cell.formatted==cell.value)cell.formatted.clear();
        budget_.Charge(cell);
        entries_.push_back({row,column,std::move(cell)});
    }
    [[nodiscard]] std::uint64_t Count() const noexcept {return entries_.size();}
    // Fails with `duplicate` when two cells share a position.
    Table Densify(Error duplicate) {
        std::stable_sort(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b) {
            return a.row!=b.row?a.row<b.row:a.column<b.column;
        });
        std::uint64_t height=0,width=0;
        for(std::size_t i=0;i<entries_.size();++i) {
            const auto& e=entries_[i];
            if(i&&entries_[i-1].row==e.row&&entries_[i-1].column==e.column)Fail(duplicate);
            height=std::max(height,e.row+1);
            width=std::max(width,e.column+1);
        }
        if(width&&height>limits_.maximumCells/width)Fail(Error::cell_limit);
        Table table;
        table.columns=width;
        table.rows.resize(height);
        for(auto& row:table.rows)row.resize(width);
        for(auto& e:entries_)table.rows[e.row][e.column]=std::move(e.cell);
        entries_.clear();
        return table;
    }
private:
    struct Entry {std::uint64_t row,column;TableCell cell;};
    const TableLimits& limits_;
    TextBudget budget_;
    std::vector<Entry> entries_;
};

// "A1" to zero-based (row, column) within the SpreadsheetML grid (XFD1048576).
inline bool ParseCellReference(std::string_view ref,std::uint64_t& row,std::uint64_t& column) {
    std::size_t p=0;
    std::uint64_t c=0;
    while(p<ref.size()&&p<3) {
        char ch=ref[p];
        if(ch>='a'&&ch<='z')ch=char(ch-'a'+'A');
        if(ch<'A'||ch>'Z')break;
        c=c*26+std::uint64_t(ch-'A'+1);
        ++p;
    }
    if(p==0||c>16384)return false;
    if(p==ref.size()||ref[p]<'1'||ref[p]>'9'||ref.size()-p>7)return false;
    std::uint64_t r=0;
    for(;p<ref.size();++p) {
        if(ref[p]<'0'||ref[p]>'9')return false;
        r=r*10+std::uint64_t(ref[p]-'0');
    }
    if(r>1'048'576)return false;
    row=r-1;column=c-1;
    return true;
}
inline bool ValidRange(std::string_view ref) {
    std::uint64_t r=0,c=0;
    const auto colon=ref.find(':');
    if(colon==std::string_view::npos)return ParseCellReference(ref,r,c);
    std::uint64_t r2=0,c2=0;
    return ParseCellReference(ref.substr(0,colon),r,c)&&ParseCellReference(ref.substr(colon+1),r2,c2);
}
inline std::string ColumnName(std::uint64_t column) {
    std::string out;
    for(auto c=column+1;c>0;c=(c-1)/26)out.insert(out.begin(),char('A'+(c-1)%26));
    return out;
}
inline std::string CellName(std::uint64_t row,std::uint64_t column) {return ColumnName(column)+std::to_string(row+1);}

// Full-string binary64 parse (decimal, optional exponent); false on garbage, inf or nan.
inline bool ParseDouble(std::string_view s,double& value) {
    if(s.empty())return false;
    const char* begin=s.data();
    if(*begin=='+')return false;
    const auto [end,ec]=std::from_chars(begin,s.data()+s.size(),value,std::chars_format::general);
    return ec==std::errc()&&end==s.data()+s.size()&&std::isfinite(value);
}

// Proleptic Gregorian civil date from days since 1970-01-01.
inline void CivilFromDays(std::int64_t z,std::int64_t& y,unsigned& m,unsigned& d) {
    z+=719468;
    const std::int64_t era=(z>=0?z:z-146096)/146097;
    const auto doe=unsigned(z-era*146097);
    const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    y=std::int64_t(yoe)+era*400;
    const unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    const unsigned mp=(5*doy+2)/153;
    d=doy-(153*mp+2)/5+1;
    m=mp<10?mp+3:mp-9;
    if(m<=2)++y;
}
inline void Pad(std::string& out,std::uint64_t v,int width) {
    auto s=std::to_string(v);
    if(int(s.size())<width)out.append(std::size_t(width)-s.size(),'0');
    out+=s;
}
inline void AppendClock(std::string& out,std::uint64_t hours,std::uint64_t ms) {
    Pad(out,hours,2);out.push_back(':');Pad(out,ms/60000%60,2);out.push_back(':');Pad(out,ms/1000%60,2);
    if(ms%1000) {out.push_back('.');Pad(out,ms%1000,3);}
}

// Spreadsheet readers (xlsx.cpp, ods.cpp). They throw Failure or document Failure.
Table XlsxTable(Bytes bytes,const ReadOptions& options,TableReport& report);
Table OdsTable(Bytes bytes,const ReadOptions& options,TableReport& report);
}
