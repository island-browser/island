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

TEST(BrowserChromeContractTest,
     GivenTheAddressControlWhenInspectedThenItContainsLocationAndTrailingReload) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode& rail = tree.children[0];
    const ChromeViewTreeNode& navigation_row = rail.children[0];
    const ChromeViewTreeNode& address_row = rail.children[1];

    ASSERT_EQ(navigation_row.id, ChromeViewId::kNavigationRow);
    ASSERT_EQ(navigation_row.children.size(), 2U);
    EXPECT_EQ(navigation_row.children[0].id, ChromeViewId::kBack);
    EXPECT_EQ(navigation_row.children[1].id, ChromeViewId::kForward);
    ASSERT_EQ(address_row.id, ChromeViewId::kAddressRow);
    ASSERT_EQ(address_row.children.size(), 3U);
    EXPECT_EQ(address_row.children[0].id, ChromeViewId::kAddressLocationIcon);
    EXPECT_EQ(address_row.children[1].id, ChromeViewId::kAddress);
    EXPECT_EQ(address_row.children[2].id, ChromeViewId::kReload);
}

TEST(BrowserChromeContractTest, GivenTheCurrentPageWhenInspectedThenItHasFallbackAndIndicator) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode& active_page = tree.children[0].children[5];

    ASSERT_EQ(active_page.id, ChromeViewId::kActivePage);
    ASSERT_EQ(active_page.children.size(), 3U);
    EXPECT_EQ(active_page.children[0].id, ChromeViewId::kActivePageFallbackFavicon);
    EXPECT_EQ(active_page.children[1].id, ChromeViewId::kActiveTab);
    EXPECT_EQ(active_page.children[2].id, ChromeViewId::kActivePageIndicator);
}

TEST(BrowserChromeContractTest, GivenTheChromeIdsWhenComparedThenTheyRemainStable) {
    EXPECT_EQ(static_cast<int>(ChromeViewId::kRoot), 1001);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kBack), 1004);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kForward), 1005);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kAddress), 1007);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kReload), 1008);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kActiveTab), 1012);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kBrowserView), 1014);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kAddressLocationIcon), 1015);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kActivePage), 1016);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kActivePageFallbackFavicon), 1017);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kActivePageIndicator), 1018);
}

}  // namespace
}  // namespace island
