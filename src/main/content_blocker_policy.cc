#include "content_blocker_policy.h"

#include <algorithm>
#include <cctype>

namespace island {

ContentBlockerCachePaths ContentBlockerCacheForProfile(
    const std::filesystem::path& profile_directory) {
    const std::filesystem::path directory = profile_directory / "content-blocker";
    return {directory, directory / "easylist.txt", directory / "easyprivacy.txt"};
}

bool ContentBlockerRefreshDue(std::optional<std::chrono::system_clock::time_point> last_updated,
                              std::chrono::system_clock::time_point now) {
    return !last_updated.has_value() || now < *last_updated ||
           now - *last_updated >= kContentBlockerRefreshInterval;
}

ContentBlockerStartupPolicy DecideContentBlockerStartupPolicy(
    bool cache_is_valid, std::optional<std::chrono::system_clock::time_point> last_updated,
    std::chrono::system_clock::time_point now) {
    return {
        .activate_cache = cache_is_valid,
        .refresh = !cache_is_valid || ContentBlockerRefreshDue(last_updated, now),
    };
}

std::string_view ContentBlockerRequestType(cef_resource_type_t resource_type) {
    switch (resource_type) {
        case RT_MAIN_FRAME:
        case RT_NAVIGATION_PRELOAD_MAIN_FRAME:
            return "document";
        case RT_SUB_FRAME:
        case RT_NAVIGATION_PRELOAD_SUB_FRAME:
            return "subdocument";
        case RT_STYLESHEET:
            return "stylesheet";
        case RT_SCRIPT:
        case RT_WORKER:
        case RT_SHARED_WORKER:
        case RT_SERVICE_WORKER:
            return "script";
        case RT_IMAGE:
        case RT_FAVICON:
            return "image";
        case RT_FONT_RESOURCE:
            return "font";
        case RT_OBJECT:
        case RT_PLUGIN_RESOURCE:
            return "object";
        case RT_MEDIA:
            return "media";
        case RT_XHR:
            return "xmlhttprequest";
        case RT_PING:
            return "ping";
        case RT_CSP_REPORT:
            return "csp";
        case RT_SUB_RESOURCE:
        case RT_PREFETCH:
        case RT_NUM_VALUES:
            return "other";
    }
    return "other";
}

bool ShouldBypassContentBlocker(std::string_view url, cef_resource_type_t resource_type) {
    if (resource_type == RT_MAIN_FRAME || resource_type == RT_NAVIGATION_PRELOAD_MAIN_FRAME) {
        return true;
    }
    return url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0 &&
           url.rfind("wss://", 0) != 0 && url.rfind("ws://", 0) != 0;
}

std::string SiteHostFromUrl(std::string_view url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return {};
    }
    std::string_view authority = url.substr(scheme + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    const std::size_t user = authority.rfind('@');
    if (user != std::string_view::npos) {
        authority.remove_prefix(user + 1);
    }
    if (authority.empty()) {
        return {};
    }
    std::string_view host = authority;
    if (authority.front() == '[') {
        const std::size_t end = authority.find(']');
        if (end == std::string_view::npos) {
            return {};
        }
        host = authority.substr(1, end - 1);
    } else if (const std::size_t port = authority.find(':'); port != std::string_view::npos) {
        host = authority.substr(0, port);
    }
    std::string result(host);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    while (!result.empty() && result.back() == '.') {
        result.pop_back();
    }
    return result;
}

std::string NormalizeAllowlistSite(std::string_view value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    value.remove_prefix(first);
    value = value.substr(0, value.find_last_not_of(" \t\r\n") + 1);
    std::string host = value.find("://") != std::string_view::npos
                           ? SiteHostFromUrl(value)
                           : SiteHostFromUrl("https://" + std::string(value));
    while (!host.empty() && host.front() == '.') {
        host.erase(host.begin());
    }
    return host;
}

bool IsSiteAllowlisted(std::string_view source_url, const std::vector<std::string>& allowlist) {
    const std::string host = SiteHostFromUrl(source_url);
    if (host.empty()) {
        return false;
    }
    return std::any_of(allowlist.begin(), allowlist.end(), [&host](const std::string& entry) {
        return host == entry || (host.size() > entry.size() && host.ends_with(entry) &&
                                 host[host.size() - entry.size() - 1] == '.');
    });
}

}  // namespace island
