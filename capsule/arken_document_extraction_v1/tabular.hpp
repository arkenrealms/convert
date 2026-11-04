#pragma once
#include "api.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bounded, memory-only tabular conversion: CSV, TSV, JSON, XLSX (SpreadsheetML)
// and ODS (OpenDocument spreadsheet) into one rectangular Table model, and that
// model out to CSV, TSV or JSON. The caller supplies bytes and receives bytes;
// no file, network, process or Office access, no formula evaluation. Every
// malformed, encrypted, macro-enabled or over-budget input is a typed error,
// never a partially filled table reported as success.
namespace arken::document::v1::tabular {
inline constexpr std::string_view revision="arken-document-tabular-v1-r1";

enum class TableFormat : std::uint32_t {csv=1, tsv=2, json=3, xlsx=4, ods=5};

// value is the canonical text of the cell:
//   text    the string as stored;
//   number  the source lexical form (CSV field, JSON number, XLSX <v>, ODS office:value);
//   boolean "TRUE"/"FALSE" from XLSX/ODS, the JSON literal "true"/"false", or the CSV field as written;
//   date    ISO 8601: YYYY-MM-DD, YYYY-MM-DDThh:mm:ss[.fff] or hh:mm:ss[.fff];
//   error   the spreadsheet error literal such as "#DIV/0!".
// formatted keeps the original source text when it differs from value (the XLSX
// serial number behind a date or the "1"/"0" behind a boolean, the ODS text:p
// display text); it is empty when identical. Display() returns whichever applies.
enum class CellKind : std::uint8_t {empty=0, text, number, boolean, date, error};
struct TableCell {
    CellKind kind=CellKind::empty;
    std::string value;
    std::string formatted;
    [[nodiscard]] const std::string& Display() const noexcept {return formatted.empty()?value:formatted;}
    friend bool operator==(const TableCell&,const TableCell&)=default;
};
// Rectangular: every row holds exactly `columns` cells (short rows are padded with empty cells).
struct Table {
    std::vector<std::vector<TableCell>> rows;
    std::uint64_t columns=0;
};

enum class Error : std::uint32_t {
    none=0,
    invalid_options,
    source_limit,        // Source exceeds maximumSourceBytes.
    output_limit,        // Serialized result exceeds maximumOutputBytes.
    allocation_failed,
    row_limit,           // More rows than maximumRows (a non-empty cell beyond it).
    column_limit,        // More columns than maximumColumns.
    cell_limit,          // rows x columns, or cells read, beyond maximumCells.
    cell_size_limit,     // One cell longer than maximumCellBytes.
    text_limit,          // Total cell text beyond maximumTextBytes.
    // Delimited text.
    csv_encoding,        // Not strict UTF-8.
    csv_malformed,       // Unterminated quoted field or text after a closing quote.
    // JSON.
    json_encoding,
    json_malformed,      // Not RFC 8259 JSON (including lone surrogates, duplicate keys, depth over 64).
    json_not_table,      // Valid JSON but not an array of scalar arrays or of flat scalar objects.
    // ZIP container (XLSX, ODS); same meaning as arken::document::v1::Error.
    zip_not_archive,
    zip_malformed,
    zip_unsupported,
    zip_encrypted_entry,
    zip_unsafe_name,
    zip_duplicate_name,
    zip_entry_limit,
    zip_size_limit,
    zip_crc_mismatch,
    zip_corrupt_data,
    // XML parts; same meaning as arken::document::v1::Error.
    xml_malformed,
    xml_doctype,
    xml_encoding,
    xml_entity,
    xml_limit,
    // Packages.
    package_encrypted,     // EncryptedPackage/EncryptionInfo members, or ODF manifest encryption-data.
    package_macro_enabled, // .xlsm/.xltm main part or a VBA project part.
    package_invalid,       // Invalid [Content_Types].xml, relationships, mimetype or manifest.
    package_missing_part,  // A part named by the package is absent.
    not_spreadsheet,       // The package is another kind of document (or XLSB).
    xlsx_malformed,        // Invalid cell reference, cell type, shared-string index or numeric value.
    ods_malformed,         // Invalid repeat count or typed value.
    sheet_not_found,       // The selected sheet does not exist, or the workbook has no sheets.
    sheet_unsupported,     // The selected sheet is a chart, dialog or macro sheet.
    ole_compound_file,     // A legacy binary workbook (.xls) or a password-protected Office file.
};

struct TableLimits {
    std::uint64_t maximumRows=1'048'576;
    std::uint64_t maximumColumns=16'384;
    std::uint64_t maximumCells=2'000'000;          // rows x columns of the dense table, and cells read.
    std::uint64_t maximumCellBytes=1024*1024;      // value + formatted of one cell.
    std::uint64_t maximumTextBytes=64ull*1024*1024;// Sum over all cells.
    std::uint64_t maximumSharedStrings=2'000'000;  // XLSX shared-string table entries.
};

enum class SheetSelection : std::uint8_t {first_visible=0, index, name};
struct SheetSelector {
    SheetSelection by=SheetSelection::first_visible;
    std::uint32_t index=0;  // Zero-based, workbook order, hidden sheets included.
    std::string name;       // Exact (case-sensitive) sheet name.
};

struct ReadOptions {
    std::uint64_t maximumSourceBytes=32ull*1024*1024;
    char delimiter=',';     // CSV only: ',', ';' or '\t'. TSV always uses '\t'.
    bool inferTypes=true;   // CSV/TSV: JSON-grammar numbers and true/false become typed cells.
    SheetSelector sheet;    // XLSX/ODS.
    TableLimits table;
    ZipLimits zip;
    // DOM-shaped parts: a worksheet holds about three nodes per cell.
    XmlLimits xml{256,64,256,4ull*1024*1024,8'000'000,32ull*1024*1024};
};

enum class JsonShape : std::uint8_t {arrays=0, objects};
struct WriteOptions {
    std::uint64_t maximumOutputBytes=64ull*1024*1024;
    char delimiter=',';     // CSV only: ',', ';' or '\t'. TSV always uses '\t'.
    bool byteOrderMark=false; // CSV/TSV: prefix a UTF-8 BOM.
    bool crlf=false;          // CSV/TSV: CRLF record separators instead of LF.
    bool formattedText=false; // Write Display() instead of value.
    JsonShape json=JsonShape::arrays;
    bool emptyAsNull=false;   // JSON: empty cells as null instead of "".
};

enum class SheetState : std::uint8_t {visible=0, hidden, very_hidden};
enum class SheetKind : std::uint8_t {worksheet=0, chartsheet, other};
struct SheetInfo {
    std::string name;
    SheetState state=SheetState::visible;
    SheetKind kind=SheetKind::worksheet;
};
struct TableReport {
    std::vector<SheetInfo> sheets;   // Workbook order (XLSX/ODS only).
    std::uint32_t selectedSheet=0;
    std::string dimension;           // XLSX <dimension ref> as declared, unvalidated beyond A1 syntax.
    std::uint64_t rows=0, columns=0;
    std::uint64_t raggedRows=0;      // Rows whose source width differed from `columns` (padded).
    std::vector<std::string> mergedRanges; // A1:B2 ranges, reported, never expanded.
    std::uint64_t formulas=0;        // Formula cells seen; their cached value is used.
    std::uint64_t formulasWithoutValue=0; // Formula cells with no cached value (left empty).
    std::uint64_t trimmedRepeatedRows=0;  // ODS empty repeated rows beyond the data, never materialized.
    std::uint64_t trimmedRepeatedCells=0; // ODS empty repeated cells beyond the data, never materialized.
    bool byteOrderMark=false;        // CSV/TSV/JSON source began with a UTF-8 BOM.
};

struct ReadResult {
    Error error=Error::none;
    Table table;
    TableReport report;
    explicit operator bool() const noexcept {return error==Error::none;}
};
struct WriteResult {
    Error error=Error::none;
    std::string text;
    std::uint64_t renamedKeys=0; // JSON objects: empty or duplicate header names that were renamed.
    explicit operator bool() const noexcept {return error==Error::none;}
};
struct ConvertOptions {
    ReadOptions read;
    WriteOptions write;
};
struct ConvertResult {
    Error error=Error::none;
    std::string text;
    TableReport report;
    std::uint64_t renamedKeys=0;
    explicit operator bool() const noexcept {return error==Error::none;}
};

// CSV per RFC 4180 with the options' delimiter; TSV is the same grammar with '\t'.
[[nodiscard]] ReadResult ReadCsv(std::span<const std::byte>,const ReadOptions& ={}) noexcept;
[[nodiscard]] ReadResult ReadTsv(std::span<const std::byte>,const ReadOptions& ={}) noexcept;
// An array of scalar arrays, or an array of flat objects (header row = union of keys in first-seen order).
[[nodiscard]] ReadResult ReadJson(std::span<const std::byte>,const ReadOptions& ={}) noexcept;
[[nodiscard]] ReadResult ReadXlsx(std::span<const std::byte>,const ReadOptions& ={}) noexcept;
[[nodiscard]] ReadResult ReadOds(std::span<const std::byte>,const ReadOptions& ={}) noexcept;
[[nodiscard]] ReadResult ReadTable(TableFormat,std::span<const std::byte>,const ReadOptions& ={}) noexcept;

// RFC 4180 quoting (fields holding the delimiter, '"', CR or LF), UTF-8, LF by default.
[[nodiscard]] WriteResult WriteCsv(const Table&,const WriteOptions& ={}) noexcept;
[[nodiscard]] WriteResult WriteTsv(const Table&,const WriteOptions& ={}) noexcept;
// RFC 8259. Numbers only for number cells whose value is a JSON number that
// round-trips exactly through binary64; booleans for boolean cells; all else strings.
[[nodiscard]] WriteResult WriteJson(const Table&,const WriteOptions& ={}) noexcept;
// Output must be csv, tsv or json.
[[nodiscard]] WriteResult WriteTable(TableFormat,const Table&,const WriteOptions& ={}) noexcept;
[[nodiscard]] ConvertResult ConvertTable(TableFormat input,TableFormat output,std::span<const std::byte>,
    const ConvertOptions& ={}) noexcept;

[[nodiscard]] std::string_view ErrorCode(Error) noexcept;
[[nodiscard]] std::string_view ErrorMessage(Error) noexcept;
// text/csv; charset=utf-8 | text/tab-separated-values | application/json; empty for input-only formats.
[[nodiscard]] std::string_view MediaType(TableFormat) noexcept;
}
