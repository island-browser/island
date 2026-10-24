#include "sidebar_collection.h"

#include <algorithm>

namespace island {

GlobalStripLayout ComputeGlobalStripLayout(std::size_t count, int width_dip, int plate_dip,
                                           int gap_dip, bool add_visible) {
    const std::size_t largest = std::min(kGlobalStripMaxIcons, count);
    // Largest icon count first; each candidate counts its own "+N" and "+" controls.
    for (std::size_t icons = largest + 1; icons-- > 0;) {
        const std::size_t hidden = count - icons;
        const std::size_t controls = icons + (hidden > 0 ? 1 : 0) + (add_visible ? 1 : 0);
        if (controls == 0) {
            return {.icons = 0, .hidden = hidden, .add_visible = add_visible};
        }
        const int needed =
            static_cast<int>(controls) * plate_dip + static_cast<int>(controls - 1) * gap_dip;
        if (needed <= width_dip) {
            return {.icons = icons, .hidden = hidden, .add_visible = add_visible};
        }
    }
    // Not even one control fits: keep the "+N" (or "+") reachable with no icons.
    return {.icons = 0, .hidden = count, .add_visible = add_visible};
}

std::string GlobalFavoriteAccessibleName(std::string_view title) {
    return "Global favorite: " + std::string(title);
}

std::string MoreGlobalFavoritesAccessibleName(std::size_t hidden) {
    return "Show " + std::to_string(hidden) + " more global favorites";
}

std::string MoreGlobalFavoritesLabel(std::size_t hidden) { return "+" + std::to_string(hidden); }

std::string_view AddGlobalFavoriteAccessibleName() {
    return "Add current page as a global favorite";
}

std::string SpaceFavoriteAccessibleName(std::string_view title) {
    return "Favorite: " + std::string(title);
}

std::string GlobalFavoritesHeaderTitle(LookPreset look) {
    switch (look) {
        case LookPreset::kNative:
            return "Global favorites";
        case LookPreset::kArc:
            return "Favorites";
        case LookPreset::kGraphite:
            return "Global";
    }
    return "Global favorites";
}

std::string SpaceFavoritesHeaderTitle(std::string_view space_name) {
    return std::string(space_name) + " \xC2\xB7 Favorites";
}

ArgbColor FavoriteFillColor(const ChromeTokens& tokens, bool displayed, bool hovered) {
    if (displayed) {
        return tokens.segment_selected;
    }
    if (hovered) {
        return tokens.hover;
    }
    return tokens.canvas;
}

}  // namespace island
