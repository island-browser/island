#include "search_engine.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "search_provider.h"

namespace island {
namespace {

constexpr std::array<SearchEngine, 3> kSearchEngines = {{
    {SearchEngineId::kGoogle, "google", "Google", "https://www.google.com/search?q="},
    {SearchEngineId::kDuckDuckGo, "duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q="},
    {SearchEngineId::kYandex, "yandex", "Yandex", "https://yandex.com/search/?text="},
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
        if (!EqualsAsciiIgnoreCase(authority, shape.host) || path != shape.path) {
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
            if (IsBlank(decoded)) {
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
