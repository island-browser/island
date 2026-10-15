#include "content_blocker.h"

#include <chrono>
#include <memory>
#include <string_view>

#include "gtest/gtest.h"

namespace island {
namespace {

constexpr std::string_view kAds = R"(
||ads.example^
@@||ads.example/allowed.js$script
||cdn.example^$third-party,script,domain=news.example
||img.example^$image
||important.example^$important
)";
constexpr std::string_view kTrackers = "||tracker.example^$xmlhttprequest\n";

std::shared_ptr<ContentBlocker> EngineOrSkip() {
    if (!ContentBlocker::CompiledIn()) {
        return nullptr;
    }
    return ContentBlocker::Create(kAds, kTrackers);
}

TEST(ContentBlockerTest, HostAnchorsBlockAndExceptionsWin) {
    const std::shared_ptr<ContentBlocker> engine = EngineOrSkip();
    if (engine == nullptr) {
        GTEST_SKIP() << "Rust content blocker is disabled";
    }
    EXPECT_TRUE(engine
                    ->Check("https://ads.example/banner.js", "https://news.example/", "script",
                            ContentBlockerCategories::kAds)
                    .blocked);
    const ContentBlockerResult exception =
        engine->Check("https://ads.example/allowed.js", "https://news.example/", "script",
                      ContentBlockerCategories::kAds);
    EXPECT_FALSE(exception.blocked);
    EXPECT_TRUE(exception.exception);
}

TEST(ContentBlockerTest, ThirdPartyDomainAndResourceTypesUseAdblockSemantics) {
    const std::shared_ptr<ContentBlocker> engine = EngineOrSkip();
    if (engine == nullptr) {
        GTEST_SKIP() << "Rust content blocker is disabled";
    }
    EXPECT_TRUE(engine
                    ->Check("https://cdn.example/app.js", "https://news.example/", "script",
                            ContentBlockerCategories::kAds)
                    .blocked);
    EXPECT_FALSE(engine
                     ->Check("https://cdn.example/app.js", "https://cdn.example/", "script",
                             ContentBlockerCategories::kAds)
                     .blocked);
    EXPECT_FALSE(engine
                     ->Check("https://cdn.example/app.js", "https://other.example/", "script",
                             ContentBlockerCategories::kAds)
                     .blocked);
    EXPECT_TRUE(engine
                    ->Check("https://img.example/pixel.png", "https://news.example/", "image",
                            ContentBlockerCategories::kAds)
                    .blocked);
    EXPECT_FALSE(engine
                     ->Check("https://img.example/pixel.png", "https://news.example/", "script",
                             ContentBlockerCategories::kAds)
                     .blocked);
}

TEST(ContentBlockerTest, CategoriesAndImportantFlagsAreReported) {
    const std::shared_ptr<ContentBlocker> engine = EngineOrSkip();
    if (engine == nullptr) {
        GTEST_SKIP() << "Rust content blocker is disabled";
    }
    const ContentBlockerResult tracker =
        engine->Check("https://tracker.example/collect", "https://news.example/", "xmlhttprequest",
                      ContentBlockerCategories::kAds | ContentBlockerCategories::kTrackers);
    EXPECT_TRUE(tracker.blocked);
    EXPECT_TRUE(tracker.matched_trackers);
    EXPECT_FALSE(engine
                     ->Check("https://tracker.example/collect", "https://news.example/",
                             "xmlhttprequest", ContentBlockerCategories::kAds)
                     .blocked);
    const ContentBlockerResult important =
        engine->Check("https://important.example/a", "https://news.example/", "script",
                      ContentBlockerCategories::kAds);
    EXPECT_TRUE(important.blocked);
    EXPECT_TRUE(important.important);
    EXPECT_GE(engine->ads_rule_count(), 5U);
    EXPECT_GE(engine->tracker_rule_count(), 1U);
}

TEST(ContentBlockerTest, InlineListChecksStayInLowMicroseconds) {
    const std::shared_ptr<ContentBlocker> engine = EngineOrSkip();
    if (engine == nullptr) {
        GTEST_SKIP() << "Rust content blocker is disabled";
    }
    constexpr int kChecks = 20000;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kChecks; ++index) {
        static_cast<void>(
            engine->Check("https://ads.example/banner.js", "https://news.example/", "script",
                          ContentBlockerCategories::kAds | ContentBlockerCategories::kTrackers));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    const double average_us = static_cast<double>(elapsed.count()) / kChecks;
    RecordProperty("average_microseconds", average_us);
    EXPECT_LT(average_us, 50.0);
}

}  // namespace
}  // namespace island
