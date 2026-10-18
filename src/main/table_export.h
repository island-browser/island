#ifndef ISLAND_TABLE_EXPORT_H_
#define ISLAND_TABLE_EXPORT_H_

// Reads the first Markdown table of a report and writes it as CSV or JSON. CEF-free.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace island {

struct Table {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    bool operator==(const Table&) const = default;
};

// The first GFM table in `markdown`: a header row, a delimiter row, then body rows up to the
// first blank or non-table line. Escaped pipes (\|) are literal pipes, <br> is a line break, and
// short rows are padded with empty cells. A cell that is exactly one Markdown link becomes its
// URL; a column whose link texts differ from their URLs gets a "<header> title" column after it
// with the link texts. std::nullopt when there is no table.
[[nodiscard]] std::optional<Table> FirstMarkdownTable(std::string_view markdown);

// RFC 4180 with a UTF-8 byte order mark and CRLF line ends. A field is quoted when it holds a
// comma, a quote, or a line break; quotes inside it are doubled.
[[nodiscard]] std::string TableToCsv(const Table& table);

// An array of objects keyed by the header. Cells are strings; non-ASCII text stays UTF-8.
[[nodiscard]] std::string TableToJson(const Table& table);

}  // namespace island

#endif  // ISLAND_TABLE_EXPORT_H_
