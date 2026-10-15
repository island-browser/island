#include "content_blocker_policy.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace island {
namespace {

TEST(ContentBlockerPolicyTest, MapsCefResourceTypesToAdblockTypes) {
    EXPECT_EQ(ContentBlockerRequestType(RT_SCRIPT), "script");
    EXPECT_EQ(ContentBlockerRequestType(RT_STYLESHEET), "stylesheet");
    EXPECT_EQ(ContentBlockerRequestType(RT_IMAGE), "image");
    EXPECT_EQ(ContentBlockerRequestType(RT_FONT_RESOURCE), "font");
    EXPECT_EQ(ContentBlockerRequestType(RT_XHR), "xmlhttprequest");
    EXPECT_EQ(ContentBlockerRequestType(RT_SUB_FRAME), "subdocument");
    EXPECT_EQ(ContentBlockerRequestType(RT_MEDIA), "media");
    EXPECT_EQ(ContentBlockerRequestType(RT_PING), "ping");
    EXPECT_EQ(ContentBlockerRequestType(RT_WORKER), "script");
    EXPECT_EQ(ContentBlockerRequestType(RT_SUB_RESOURCE), "other");
}

TEST(ContentBlockerPolicyTest, MainFramesAndLocalPagesAlwaysBypassBlocking) {
    EXPECT_TRUE(ShouldBypassContentBlocker("https://ads.example/", RT_MAIN_FRAME));
    EXPECT_TRUE(ShouldBypassContentBlocker("data:text/html,Island", RT_SCRIPT));
    EXPECT_TRUE(ShouldBypassContentBlocker("file:///tmp/page.html", RT_IMAGE));
    EXPECT_FALSE(ShouldBypassContentBlocker("https://ads.example/a.js", RT_SCRIPT));
    EXPECT_FALSE(ShouldBypassContentBlocker("wss://tracker.example/socket", RT_SUB_RESOURCE));
}

TEST(ContentBlockerPolicyTest, RefreshIsDueOnlyAfterFourDays) {
    using Clock = std::chrono::system_clock;
    const Clock::time_point now{std::chrono::seconds(1'000'000)};
    EXPECT_TRUE(ContentBlockerRefreshDue(std::nullopt, now));
    EXPECT_FALSE(ContentBlockerRefreshDue(
        now - kContentBlockerRefreshInterval + std::chrono::seconds(1), now));
    EXPECT_TRUE(ContentBlockerRefreshDue(now - kContentBlockerRefreshInterval, now));
    EXPECT_TRUE(ContentBlockerRefreshDue(now + std::chrono::seconds(1), now));
}

TEST(ContentBlockerPolicyTest, StartupUsesCacheWhileRefreshingStaleOrMissingLists) {
    using Clock = std::chrono::system_clock;
    const Clock::time_point now{std::chrono::seconds(1'000'000)};

    const ContentBlockerStartupPolicy fresh =
        DecideContentBlockerStartupPolicy(true, now - std::chrono::hours(2), now);
    EXPECT_TRUE(fresh.activate_cache);
    EXPECT_FALSE(fresh.refresh);

    const ContentBlockerStartupPolicy stale =
        DecideContentBlockerStartupPolicy(true, now - kContentBlockerRefreshInterval, now);
    EXPECT_TRUE(stale.activate_cache);
    EXPECT_TRUE(stale.refresh);

    const ContentBlockerStartupPolicy missing =
        DecideContentBlockerStartupPolicy(false, std::nullopt, now);
    EXPECT_FALSE(missing.activate_cache);
    EXPECT_TRUE(missing.refresh);
}

TEST(ContentBlockerPolicyTest, CacheLivesBelowTheIslandProfileDirectory) {
    const ContentBlockerCachePaths paths = ContentBlockerCacheForProfile("/profile/Island");
    EXPECT_EQ(paths.directory, std::filesystem::path("/profile/Island/content-blocker"));
    EXPECT_EQ(paths.easylist.filename(), "easylist.txt");
    EXPECT_EQ(paths.easyprivacy.filename(), "easyprivacy.txt");
}

TEST(ContentBlockerPolicyTest, AllowlistNormalizesHostsAndIncludesSubdomains) {
    EXPECT_EQ(NormalizeAllowlistSite(" HTTPS://Example.COM/path "), "example.com");
    EXPECT_EQ(NormalizeAllowlistSite(".example.com"), "example.com");
    const std::vector<std::string> allowlist{"example.com"};
    EXPECT_TRUE(IsSiteAllowlisted("https://example.com/article", allowlist));
    EXPECT_TRUE(IsSiteAllowlisted("https://www.example.com/article", allowlist));
    EXPECT_FALSE(IsSiteAllowlisted("https://notexample.com/", allowlist));
}

}  // namespace
}  // namespace island
