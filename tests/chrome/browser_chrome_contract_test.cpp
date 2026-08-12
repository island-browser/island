#include <gtest/gtest.h>

#include "browser_chrome.h"

namespace island {
namespace {

const ChromeViewTreeNode* FindChild(const ChromeViewTreeNode& node, ChromeViewId id) {
    for (const ChromeViewTreeNode& child : node.children) {
        if (child.id == id) {
            return &child;
        }
        if (const ChromeViewTreeNode* match = FindChild(child, id); match != nullptr) {
            return match;
        }
    }
    return nullptr;
}

TEST(BrowserChromeContractTest, GivenTheChromeTreeWhenInspectedThenItHasOneRailAndOneBrowserView) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();

    ASSERT_EQ(tree.id, ChromeViewId::kRoot);
    ASSERT_EQ(tree.children.size(), 2U);
    EXPECT_EQ(tree.children[0].id, ChromeViewId::kRail);
    ASSERT_NE(FindChild(tree, ChromeViewId::kBrowserContent), nullptr);
    const ChromeViewTreeNode* browser_content = FindChild(tree, ChromeViewId::kBrowserContent);
    ASSERT_EQ(browser_content->children.size(), 1U);
    EXPECT_EQ(browser_content->children[0].id, ChromeViewId::kBrowserView);
}

TEST(BrowserChromeContractTest,
     GivenTheAddressControlWhenInspectedThenItContainsLocationAndTrailingReload) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    const ChromeViewTreeNode* navigation_row = FindChild(*rail, ChromeViewId::kNavigationRow);
    const ChromeViewTreeNode* address_row = FindChild(*rail, ChromeViewId::kAddressRow);

    ASSERT_NE(navigation_row, nullptr);
    ASSERT_EQ(navigation_row->children.size(), 2U);
    EXPECT_EQ(navigation_row->children[0].id, ChromeViewId::kBack);
    EXPECT_EQ(navigation_row->children[1].id, ChromeViewId::kForward);
    ASSERT_NE(address_row, nullptr);
    ASSERT_EQ(address_row->children.size(), 3U);
    EXPECT_EQ(address_row->children[0].id, ChromeViewId::kAddressLocationIcon);
    EXPECT_EQ(address_row->children[1].id, ChromeViewId::kAddress);
    EXPECT_EQ(address_row->children[2].id, ChromeViewId::kReload);
}

TEST(BrowserChromeContractTest, GivenTheCurrentPageWhenInspectedThenItHasFallbackAndIndicator) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    const ChromeViewTreeNode* active_page = FindChild(*rail, ChromeViewId::kActivePage);

    ASSERT_NE(FindChild(*rail, ChromeViewId::kNavigationRow), nullptr);
    ASSERT_NE(FindChild(*rail, ChromeViewId::kAddressRow), nullptr);
    ASSERT_NE(FindChild(*rail, ChromeViewId::kValidationMessage), nullptr);
    ASSERT_NE(FindChild(*rail, ChromeViewId::kSpacer), nullptr);
    ASSERT_NE(FindChild(*rail, ChromeViewId::kDivider), nullptr);
    ASSERT_NE(active_page, nullptr);
    ASSERT_EQ(active_page->children.size(), 3U);
    EXPECT_EQ(active_page->children[0].id, ChromeViewId::kActivePageFallbackFavicon);
    EXPECT_EQ(active_page->children[1].id, ChromeViewId::kActiveTab);
    EXPECT_EQ(active_page->children[2].id, ChromeViewId::kActivePageIndicator);
}

TEST(BrowserChromeContractTest,
     GivenTheRailWhenInspectedThenItExposesTabStripAndSpaceSwitcherCollections) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);

    const ChromeViewTreeNode* tab_strip = FindChild(*rail, ChromeViewId::kTabStrip);
    ASSERT_NE(tab_strip, nullptr);
    EXPECT_EQ(tab_strip->children.size(), 0U);

    const ChromeViewTreeNode* space_switcher = FindChild(*rail, ChromeViewId::kSpaceSwitcher);
    ASSERT_NE(space_switcher, nullptr);
    EXPECT_EQ(space_switcher->children.size(), 0U);
}

