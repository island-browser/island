#include "tab.h"

namespace island {

Tab::Tab(TabId id) : id_(id) {}

TabId Tab::id() const noexcept { return id_; }

NavigationState& Tab::navigation_state() noexcept { return navigation_state_; }

const NavigationState& Tab::navigation_state() const noexcept { return navigation_state_; }

}  // namespace island
