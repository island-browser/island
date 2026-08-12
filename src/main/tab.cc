#include "tab.h"

namespace island {

Tab::Tab(TabId id) : id_(id) {}

TabId Tab::id() const noexcept { return id_; }

NavigationState& Tab::navigation_state() noexcept { return navigation_state_; }

const NavigationState& Tab::navigation_state() const noexcept { return navigation_state_; }

void Tab::SetBrowserView(CefRefPtr<CefBrowserView> browser_view) {
    browser_view_ = browser_view;
}

void Tab::SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

CefRefPtr<CefBrowserView> Tab::browser_view() const noexcept { return browser_view_; }

CefRefPtr<CefBrowser> Tab::browser() const noexcept { return browser_; }

bool Tab::has_browser() const noexcept { return browser_ != nullptr; }

}  // namespace island
