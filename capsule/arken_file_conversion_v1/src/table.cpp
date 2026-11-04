#include "arken_file_conversion_v1/tables.hpp"
#include <new>

namespace arken::file_conversion::v1 {
namespace {
using tabular::TableFormat;
TableResult Failure(Error error, tabular::Error reason = tabular::Error::none) {
    TableResult result; result.error = error; result.tableError = reason; return result;
}
Error Map(tabular::Error error) noexcept {
    switch (error) {
    case tabular::Error::source_limit: case tabular::Error::row_limit: case tabular::Error::column_limit:
    case tabular::Error::cell_limit: case tabular::Error::cell_size_limit: case tabular::Error::text_limit:
    case tabular::Error::zip_size_limit: case tabular::Error::zip_entry_limit: case tabular::Error::xml_limit:
        return Error::input_limit;
    case tabular::Error::output_limit: return Error::encode_failed;
    case tabular::Error::allocation_failed: return Error::allocation_failed;
    case tabular::Error::invalid_options: return Error::invalid_options;
    default: return Error::decode_failed;
    }
}
bool Input(TableFormat format) noexcept {
    return format == TableFormat::csv || format == TableFormat::tsv || format == TableFormat::json
        || format == TableFormat::xlsx || format == TableFormat::ods;
}
bool Output(TableFormat format) noexcept {
    return format == TableFormat::csv || format == TableFormat::tsv || format == TableFormat::json;
}
bool Workbook(TableFormat format) noexcept { return format == TableFormat::xlsx || format == TableFormat::ods; }
}
tabular::ConvertOptions StandaloneTableOptions(const TableOptions& options) noexcept {
    tabular::ConvertOptions result;
    auto& read = result.read;
    read.maximumSourceBytes = maximumTableSourceBytes;
    read.delimiter = options.delimiter;
    read.sheet = options.sheet;
    read.table.maximumRows = maximumTableRows;
    read.table.maximumColumns = maximumTableColumns;
    read.table.maximumCells = maximumTableCells;
    read.table.maximumCellBytes = maximumTableCellBytes;
    read.table.maximumTextBytes = maximumTableTextBytes;
    read.table.maximumSharedStrings = maximumTableSharedStrings;
    read.zip.maximumEntryCompressedBytes = maximumTableEntryExpandedBytes;
    read.zip.maximumEntryExpandedBytes = maximumTableEntryExpandedBytes;
    result.write.maximumOutputBytes = maximumTableOutputBytes;
    result.write.delimiter = options.delimiter;
    result.write.json = options.json;
    return result;
}
TableResult ConvertStandaloneTable(const TableOptions& options, std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > maximumTableSourceBytes) return Failure(Error::input_limit);
    if (!Input(options.input)) return Failure(Error::input_format_unavailable);
    if (!Output(options.output)) return Failure(Error::output_format_unavailable);
    // Options for another format are rejected, never ignored.
    const bool csvSide = options.input == TableFormat::csv || options.output == TableFormat::csv;
    if (options.input == options.output
        || (options.delimiter != ',' && options.delimiter != ';' && options.delimiter != '\t')
        || (options.delimiter != ',' && !csvSide)
        || (options.json != tabular::JsonShape::arrays && options.output != TableFormat::json)
        || (options.sheet.by != tabular::SheetSelection::first_visible && !Workbook(options.input)))
        return Failure(Error::invalid_options);
    try {
        auto converted = tabular::ConvertTable(options.input, options.output, bytes, StandaloneTableOptions(options));
        if (!converted) return Failure(Map(converted.error), converted.error);
        if (converted.report.sheets.size() > maximumTableSheets) return Failure(Error::input_limit);
        TableResult result;
        result.report = std::move(converted.report);
        result.renamedKeys = converted.renamedKeys;
        const auto* begin = reinterpret_cast<const std::byte*>(converted.text.data());
        result.bytes.assign(begin, begin + converted.text.size());
        return result;
    } catch (const std::bad_alloc&) { return Failure(Error::allocation_failed); }
    catch (...) { return Failure(Error::internal_failure); }
}
std::string_view TableErrorMessage(Error error) noexcept {
    switch (error) {
    case Error::none: return "Table converted";
    case Error::input_limit: return "Input must contain between 1 byte and 32 MiB and stay within the table limits";
    case Error::decode_failed: return "Table source is invalid, encrypted, macro-enabled or outside the supported profile";
    case Error::encode_failed: return "Converted table exceeds the 64 MiB output limit";
    case Error::invalid_options: return "Table options do not apply to this input and output";
    default: return ErrorMessage(error);
    }
}
}