TEST(BrowserChromeContractTest, GivenTabStripEntriesWhenProjectedThenEachEntryHasTheTabItemShape) {
    const std::vector<TabStripEntrySnapshot> tabs = {
        {.title = "Island"},
        {.title = "A very long page title that must truncate in the rail"},
    };
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = {
        {.color = ArgbColor{0xFF336699}, .name = "Personal"}};
    const ChromeViewTreeNode tree = BrowserChrome::CollectionCountContract(tabs, spaces);
    const ChromeViewTreeNode* tab_strip = FindChild(tree, ChromeViewId::kTabStrip);
    ASSERT_NE(tab_strip, nullptr);

    ASSERT_EQ(tab_strip->children.size(), tabs.size());
    for (const ChromeViewTreeNode& entry : tab_strip->children) {
        EXPECT_EQ(entry.id, ChromeViewId::kTabStripEntry);
        ASSERT_EQ(entry.children.size(), 3U);
        EXPECT_EQ(entry.children[0].id, ChromeViewId::kTabStripEntryFavicon);
        EXPECT_EQ(entry.children[1].id, ChromeViewId::kTabStripEntryTitle);
        EXPECT_EQ(entry.children[2].id, ChromeViewId::kTabStripEntryClose);
        for (const ChromeViewTreeNode& child : entry.children) {
            EXPECT_TRUE(child.children.empty());
        }
    }
}

TEST(BrowserChromeContractTest,
     GivenSpaceSwitcherEntriesWhenProjectedThenEachEntryHasTheSpaceItemShape) {
    const std::vector<TabStripEntrySnapshot> tabs = {{.title = "Island"}};
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = {
        {.color = ArgbColor{0xFF336699}, .name = "Personal"},
        {.color = ArgbColor{0xFF993366},
         .name = "A very long space name that must truncate in the rail"},
    };
    const ChromeViewTreeNode tree = BrowserChrome::CollectionCountContract(tabs, spaces);
    const ChromeViewTreeNode* space_switcher = FindChild(tree, ChromeViewId::kSpaceSwitcher);
    ASSERT_NE(space_switcher, nullptr);

    ASSERT_EQ(space_switcher->children.size(), spaces.size());
    for (const ChromeViewTreeNode& entry : space_switcher->children) {
        EXPECT_EQ(entry.id, ChromeViewId::kSpaceSwitcherEntry);
        ASSERT_EQ(entry.children.size(), 2U);
        EXPECT_EQ(entry.children[0].id, ChromeViewId::kSpaceSwitcherEntryColorMark);
        EXPECT_EQ(entry.children[1].id, ChromeViewId::kSpaceSwitcherEntryName);
        for (const ChromeViewTreeNode& child : entry.children) {
            EXPECT_TRUE(child.children.empty());
        }
    }
}

TEST(BrowserChromeContractTest,
     GivenNoTabsOrSpacesWhenProjectedThenBothCollectionsAreEmptyAndFixedRegionsRemain) {
    const ChromeViewTreeNode tree = BrowserChrome::CollectionCountContract({}, {});
    const ChromeViewTreeNode* tab_strip = FindChild(tree, ChromeViewId::kTabStrip);
    const ChromeViewTreeNode* space_switcher = FindChild(tree, ChromeViewId::kSpaceSwitcher);
    ASSERT_NE(tab_strip, nullptr);
    ASSERT_NE(space_switcher, nullptr);
    EXPECT_TRUE(tab_strip->children.empty());
    EXPECT_TRUE(space_switcher->children.empty());

    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    EXPECT_NE(FindChild(*rail, ChromeViewId::kNavigationRow), nullptr);
    EXPECT_NE(FindChild(*rail, ChromeViewId::kAddressRow), nullptr);
    EXPECT_NE(FindChild(*rail, ChromeViewId::kActivePage), nullptr);
    const ChromeViewTreeNode* browser_content = FindChild(tree, ChromeViewId::kBrowserContent);
    ASSERT_NE(browser_content, nullptr);
    ASSERT_EQ(browser_content->children.size(), 1U);
    EXPECT_EQ(browser_content->children[0].id, ChromeViewId::kBrowserView);
}

