#ifndef ISLAND_ADDRESS_INPUT_H_
#define ISLAND_ADDRESS_INPUT_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace island {

// How the address bar treats what the user typed, decided before the URL
// validator (address_policy.h) sees it. Only the address bar's submit path
// uses this; session restore, bookmarks, the command palette and agent
// navigation keep validating strictly.
enum class AddressInputKind : std::uint8_t {
    // `text` is an address to hand to the validator.
    kUrl,
    // `text` is a web search query.
    kSearch,
};

struct AddressInput {
    AddressInputKind kind = AddressInputKind::kUrl;
    std::string text;

    bool operator==(const AddressInput&) const = default;
};

// The browser-style heuristic, applied to the input with edge whitespace
// trimmed:
//   1. An explicit scheme ("scheme://..." or a well-known scheme such as
//      http:, https:, about:, data:, file:, javascript:, mailto:) is a URL, so
//      the validator keeps judging it exactly as before ("javascript:x" is
//      still rejected, not searched). "localhost:3000" and "site.test:8080"
//      only look like schemes; they are hosts with a port, not schemes.
//   2. Anything else containing whitespace or control characters is a search.
//   3. A host (localhost, an IPv4 or bracketed IPv6 literal, or a dotted name
//      whose last label is alphabetic and at least two characters), with an
//      optional :port and /path?query#fragment, is a URL: "http://" + input for
//      localhost and IP literals (dev servers and LAN devices rarely speak
//      TLS), "https://" + input for domain names.
//   4. Everything else is a search ("google", "3.14", "user@host").
// Empty input stays a URL so the validator reports it as it always did.
[[nodiscard]] AddressInput ClassifyAddressInput(std::string_view input);

}  // namespace island

#endif  // ISLAND_ADDRESS_INPUT_H_
