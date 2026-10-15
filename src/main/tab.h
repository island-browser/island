#ifndef ISLAND_TAB_H_
#define ISLAND_TAB_H_

#include <string>
#include <utility>

#include "include/cef_browser.h"
#include "include/cef_image.h"
#include "include/views/cef_browser_view.h"
#include "navigation_state.h"
#include "tab_id.h"

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

    // The URL this tab's CefBrowserView loads when it is created. Empty means
    // the window's fixed startup page; session restore sets a persisted URL
    // here after it passes the address validation path.
    void SetStartupUrl(std::string url);
    [[nodiscard]] const std::string& startup_url() const noexcept;

    // Arc-style pinned tabs sit in their own group at the top of the sidebar
    // and persist with the session. Ordering is the Space's job (SetTabPinned).
    void SetPinned(bool pinned) noexcept { pinned_ = pinned; }
    [[nodiscard]] bool pinned() const noexcept { return pinned_; }

    // The page's downloaded favicon (null until one arrives).
    void SetFavicon(CefRefPtr<CefImage> favicon) { favicon_ = std::move(favicon); }
    [[nodiscard]] CefRefPtr<CefImage> favicon() const noexcept { return favicon_; }

    // Favicon download bookkeeping (rules in favicon_policy.h). BeginFaviconRequest records
    // the icon URL being fetched for the tab's current page; CompleteFaviconRequest accepts a
    // finished download only while that URL is still the pending one, so a slow response for
    // a page the tab has since left never replaces the newer icon. A successful completion
    // stores `image`; a null or empty one is ignored (the previous icon stays). ClearFavicon
    // drops both the icon and any pending request, restoring the globe.
    void BeginFaviconRequest(std::string icon_url) { pending_favicon_url_ = std::move(icon_url); }
    bool CompleteFaviconRequest(const std::string& icon_url, CefRefPtr<CefImage> image);
    void ClearFavicon() {
        favicon_ = nullptr;
        pending_favicon_url_.clear();
    }
    [[nodiscard]] const std::string& pending_favicon_url() const noexcept {
        return pending_favicon_url_;
    }

    // U2: Caller-built CEF browser seam. The tab's CefBrowserView is created and owned
    // externally (by BrowserWindow); the tab holds raw CefRefPtr references to the view and
    // its underlying CefBrowser. Callers SetBrowserView after BrowserWindow creates the view;
    // SetBrowser after OnBrowserCreated; both are null while the tab is not attached.
    void SetBrowserView(CefRefPtr<CefBrowserView> browser_view);
    void SetBrowser(CefRefPtr<CefBrowser> browser);
    [[nodiscard]] CefRefPtr<CefBrowserView> browser_view() const noexcept;
    [[nodiscard]] CefRefPtr<CefBrowser> browser() const noexcept;
    [[nodiscard]] bool has_browser() const noexcept;

    // Tab sleep: a sleeping tab has released its CefBrowser (and view) but keeps its model state
    // (URL via startup_url/navigation snapshot, title, favicon). The scroll offset read from the
    // page just before it closed is replayed once the woken page finishes loading.
    void SetSleeping(bool sleeping) noexcept { sleeping_ = sleeping; }
    [[nodiscard]] bool sleeping() const noexcept { return sleeping_; }
    void SetSavedScroll(double x, double y) noexcept {
        saved_scroll_x_ = x;
        saved_scroll_y_ = y;
        has_saved_scroll_ = x != 0.0 || y != 0.0;
    }
    void ClearSavedScroll() noexcept { SetSavedScroll(0.0, 0.0); }
    [[nodiscard]] bool has_saved_scroll() const noexcept { return has_saved_scroll_; }
    [[nodiscard]] double saved_scroll_x() const noexcept { return saved_scroll_x_; }
    [[nodiscard]] double saved_scroll_y() const noexcept { return saved_scroll_y_; }

  private:
    TabId id_;
    bool sleeping_ = false;
    bool has_saved_scroll_ = false;
    double saved_scroll_x_ = 0.0;
    double saved_scroll_y_ = 0.0;
    std::string startup_url_;
    bool pinned_ = false;
    NavigationState navigation_state_;

    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<CefImage> favicon_;
    std::string pending_favicon_url_;
};

}  // namespace island

#endif  // ISLAND_TAB_H_
