#ifndef ISLAND_SPACE_ROUTING_H_
#define ISLAND_SPACE_ROUTING_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "space.h"
#include "tab_id.h"

namespace island {

// Host routing (R-R1): a host pattern such as "github.com" sends a new web tab to the
// space that holds the rule. The rules live in each space's routes array in session.json.
// CEF-free: the window (R-R2) applies the result when it opens a tab.

struct RouteRule {
    std::string pattern;  // normalized: lowercase host, no port, dot or "www."
    SpaceId space;
};

struct RouteMove {
    // The space the pattern was taken from when it already had a rule elsewhere.
    std::optional<SpaceId> moved_from;
};

// Normalizes what the person typed into a host pattern. Accepts a bare host or an http(s)
// URL; strips the scheme, path, port, a trailing dot and a leading "www.", and lowercases.
// Returns nullopt for IP literals, localhost, names without a dot, and invalid characters.
[[nodiscard]] std::optional<std::string> NormalizeRoutePattern(std::string_view input);

// The space for an http(s) URL, or nullopt when no rule matches or the URL is not a web URL
// (file:, data:, about:, a scheme-less string). A rule matches the host itself and any
// subdomain of it, never a lookalike (notgithub.com does not match github.com). The longest
// matching pattern wins.
[[nodiscard]] std::optional<SpaceId> RouteForUrl(std::string_view url,
                                                 const std::vector<RouteRule>& rules);

// Every rule of every space, in space order.
[[nodiscard]] std::vector<RouteRule> RouteRulesFromSpaces(const std::vector<Space>& spaces);

// Gives |pattern| to |target|, removing it from every other space, so one host belongs to one
// space. |pattern| must already be normalized (NormalizeRoutePattern).
RouteMove AssignRoute(std::vector<Space>& spaces, SpaceId target, std::string pattern);

// Removes |pattern| from whichever space holds it. Returns false when no space holds it.
bool RemoveRoute(std::vector<Space>& spaces, std::string_view pattern);

}  // namespace island

#endif  // ISLAND_SPACE_ROUTING_H_
