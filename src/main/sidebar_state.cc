#include "sidebar_state.h"

namespace island {

void SidebarState::Toggle() { SetPinned(!pinned_); }

void SidebarState::SetPinned(bool pinned) {
    pinned_ = pinned;
    if (!pinned_) {
        // An explicit collapse wins over a live hover reveal; the pointer has to
        // re-enter the edge band to reveal again.
        hover_revealed_ = false;
    }
}

void SidebarState::OnPointerMoved(int x_dip, int revealed_rail_width) {
    if (pinned_) {
        // Hover never overrides an explicit toggle.
        return;
    }
    if (!hover_revealed_) {
        if (x_dip >= 0 && x_dip < kHoverRevealBandDip) {
            hover_revealed_ = true;
        }
        return;
    }
    if (x_dip > revealed_rail_width + kHoverGraceBandDip) {
        hover_revealed_ = false;
    }
}

void SidebarState::OnPointerLeft() { hover_revealed_ = false; }

}  // namespace island
