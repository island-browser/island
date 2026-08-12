#include "search/query_parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace island {
namespace search {
namespace {

constexpr char FoldAscii(char c) {
    const std::uint8_t u = static_cast<std::uint8_t>(c);
    const bool is_upper_ascii = u >= 'A' && u <= 'Z';
    return is_upper_ascii ? static_cast<char>(u + ('a' - 'A')) : c;
}

// A single query input unit (a bare term or a quoted phrase) with the byte
// extent of the raw text it was drawn from.
struct RawUnit {
    enum class Kind { kTerm, kPhrase };
    Kind kind = Kind::kTerm;
    std::string_view text;
    // True when the closing quote (phrases) or term end (terms) is followed by
    // a single `*` prefix marker.
    bool is_prefix = false;
    std::size_t start = 0;
};

// Returns true and advances `i` past the whitespace run; otherwise returns
// false leaving `i` unchanged.
bool SkipWs(std::string_view input, std::size_t& i) {
    const std::size_t begin = i;
    while (i < input.size()) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        const bool is_ws =
            c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        if (!is_ws) {
            break;
        }
        ++i;
    }
    return i > begin;
}

// Scans raw units. Returns false on error and fills `*error`.
bool ScanUnits(std::string_view input, std::vector<RawUnit>& units, QueryParseError* error) {
    std::size_t i = 0;
    while (i < input.size()) {
        SkipWs(input, i);
        if (i >= input.size()) {
            break;
        }

        RawUnit unit;
        unit.start = i;

        if (input[i] == '"') {
            unit.kind = RawUnit::Kind::kPhrase;
            const std::size_t open = i;
            ++i;
            const std::size_t content_begin = i;
            while (i < input.size() && input[i] != '"') {
                ++i;
            }
            if (i >= input.size()) {
                error->kind = QueryParseError::Kind::kUnmatchedQuote;
                error->offset = open;
                return false;
            }
            const std::size_t content_end = i;
            ++i;  // consume closing quote
            unit.text = input.substr(content_begin, content_end - content_begin);
            // Optional trailing prefix marker must be the very next byte.
            if (i < input.size() && input[i] == '*') {
                unit.is_prefix = true;
                ++i;
            }
        } else {
            unit.kind = RawUnit::Kind::kTerm;
            const std::size_t begin = i;
            while (i < input.size() && input[i] != '"' && input[i] != ' ' && input[i] != '\t' &&
                   input[i] != '\n' && input[i] != '\r' && input[i] != '\f' && input[i] != '\v') {
                ++i;
            }
            const std::size_t end = i;
            unit.text = input.substr(begin, end - begin);
            // Optional trailing prefix marker must be the very last byte of
            // the term.
            if (!unit.text.empty() && unit.text.back() == '*') {
                unit.is_prefix = true;
                unit.text.remove_suffix(1);
            }
        }

        units.push_back(std::move(unit));
    }
    return true;
}

// Builds the parsed query from raw units, applying bounds and the normalizer.
// Returns false on error and fills `*error`.
bool BuildQuery(const std::vector<RawUnit>& units, const TermNormalizer& raw_normalizer,
                ParsedQuery& query, QueryParseError* error) {
    const TermNormalizer& normalize =
        raw_normalizer ? raw_normalizer : [](std::string_view t) { return DefaultNormalize(t); };

    std::size_t total_units = 0;
    for (const RawUnit& unit : units) {
        if (unit.kind == RawUnit::Kind::kTerm) {
            if (unit.is_prefix && unit.text.empty()) {
                error->kind = QueryParseError::Kind::kEmptyTermAfterMarker;
                error->offset = unit.start;
                return false;
            }
            if (unit.text.size() > kMaxTermLength) {
                error->kind = QueryParseError::Kind::kTermTooLong;
                error->offset = unit.start;
                return false;
            }
            std::string normalized = normalize(unit.text);
            query.terms.push_back(QueryTerm{std::move(normalized), unit.is_prefix});
        } else {
            if (unit.text.empty()) {
                error->kind = QueryParseError::Kind::kEmptyPhrase;
                error->offset = unit.start;
                return false;
            }
            if (unit.text.size() > kMaxPhraseLength) {
                error->kind = QueryParseError::Kind::kPhraseTooLong;
                error->offset = unit.start;
                return false;
            }
            QueryPhrase phrase;
            std::size_t total_bytes = 0;
            std::size_t word_count = 0;
            std::size_t word_start = 0;
            const std::size_t n = unit.text.size();
            while (word_start < n) {
                while (word_start < n &&
                       (unit.text[word_start] == ' ' || unit.text[word_start] == '\t' ||
                        unit.text[word_start] == '\n' || unit.text[word_start] == '\r' ||
                        unit.text[word_start] == '\f' || unit.text[word_start] == '\v')) {
                    ++word_start;
                }
                if (word_start >= n) {
                    break;
                }
                std::size_t word_end = word_start;
                while (word_end < n && unit.text[word_end] != ' ' && unit.text[word_end] != '\t' &&
                       unit.text[word_end] != '\n' && unit.text[word_end] != '\r' &&
                       unit.text[word_end] != '\f' && unit.text[word_end] != '\v') {
                    ++word_end;
                }
                std::string normalized =
                    normalize(unit.text.substr(word_start, word_end - word_start));
                total_bytes += normalized.size();
                phrase.words.push_back(std::move(normalized));
                ++word_count;
                word_start = word_end;
            }
            if (word_count > kMaxPhraseWords || total_bytes > kMaxPhraseLength) {
                error->kind = QueryParseError::Kind::kPhraseTooLong;
                error->offset = unit.start;
                return false;
            }
            phrase.is_prefix = unit.is_prefix;
            query.phrases.push_back(std::move(phrase));
        }

        ++total_units;
        if (total_units > kMaxQueryUnits) {
            error->kind = QueryParseError::Kind::kTooManyUnits;
            error->offset = unit.start;
            return false;
        }
    }

    return true;
}

}  // namespace

std::string DefaultNormalize(std::string_view token) {
    std::string out;
    out.reserve(token.size() < kMaxTermLength ? token.size() : kMaxTermLength);
    std::size_t written = 0;
    for (const char c : token) {
        if (written >= kMaxTermLength) {
            break;
        }
        out.push_back(FoldAscii(c));
        ++written;
    }
    return out;
}

QueryParseResult ParseQuery(std::string_view input) { return ParseQuery(input, TermNormalizer{}); }

QueryParseResult ParseQuery(std::string_view input, const TermNormalizer& normalizer) {
    QueryParseResult result;
    result.ok = true;

    std::vector<RawUnit> units;
    units.reserve(16);
    if (!ScanUnits(input, units, &result.error)) {
        result.ok = false;
        return result;
    }
    if (!BuildQuery(units, normalizer, result.query, &result.error)) {
        result.ok = false;
        return result;
    }
    return result;
}

}  // namespace search
}  // namespace island