#include "space_routing.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

#include "history_recorder.h"

namespace island {
namespace {

std::string_view TrimSpaces(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// A DNS-style name: at least one dot, labels of letters, digits and hyphens that do not
// start or end with a hyphen, and an alphabetic top-level label of two or more characters
// (so IPv4 literals such as 192.168.1.10 are rejected). The same last-label rule the address
// bar uses to tell a host from a search (address_input.h).
bool IsRoutableHost(std::string_view host) {
    if (host.find('.') == std::string_view::npos) return false;
    std::size_t start = 0;
    std::string_view last_label;
    while (true) {
        const std::size_t end = host.find('.', start);
        const std::string_view label = host.substr(start, end - start);
        if (label.empty() || label.front() == '-' || label.back() == '-') return false;
        for (char c : label) {
            const bool allowed = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-';
            if (!allowed) return false;
        }
        if (end == std::string_view::npos) {
            last_label = label;
            break;
        }
        start = end + 1;
    }
    if (last_label.size() < 2) return false;
    return std::all_of(last_label.begin(), last_label.end(),
                       [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
}

// A bare "host" or "host:port" (no scheme). Returns the host part, or nullopt.
std::optional<std::string_view> BareHost(std::string_view text) {
    const std::string_view authority = text.substr(0, text.find_first_of("/?#"));
    if (authority.empty() || authority.front() == '[' ||
        authority.find('@') != std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t colon = authority.find(':');
    if (colon == std::string_view::npos) return authority;
    const std::string_view port = authority.substr(colon + 1);
    if (port.empty() || authority.find(':', colon + 1) != std::string_view::npos ||
        !std::all_of(port.begin(), port.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return std::nullopt;
    }
    return authority.substr(0, colon);
}

// True when |host| is |pattern| or ends with "." + |pattern|.
bool HostMatches(std::string_view host, std::string_view pattern) {
    if (host == pattern) return true;
    return host.size() > pattern.size() && host.ends_with(pattern) &&
           host[host.size() - pattern.size() - 1] == '.';
}

std::vector<std::string>::iterator FindRoute(std::vector<std::string>& routes,
                                             std::string_view pattern) {
    return std::find(routes.begin(), routes.end(), pattern);
}

}  // namespace

std::optional<std::string> NormalizeRoutePattern(std::string_view input) {
    const std::string_view text = TrimSpaces(input);
    if (text.empty()) return std::nullopt;

    std::optional<std::string> host;
    if (text.find("://") != std::string_view::npos) {
        // http(s) only; WebUrlHost drops the port and refuses credentials.
        host = WebUrlHost(text);
    } else if (const std::optional<std::string_view> bare = BareHost(text)) {
        host = std::string(*bare);
    }
    if (!host) return std::nullopt;

    std::string normalized = LowerAscii(*host);
    if (normalized.ends_with('.')) normalized.pop_back();
    if (normalized.starts_with("www.")) normalized.erase(0, 4);
    if (!IsRoutableHost(normalized)) return std::nullopt;
    return normalized;
}

std::optional<SpaceId> RouteForUrl(std::string_view url, const std::vector<RouteRule>& rules) {
    const std::optional<std::string> web_host = WebUrlHost(url);
    if (!web_host) return std::nullopt;
    std::string_view host = *web_host;
    if (host.ends_with('.')) host.remove_suffix(1);

    const RouteRule* best = nullptr;
    for (const RouteRule& rule : rules) {
        if (!HostMatches(host, rule.pattern)) continue;
        if (best == nullptr || rule.pattern.size() > best->pattern.size()) best = &rule;
    }
    if (best == nullptr) return std::nullopt;
    return best->space;
}

std::vector<RouteRule> RouteRulesFromSpaces(const std::vector<Space>& spaces) {
    std::vector<RouteRule> rules;
    for (const Space& space : spaces) {
        for (const std::string& pattern : space.routes()) {
            rules.push_back(RouteRule{pattern, space.id()});
        }
    }
    return rules;
}

RouteMove AssignRoute(std::vector<Space>& spaces, SpaceId target, std::string pattern) {
    RouteMove move;
    for (Space& space : spaces) {
        if (space.id() == target) continue;
        std::vector<std::string> routes = space.routes();
        const auto found = FindRoute(routes, pattern);
        if (found == routes.end()) continue;
        routes.erase(found);
        space.SetRoutes(std::move(routes));
        move.moved_from = space.id();
    }
    for (Space& space : spaces) {
        if (space.id() != target) continue;
        std::vector<std::string> routes = space.routes();
        if (FindRoute(routes, pattern) == routes.end()) {
            routes.push_back(std::move(pattern));
            space.SetRoutes(std::move(routes));
        }
        break;
    }
    return move;
}

bool RemoveRoute(std::vector<Space>& spaces, std::string_view pattern) {
    for (Space& space : spaces) {
        std::vector<std::string> routes = space.routes();
        const auto found = FindRoute(routes, pattern);
        if (found == routes.end()) continue;
        routes.erase(found);
        space.SetRoutes(std::move(routes));
        return true;
    }
    return false;
}

}  // namespace island
