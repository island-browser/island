#include "sidebar_collection.h"

#include <algorithm>

#include "row_raster.h"

namespace island {
namespace {

// The plate size that fits `controls` squares with at least the minimum gap in `width_dip`, or 0
// when even the floors do not fit. The largest plate wins; the gap then takes the spare room.
struct PlateFit {
    int plate_dip = 0;
    int gap_dip = 0;
};

PlateFit FitPlate(std::size_t controls, int width_dip) {
    const int count = static_cast<int>(controls);
    const int gaps = count - 1;
    for (int plate = kGlobalStripMaxPlateDip; plate >= kGlobalStripMinPlateDip; --plate) {
        const int used = count * plate;
        if (gaps == 0) {
            if (used <= width_dip) {
                return {.plate_dip = plate, .gap_dip = kGlobalStripMaxGapDip};
            }
            continue;
        }
        if (used + gaps * kGlobalStripMinGapDip > width_dip) {
            continue;
        }
        const int room = (width_dip - used) / gaps;
        return {.plate_dip = plate,
                .gap_dip = std::clamp(room, kGlobalStripMinGapDip, kGlobalStripMaxGapDip)};
    }
    return {};
}

}  // namespace

GlobalStripLayout ComputeGlobalStripLayout(std::size_t count, int width_dip) {
    // Largest icon count first; each candidate counts its own "+N" control.
    for (std::size_t icons = std::min(kGlobalStripMaxIcons, count) + 1; icons-- > 0;) {
        const std::size_t hidden = count - icons;
        const std::size_t controls = icons + (hidden > 0 ? 1 : 0);
        if (controls == 0) {
            return {.icons = 0, .hidden = 0};
        }
        if (const PlateFit fit = FitPlate(controls, width_dip); fit.plate_dip > 0) {
            return {.icons = icons,
                    .hidden = hidden,
                    .plate_dip = fit.plate_dip,
                    .gap_dip = fit.gap_dip};
        }
    }
    // Not even one control fits at the floors: keep "+N" reachable with no icons.
    return {.icons = 0,
            .hidden = count,
            .plate_dip = kGlobalStripMinPlateDip,
            .gap_dip = kGlobalStripMinGapDip};
}

BookmarkStatusColors BookmarkStatusRowColors(const ChromeTokens& tokens, bool ok) {
    if (ok) {
        return {.row = tokens.surface_secondary,
                .text = tokens.text,
                .retry = tokens.text,
                .dismiss = tokens.text};
    }
    return {.row = tokens.warning_bg,
            .text = tokens.warning_text,
            .retry = tokens.text,
            .dismiss = tokens.warning_text};
}

bool GlobalHeaderRowVisible(const GlobalStripLayout& layout, bool add_visible) {
    return GlobalStripVisible(layout) || add_visible;
}

bool GlobalStripVisible(const GlobalStripLayout& layout) {
    return layout.icons > 0 || layout.hidden > 0;
}

bool FavoritesPrecedeTabs(std::size_t global_count, std::size_t space_count) {
    return global_count > 0 || space_count > 0;
}

bool CollapsedRailSeparatorVisible(std::size_t global_count, bool below) {
    return global_count > 0 && below;
}

std::optional<ArgbColor> TabRowFill(const ChromeTokens& tokens, bool active, bool pinned,
                                    bool hovered, bool behind_favorite) {
    if (active && behind_favorite) {
        return tokens.segment_selected;
    }
    if (active) {
        return ResolveRowColors(tokens, /*selected=*/true, hovered, /*slept=*/false).fill;
    }
    if (hovered) {
        return ResolveRowColors(tokens, /*selected=*/false, /*hovered=*/true, /*slept=*/false).fill;
    }
    if (pinned && tokens.look != LookPreset::kNative) {
        return MixColor(tokens.canvas, tokens.selected, 0.5);
    }
    return std::nullopt;
}

std::string GlobalFavoriteAccessibleName(std::string_view title, std::size_t position,
                                         std::size_t total) {
    return "Global favorite: " + std::string(title) + " (" + std::to_string(position) + " of " +
           std::to_string(total) + ")";
}

std::string SpaceFavoriteAccessibleName(std::string_view title) {
    return "Favorite: " + std::string(title);
}

std::string MoreGlobalFavoritesAccessibleName(std::size_t hidden) {
    return "Show " + std::to_string(hidden) + " more global favorites";
}

std::string MoreGlobalFavoritesLabel(std::size_t hidden) { return "+" + std::to_string(hidden); }

std::string_view AddGlobalFavoriteAccessibleName() {
    return "Add current page as a global favorite";
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

std::string SpaceFavoritesHeaderTitle(LookPreset look, std::string_view space_name) {
    if (look == LookPreset::kGraphite || space_name.empty()) {
        return "Favorites";
    }
    return std::string(space_name) + " \xC2\xB7 Favorites";
}

std::string TabsHeaderTitle(LookPreset look, std::string_view space_name) {
    if (look != LookPreset::kNative || space_name.empty()) {
        return "Tabs";
    }
    return std::string(space_name) + " \xC2\xB7 Tabs";
}

bool TabsHeaderBeforeFirstRow(LookPreset look, bool favorites_precede, bool has_tabs) {
    if (!has_tabs) {
        return false;
    }
    return look == LookPreset::kNative || favorites_precede;
}

int SpaceFavoriteRowHeightDip(LookPreset look, Density density) {
    if (look != LookPreset::kNative) {
        return 32;
    }
    return density == Density::kCompact ? 28 : 34;
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

ArgbColor FavoriteTextColor(const ChromeTokens& tokens) { return tokens.text; }

ArgbColor FavoriteMoreTextColor(const ChromeTokens& tokens) { return tokens.text_secondary; }

std::vector<FavoriteInkEntry> FavoriteInkPlan(const ChromeTokens& tokens) {
    return {
        {FavoriteInkTarget::kRowText, FavoriteTextColor(tokens)},
        {FavoriteInkTarget::kMoreText, FavoriteMoreTextColor(tokens)},
        {FavoriteInkTarget::kAddGlyph, tokens.text_secondary},
        {FavoriteInkTarget::kFallbackGlyph, tokens.text_secondary},
    };
}

}  // namespace island
