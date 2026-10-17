#include "space.h"

#include <algorithm>

#include "include/cef_request_context_handler.h"
#include "space_icon.h"

namespace island {

Space::Space(SpaceId id, std::string name, SpaceColor color, std::string icon_id)
    : id_(id), name_(std::move(name)), color_(color), icon_id_(NormalizeSpaceIconId(icon_id)) {}

SpaceId Space::id() const noexcept { return id_; }

const std::string& Space::name() const noexcept { return name_; }

SpaceColor Space::color() const noexcept { return color_; }

const std::string& Space::icon_id() const noexcept { return icon_id_; }

void Space::Rename(std::string name) { name_ = std::move(name); }

void Space::SetColor(SpaceColor color) { color_ = color; }

void Space::SetIconId(std::string icon_id) { icon_id_ = NormalizeSpaceIconId(icon_id); }

const std::vector<Tab>& Space::tabs() const noexcept { return tabs_; }

std::vector<Tab>& Space::tabs() noexcept { return tabs_; }

std::size_t Space::tab_count() const noexcept { return tabs_.size(); }

void Space::AppendTab(Tab tab) {
    tabs_.push_back(std::move(tab));
    active_tab_index_ = tabs_.size() - 1;
}

TabId Space::AppendChildTab(TabId parent, Tab tab, bool activate) {
    const TabId id = tab.id();
    const std::optional<std::size_t> parent_index = IndexOfTab(parent);
    if (!parent_index.has_value()) {
        AppendTab(std::move(tab));
        return id;
    }
    tab.SetGroupParent(parent);
    std::size_t insert_at = *parent_index + 1;
    for (std::size_t i = insert_at; i < tabs_.size(); ++i) {
        if (tabs_[i].group_parent() == parent) {
            insert_at = i + 1;
        }
    }
    // A child is never pinned, so it stays out of the pinned group at the front.
    insert_at = std::max(insert_at, pinned_count());
    const bool had_active = has_active_tab();
    const std::optional<TabId> previous_active =
        had_active ? std::optional<TabId>(active_tab_id()) : std::nullopt;
    tabs_.insert(tabs_.begin() + static_cast<std::vector<Tab>::difference_type>(insert_at),
                 std::move(tab));
    if (activate || !had_active) {
        active_tab_index_ = insert_at;
    } else if (const std::optional<std::size_t> kept = IndexOfTab(*previous_active)) {
        active_tab_index_ = *kept;
    }
    return id;
}

void Space::NormalizeGroups(std::optional<TabId> moved) {
    const std::size_t pinned = pinned_count();
    // A child is dropped when it cannot be one: its parent is gone, is itself a child, or it is
    // pinned. A tab the person moved by hand also leaves its group when it no longer follows
    // the parent or a sibling.
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const std::optional<TabId> parent = tabs_[i].group_parent();
        if (!parent.has_value()) {
            continue;
        }
        const std::optional<std::size_t> parent_index = IndexOfTab(*parent);
        bool keep = parent_index.has_value() && !tabs_[i].pinned() && *parent_index != i &&
                    !tabs_[*parent_index].group_parent().has_value();
        if (keep && moved == tabs_[i].id()) {
            const bool after_run =
                i > 0 && (tabs_[i - 1].id() == *parent || tabs_[i - 1].group_parent() == parent);
            const bool starts_unpinned = tabs_[*parent_index].pinned() && i == pinned;
            keep = after_run || starts_unpinned;
        }
        if (!keep) {
            tabs_[i].SetGroupParent(std::nullopt);
        }
    }

    // The other children are re-placed: right after their parent, or after the pinned group
    // when the parent is pinned. Nothing moves when the run is already in place.
    std::vector<std::size_t> order;
    order.reserve(tabs_.size());
    const auto push_children = [this, &order](std::size_t parent_index) {
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].group_parent() == tabs_[parent_index].id()) {
                order.push_back(i);
            }
        }
    };
    const std::size_t pinned_end = std::min(pinned, tabs_.size());
    for (std::size_t i = 0; i < pinned_end; ++i) {
        order.push_back(i);
    }
    for (std::size_t i = 0; i < pinned_end; ++i) {
        push_children(i);
    }
    for (std::size_t i = pinned_end; i < tabs_.size(); ++i) {
        if (!tabs_[i].group_parent().has_value()) {
            order.push_back(i);
            push_children(i);
        }
    }
    if (order.size() != tabs_.size() || std::is_sorted(order.begin(), order.end())) {
        return;
    }
    const std::optional<TabId> active =
        has_active_tab() ? std::optional<TabId>(active_tab_id()) : std::nullopt;
    std::vector<Tab> ordered;
    ordered.reserve(tabs_.size());
    for (const std::size_t index : order) {
        ordered.push_back(std::move(tabs_[index]));
    }
    tabs_ = std::move(ordered);
    if (active.has_value()) {
        if (const std::optional<std::size_t> index = IndexOfTab(*active)) {
            active_tab_index_ = *index;
        }
    }
}

Tab* Space::FindTab(TabId id) {
    for (Tab& tab : tabs_) {
        if (tab.id() == id) {
            return &tab;
        }
    }
    return nullptr;
}

const Tab* Space::FindTab(TabId id) const {
    for (const Tab& tab : tabs_) {
        if (tab.id() == id) {
            return &tab;
        }
    }
    return nullptr;
}

