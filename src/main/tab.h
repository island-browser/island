#ifndef ISLAND_TAB_H_
#define ISLAND_TAB_H_

#include "navigation_state.h"
#include "tab_id.h"

#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"

class CefBrowserView;
class CefBrowser;
class CefClient;

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

    // U2: Caller-built CEF browser seam. The tab's CefBrowserView is created and owned
    // externally (by BrowserWindow); the tab holds raw CefRefPtr references to the view and
    // its underlying CefBrowser. Callers SetBrowserView after BrowserWindow creates the view;
    // SetBrowser after OnBrowserCreated; both are null while the tab is not attached.
    void SetBrowserView(CefRefPtr<CefBrowserView> browser_view);
    void SetBrowser(CefRefPtr<CefBrowser> browser);
    [[nodiscard]] CefRefPtr<CefBrowserView> browser_view() const noexcept;
    [[nodiscard]] CefRefPtr<CefBrowser> browser() const noexcept;
    [[nodiscard]] bool has_browser() const noexcept;

  private:
    TabId id_;
    NavigationState navigation_state_;

    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefBrowser> browser_;
};

}  // namespace island

#endif  // ISLAND_TAB_H_
