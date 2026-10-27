#include "displayed_tab.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace island {

DisplayedSurface DisplayedSurface::Tab(TabId tab, bool second_slot) {
    return {.kind = DisplayedSurfaceKind::kTab, .id = tab.value, .second_slot = second_slot};
}

DisplayedSurface DisplayedSurface::LocalPage(std::uint64_t page, bool second_slot) {
    return {.kind = DisplayedSurfaceKind::kLocalPage, .id = page, .second_slot = second_slot};
}

void DisplayedTabModel::ActivateOrdinary(TabId focused, std::optional<DisplayedSplit> split) {
    static_cast<void>(DeactivateFavorite());
    UpdateOrdinaryLayout(focused, std::move(split));
}

void DisplayedTabModel::UpdateOrdinaryLayout(TabId focused, std::optional<DisplayedSplit> split) {
    ordinary_focused_ = focused;
    ordinary_split_ = std::move(split);
}

void DisplayedTabModel::ClearOrdinary() {
    ordinary_focused_.reset();
    ordinary_split_.reset();
    static_cast<void>(DeactivateFavorite());
}

bool DisplayedTabModel::ActivateFavorite(FavoriteTabKey key, TabId virtual_tab) {
    if (!ordinary_focused_.has_value()) {
        return false;
    }
    favorite_ = std::move(key);
    favorite_tab_ = virtual_tab;
    favorite_pane_ = ordinary_split_.has_value() ? ordinary_focused_ : std::nullopt;
    return true;
}

bool DisplayedTabModel::DeactivateFavorite() {
    const bool had_favorite = favorite_.has_value();
    favorite_.reset();
    favorite_tab_.reset();
    favorite_pane_.reset();
    return had_favorite;
}

std::optional<TabId> DisplayedTabModel::focused_tab() const noexcept {
    return favorite_tab_.has_value() ? favorite_tab_ : ordinary_focused_;
}

std::optional<DisplayedPanes> DisplayedTabModel::panes() const noexcept {
    const std::optional<TabId> focused = focused_tab();
    if (!focused.has_value()) {
        return std::nullopt;
    }
    if (!ordinary_split_.has_value()) {
        return DisplayedPanes{.first = *focused};
    }
    if (!favorite_.has_value()) {
        return DisplayedPanes{.first = ordinary_split_->first, .second = ordinary_split_->second};
    }
    if (favorite_pane_ == ordinary_split_->first) {
        return DisplayedPanes{.first = *focused, .second = ordinary_split_->second};
    }
    if (favorite_pane_ == ordinary_split_->second) {
        return DisplayedPanes{.first = ordinary_split_->first, .second = *focused};
    }
    return DisplayedPanes{.first = *focused};
}

bool DisplayedTabModel::IsShown(TabId tab) const noexcept {
    const std::optional<DisplayedPanes> shown = panes();
    return shown.has_value() &&
           (shown->first == tab || (shown->second.has_value() && *shown->second == tab));
}

std::uint64_t DisplayedTabModel::DidAttach(std::vector<DisplayedSurface> surfaces) {
    attached_surfaces_ = std::move(surfaces);
    return ++attachment_generation_;
}

AttachDecision DisplayedTabModel::EvaluateReplacement(
    const std::vector<AttachedSurfaceState>& states, const std::vector<DisplayedSurface>& desired,
    Clock::time_point now, bool allow_uncreated) const {
    AttachDecision decision;
    for (const DisplayedSurface& attached : attached_surfaces_) {
        if (std::find(desired.begin(), desired.end(), attached) != desired.end()) {
            continue;
        }
        const auto state = std::find_if(states.begin(), states.end(), [&](const auto& candidate) {
            return candidate.surface == attached;
        });
        if (state == states.end() || !state->live) {
            continue;
        }
        if (!state->browser_created) {
            if (!allow_uncreated) {
                decision.can_replace = false;
                decision.waiting_for_creation = true;
            }
            continue;
        }
        if (!state->browser_created_at.has_value()) {
            decision.can_replace = false;
            decision.retry_after = std::max(decision.retry_after, kMinimumAttachedViewAge);
            continue;
        }
        const Clock::duration age = now >= *state->browser_created_at
                                        ? now - *state->browser_created_at
                                        : Clock::duration{};
        if (age >= kMinimumAttachedViewAge) {
            continue;
        }
        decision.can_replace = false;
        const auto wait =
            std::chrono::ceil<std::chrono::milliseconds>(kMinimumAttachedViewAge - age);
        decision.retry_after = std::max(decision.retry_after, wait);
    }
    return decision;
}

}  // namespace island
