#include "tab.h"

namespace island {

Tab::Tab(TabId id) : id_(id) {}

TabId Tab::id() const noexcept { return id_; }

void Tab::SetStartupUrl(std::string url) { startup_url_ = std::move(url); }

const std::string& Tab::startup_url() const noexcept { return startup_url_; }

void Tab::SetGroupParent(std::optional<TabId> parent) { group_parent_ = parent; }

std::optional<TabId> Tab::group_parent() const noexcept { return group_parent_; }

void Tab::SetGroupLabel(std::string label) { group_label_ = std::move(label); }

const std::string& Tab::group_label() const noexcept { return group_label_; }

NavigationState& Tab::navigation_state() noexcept { return navigation_state_; }

const NavigationState& Tab::navigation_state() const noexcept { return navigation_state_; }

void Tab::SetBrowserView(CefRefPtr<CefBrowserView> browser_view) { browser_view_ = browser_view; }

void Tab::SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

CefRefPtr<CefBrowserView> Tab::browser_view() const noexcept { return browser_view_; }

CefRefPtr<CefBrowser> Tab::browser() const noexcept { return browser_; }

bool Tab::has_browser() const noexcept { return browser_ != nullptr; }

bool Tab::CompleteFaviconRequest(const std::string& icon_url, CefRefPtr<CefImage> image) {
    if (pending_favicon_url_.empty() || icon_url != pending_favicon_url_) {
        return false;
    }
    pending_favicon_url_.clear();
    if (image != nullptr && !image->IsEmpty()) {
        favicon_ = std::move(image);
    }
    return true;
}

}  // namespace island
