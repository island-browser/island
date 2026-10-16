#ifndef ISLAND_START_TILES_H_
#define ISLAND_START_TILES_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "history_recorder.h"

namespace island {

// One speed-dial tile on the start page: a site the person visits often.
struct StartTile {
    std::string title;
    std::string url;
    std::string host;

    bool operator==(const StartTile&) const = default;
};

// The start page shows at most this many tiles.
inline constexpr std::size_t kStartTileLimit = 6;

// Turns the most-visited history (already ordered by the caller) into tiles. Only http and
// https pages with a plain host qualify (no credentials), one tile per host (the first ranked
// page wins), at most `limit`. A page without a title is labelled with its host.
[[nodiscard]] std::vector<StartTile> BuildStartTiles(const std::vector<HistoryResult>& ranked,
                                                     std::size_t limit = kStartTileLimit);

// Whether a start page tile may open `url`: http or https, with a host and no credentials.
[[nodiscard]] bool IsWebTileUrl(std::string_view url);

}  // namespace island

#endif  // ISLAND_START_TILES_H_
