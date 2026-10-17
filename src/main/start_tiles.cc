#include "start_tiles.h"

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
        std::optional<std::string> host = WebUrlHost(result.url);
        if (!host || !hosts.insert(*host).second) {
            continue;
        }
        std::string title = Trimmed(result.title);
        tiles.push_back(
            {.title = title.empty() ? *host : std::move(title), .url = result.url, .host = *host});
    }
    return tiles;
}

bool IsWebTileUrl(std::string_view url) { return WebUrlHost(url).has_value(); }

}  // namespace island
