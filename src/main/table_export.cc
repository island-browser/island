#include "table_export.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island {

namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kBreakTags[] = {"<br>", "<br/>", "<br />"};

std::string_view Trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The cells of one table row. Escaped pipes are literal; <br> becomes a line break.
std::vector<std::string> SplitRow(std::string_view line) {
    std::string_view row = Trim(line);
    if (row.starts_with('|')) {
        row.remove_prefix(1);
    }
    if (row.ends_with('|') && !row.ends_with("\\|")) {
        row.remove_suffix(1);
    }
    std::vector<std::string> cells;
    std::string current;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') {
            current += '|';
            ++i;
        } else if (row[i] == '|') {
            cells.push_back(std::string(Trim(current)));
            current.clear();
        } else {
            current += row[i];
        }
    }
    cells.push_back(std::string(Trim(current)));
    for (std::string& cell : cells) {
        for (std::string_view tag : kBreakTags) {
            for (std::size_t at = cell.find(tag); at != std::string::npos;
                 at = cell.find(tag, at + 1)) {
                cell.replace(at, tag.size(), "\n");
            }
        }
    }
    return cells;
}

// A delimiter row: "---", ":--", "--:" or ":-:" in every cell.
bool IsDelimiterRow(std::string_view line, std::size_t columns) {
    if (line.find('|') == std::string_view::npos) {
        return false;
    }
    const std::vector<std::string> cells = SplitRow(line);
    if (cells.size() != columns) {
        return false;
    }
    for (const std::string& cell : cells) {
        std::string_view body = cell;
        if (body.starts_with(':')) {
            body.remove_prefix(1);
        }
        if (body.ends_with(':')) {
            body.remove_suffix(1);
        }
        if (body.empty() || body.find_first_not_of('-') != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

struct Link {
    std::string text;
    std::string url;
};

// A cell that is exactly one Markdown link: [text](url), with no space in the URL.
std::optional<Link> ParseLink(std::string_view cell) {
    if (!cell.starts_with('[') || !cell.ends_with(')')) {
        return std::nullopt;
    }
    const std::size_t split = cell.find("](");
    if (split == std::string_view::npos || split + 2 >= cell.size() - 1) {
        return std::nullopt;
    }
    const std::string_view url = cell.substr(split + 2, cell.size() - split - 3);
    // A second "](" means two links ("[a](b)[c](d)"), not one link with a strange URL.
    const bool spaced = url.find_first_of(" \t") != std::string_view::npos;
    if (spaced || url.find("](") != std::string_view::npos) {
        return std::nullopt;
    }
    return Link{std::string(cell.substr(1, split - 1)), std::string(url)};
}

// Link cells become their URL; a column whose link texts differ from the URLs gets a title
// column after it.
Table ExpandLinks(const Table& raw) {
    const std::size_t columns = raw.header.size();
    std::vector<bool> with_title(columns, false);
    for (const std::vector<std::string>& row : raw.rows) {
        for (std::size_t c = 0; c < columns; ++c) {
            const std::optional<Link> link = ParseLink(row[c]);
            if (link.has_value() && link->text != link->url) {
                with_title[c] = true;
            }
        }
    }
    Table out;
    for (std::size_t c = 0; c < columns; ++c) {
        out.header.push_back(raw.header[c]);
        if (with_title[c]) {
            out.header.push_back(raw.header[c] + " title");
        }
    }
    for (const std::vector<std::string>& row : raw.rows) {
        std::vector<std::string> cells;
        for (std::size_t c = 0; c < columns; ++c) {
            const std::optional<Link> link = ParseLink(row[c]);
            cells.push_back(link.has_value() ? link->url : row[c]);
            if (with_title[c]) {
                cells.push_back(link.has_value() ? link->text : std::string());
            }
        }
        out.rows.push_back(std::move(cells));
    }
    return out;
}

// A cell that starts with one of these is run as a formula by spreadsheets: a leading quote makes
// it text. The page text is untrusted, so this applies to every CSV cell.
constexpr std::string_view kFormulaStarts = "=+-@\t\r";

std::string CsvField(std::string_view field) {
    std::string text(field);
    if (!text.empty() && kFormulaStarts.find(text.front()) != std::string_view::npos) {
        text.insert(text.begin(), '\'');
    }
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

void AppendJsonString(std::string& out, std::string_view text) {
    constexpr std::string_view kHex = "0123456789abcdef";
    out += '"';
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out += kHex[byte >> 4U];
                    out += kHex[byte & 0x0FU];
                } else {
                    out += c;  // UTF-8 bytes pass through unchanged
                }
        }
    }
    out += '"';
}

}  // namespace

std::optional<Table> FirstMarkdownTable(std::string_view markdown) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= markdown.size()) {
        std::size_t end = markdown.find('\n', start);
        if (end == std::string_view::npos) {
            end = markdown.size();
        }
        std::string_view line = markdown.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = end + 1;
    }
    // Lines inside a fenced code block (and the fences) are not table text.
    std::vector<bool> fenced(lines.size(), false);
    std::string_view fence;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = Trim(lines[i]);
        if (fence.empty()) {
            if (line.starts_with("```") || line.starts_with("~~~")) {
                fence = line.substr(0, 3);
                fenced[i] = true;
            }
        } else {
            fenced[i] = true;
            if (line.starts_with(fence)) {
                fence = {};
            }
        }
    }
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        if (fenced[i] || fenced[i + 1] || lines[i].find('|') == std::string_view::npos) {
            continue;
        }
        Table raw;
        raw.header = SplitRow(lines[i]);
        if (!IsDelimiterRow(lines[i + 1], raw.header.size())) {
            continue;
        }
        for (std::size_t j = i + 2; j < lines.size(); ++j) {
            const std::string_view line = Trim(lines[j]);
            if (fenced[j] || line.empty() || line.find('|') == std::string_view::npos) {
                break;
            }
            std::vector<std::string> cells = SplitRow(line);
            cells.resize(raw.header.size());
            raw.rows.push_back(std::move(cells));
        }
        return ExpandLinks(raw);
    }
    return std::nullopt;
}

std::string TableToCsv(const Table& table) {
    std::string out(kUtf8Bom);
    const auto append_row = [&out](const std::vector<std::string>& cells) {
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (c > 0) {
                out += ',';
            }
            out += CsvField(cells[c]);
        }
        out += "\r\n";
    };
    append_row(table.header);
    for (const std::vector<std::string>& row : table.rows) {
        // A short hand-built row reads as empty cells, as in JSON.
        std::vector<std::string> cells = row;
        cells.resize(table.header.size());
        append_row(cells);
    }
    return out;
}

std::string TableToJson(const Table& table) {
    // A repeated header would repeat a JSON key, and most parsers keep only the last one, losing
    // a column. Repeats get " 2", " 3", ... instead.
    std::vector<std::string> keys;
    for (const std::string& name : table.header) {
        std::string key = name;
        for (std::size_t n = 2; std::find(keys.begin(), keys.end(), key) != keys.end(); ++n) {
            key = name + " " + std::to_string(n);
        }
        keys.push_back(std::move(key));
    }
    std::string out = "[";
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        if (r > 0) {
            out += ',';
        }
        out += '{';
        for (std::size_t c = 0; c < keys.size(); ++c) {
            if (c > 0) {
                out += ',';
            }
            AppendJsonString(out, keys[c]);
            out += ':';
            // A short hand-built row reads as empty cells.
            const std::vector<std::string>& row = table.rows[r];
            AppendJsonString(out, c < row.size() ? std::string_view(row[c]) : std::string_view());
        }
        out += '}';
    }
    out += ']';
    return out;
}

}  // namespace island
