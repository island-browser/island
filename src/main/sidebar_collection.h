#ifndef ISLAND_SIDEBAR_COLLECTION_H_
#define ISLAND_SIDEBAR_COLLECTION_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "design_tokens.h"

namespace island {

// CEF-free rules for the sidebar's favorite collection (spec §3.1): how many global icons the
// strip shows, and the accessible names and headers the rows carry. Kept free of CEF so the
// fit and the wording are unit-tested without a running browser.

// The global strip shows at most this many favorite icons.
inline constexpr std::size_t kGlobalStripMaxIcons = 6;

// What the global strip draws: the first `icons` favorites, a "+N" control for the remaining
// `hidden` favorites (present only when hidden > 0), and the "+" add control when the active page
// has an acceptable URL.
struct GlobalStripLayout {
    std::size_t icons = 0;
    std::size_t hidden = 0;
    bool add_visible = false;

    bool operator==(const GlobalStripLayout&) const = default;
};

// Picks the largest icon count, at most kGlobalStripMaxIcons, whose controls (icons, the "+N"
// control when some favorites are hidden, and "+" when `add_visible`) fit `width_dip` with
// `gap_dip` between them. The "+N" control counts toward the fit, so 8 favorites in a narrow
// sidebar show fewer icons and a larger N rather than wrapping or overflowing.
[[nodiscard]] GlobalStripLayout ComputeGlobalStripLayout(std::size_t count, int width_dip,
                                                         int plate_dip, int gap_dip,
                                                         bool add_visible);

// The accessible names of the strip (spec §6).
[[nodiscard]] std::string GlobalFavoriteAccessibleName(std::string_view title);
[[nodiscard]] std::string MoreGlobalFavoritesAccessibleName(std::size_t hidden);
[[nodiscard]] std::string MoreGlobalFavoritesLabel(std::size_t hidden);
[[nodiscard]] std::string_view AddGlobalFavoriteAccessibleName();
// A favorite row in the current space's favorites list.
[[nodiscard]] std::string SpaceFavoriteAccessibleName(std::string_view title);

// Section header text per look (spec §3.1): Native and Graphite name the strip "Global…", Arc
// keeps the small "Favorites" label. The current space's favorites are "<Space> · Favorites" in
// every look.
[[nodiscard]] std::string GlobalFavoritesHeaderTitle(LookPreset look);
[[nodiscard]] std::string SpaceFavoritesHeaderTitle(std::string_view space_name);

// The fill of a favorite plate or row (spec §3.1): the neutral raised `segment_selected` when it
// is the displayed favorite, the soft hover fill under the pointer, and the surface otherwise.
// The accent is never used for a favorite.
[[nodiscard]] ArgbColor FavoriteFillColor(const ChromeTokens& tokens, bool displayed, bool hovered);

}  // namespace island

#endif  // ISLAND_SIDEBAR_COLLECTION_H_
