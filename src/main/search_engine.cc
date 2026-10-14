#include "search_engine.h"

#include <array>

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

std::string_view NormalizeStartMode(std::string_view mode) noexcept {
    return mode == kStartModeAgent ? kStartModeAgent : kStartModeSearch;
}

}  // namespace island
