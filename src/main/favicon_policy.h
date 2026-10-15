#ifndef ISLAND_FAVICON_POLICY_H_
#define ISLAND_FAVICON_POLICY_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace island {

// CEF-free rules for the per-tab favicon, shared by the expanded tab rows and the
// collapsed icon rail. BrowserWindow asks these before it downloads an icon, when a
// download completes, and when a tab navigates, so a stale or unwanted icon never
// reaches the chrome.

// Size, in DIP, of the favicon download. The rail draws favicons at 16 DIP, so asking
// for more would hand back 32 DIP images that overflow their slot; CEF still returns the
// 2x variant of a 16 DIP icon when the page ships one.
inline constexpr unsigned int kFaviconDownloadDip = 16;

namespace favicon_policy_detail {

inline char AsciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (AsciiLower(text[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace favicon_policy_detail

// A page whose icon may be fetched: a real web page (http or https). Local data: pages
// (the start page, Settings, All tabs, the agent panel), file:, about: and every other
// scheme never trigger a request.
[[nodiscard]] inline bool IsFaviconEligiblePage(std::string_view page_url) {
    return favicon_policy_detail::StartsWithIgnoreCase(page_url, "http://") ||
           favicon_policy_detail::StartsWithIgnoreCase(page_url, "https://");
}

// An icon location worth downloading for an eligible page: http(s), or an inline
// data:image (no network involved). file:, javascript:, chrome-extension: and the rest
// are ignored.
[[nodiscard]] inline bool IsFaviconEligibleIconUrl(std::string_view icon_url) {
    return favicon_policy_detail::StartsWithIgnoreCase(icon_url, "http://") ||
           favicon_policy_detail::StartsWithIgnoreCase(icon_url, "https://") ||
           favicon_policy_detail::StartsWithIgnoreCase(icon_url, "data:image/");
}

[[nodiscard]] inline bool ShouldFetchFavicon(std::string_view page_url, std::string_view icon_url) {
    return IsFaviconEligiblePage(page_url) && IsFaviconEligibleIconUrl(icon_url);
}

// "scheme://host:port" of an absolute URL, lower-cased; "" when there is none. The
// userinfo, path, query and fragment are dropped.
[[nodiscard]] inline std::string FaviconOrigin(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return {};
    }
    std::size_t authority_end = url.find_first_of("/?#", scheme_end + 3);
    if (authority_end == std::string_view::npos) {
        authority_end = url.size();
    }
    std::string_view authority = url.substr(scheme_end + 3, authority_end - (scheme_end + 3));
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    std::string origin;
    origin.reserve(scheme_end + 3 + authority.size());
    for (const char c : url.substr(0, scheme_end + 3)) {
        origin.push_back(favicon_policy_detail::AsciiLower(c));
    }
    for (const char c : authority) {
        origin.push_back(favicon_policy_detail::AsciiLower(c));
    }
    return origin;
}

// Whether a tab that committed `new_url` must drop the icon it showed for `old_url`:
// always when the new page is not an eligible web page (the globe returns), and when
// the origin changes. Same-origin navigations (paths, queries, single-page apps) keep
// the icon until the page declares another one.
[[nodiscard]] inline bool ShouldClearFaviconOnNavigation(std::string_view old_url,
                                                         std::string_view new_url) {
    if (!IsFaviconEligiblePage(new_url)) {
        return true;
    }
    return FaviconOrigin(old_url) != FaviconOrigin(new_url);
}

}  // namespace island

#endif  // ISLAND_FAVICON_POLICY_H_
