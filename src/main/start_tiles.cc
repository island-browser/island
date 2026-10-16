#include "start_tiles.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "history_recorder.h"

namespace island {
namespace {

// The lowercase host of an absolute http or https URL. Nothing else qualifies: other schemes,
// URLs that carry credentials, and URLs without a host.
std::optional<std::string> WebHost(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::nullopt;
    }
    std::string scheme(url.substr(0, scheme_end));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (scheme != "http" && scheme != "https") {
        return std::nullopt;
    }
    const std::string_view rest = url.substr(scheme_end + 3);
    const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view host = authority;
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = host.substr(0, close + 1);
    } else {
        host = host.substr(0, host.find(':'));
    }
    if (host.empty()) {
        return std::nullopt;
    }
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower;
}

std::string Trimmed(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

}  // namespace

std::vector<StartTile> BuildStartTiles(const std::vector<HistoryResult>& ranked,
                                       std::size_t limit) {
    std::vector<StartTile> tiles;
    std::unordered_set<std::string> hosts;
    for (const HistoryResult& result : ranked) {
        if (tiles.size() >= limit) {
            break;
        }
        std::optional<std::string> host = WebHost(result.url);
        if (!host || !hosts.insert(*host).second) {
            continue;
        }
        std::string title = Trimmed(result.title);
        tiles.push_back(
            {.title = title.empty() ? *host : std::move(title), .url = result.url, .host = *host});
    }
    return tiles;
}

bool IsWebTileUrl(std::string_view url) { return WebHost(url).has_value(); }

}  // namespace island
