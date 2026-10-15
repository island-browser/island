#include "sidebar_state.h"

namespace island {

void SidebarState::Toggle() { SetExpanded(!expanded()); }

void SidebarState::SetExpanded(bool expanded) {
    persistent_expanded_ = expanded;
    transient_expanded_ = false;
}

bool SidebarState::BeginTransientExpand() {
    if (expanded()) {
        return false;
    }
    transient_expanded_ = true;
    return true;
}

bool SidebarState::EndTransientExpand() {
    if (!transient_expanded_) {
        return false;
    }
    transient_expanded_ = false;
    return true;
}

}  // namespace island
