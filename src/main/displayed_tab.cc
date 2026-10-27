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
    // A shown favorite does not depend on an ordinary selection and stays shown (alone).
    ordinary_focused_.reset();
    ordinary_split_.reset();
    favorite_pane_.reset();
}

void DisplayedTabModel::ActivateFavorite(FavoriteTabKey key, TabId virtual_tab) {
    // A favorite shows even when no ordinary tab is selected (it then fills the slot alone).
    favorite_ = std::move(key);
    favorite_tab_ = virtual_tab;
    favorite_pane_ = ordinary_split_.has_value() ? ordinary_focused_ : std::nullopt;
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

DisplayedShutdownPlan DisplayedTabModel::shutdown_plan() const {
    DisplayedShutdownPlan plan;
    plan.navigation_tab = ordinary_focused_;
    if (ordinary_focused_.has_value()) {
        plan.graceful_tabs.push_back(*ordinary_focused_);
    }
    if (favorite_tab_.has_value()) {
        plan.graceful_tabs.push_back(*favorite_tab_);
    }
    return plan;
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
    // A single pane keeps its view whatever slot it had (the attach does not remove it); in a
    // split the slot is part of the identity, because a pane that changes slot is removed and
    // re-added.
    const bool single_pane = desired.size() == 1;
    const auto remains = [&](const DisplayedSurface& attached) {
        return std::any_of(desired.begin(), desired.end(), [&](const DisplayedSurface& want) {
            return single_pane ? want.kind == attached.kind && want.id == attached.id
                               : want == attached;
        });
    };
    for (const DisplayedSurface& attached : attached_surfaces_) {
        if (remains(attached)) {
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
            // Created but of unknown age: treated like an uncreated browser, so the one-shot
            // fallback timer (not 100 ms polling) bounds the wait. allow_uncreated does not
            // release it, as before.
            decision.can_replace = false;
            decision.waiting_for_creation = true;
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