std::optional<std::size_t> Space::IndexOfTab(TabId id) const {
    for (std::size_t index = 0; index < tabs_.size(); ++index) {
        if (tabs_[index].id() == id) {
            return index;
        }
    }
    return std::nullopt;
}

bool Space::has_active_tab() const noexcept { return !tabs_.empty(); }

std::size_t Space::active_tab_index() const noexcept { return active_tab_index_; }

TabId Space::active_tab_id() const {
    return has_active_tab() ? tabs_[active_tab_index_].id() : TabId{};
}

bool Space::SelectTab(TabId id) {
    const std::optional<std::size_t> index = IndexOfTab(id);
    if (!index.has_value()) {
        return false;
    }
    active_tab_index_ = *index;
    return true;
}

bool Space::SelectTabIndex(std::size_t index) {
    if (index >= tabs_.size()) {
        return false;
    }
    active_tab_index_ = index;
    return true;
}

void Space::SelectNextTab() {
    if (tabs_.size() < 2) {
        return;
    }
    active_tab_index_ = (active_tab_index_ + 1) % tabs_.size();
}

void Space::SelectPreviousTab() {
    if (tabs_.size() < 2) {
        return;
    }
    active_tab_index_ = active_tab_index_ == 0 ? tabs_.size() - 1 : active_tab_index_ - 1;
}

std::optional<Tab> Space::RemoveTab(TabId id) {
    const std::optional<std::size_t> index = IndexOfTab(id);
    if (!index.has_value()) {
        return std::nullopt;
    }

    if (split_.has_value() && (split_->first == id || split_->second == id)) {
        split_.reset();
    }

    for (Tab& tab : tabs_) {
        if (tab.group_parent() == id) {
            tab.SetGroupParent(std::nullopt);
        }
    }

    const std::size_t removed_index = *index;
    Tab removed = std::move(tabs_[removed_index]);
    tabs_.erase(tabs_.begin() + static_cast<std::vector<Tab>::difference_type>(removed_index));
    MaintainActiveIndexAfterRemoval(removed_index);
    return removed;
}

bool Space::MoveTab(std::size_t from_index, std::size_t to_index) {
    if (from_index >= tabs_.size() || to_index >= tabs_.size()) {
        return false;
    }

    const TabId active_id = active_tab_id();
    const TabId moved_id = tabs_[from_index].id();
    Tab moved = std::move(tabs_[from_index]);
    tabs_.erase(tabs_.begin() + static_cast<std::vector<Tab>::difference_type>(from_index));
    tabs_.insert(tabs_.begin() + static_cast<std::vector<Tab>::difference_type>(to_index),
                 std::move(moved));

    if (has_active_tab()) {
        const std::optional<std::size_t> new_active_index = IndexOfTab(active_id);
        if (new_active_index.has_value()) {
            active_tab_index_ = *new_active_index;
        }
    }
    NormalizeGroups(moved_id);
    const std::optional<std::size_t> moved_index = IndexOfTab(moved_id);
    return moved_index.has_value() && *moved_index != from_index;
}

bool Space::SetTabPinned(TabId id, bool pinned) {
    const std::optional<std::size_t> index = IndexOfTab(id);
    if (!index.has_value()) {
        return false;
    }
    if (tabs_[*index].pinned() == pinned) {
        return true;
    }
    tabs_[*index].SetPinned(pinned);
    if (pinned) {
        tabs_[*index].SetGroupParent(std::nullopt);
    }
    // pinned_count() now counts the toggled tab when pinning, so the pinned
    // group's last slot is count - 1; when unpinning, the first regular slot
    // (just after the remaining pinned tabs) is count.
    const std::size_t count = pinned_count();
    const std::size_t target = pinned ? count - 1 : count;
    static_cast<void>(MoveTab(*index, target));
    return true;
}

std::size_t Space::pinned_count() const noexcept {
    std::size_t count = 0;
    for (const Tab& tab : tabs_) {
        if (tab.pinned()) {
            ++count;
        }
    }
    return count;
}

const std::optional<SplitPairing>& Space::split() const noexcept { return split_; }

bool Space::SetSplit(SplitPairing pairing) {
    if (pairing.first == pairing.second) {
        return false;
    }
    if (!IndexOfTab(pairing.first).has_value() || !IndexOfTab(pairing.second).has_value()) {
        return false;
    }
    split_ = pairing;
    return true;
}

void Space::ClearSplit() { split_.reset(); }

void Space::CreateRequestContextIfNeeded(const std::string& cache_path) const {
    if (request_context_) {
        return;
    }
    CefRequestContextSettings settings;
    if (!cache_path.empty()) {
        CefString(&settings.cache_path) = cache_path;
    }
    // Always create a fresh per-space context. Using the global CEF request
    // context (via CreateContext(GetGlobalContext(), ...)) segfaults when CEF
    // is not yet initialized (e.g. in tests) because it dereferences a null
    // global context reference.
    request_context_ = CefRequestContext::CreateContext(settings, nullptr);
}

CefRefPtr<CefRequestContext> Space::request_context() const noexcept {
    CreateRequestContextIfNeeded("");
    return request_context_;
}

void Space::MaintainActiveIndexAfterRemoval(std::size_t removed_index) {
    if (tabs_.empty()) {
        active_tab_index_ = 0;
        return;
    }
    if (removed_index < active_tab_index_) {
        --active_tab_index_;
    } else if (removed_index == active_tab_index_) {
        active_tab_index_ = std::min(removed_index, tabs_.size() - 1);
    }
}

}  // namespace island
