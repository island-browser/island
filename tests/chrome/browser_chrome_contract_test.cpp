#include <gtest/gtest.h>

#include "browser_chrome.h"

namespace island {
namespace {

TEST(BrowserChromeContractTest, GivenTheChromeTreeWhenInspectedThenItHasOneRailAndOneBrowserView) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();

    ASSERT_EQ(tree.id, ChromeViewId::kRoot);
    ASSERT_EQ(tree.children.size(), 2U);
    EXPECT_EQ(tree.children[0].id, ChromeViewId::kRail);
    ASSERT_EQ(tree.children[1].id, ChromeViewId::kBrowserContent);
    ASSERT_EQ(tree.children[1].children.size(), 1U);
    EXPECT_EQ(tree.children[1].children[0].id, ChromeViewId::kBrowserView);
}

TEST(BrowserChromeContractTest, GivenTheChromeIdsWhenComparedThenTheyRemainStable) {
    EXPECT_EQ(static_cast<int>(ChromeViewId::kRoot), 1001);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kBack), 1004);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kForward), 1005);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kAddress), 1007);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kReload), 1008);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kActiveTab), 1012);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kBrowserView), 1014);
}

}  // namespace
}  // namespace island
