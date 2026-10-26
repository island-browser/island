#ifndef ISLAND_SEARCH_ENGINE_H_
#define ISLAND_SEARCH_ENGINE_H_

#include <array>
#include <cstdint>
#include <optional>
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
    // Regional hosts whose results pages use the same path and parameter as url_prefix ("" = unused
    // slot). A results page on one of them still shows its query in the address field.
    std::array<std::string_view, 2> extra_results_hosts = {};

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

// The query a results page of one of SearchEngines() was opened for, or nullopt when `url` is not
// such a page. A match needs https, the engine's host or one of its extra_results_hosts (ASCII
// case ignored), the exact path, and the engine's query parameter (the part of url_prefix after
// '?', "q" or "text"). Other parameters and the fragment are ignored, the first occurrence of the
// parameter wins, and '+' and %XX decode. nullopt also comes back for a blank query, and for a
// query the address field must not show as text: one that reads as an address rather than a
// search (ClassifyAddressInput), that has a control, a format (Cf) character, a line separator or
// look-alike URL punctuation (fullwidth ASCII included), or that is not valid UTF-8. The address
// field then shows the real URL. While a results page shows its query, the real URL stays in the
// tab.
[[nodiscard]] std::optional<std::string> SearchQueryFromResultsUrl(std::string_view url);

// What the start page's three-mode switch does with Enter, persisted as the
// last-used mode.
inline constexpr std::string_view kStartModeSearch = "search";
inline constexpr std::string_view kStartModeAgent = "agent";
inline constexpr std::string_view kStartModeResearch = "research";
// Any value other than "agent" or "research" is Search, the default.
[[nodiscard]] std::string_view NormalizeStartMode(std::string_view mode) noexcept;

}  // namespace island

#endif  // ISLAND_SEARCH_ENGINE_H_
