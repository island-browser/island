#ifndef ISLAND_TAB_H_
#define ISLAND_TAB_H_

#include "navigation_state.h"
#include "tab_id.h"

namespace island {

// Owns one tab's browsing-surface state independently of any CEF object: a stable
// identity and the tab's NavigationState. U2 adds the CefBrowserView/CefBrowser
// seam to this type; this core stays CEF-free so it unit-tests without a runtime.
class Tab {
  public:
    explicit Tab(TabId id);

    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;
    Tab(Tab&&) noexcept = default;
    Tab& operator=(Tab&&) noexcept = default;
    ~Tab() = default;

    [[nodiscard]] TabId id() const noexcept;

    [[nodiscard]] NavigationState& navigation_state() noexcept;
    [[nodiscard]] const NavigationState& navigation_state() const noexcept;

  private:
    TabId id_;
    NavigationState navigation_state_;
};

}  // namespace island

#endif  // ISLAND_TAB_H_
