#ifndef ISLAND_SIDEBAR_STATE_H_
#define ISLAND_SIDEBAR_STATE_H_

#include <cstdint>

namespace island {

// The sidebar is hidden by default on every platform. Cmd/Ctrl+B is the
// keyboard path everywhere; hover-reveal is a macOS-only enhancement layered on
// top of it, so the hidden default is always escapable without a pointer.
inline constexpr bool kSidebarRevealedByDefault = false;

// Width of the always-visible edge marker while the rail is hidden. The spec
// allows 1-2 DIP; 2 matches the ActivePageIndicator accent thickness so the two
// chrome marks read as one system.
inline constexpr int kHoverSliverWidthDip = 2;

// Reveal when the cursor enters the leftmost 12-DIP band of the window.
inline constexpr int kHoverRevealBandDip = 12;

// Hide again once the cursor leaves the revealed rail by more than 16 DIP, so
// small overshoots past the rail edge do not flicker the sidebar.
inline constexpr int kHoverGraceBandDip = 16;

// The CEF-free reveal/hide state machine. Two independent inputs fold into one
// revealed flag with a fixed precedence: an explicit Cmd/Ctrl+B toggle pins the
// sidebar and always wins, and hover only ever applies to the unpinned hidden
// state.
class SidebarState {
  public:
    SidebarState() = default;
    explicit SidebarState(bool pinned) : pinned_(pinned) {}

    // Flips the explicit pin. Toggling off also clears any hover reveal, so one
    // Cmd/Ctrl+B always collapses a visible sidebar whatever revealed it.
    void Toggle();
    void SetPinned(bool pinned);

    // Feeds a pointer position, in window-relative DIP, to the hover rules.
    // `revealed_rail_width` is the rail's revealed width so the grace band is
    // measured from the rail's trailing edge. Idempotent: repeated moves inside
    // the same band do not change state.
    void OnPointerMoved(int x_dip, int revealed_rail_width);

    // The pointer left the window (or the seam was uninstalled). Any hover
    // reveal ends; a pinned reveal is untouched.
    void OnPointerLeft();

    [[nodiscard]] bool pinned() const noexcept { return pinned_; }
    [[nodiscard]] bool hover_revealed() const noexcept { return hover_revealed_; }
    [[nodiscard]] bool revealed() const noexcept { return pinned_ || hover_revealed_; }

    // The rail's layout width: 0 while hidden, the full revealed width otherwise.
    // The hidden rail stays in the view tree and occupies no layout width.
    [[nodiscard]] int RailWidthDip(int revealed_rail_width) const noexcept {
        return revealed() ? revealed_rail_width : 0;
    }

    // The edge sliver is visible exactly when the rail is not.
    [[nodiscard]] bool sliver_visible() const noexcept { return !revealed(); }

  private:
    bool pinned_ = kSidebarRevealedByDefault;
    bool hover_revealed_ = false;
};

// Installs the platform-native hover seam on `window_handle`, calling
// `on_pointer_x` with the pointer's window-relative x in DIP. Returns an opaque
// handle to pass to RemoveSidebarHoverSeam, or nullptr when the platform has no
// seam. Only macOS implements this; Windows and Linux are deliberately
// toggle-only, and their stub returns nullptr.
using SidebarHoverCallback = void (*)(void* context, int x_dip);
[[nodiscard]] void* InstallSidebarHoverSeam(void* window_handle, SidebarHoverCallback callback,
                                            void* context);
void RemoveSidebarHoverSeam(void* seam);

}  // namespace island

#endif  // ISLAND_SIDEBAR_STATE_H_
