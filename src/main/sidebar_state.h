#ifndef ISLAND_SIDEBAR_STATE_H_
#define ISLAND_SIDEBAR_STATE_H_

#include <algorithm>

namespace island {

// The sidebar is expanded by default: the rail is the product's primary surface
// (tabs, address, spaces). Cmd/Ctrl+B collapses it to a narrow icon rail instead of
// hiding it, so the open tabs stay one click away.
inline constexpr bool kSidebarExpandedByDefault = true;

// Layout width of the collapsed icon rail: one 32 DIP icon button per tab with a
// 10 DIP gutter on each side. The rail never goes away, so there is no edge
// marker and no hover reveal (expanding under the pointer would swap the target
// under the cursor and reflow the page).
inline constexpr int kCollapsedRailWidthDip = 52;

// The CEF-free expand/collapse state machine. Two inputs fold into one `expanded()`
// flag: the persistent choice (Cmd/Ctrl+B, saved in the preferences) and a transient
// expansion that only lasts while the address field is being edited in the collapsed
// rail (Cmd/Ctrl+L), which is never persisted.
class SidebarState {
  public:
    SidebarState() = default;
    explicit SidebarState(bool expanded) : persistent_expanded_(expanded) {}

    // Cmd/Ctrl+B flips what is on screen: an expanded sidebar (persistent or
    // transient) collapses, a collapsed one expands. The result is the new persistent
    // choice and always ends any transient expansion.
    void Toggle();
    void SetExpanded(bool expanded);

    // Expands a collapsed sidebar for the duration of an address edit. Returns true
    // when it changed the layout (false when the sidebar was already expanded).
    bool BeginTransientExpand();
    // Returns true when a transient expansion ended (the layout changed).
    bool EndTransientExpand();

    // The persistent choice, as saved in the preferences.
    [[nodiscard]] bool persistent_expanded() const noexcept { return persistent_expanded_; }
    [[nodiscard]] bool transient_expanded() const noexcept { return transient_expanded_; }
    // What is on screen: the full sidebar (true) or the icon rail (false).
    [[nodiscard]] bool expanded() const noexcept {
        return persistent_expanded_ || transient_expanded_;
    }

    // The rail's layout width: the full width while expanded, the icon rail otherwise.
    [[nodiscard]] int RailWidthDip(int expanded_rail_width) const noexcept {
        return expanded() ? expanded_rail_width
                          : std::min(kCollapsedRailWidthDip, expanded_rail_width);
    }

  private:
    bool persistent_expanded_ = kSidebarExpandedByDefault;
    bool transient_expanded_ = false;
};

}  // namespace island

#endif  // ISLAND_SIDEBAR_STATE_H_
