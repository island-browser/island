#include "search_engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "address_input.h"
#include "query_display_ranges.h"
#include "search_provider.h"

namespace island {
namespace {

constexpr std::array<SearchEngine, 3> kSearchEngines = {{
    {SearchEngineId::kGoogle, "google", "Google", "https://www.google.com/search?q="},
    {SearchEngineId::kDuckDuckGo, "duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q="},
    {SearchEngineId::kYandex,
     "yandex",
     "Yandex",
     "https://yandex.com/search/?text=",
     {"yandex.ru", "ya.ru"}},
}};

[[nodiscard]] constexpr bool IsEdgeWhitespace(char character) noexcept {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

[[nodiscard]] bool EqualsAsciiIgnoreCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

constexpr int kHexRadix = 16;
constexpr int kFirstHexLetterValue = 10;

[[nodiscard]] int HexDigitValue(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + kFirstHexLetterValue;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + kFirstHexLetterValue;
    }
    return -1;
}

// '+' is a space and "%XX" is the byte it names. A "%" that is not followed by two hex digits
// stays literal, so a hand-typed query never fails to read back.
[[nodiscard]] std::string DecodeQueryValue(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '+') {
            decoded.push_back(' ');
        } else if (character == '%' && index + 2 < value.size() &&
                   HexDigitValue(value[index + 1]) >= 0 && HexDigitValue(value[index + 2]) >= 0) {
            decoded.push_back(static_cast<char>(HexDigitValue(value[index + 1]) * kHexRadix +
                                                HexDigitValue(value[index + 2])));
            index += 2;
        } else {
            decoded.push_back(character);
        }
    }
    return decoded;
}

// True when a non-ASCII code point may be shown in the address field's query: a letter, mark,
// number or space separator outside the excluded modifier-letter and fullwidth blocks (the
// generated table in query_display_ranges.h). Every other non-ASCII code point, including
// punctuation, symbols, format characters and line separators, makes the full address show.
[[nodiscard]] bool IsDisplayShownNonAscii(std::uint32_t code_point) noexcept {
    const auto after = std::upper_bound(
        kDisplayShownNonAsciiRanges.begin(), kDisplayShownNonAsciiRanges.end(), code_point,
        [](std::uint32_t value, const CodePointRange& range) { return value < range.first; });
    if (after == kDisplayShownNonAsciiRanges.begin()) {
        return false;
    }
    return code_point <= std::prev(after)->last;
}