// The floating-canvas layout insets the browser view inside the browser_content
// panel by BrowserContentPaddingDip() on every side, so browser_view_bounds is now a
// strict inset of browser_content_bounds rather than equal to it. These tests pin the
// new geometry: the rail stays fixed at 286 DIP, the content panel keeps the remaining
// width, and the view is the content panel shrunk by the gutter (clamped at zero so the
// card never inverts on narrow windows). The ID/tree contract is untouched.
TEST(BrowserChromeContractTest, GivenReferenceWindowBoundsWhenLaidOutThenRailStaysFixed) {
    ChromeTokens tokens;
    tokens.rail_width_dip = 286;
    const ChromeGeometrySnapshot geometry = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 1440, .height = 900}, tokens);
    const int pad = BrowserChrome::BrowserContentPaddingDip();

    EXPECT_EQ(geometry.rail_bounds, (DipRect{.x = 0, .y = 0, .width = 286, .height = 900}));
    EXPECT_EQ(geometry.browser_content_bounds,
              (DipRect{.x = 286, .y = 0, .width = 1154, .height = 900}));
    EXPECT_EQ(
        geometry.browser_view_bounds,
        (DipRect{.x = 286 + pad, .y = pad, .width = 1154 - 2 * pad, .height = 900 - 2 * pad}));
}

TEST(BrowserChromeContractTest, GivenMinimumWindowBoundsWhenLaidOutThenBrowserViewStaysNonzero) {
    ChromeTokens tokens;
    tokens.rail_width_dip = 286;
    const ChromeGeometrySnapshot geometry = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 800, .height = 560}, tokens);
    const int pad = BrowserChrome::BrowserContentPaddingDip();

    EXPECT_EQ(geometry.rail_bounds, (DipRect{.x = 0, .y = 0, .width = 286, .height = 560}));
    EXPECT_EQ(geometry.browser_content_bounds,
              (DipRect{.x = 286, .y = 0, .width = 514, .height = 560}));
    EXPECT_EQ(geometry.browser_view_bounds,
              (DipRect{.x = 286 + pad, .y = pad, .width = 514 - 2 * pad, .height = 560 - 2 * pad}));
    EXPECT_GT(geometry.browser_view_bounds.width, 0);
    EXPECT_GT(geometry.browser_view_bounds.height, 0);
}

TEST(BrowserChromeContractTest, GivenNarrowBoundsWhenLaidOutThenBrowserContentClampsAtZero) {
    ChromeTokens tokens;
    tokens.rail_width_dip = 286;
    const ChromeGeometrySnapshot geometry = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 200, .height = 560}, tokens);

    EXPECT_EQ(geometry.rail_bounds, (DipRect{.x = 0, .y = 0, .width = 200, .height = 560}));
    EXPECT_EQ(geometry.browser_content_bounds,
              (DipRect{.x = 200, .y = 0, .width = 0, .height = 560}));
    EXPECT_EQ(geometry.browser_view_bounds.width, 0);
    EXPECT_GE(geometry.browser_view_bounds.height, 0);
}

TEST(BrowserChromeContractTest, GivenZeroBoundsWhenLaidOutThenEveryChildIsZeroSized) {
    ChromeTokens tokens;
    tokens.rail_width_dip = 286;
    const ChromeGeometrySnapshot geometry = BrowserChrome::LayoutForBounds(DipRect{}, tokens);

    EXPECT_EQ(geometry.rail_bounds, DipRect{});
    EXPECT_EQ(geometry.browser_content_bounds, DipRect{});
    EXPECT_EQ(geometry.browser_view_bounds, DipRect{});
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

TEST(BrowserChromeContractTest, GivenTheCollectionIdsWhenComparedThenTheyStartAfter1018) {
    EXPECT_GT(static_cast<int>(ChromeViewId::kTabStrip),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kTabStripEntry),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kTabStripEntryFavicon),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kTabStripEntryTitle),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kTabStripEntryClose),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kSpaceSwitcher),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kSpaceSwitcherEntry),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryColorMark),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
    EXPECT_GT(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryName),
              static_cast<int>(ChromeViewId::kActivePageIndicator));
}

}  // namespace
}  // namespace island
