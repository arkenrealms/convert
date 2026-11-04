#pragma once
#include "arken_file_conversion_v1/api.hpp"
#include "arken_document_extraction_v1/tabular.hpp"

namespace arken::file_conversion::v1 {
namespace tabular = arken::document::v1::tabular;
// Standalone native MCP table profile: CSV, TSV, JSON, XLSX or ODS read into one
// rectangular table and written as UTF-8 CSV, TSV or JSON. One sheet per call (the first
// visible sheet unless an index or name is given); cached formula values only, never
// recalculated; merged ranges reported, not expanded; no formatting. Resident, not
// streaming. Separate from every other surface's admission.
//
// Source and per-part bounds are the document profile's: 32 MiB of source and 16 MiB per
// expanded ZIP member (the capsule default is 32 MiB). The capsule measured about 15-17
// bytes of memory per byte of worksheet XML (the XML node tree dominates), so one XLSX/ODS
// call stays near 280 MB instead of ~550 MB. The cell cap is the binding bound for CSV and
// JSON: 2,000,000 cells measured ~280 MB peak for a 14 MB CSV.
// Output is 64 MiB (the capsule default): JSON objects repeat the header keys on every row,
// so output can exceed the source. A larger result is rejected, never cut.
inline constexpr std::uint32_t maximumTableSourceBytes = 32u * 1024u * 1024u;
inline constexpr std::uint32_t maximumTableEntryExpandedBytes = 16u * 1024u * 1024u;
inline constexpr std::uint32_t maximumTableOutputBytes = 64u * 1024u * 1024u;
// Row, column, cell and text caps: the capsule's defaults, stated here so a capsule change
// cannot silently widen this profile.
inline constexpr std::uint32_t maximumTableRows = 1'048'576;
inline constexpr std::uint32_t maximumTableColumns = 16'384;
inline constexpr std::uint32_t maximumTableCells = 2'000'000;
inline constexpr std::uint32_t maximumTableCellBytes = 1024u * 1024u;
inline constexpr std::uint32_t maximumTableTextBytes = 64u * 1024u * 1024u;
inline constexpr std::uint32_t maximumTableSharedStrings = 2'000'000;
// Sheets named in the report (an ODS keeps every table in content.xml, so the ZIP entry
// limit does not bound it). A workbook with more is rejected rather than half-reported.
inline constexpr std::uint32_t maximumTableSheets = 4096;

struct TableOptions {
    tabular::TableFormat input = tabular::TableFormat::csv;
    tabular::TableFormat output = tabular::TableFormat::json;
    // CSV field delimiter (',', ';' or '\t') for a CSV input and a CSV output; changed values
    // require one of them to be CSV.
    char delimiter = ',';
    // JSON output only: arrays of arrays, or objects keyed by the first row.
    tabular::JsonShape json = tabular::JsonShape::arrays;
    // XLSX/ODS input only; first_visible otherwise.
    tabular::SheetSelector sheet;
};
struct TableResult {
    Error error = Error::none;
    tabular::Error tableError = tabular::Error::none;
    tabular::TableReport report;
    std::uint64_t renamedKeys = 0;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return error == Error::none; }
};
[[nodiscard]] tabular::ConvertOptions StandaloneTableOptions(const TableOptions&) noexcept;
// Identical input and output formats, options for another format, an unknown delimiter, or
// an index/name sheet selection for a non-workbook input are invalid_options.
[[nodiscard]] TableResult ConvertStandaloneTable(const TableOptions&, std::span<const std::byte>) noexcept;
[[nodiscard]] std::string_view TableErrorMessage(Error) noexcept;
}