// True when `text` is well-formed UTF-8 (no overlong forms, surrogates or values past U+10FFFF)
// and every code point is one the field may show: ASCII without controls, or a non-ASCII code
// point that IsDisplayShownNonAscii accepts.
[[nodiscard]] bool IsShowableQuery(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<std::uint8_t>(text[index]);
        std::size_t length = 1;
        std::uint32_t code_point = lead;
        if (lead >= 0x80) {
            if (lead >= 0xC2 && lead <= 0xDF) {
                length = 2;
                code_point = lead & 0x1FU;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                length = 3;
                code_point = lead & 0x0FU;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                length = 4;
                code_point = lead & 0x07U;
            } else {
                return false;
            }
        }
        if (index + length > text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<std::uint8_t>(text[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        const bool overlong =
            (length == 3 && code_point < 0x800) || (length == 4 && code_point < 0x10000);
        const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
        if (overlong || surrogate || code_point > 0x10FFFF) {
            return false;
        }
        const bool ascii_control = code_point < 0x20 || code_point == 0x7F;
        if (ascii_control || (code_point >= 0x80 && !IsDisplayShownNonAscii(code_point))) {
            return false;
        }
        index += length;
    }
    return true;
}

[[nodiscard]] bool IsBlank(std::string_view text) noexcept {
    for (const char character : text) {
        if (!IsEdgeWhitespace(character)) {
            return false;
        }
    }
    return true;
}

// The host, path and query parameter of an engine's url_prefix, which is
// "https://<host><path>?<param>=".
struct ResultsPageShape {
    std::string_view host;
    std::string_view path;
    std::string_view param;
};

[[nodiscard]] ResultsPageShape ShapeOf(const SearchEngine& engine) noexcept {
    constexpr std::string_view kScheme = "https://";
    const std::string_view rest = engine.url_prefix.substr(kScheme.size());
    const std::size_t path_start = rest.find_first_of("/?");
    const std::string_view tail = rest.substr(path_start);
    const std::size_t question = tail.find('?');
    return {
        .host = rest.substr(0, path_start),
        .path = tail.substr(0, question),
        // Drop the '?' and the trailing '='.
        .param = tail.substr(question + 1, tail.size() - question - 2),
    };
}

// The engine's own host or one of its regional results hosts, ASCII case ignored.
[[nodiscard]] bool HostMatches(const SearchEngine& engine, std::string_view authority) noexcept {
    if (EqualsAsciiIgnoreCase(authority, ShapeOf(engine).host)) {
        return true;
    }
    for (const std::string_view host : engine.extra_results_hosts) {
        if (!host.empty() && EqualsAsciiIgnoreCase(authority, host)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::span<const SearchEngine> SearchEngines() noexcept { return kSearchEngines; }

bool IsSearchEngineKey(std::string_view key) noexcept {
    for (const SearchEngine& engine : kSearchEngines) {
        if (engine.key == key) {
            return true;
        }
    }
    return false;
}

const SearchEngine& SearchEngineForKey(std::string_view key) noexcept {
    for (const SearchEngine& engine : kSearchEngines) {
        if (engine.key == key) {
            return engine;
        }
    }
    return kSearchEngines.front();
}

std::string BuildSearchUrl(const SearchEngine& engine, std::string_view query) {
    while (!query.empty() && IsEdgeWhitespace(query.front())) {
        query.remove_prefix(1);
    }
    while (!query.empty() && IsEdgeWhitespace(query.back())) {
        query.remove_suffix(1);
    }
    return std::string(engine.url_prefix) + PercentEncodeQuery(query);
}

std::optional<std::string> SearchQueryFromResultsUrl(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (!url.starts_with(kScheme)) {
        return std::nullopt;
    }
    url.remove_prefix(kScheme.size());
    url = url.substr(0, url.find('#'));
    const std::size_t path_start = url.find_first_of("/?");
    const std::string_view authority = url.substr(0, path_start);
    const std::string_view rest =
        path_start == std::string_view::npos ? std::string_view() : url.substr(path_start);
    const std::size_t question = rest.find('?');
    std::string_view path = rest.substr(0, question);
    if (path.empty()) {
        path = "/";
    }
    const std::string_view query =
        question == std::string_view::npos ? std::string_view() : rest.substr(question + 1);

    for (const SearchEngine& engine : kSearchEngines) {
        const ResultsPageShape shape = ShapeOf(engine);
        if (!HostMatches(engine, authority) || path != shape.path) {
            continue;
        }
        std::string_view remaining = query;
        while (!remaining.empty()) {
            const std::size_t amp = remaining.find('&');
            const std::string_view pair = remaining.substr(0, amp);
            remaining =
                amp == std::string_view::npos ? std::string_view() : remaining.substr(amp + 1);
            const std::size_t equals = pair.find('=');
            if (equals == std::string_view::npos || pair.substr(0, equals) != shape.param) {
                continue;
            }
            std::string decoded = DecodeQueryValue(pair.substr(equals + 1));
            // The field shows the query as text, so a query that would read as an address (or
            // that reorders or hides characters) is not shown; the URL is.
            if (IsBlank(decoded) || !IsShowableQuery(decoded) ||
                ClassifyAddressInput(decoded).kind != AddressInputKind::kSearch) {
                return std::nullopt;
            }
            return decoded;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string_view NormalizeStartMode(std::string_view mode) noexcept {
    if (mode == kStartModeAgent) return kStartModeAgent;
    if (mode == kStartModeResearch) return kStartModeResearch;
    return kStartModeSearch;
}

}  // namespace island
