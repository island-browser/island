#ifndef ISLAND_SIDEBAR_COLLECTION_H_
#define ISLAND_SIDEBAR_COLLECTION_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "design_tokens.h"

namespace island {

// CEF-free rules for the sidebar's favorite collection (spec §3.1, ruling R11, ruling c): how the
// global strip fits its width, which headers the sections carry, and the accessible names. Kept
// free of CEF so the fit, the order rules and the wording are unit-tested without a browser.

// The global strip shows exactly min(kGlobalStripMaxIcons, count) icons, then "+N" when more exist.
inline constexpr std::size_t kGlobalStripMaxIcons = 6;
// The plate shrinks from 28 DIP toward 24 and the gap from 4 DIP toward 2 to fit the width.
inline constexpr int kGlobalStripMaxPlateDip = 28;
inline constexpr int kGlobalStripMinPlateDip = 24;
inline constexpr int kGlobalStripMaxGapDip = 4;
inline constexpr int kGlobalStripMinGapDip = 2;

// What the global strip draws: the first `icons` favorites, then a "+N" control for the remaining
// `hidden` favorites (present only when hidden > 0), with each control `plate_dip` square and
// `gap_dip` apart. The "+" add control is not part of the strip; it trails the header row.
struct GlobalStripLayout {
    std::size_t icons = 0;
    std::size_t hidden = 0;
    int plate_dip = kGlobalStripMaxPlateDip;
    int gap_dip = kGlobalStripMaxGapDip;

    bool operator==(const GlobalStripLayout&) const = default;
};

// Fits the strip into `width_dip`. The icon count is min(6, count) unless the controls do not
// fit even at the floors (24-DIP plates, 2-DIP gaps); then the count drops until they do, and the
// "+N" control grows to cover the rest. Among fits, the plate is as large as the width allows and
// the gap takes the remaining room (up to 4 DIP).
[[nodiscard]] GlobalStripLayout ComputeGlobalStripLayout(std::size_t count, int width_dip);

// The accessible names (spec §6). A global favorite's name carries its 1-based position and the
// total; a space favorite is named by its title only.
[[nodiscard]] std::string GlobalFavoriteAccessibleName(std::string_view title, std::size_t position,
                                                       std::size_t total);
[[nodiscard]] std::string SpaceFavoriteAccessibleName(std::string_view title);
[[nodiscard]] std::string MoreGlobalFavoritesAccessibleName(std::size_t hidden);
[[nodiscard]] std::string MoreGlobalFavoritesLabel(std::size_t hidden);
[[nodiscard]] std::string_view AddGlobalFavoriteAccessibleName();

// Section header text per look (spec §3.1 table). The global strip is "Global favorites" (Native),
// "Favorites" (Arc) or "Global" (Graphite). The current space's favorites are "<Space> ·
// Favorites", except Graphite, which reads "Favorites".
[[nodiscard]] std::string GlobalFavoritesHeaderTitle(LookPreset look);
[[nodiscard]] std::string SpaceFavoritesHeaderTitle(LookPreset look, std::string_view space_name);
// The tabs header: "<Space> · Tabs" in Native; Arc and Graphite show a small "Tabs" label (ruling
// c).
[[nodiscard]] std::string TabsHeaderTitle(LookPreset look, std::string_view space_name);

// Whether the tabs header sits before the first tab row (pinned or not). Native always carries it
// when there are tabs. Arc and Graphite carry it only when favorites precede the tabs (ruling c).
// With no tabs there is never a header, so none trails an empty list.
[[nodiscard]] bool TabsHeaderBeforeFirstRow(LookPreset look, bool favorites_precede, bool has_tabs);

// A space favorite row's height (spec §3.1): Native 28 compact, 34 comfortable; Arc and Graphite
// 32 in both densities.
[[nodiscard]] int SpaceFavoriteRowHeightDip(LookPreset look, Density density);

// The fill of a favorite plate or row (spec §3.1): the neutral raised `segment_selected` when it
// is the displayed favorite, the soft hover fill under the pointer, and the surface otherwise.
// The accent is never used for a favorite.
[[nodiscard]] ArgbColor FavoriteFillColor(const ChromeTokens& tokens, bool displayed, bool hovered);

}  // namespace island

#endif  // ISLAND_SIDEBAR_COLLECTION_H_
