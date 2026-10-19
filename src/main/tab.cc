#include "tab.h"

namespace island {

Tab::Tab(TabId id) : id_(id) {}

TabId Tab::id() const noexcept { return id_; }

void Tab::SetStartupUrl(std::string url) { startup_url_ = std::move(url); }

const std::string& Tab::startup_url() const noexcept { return startup_url_; }

namespace {

// The host of a web address ("developer.example.com" for "https://developer.example.com/a?b"),
// or the address itself when it has none.
std::string HostOf(const std::string& url) {
    std::string rest = url;
    const std::size_t scheme_end = rest.find("://");
    if (scheme_end != std::string::npos) {
        rest = rest.substr(scheme_end + 3);
    }
    rest = rest.substr(0, rest.find_first_of("/?#"));
    const std::size_t at = rest.rfind('@');
    if (at != std::string::npos) {
        rest = rest.substr(at + 1);
    }
    return rest.empty() ? url : rest;
}

}  // namespace

std::string Tab::DisplayTitle() const {
    const std::string& page_title = navigation_state_.snapshot().page_title;
    if (!page_title.empty()) {
        return page_title;
    }
    if (browser_view_ == nullptr && startup_url_.rfind("http", 0) == 0) {
        // Before the page loads its host names it; the full address is in the agent's url field.
        return HostOf(startup_url_);
    }
    return "Island";
}

void Tab::SetGroupParent(std::optional<TabId> parent) { group_parent_ = parent; }

std::optional<TabId> Tab::group_parent() const noexcept { return group_parent_; }

void Tab::SetGroupLabel(std::string label) { group_label_ = std::move(label); }

const std::string& Tab::group_label() const noexcept { return group_label_; }

NavigationState& Tab::navigation_state() noexcept { return navigation_state_; }

const NavigationState& Tab::navigation_state() const noexcept { return navigation_state_; }

void Tab::SetBrowserView(CefRefPtr<CefBrowserView> browser_view) {
    browser_view_ = browser_view;
    browser_created_at_.reset();
}

void Tab::SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

void Tab::MarkBrowserCreated(BrowserCreationTime created_at) noexcept {
    if (!browser_created_at_.has_value()) {
        browser_created_at_ = created_at;
    }
}

std::optional<std::chrono::steady_clock::duration> Tab::BrowserViewAgeAt(
    BrowserCreationTime now) const noexcept {
    if (!browser_created_at_.has_value() || now < *browser_created_at_) {
        return std::nullopt;
    }
    return now - *browser_created_at_;
}

bool Tab::BrowserViewIsOldEnough(BrowserCreationTime now,
                                 std::chrono::steady_clock::duration minimum_age) const noexcept {
    const std::optional<std::chrono::steady_clock::duration> age = BrowserViewAgeAt(now);
    return age.has_value() && *age >= minimum_age;
}

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
