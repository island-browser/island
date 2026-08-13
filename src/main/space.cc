#include "space.h"

#include <algorithm>

#include "include/cef_request_context_handler.h"

namespace island {

Space::Space(SpaceId id, std::string name, SpaceColor color)
    : id_(id), name_(std::move(name)), color_(color) {}

SpaceId Space::id() const noexcept { return id_; }

const std::string& Space::name() const noexcept { return name_; }

SpaceColor Space::color() const noexcept { return color_; }

void Space::Rename(std::string name) { name_ = std::move(name); }

void Space::SetColor(SpaceColor color) { color_ = color; }

const std::vector<Tab>& Space::tabs() const noexcept { return tabs_; }

std::size_t Space::tab_count() const noexcept { return tabs_.size(); }

void Space::AppendTab(Tab tab) {
    tabs_.push_back(std::move(tab));
    active_tab_index_ = tabs_.size() - 1;
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
    return true;
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

void Space::CreateRequestContextIfNeeded(const std::string& cache_path) {
    if (request_context_) {
        return;
    }
    if (cache_path.empty()) {
        request_context_ = CefRequestContext::CreateContext(
            CefRequestContext::GetGlobalContext(), CefRefPtr<CefRequestContextHandler>());
    } else {
        CefRequestContextSettings settings;
        CefString(&settings.cache_path) = cache_path;
        request_context_ = CefRequestContext::CreateContext(settings, nullptr);
    }
}

CefRefPtr<CefRequestContext> Space::request_context() const noexcept {
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
