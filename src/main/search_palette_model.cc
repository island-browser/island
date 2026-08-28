#include "search_palette_model.h"

#include <algorithm>
#include <utility>

namespace island {

void SearchPaletteModel::Open() {
    visible_ = true;
    query_.clear();
    highlighted_index_ = 0;
}

bool SearchPaletteModel::Close() {
    const bool was_visible = visible_;
    visible_ = false;
    return was_visible;
}

void SearchPaletteModel::SetQuery(std::string query) { query_ = std::move(query); }

void SearchPaletteModel::MoveHighlight(int delta) {
    const auto count = static_cast<int>(SearchProviders().size());
    if (count == 0) {
        return;
    }
    const int current = static_cast<int>(highlighted_index_);
    int next = (current + delta) % count;
    if (next < 0) {
        next += count;
    }
    highlighted_index_ = static_cast<std::size_t>(next);
}

void SearchPaletteModel::SetHighlightedIndex(std::size_t index) {
    if (index < SearchProviders().size()) {
        highlighted_index_ = index;
    }
}

std::optional<SearchSubmission> SearchPaletteModel::Submit() {
    if (!IsSubmittableQuery(query_)) {
        return std::nullopt;
    }
    const SearchProvider& provider = highlighted_provider();
    SearchSubmission submission{
        .provider = provider.id,
        .query = query_,
        .url = ComposeSearchUrl(provider, query_),
    };
    visible_ = false;
    return submission;
}

const SearchProvider& SearchPaletteModel::highlighted_provider() const noexcept {
    const std::span<const SearchProvider> providers = SearchProviders();
    return providers[highlighted_index_ < providers.size() ? highlighted_index_ : 0];
}

ArgbColor PaletteSurfaceRole(PaletteSurfaceSlot slot, ChromeTheme theme) noexcept {
    return PaletteSurfaceRoleForTokens(slot, ChromeTokens::ForTheme(theme));
}

ArgbColor PaletteSurfaceRoleForTokens(PaletteSurfaceSlot slot,
                                      const ChromeTokens& tokens) noexcept {
    switch (slot) {
        case PaletteSurfaceSlot::kPanel:
            // The palette card lifts to the primary surface so it reads as one
            // raised sheet over the canvas and the rail alike.
            return tokens.surface;
        case PaletteSurfaceSlot::kQueryWell:
            // The query field sits one calm step back inside the card, the same
            // relationship the rail address pill has with the tinted rail.
            return tokens.surface_secondary;
        case PaletteSurfaceSlot::kRow:
            return tokens.surface;
        case PaletteSurfaceSlot::kHighlightedRow:
            // Only the highlighted row is tinted; the accent stays reserved for
            // the active state, matching the rail's accent semantics.
            return tokens.surface_secondary;
        case PaletteSurfaceSlot::kBorder:
            return tokens.border;
    }
    return tokens.surface;
}

DipRect SearchPaletteBounds(const DipRect& window_bounds, int content_height) noexcept {
    const int width = std::min(SearchPaletteWidthDip(), window_bounds.width);
    const int height = std::max(0, std::min(content_height, window_bounds.height));
    const int x = window_bounds.x + std::max(0, (window_bounds.width - width) / 2);
    const int top_offset = std::min(SearchPaletteTopOffsetDip(),
                                    std::max(0, window_bounds.height - height));
    return {
        .x = x,
        .y = window_bounds.y + top_offset,
        .width = width,
        .height = height,
    };
}

std::string SearchProviderRowAccessibleName(const SearchProvider& provider,
                                            std::string_view query) {
    std::string name = "Search with ";
    name.append(provider.display_name);
    name.append(" for ");
    name.append(query);
    return name;
}

}  // namespace island
