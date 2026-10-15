#ifndef ISLAND_CONTENT_BLOCKER_POLICY_H_
#define ISLAND_CONTENT_BLOCKER_POLICY_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "include/internal/cef_types.h"

namespace island {

inline constexpr std::chrono::hours kContentBlockerRefreshInterval{24 * 4};

struct ContentBlockerCachePaths {
    std::filesystem::path directory;
    std::filesystem::path easylist;
    std::filesystem::path easyprivacy;
};

struct ContentBlockerStartupPolicy {
    bool activate_cache = false;
    bool refresh = true;
};

[[nodiscard]] ContentBlockerCachePaths ContentBlockerCacheForProfile(
    const std::filesystem::path& profile_directory);
[[nodiscard]] bool ContentBlockerRefreshDue(
    std::optional<std::chrono::system_clock::time_point> last_updated,
    std::chrono::system_clock::time_point now);
[[nodiscard]] ContentBlockerStartupPolicy DecideContentBlockerStartupPolicy(
    bool cache_is_valid, std::optional<std::chrono::system_clock::time_point> last_updated,
    std::chrono::system_clock::time_point now);
[[nodiscard]] std::string_view ContentBlockerRequestType(cef_resource_type_t resource_type);
[[nodiscard]] bool ShouldBypassContentBlocker(std::string_view url,
                                              cef_resource_type_t resource_type);
[[nodiscard]] std::string NormalizeAllowlistSite(std::string_view value);
[[nodiscard]] std::string SiteHostFromUrl(std::string_view url);
[[nodiscard]] bool IsSiteAllowlisted(std::string_view source_url,
                                     const std::vector<std::string>& allowlist);

}  // namespace island

#endif  // ISLAND_CONTENT_BLOCKER_POLICY_H_
