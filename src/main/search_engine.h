#ifndef ISLAND_SEARCH_ENGINE_H_
#define ISLAND_SEARCH_ENGINE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace island {

// The web search engines the address bar and the Island start page can send a
// query to. Unlike the search palette's providers (search_provider.h, a fixed
// five-row palette contract that always appends "?q="), each engine carries
// the full URL prefix up to the query value, because the engines disagree on
// the parameter name (Yandex uses "text"). The user picks one in Settings and
// the choice is stored by `key`.
enum class SearchEngineId : std::uint8_t {
    kGoogle,
    kDuckDuckGo,
    kYandex,
};

struct SearchEngine {
    SearchEngineId id;
    // Stable identifier used in prefs and the page bridge.
    std::string_view key;
    std::string_view display_name;
    // HTTPS URL up to and including "<param>=", with no query value.
    std::string_view url_prefix;

    bool operator==(const SearchEngine&) const = default;
};

// The engines in display order; the first entry (Google) is the default.
[[nodiscard]] std::span<const SearchEngine> SearchEngines() noexcept;

// True when `key` names an engine exactly.
[[nodiscard]] bool IsSearchEngineKey(std::string_view key) noexcept;

// The engine whose key is `key`; any unknown key (an edited prefs file, a
// future engine) resolves to the default Google entry so a search never fails.
[[nodiscard]] const SearchEngine& SearchEngineForKey(std::string_view key) noexcept;

// `engine.url_prefix` plus `query` (edge ASCII whitespace trimmed) percent
// encoded per RFC 3986 with PercentEncodeQuery, e.g.
// "https://www.google.com/search?q=hello%20world".
[[nodiscard]] std::string BuildSearchUrl(const SearchEngine& engine, std::string_view query);

// What the start page's two-mode switch does with Enter, persisted as the
// last-used mode.
inline constexpr std::string_view kStartModeSearch = "search";
inline constexpr std::string_view kStartModeAgent = "agent";
// Any value other than "agent" is Search, the default.
[[nodiscard]] std::string_view NormalizeStartMode(std::string_view mode) noexcept;

}  // namespace island

#endif  // ISLAND_SEARCH_ENGINE_H_
