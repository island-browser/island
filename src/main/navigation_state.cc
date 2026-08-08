#include "navigation_state.h"

#include <utility>

namespace island {

const NavigationSnapshot& NavigationState::snapshot() const noexcept { return snapshot_; }

void NavigationState::SetObserver(NavigationObserver* observer) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    observer_ = observer;
    if (observer_ != nullptr) {
        observer_->OnNavigationChanged(snapshot_);
    }
}

void NavigationState::SetAddress(std::string url) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.url = std::move(url);
    PublishIfChanged(previous);
}

void NavigationState::OnLoadStart(std::string url) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.url = std::move(url);
    snapshot_.page_title.clear();
    snapshot_.display_title = "Island";
    snapshot_.load_phase = LoadPhase::kLoading;
    snapshot_.http_status.reset();
    snapshot_.network_error.reset();
    PublishIfChanged(previous);
}

void NavigationState::OnTitleChange(std::string page_title) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.page_title = std::move(page_title);
    snapshot_.display_title =
        snapshot_.page_title.empty() ? "Island" : snapshot_.page_title + " — Island";
    PublishIfChanged(previous);
}

void NavigationState::OnLoadEnd(int http_status) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.load_phase = LoadPhase::kCompleted;
    snapshot_.http_status = http_status;
    snapshot_.network_error.reset();
    PublishIfChanged(previous);
}

void NavigationState::OnLoadError(int network_error) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.load_phase = LoadPhase::kFailed;
    snapshot_.http_status.reset();
    snapshot_.network_error = network_error;
    PublishIfChanged(previous);
}

void NavigationState::OnLoadingStateChange(bool can_go_back, bool can_go_forward) {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.can_go_back = can_go_back;
    snapshot_.can_go_forward = can_go_forward;
    PublishIfChanged(previous);
}

void NavigationState::Close() {
    if (snapshot_.load_phase == LoadPhase::kClosed) {
        return;
    }

    const NavigationSnapshot previous = snapshot_;
    snapshot_.load_phase = LoadPhase::kClosed;
    PublishIfChanged(previous);
    observer_ = nullptr;
}

void NavigationState::PublishIfChanged(const NavigationSnapshot& previous) {
    if (snapshot_ == previous) {
        return;
    }

    ++snapshot_.revision;
    if (observer_ != nullptr) {
        observer_->OnNavigationChanged(snapshot_);
    }
}

}  // namespace island
