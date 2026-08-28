#ifndef ISLAND_SEARCH_PROVIDER_H_
#define ISLAND_SEARCH_PROVIDER_H_

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace island {

// Identifies one entry of the static provider table. The order of the
// enumerators is the display order of the palette rows and is part of the
// testable contract.
enum class SearchProviderId : std::uint8_t {
    kChatGpt,
    kPerplexity,
    kClaude,
    kGemini,
    kGoogle,
};

// One palette row: a stable id, the display name, and the HTTPS base URL the
// query is appended to. `base_url` never carries the query component; the
// "?q=" separator is supplied by ComposeSearchUrl so no call site can produce a
// doubled query separator.
struct SearchProvider {
    SearchProviderId id;
    std::string_view display_name;
    std::string_view base_url;

    bool operator==(const SearchProvider&) const = default;
};

// Percent-encodes `query` per RFC 3986: the unreserved set (A-Z, a-z, 0-9, '-',
// '.', '_', '~') passes through verbatim and every other byte becomes "%XX"
// with uppercase hex digits. Input is treated as an opaque UTF-8 byte sequence,
// so the function is total and deterministic even for invalid UTF-8. Spaces
// become "%20", never "+".
[[nodiscard]] std::string PercentEncodeQuery(std::string_view query);

// The five providers in their contractual display order.
[[nodiscard]] std::span<const SearchProvider> SearchProviders() noexcept;

// Returns the provider with `id`, or nullptr when the table has no such entry.
[[nodiscard]] const SearchProvider* FindSearchProvider(SearchProviderId id) noexcept;

// Composes exactly `provider.base_url + "?q=" + PercentEncodeQuery(query)`.
// This is the only place in the project that builds a provider URL.
[[nodiscard]] std::string ComposeSearchUrl(const SearchProvider& provider, std::string_view query);

// True when `query` holds at least one non-whitespace byte. The palette rejects
// everything else without navigating; this is the only validation it performs.
[[nodiscard]] bool IsSubmittableQuery(std::string_view query) noexcept;

}  // namespace island

#endif  // ISLAND_SEARCH_PROVIDER_H_
