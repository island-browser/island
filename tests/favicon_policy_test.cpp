#include "favicon_policy.h"

#include <gtest/gtest.h>

#include "tab.h"

namespace island {
namespace {

TEST(FaviconPolicyTest, GivenLocalPagesWhenAskedThenNoIconIsFetched) {
    EXPECT_FALSE(ShouldFetchFavicon("data:text/html,<title>x</title>", "https://a.example/i.png"));
    EXPECT_FALSE(ShouldFetchFavicon("file:///tmp/a.html", "https://a.example/i.png"));
    EXPECT_FALSE(ShouldFetchFavicon("about:blank", "https://a.example/i.png"));
    EXPECT_FALSE(ShouldFetchFavicon("", "https://a.example/i.png"));
}

TEST(FaviconPolicyTest, GivenWebPagesWhenAskedThenOnlyWebAndInlineIconsAreFetched) {
    EXPECT_TRUE(ShouldFetchFavicon("https://a.example/", "https://a.example/favicon.ico"));
    EXPECT_TRUE(ShouldFetchFavicon("HTTP://a.example/", "http://cdn.example/i.png"));
    EXPECT_TRUE(ShouldFetchFavicon("https://a.example/", "data:image/png;base64,AAAA"));
    EXPECT_FALSE(ShouldFetchFavicon("https://a.example/", "file:///etc/passwd"));
    EXPECT_FALSE(ShouldFetchFavicon("https://a.example/", "javascript:alert(1)"));
    EXPECT_FALSE(ShouldFetchFavicon("https://a.example/", "data:text/html,x"));
}

TEST(FaviconPolicyTest, GivenOriginsWhenComparedThenPathsQueriesAndUserinfoAreIgnored) {
    EXPECT_EQ(FaviconOrigin("https://User@A.Example:8080/p?q#f"), "https://a.example:8080");
    EXPECT_EQ(FaviconOrigin("https://a.example"), "https://a.example");
    EXPECT_EQ(FaviconOrigin("not a url"), "");
}

TEST(FaviconPolicyTest,
     GivenANavigationWhenCommittedThenTheIconIsClearedOnlyWhenItNoLongerApplies) {
    EXPECT_FALSE(ShouldClearFaviconOnNavigation("https://a.example/x", "https://a.example/y?z"));
    EXPECT_TRUE(ShouldClearFaviconOnNavigation("https://a.example/", "https://b.example/"));
    EXPECT_TRUE(ShouldClearFaviconOnNavigation("https://a.example/", "http://a.example/"));
    EXPECT_TRUE(ShouldClearFaviconOnNavigation("https://a.example/", "data:text/html,x"));
}

TEST(FaviconPolicyTest, GivenTheDownloadSizeWhenInspectedThenItIsTheRailsSixteenDip) {
    EXPECT_EQ(kFaviconDownloadDip, 16U);
}

TEST(FaviconBookkeepingTest, GivenAPendingRequestWhenItCompletesThenOnlyAMatchingUrlIsAccepted) {
    Tab tab(TabId{1});
    tab.BeginFaviconRequest("https://a.example/new.png");

    // A slow response for an icon the tab has since replaced is dropped.
    EXPECT_FALSE(tab.CompleteFaviconRequest("https://a.example/old.png", nullptr));
    EXPECT_EQ(tab.pending_favicon_url(), "https://a.example/new.png");

    // A failed download (null image) completes the request and keeps the globe.
    EXPECT_TRUE(tab.CompleteFaviconRequest("https://a.example/new.png", nullptr));
    EXPECT_TRUE(tab.pending_favicon_url().empty());
    EXPECT_EQ(tab.favicon(), nullptr);

    // Nothing pending: a stray completion is ignored.
    EXPECT_FALSE(tab.CompleteFaviconRequest("https://a.example/new.png", nullptr));
}

TEST(FaviconBookkeepingTest, GivenAPendingRequestWhenTheIconIsClearedThenALateResultIsDropped) {
    Tab tab(TabId{1});
    tab.BeginFaviconRequest("https://a.example/i.png");

    tab.ClearFavicon();

    EXPECT_TRUE(tab.pending_favicon_url().empty());
    EXPECT_FALSE(tab.CompleteFaviconRequest("https://a.example/i.png", nullptr));
}

}  // namespace
}  // namespace island
