#include "content_blocker.h"
#include "gtest/gtest.h"

namespace island {

TEST(ContentBlockerStubTest, DisabledBuildAlwaysAllowsRequests) {
    ASSERT_FALSE(ContentBlocker::CompiledIn());
    const std::shared_ptr<ContentBlocker> blocker =
        ContentBlocker::Create("||ads.example^", "||tracker.example^");
    ASSERT_NE(blocker, nullptr);
    EXPECT_FALSE(blocker->available());
    EXPECT_FALSE(blocker
                     ->Check("https://ads.example/a.js", "https://site.example/", "script",
                             ContentBlockerCategories::kAds | ContentBlockerCategories::kTrackers)
                     .blocked);
}

}  // namespace island
