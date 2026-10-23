#include <gtest/gtest.h>

#include <algorithm>

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
    // Rail, page content, and the (zero-width while closed) agent panel column.
    ASSERT_EQ(tree.children.size(), 3U);
    EXPECT_EQ(tree.children[0].id, ChromeViewId::kRail);
    EXPECT_EQ(tree.children[1].id, ChromeViewId::kBrowserContent);
    EXPECT_EQ(tree.children[2].id, ChromeViewId::kAgentPanel);
    EXPECT_TRUE(tree.children[2].children.empty());
    ASSERT_NE(FindChild(tree, ChromeViewId::kBrowserContent), nullptr);
    const ChromeViewTreeNode* browser_content = FindChild(tree, ChromeViewId::kBrowserContent);
    ASSERT_EQ(browser_content->children.size(), 1U);
    EXPECT_EQ(browser_content->children[0].id, ChromeViewId::kBrowserView);
}

TEST(BrowserChromeContractTest,
     GivenTheAddressControlWhenInspectedThenItContainsLocationReloadAndReader) {
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
    ASSERT_EQ(address_row->children.size(), 4U);
    EXPECT_EQ(address_row->children[0].id, ChromeViewId::kAddressLocationIcon);
    EXPECT_EQ(address_row->children[1].id, ChromeViewId::kAddress);
    EXPECT_EQ(address_row->children[2].id, ChromeViewId::kReload);
    EXPECT_EQ(address_row->children[3].id, ChromeViewId::kReaderButton);
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
    // Flush with the sidebar on its left; inset on the top, right and bottom.
    EXPECT_EQ(geometry.browser_view_bounds,
              (DipRect{.x = 286, .y = pad, .width = 1154 - pad, .height = 900 - 2 * pad}));
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
              (DipRect{.x = 286, .y = pad, .width = 514 - pad, .height = 560 - 2 * pad}));
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

TEST(BrowserChromeContractTest, GivenAnAlreadyAppliedGeometryWhenLayoutRepeatsThenItIsANoOp) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const ChromeGeometrySnapshot geometry =
        BrowserChrome::LayoutForBounds({.width = 1440, .height = 900}, tokens);

    const ManagedChromeBounds actual = {
        .rail = geometry.rail_bounds,
        .toolbar = geometry.toolbar_bounds,
        .browser_content = geometry.browser_content_bounds,
        .agent_panel = geometry.agent_panel_bounds,
    };

    EXPECT_TRUE(BrowserChrome::NeedsRelayout(std::nullopt, geometry, actual));
    EXPECT_FALSE(BrowserChrome::NeedsRelayout(geometry, geometry, actual));

    ChromeGeometrySnapshot resized = geometry;
    ++resized.root_bounds.width;
    EXPECT_TRUE(BrowserChrome::NeedsRelayout(geometry, resized, actual));
}

TEST(BrowserChromeContractTest, GivenClobberedChildBoundsWhenGeometryRepeatsThenItIsReapplied) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const ChromeGeometrySnapshot geometry =
        BrowserChrome::LayoutForBounds({.width = 1440, .height = 900}, tokens);
    ManagedChromeBounds actual = {
        .rail = geometry.rail_bounds,
        .toolbar = geometry.toolbar_bounds,
        .browser_content = geometry.browser_content_bounds,
        .agent_panel = geometry.agent_panel_bounds,
    };

    EXPECT_FALSE(BrowserChrome::NeedsRelayout(geometry, geometry, actual));

    actual.browser_content = geometry.root_bounds;
    EXPECT_TRUE(BrowserChrome::NeedsRelayout(geometry, geometry, actual));
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
    EXPECT_EQ(static_cast<int>(ChromeViewId::kFindBar), 1060);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kReaderButton), 1067);
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

TEST(BrowserChromeContractTest, GivenACollapsedSidebarWhenLaidOutThenTheRailKeepsTheIconRailWidth) {
    ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);
    tokens.rail_width_dip = 286;
    const DipRect root = {.x = 0, .y = 0, .width = 1440, .height = 900};

    const ChromeGeometrySnapshot collapsed =
        BrowserChrome::LayoutForBounds(root, tokens, /*sidebar_expanded=*/false);
    const ChromeGeometrySnapshot expanded =
        BrowserChrome::LayoutForBounds(root, tokens, /*sidebar_expanded=*/true);

    EXPECT_EQ(collapsed.rail_bounds.width, kCollapsedRailWidthDip);
    EXPECT_EQ(collapsed.browser_content_bounds.x, kCollapsedRailWidthDip);
    EXPECT_EQ(collapsed.browser_content_bounds.width, 1440 - kCollapsedRailWidthDip);
    EXPECT_EQ(expanded.rail_bounds.width, 286);
    EXPECT_EQ(expanded.browser_content_bounds.x, 286);
    EXPECT_EQ(expanded.browser_content_bounds.width, 1440 - 286);
    // The two-argument overload is the expanded layout, so every existing
    // caller and contract assertion keeps its meaning.
    EXPECT_EQ(BrowserChrome::LayoutForBounds(root, tokens), expanded);
}

TEST(BrowserChromeContractTest, GivenTheOverlayIdsWhenComparedThenTheyStartAfter1027) {
    EXPECT_EQ(static_cast<int>(ChromeViewId::kHoverSliver), 1028);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kSearchPalette), 1029);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kSearchPaletteQuery), 1030);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kSearchPaletteProvider), 1031);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kSearchPaletteProviderName), 1032);
    EXPECT_GT(static_cast<int>(ChromeViewId::kHoverSliver),
              static_cast<int>(ChromeViewId::kSpaceSwitcherEntryName));
}

TEST(BrowserChromeContractTest, GivenTheOverlayIdsWhenTheTreeIsInspectedThenNoneOfThemAppear) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();

    // The retired sliver id and the palette are window-level regions reported
    // through CefOverlayController, so the chrome view tree keeps its exact
    // Phase 2/3 shape and never gains an overlay node.
    EXPECT_EQ(FindChild(tree, ChromeViewId::kHoverSliver), nullptr);
    EXPECT_EQ(FindChild(tree, ChromeViewId::kSearchPalette), nullptr);
    EXPECT_EQ(FindChild(tree, ChromeViewId::kSearchPaletteQuery), nullptr);
    EXPECT_EQ(FindChild(tree, ChromeViewId::kSearchPaletteProvider), nullptr);
    EXPECT_EQ(FindChild(tree, ChromeViewId::kSearchPaletteProviderName), nullptr);
    ASSERT_EQ(tree.children.size(), 3U);
}

TEST(BrowserChromeContractTest, GivenTheArcRailWhenInspectedThenItHasNoNewTabRowAndAFooter) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    std::vector<ChromeViewId> order;
    for (const ChromeViewTreeNode& child : rail->children) {
        order.push_back(child.id);
    }
    // Top to bottom: navigation, address, the tab list, then the bottom group (divider, space
    // bar, footer); the current-page card stays last and collapsed. The New Tab row is gone:
    // ⌘T (Ctrl+T), the Browser menu and the palette open tabs.
    EXPECT_EQ(order, (std::vector<ChromeViewId>{
                         ChromeViewId::kNavigationRow, ChromeViewId::kAddressRow,
                         ChromeViewId::kValidationMessage, ChromeViewId::kTabStrip,
                         ChromeViewId::kSpacer, ChromeViewId::kDivider,
                         ChromeViewId::kSpaceSwitcher, ChromeViewId::kRailFooter,
                         ChromeViewId::kActivePage}));
    const ChromeViewTreeNode* footer = FindChild(*rail, ChromeViewId::kRailFooter);
    ASSERT_NE(footer, nullptr);
    ASSERT_EQ(footer->children.size(), 4U);
    EXPECT_EQ(footer->children[0].id, ChromeViewId::kRailAgentButton);
    EXPECT_EQ(footer->children[1].id, ChromeViewId::kRailTabsButton);
    EXPECT_EQ(footer->children[2].id, ChromeViewId::kRailSettingsButton);
    EXPECT_EQ(footer->children[3].id, ChromeViewId::kRailNewSpaceButton);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kRailNewSpaceButton), 1045);
}

TEST(BrowserChromeContractTest, GivenSpacePillsWhenLabeledThenOnlyTheActiveOneShowsItsName) {
    EXPECT_EQ(BrowserChrome::SpacePillLabel({.name = "research", .active = false}), "R");
    EXPECT_EQ(BrowserChrome::SpacePillLabel({.name = "Research", .active = true}), "Research");
    EXPECT_EQ(BrowserChrome::SpacePillLabel({.name = "\xC3\xA9t\xC3\xA9", .active = false}),
              "\xC3\xA9");
    EXPECT_EQ(BrowserChrome::SpacePillLabel({.name = "", .active = false}), "?");
}

TEST(BrowserChromeContractTest, GivenAnOpenAgentPanelWhenLaidOutThenItTakesTheRightColumn) {
    ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const DipRect root = {.x = 0, .y = 0, .width = 1440, .height = 900};
    const int panel = BrowserChrome::AgentPanelWidthDip();

    const ChromeGeometrySnapshot open = BrowserChrome::LayoutForBounds(root, tokens, true, true);
    EXPECT_EQ(open.agent_panel_bounds,
              (DipRect{.x = 1440 - panel, .y = 0, .width = panel, .height = 900}));
    EXPECT_EQ(open.browser_content_bounds,
              (DipRect{.x = 286, .y = 0, .width = 1440 - 286 - panel, .height = 900}));

    const ChromeGeometrySnapshot closed = BrowserChrome::LayoutForBounds(root, tokens, true, false);
    EXPECT_EQ(closed.agent_panel_bounds, (DipRect{.x = 1440, .y = 0, .width = 0, .height = 900}));
    EXPECT_EQ(closed, BrowserChrome::LayoutForBounds(root, tokens));
}

TEST(BrowserChromeContractTest,
     GivenANarrowWindowWhenTheAgentPanelOpensThenThePageKeepsItsMinimum) {
    ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const ChromeGeometrySnapshot compact = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 800, .height = 560}, tokens, true, true);
    EXPECT_EQ(compact.browser_content_bounds.width, BrowserChrome::MinimumBrowserContentWidthDip());
    EXPECT_EQ(compact.agent_panel_bounds.width, 800 - 286 - 320);

    const ChromeGeometrySnapshot tiny = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 500, .height = 560}, tokens, true, true);
    EXPECT_EQ(tiny.agent_panel_bounds.width, 0);
    EXPECT_EQ(tiny.browser_content_bounds.width, 500 - 286);
}

// Native (2026-10-16 owner request): one Agent entry at the sidebar bottom, no toolbar Agent,
// no New Tab row, and a switcher row with the space segments.
std::vector<ChromeViewId> ChildIds(const ChromeViewTreeNode& node) {
    std::vector<ChromeViewId> ids;
    for (const ChromeViewTreeNode& child : node.children) {
        ids.push_back(child.id);
    }
    return ids;
}

TEST(BrowserChromeNativeContractTest, GivenNativeLookWhenInspectedThenTheToolbarHasNoAgentButton) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract(LookPreset::kNative);
    const ChromeViewTreeNode* toolbar = FindChild(tree, ChromeViewId::kToolbar);
    ASSERT_NE(toolbar, nullptr);
    EXPECT_EQ(ChildIds(*toolbar),
              (std::vector<ChromeViewId>{ChromeViewId::kNavigationRow, ChromeViewId::kAddressRow,
                                         ChromeViewId::kToolbarShare}));
    EXPECT_EQ(FindChild(tree, ChromeViewId::kToolbarAgent), nullptr);
}

TEST(BrowserChromeNativeContractTest, GivenNativeRailWhenInspectedThenBottomHoldsAgentAndSwitcher) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract(LookPreset::kNative);
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    // There is no New Tab row: the rail holds the tab list and no row above it.
    EXPECT_EQ(ChildIds(*rail),
              (std::vector<ChromeViewId>{ChromeViewId::kValidationMessage, ChromeViewId::kTabStrip,
                                         ChromeViewId::kSpacer, ChromeViewId::kDivider,
                                         ChromeViewId::kRailFooter, ChromeViewId::kActivePage}));
    const ChromeViewTreeNode* footer = FindChild(*rail, ChromeViewId::kRailFooter);
    ASSERT_NE(footer, nullptr);
    // kDivider is the one-DIP hairline immediately above the footer. Top to bottom inside it:
    // the switcher row, then the aligned Agent row at the very bottom.
    EXPECT_EQ(ChildIds(*footer), (std::vector<ChromeViewId>{ChromeViewId::kRailSwitcherRow,
                                                            ChromeViewId::kRailAgentRow}));
    const ChromeViewTreeNode* agent_row = FindChild(*footer, ChromeViewId::kRailAgentRow);
    ASSERT_NE(agent_row, nullptr);
    EXPECT_EQ(ChildIds(*agent_row), (std::vector<ChromeViewId>{ChromeViewId::kRailAgentButton,
                                                               ChromeViewId::kRailAgentMeta,
                                                               ChromeViewId::kRailAgentKeycap}));
    const ChromeViewTreeNode* switcher_row = FindChild(*footer, ChromeViewId::kRailSwitcherRow);
    ASSERT_NE(switcher_row, nullptr);
    EXPECT_EQ(ChildIds(*switcher_row),
              (std::vector<ChromeViewId>{
                  ChromeViewId::kSpaceSwitcher, ChromeViewId::kRailNewSpaceButton,
                  ChromeViewId::kRailTabsButton, ChromeViewId::kRailSettingsButton}));
    EXPECT_EQ(static_cast<int>(ChromeViewId::kRailSwitcherRow), 1076);
}

TEST(BrowserChromeNativeContractTest, GivenNativeTabsWhenProjectedThenTheListHasNoNewTabRow) {
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = {{.name = "Default", .active = true}};
    for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
        std::vector<TabStripEntrySnapshot> tabs(count, TabStripEntrySnapshot{.title = "Island"});
        const ChromeViewTreeNode tree =
            BrowserChrome::CollectionCountContract(tabs, spaces, LookPreset::kNative);
        const ChromeViewTreeNode* tab_strip = FindChild(tree, ChromeViewId::kTabStrip);
        ASSERT_NE(tab_strip, nullptr);
        // The list holds only tab rows and its section headers and kickers.
        for (const ChromeViewTreeNode& child : tab_strip->children) {
            EXPECT_TRUE(child.id == ChromeViewId::kTabStripEntry ||
                        child.id == ChromeViewId::kTabsSectionHeader ||
                        child.id == ChromeViewId::kFavoritesSectionHeader ||
                        child.id == ChromeViewId::kTabGroupKicker);
        }
    }
}

TEST(BrowserChromeNativeContractTest, GivenNativeSpacesWhenProjectedThenEachSegmentHasNoColorMark) {
    const std::vector<TabStripEntrySnapshot> tabs = {{.title = "Island"}};
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = {{.name = "Default", .active = true},
                                                            {.name = "Work"}};
    const ChromeViewTreeNode tree =
        BrowserChrome::CollectionCountContract(tabs, spaces, LookPreset::kNative);
    const ChromeViewTreeNode* switcher = FindChild(tree, ChromeViewId::kSpaceSwitcher);
    ASSERT_NE(switcher, nullptr);
    ASSERT_EQ(switcher->children.size(), spaces.size());
    for (const ChromeViewTreeNode& segment : switcher->children) {
        EXPECT_EQ(ChildIds(segment),
                  (std::vector<ChromeViewId>{ChromeViewId::kSpaceSwitcherEntryName}));
    }
}

TEST(BrowserChromeContractTest, GivenAnIconThenItReplacesTheArcColorMark) {
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = {
        {.name = "Work", .icon_id = "briefcase", .active = true}};
    const ChromeViewTreeNode tree = BrowserChrome::CollectionCountContract({}, spaces);
    const ChromeViewTreeNode* switcher = FindChild(tree, ChromeViewId::kSpaceSwitcher);
    ASSERT_NE(switcher, nullptr);
    ASSERT_EQ(switcher->children.size(), 1U);
    EXPECT_EQ(ChildIds(switcher->children[0]),
              (std::vector<ChromeViewId>{ChromeViewId::kSpaceSwitcherEntryName}));
}

TEST(BrowserChromeNativeContractTest, GivenIconSegmentsThenTheyShowOnlyTheIcon) {
    for (const bool active : {false, true}) {
        const SpaceSwitcherEntrySnapshot icon = {
            .name = "Work", .icon_id = "briefcase", .active = active};
        EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(icon, 2, LookPreset::kNative), "");
        EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(icon, 5, LookPreset::kNative), "");
    }
    // Arc and Graphite: an icon never sits beside a lone initial. Its name shows when the chips
    // fit; otherwise the icon stands alone and the name stays in the tooltip.
    const SpaceSwitcherEntrySnapshot arc = {.name = "Work", .icon_id = "briefcase"};
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(arc, 5, LookPreset::kArc), "");
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(arc, 5, LookPreset::kArc, /*chip_names_fit=*/true),
              "Work");
}

TEST(BrowserChromeNativeContractTest, GivenArcAndGraphiteChipsThenNamesShowWhenTheyFit) {
    const SpaceSwitcherEntrySnapshot inactive = {.name = "Space 2", .active = false};
    for (const LookPreset look : {LookPreset::kArc, LookPreset::kGraphite}) {
        EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(inactive, 3, look, /*chip_names_fit=*/true),
                  "Space 2");
        EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(inactive, 3, look), "S");
    }
}

TEST(BrowserChromeNativeContractTest, GivenSegmentsWithoutIconsThenNameOrInitialNeverLoneEllipsis) {
    const SpaceSwitcherEntrySnapshot shorty = {.name = "Home", .active = false};
    const SpaceSwitcherEntrySnapshot longer = {.name = "Personal projects", .active = false};
    const SpaceSwitcherEntrySnapshot lower = {.name = "École supérieure", .active = false};
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(shorty, 1, LookPreset::kNative), "Home");
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(shorty, 5, LookPreset::kNative), "H");
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(longer, 2, LookPreset::kNative),
              "P");
    EXPECT_EQ(BrowserChrome::SpaceSegmentLabel(longer, 4, LookPreset::kNative), "P");
    for (std::size_t count = 1; count <= 8; ++count) {
        for (const SpaceSwitcherEntrySnapshot* entry : {&shorty, &longer, &lower}) {
            const std::string label = BrowserChrome::SpaceSegmentLabel(*entry, count,
                                                                       LookPreset::kNative);
            EXPECT_NE(label, "\xE2\x80\xA6");
            EXPECT_FALSE(label.empty());
        }
    }
}

TEST(BrowserChromeNativeContractTest, GivenNativeAgentEntryWhenPaintedThenOpenAndClosedDiffer) {
    const ChromeTokens light = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const ChromeTokens dark = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    const auto open =
        BrowserChrome::NativeAgentEntryColors(light, /*open=*/true, /*hovered=*/false);
    EXPECT_EQ(open.fill, light.segment_selected);
    EXPECT_EQ(open.text, light.text);
    const auto closed =
        BrowserChrome::NativeAgentEntryColors(light, /*open=*/false, /*hovered=*/false);
    EXPECT_EQ(closed.fill, light.canvas);
    EXPECT_EQ(closed.text, light.text);
    EXPECT_EQ(closed.sparkle, light.accent);
    const auto hover =
        BrowserChrome::NativeAgentEntryColors(dark, /*open=*/false, /*hovered=*/true);
    EXPECT_EQ(hover.fill, dark.hover);
}

// Arc and Graphite (owner request, 0.8.0): the Agent plate is neutral in every state, so no
// accent fill or label is ever painted behind or on it.
TEST(BrowserChromeContractTest, GivenAgentNeedsYouBadgeWhenColoredThenItIsAmberNotAccent) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        for (const LookPreset look :
             {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);
            EXPECT_EQ(BrowserChrome::AgentNeedsYouBadgeColor(tokens), tokens.warning_border);
            EXPECT_NE(BrowserChrome::AgentNeedsYouBadgeColor(tokens), tokens.accent);
        }
    }
}

TEST(BrowserChromeContractTest, GivenFooterAgentPlateWhenStatedThenItIsNeutralInEveryState) {
    const ArgbColor rail{.argb = 0xFF123456U};
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        for (const LookPreset look : {LookPreset::kArc, LookPreset::kGraphite}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);
            EXPECT_EQ(BrowserChrome::FooterAgentPlate(tokens, false, false, rail), rail);
            EXPECT_EQ(BrowserChrome::FooterAgentPlate(tokens, false, true, rail), tokens.hover);
            EXPECT_EQ(BrowserChrome::FooterAgentPlate(tokens, true, false, rail),
                      tokens.segment_selected);
            EXPECT_EQ(BrowserChrome::FooterAgentPlate(tokens, true, true, rail),
                      tokens.segment_selected);
            EXPECT_NE(BrowserChrome::FooterAgentPlate(tokens, true, false, rail), tokens.accent);
        }
    }
}

TEST(BrowserChromeNativeContractTest, GivenNativeSwitcherWhenSizedThenSegmentsFitTheTrack) {
    const BrowserChrome::NativeSwitcherMetrics sizes = BrowserChrome::NativeSwitcherSizes();
    // Footer polish: same-height controls on an 8 DIP rhythm; segments inset in the track.
    EXPECT_EQ(sizes.row_dip, 28);
    EXPECT_EQ(sizes.button_dip, sizes.row_dip);
    EXPECT_EQ(sizes.agent_dip, sizes.row_dip);
    EXPECT_EQ(sizes.footer_gap_dip, 8);
    EXPECT_EQ(sizes.track_inset_dip, 2);
    EXPECT_EQ(sizes.segment_dip, 24);
    EXPECT_EQ(sizes.segment_dip + 2 * sizes.track_inset_dip, sizes.row_dip);
}

TEST(BrowserChromeNativeContractTest, GivenNativeSegmentsWhenPaintedThenSelectedIsNeutral) {
    const ChromeTokens light = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const auto active = BrowserChrome::NativeSegmentColors(light, /*active=*/true);
    EXPECT_EQ(active.fill.argb, 0xFFE3E3E8U);
    EXPECT_EQ(active.outline, active.fill);
    EXPECT_EQ(active.title, light.text);
    const auto idle = BrowserChrome::NativeSegmentColors(light, /*active=*/false);
    EXPECT_EQ(idle.fill, light.fill);
    EXPECT_EQ(idle.title, light.text_secondary);
    EXPECT_EQ(idle.track, light.fill);

    const ChromeTokens dark = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    const auto dark_active = BrowserChrome::NativeSegmentColors(dark, /*active=*/true);
    EXPECT_EQ(dark_active.fill.argb, 0xFF3A3A3DU);
    EXPECT_EQ(dark_active.outline, dark.border_strong);
    EXPECT_NE(dark_active.fill, dark.accent);
}

TEST(BrowserChromeNativeContractTest, GivenNavigationButtonsThenTheirRowSharesTheirSurface) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens native = ChromeTokens::ForLook(LookPreset::kNative, theme);
        EXPECT_EQ(BrowserChrome::NavigationRowSurface(native), native.surface_secondary);

        const ChromeTokens arc = ChromeTokens::ForLook(LookPreset::kArc, theme);
        EXPECT_EQ(BrowserChrome::NavigationRowSurface(arc), arc.canvas);

        const ChromeTokens graphite = ChromeTokens::ForLook(LookPreset::kGraphite, theme);
        EXPECT_EQ(BrowserChrome::NavigationRowSurface(graphite), graphite.canvas);
    }
}

}  // namespace
TEST(BrowserChromeContractTest, GivenAFixedWidthColumnWhenSizedThenNeitherDimensionIsZero) {
    // CEF ignores a delegate size with a zero dimension, which let the agent panel's content
    // pick its own width and cover the page.
    const auto open = BrowserChrome::FixedWidthColumnFor(BrowserChrome::AgentPanelWidthDip());
    EXPECT_EQ(open.width, BrowserChrome::AgentPanelWidthDip());
    EXPECT_GT(open.min_height, 0);
    EXPECT_GT(open.max_height, open.min_height);

    EXPECT_EQ(BrowserChrome::FixedWidthColumnFor(0), BrowserChrome::FixedWidthColumn{});
}

}  // namespace island

namespace island {
namespace {

TEST(BrowserChromeNativeContractTest, GivenAgentStatusWhenLabeledThenQueueProgressReplacesTheName) {
    AgentEntryStatus status{.provider_name = "Claude Code"};
    EXPECT_EQ(BrowserChrome::AgentEntryMetaText(status), "Claude Code");
    status.activity = AgentActivity::kWorking;
    status.queue_done = 3;
    status.queue_total = 7;
    EXPECT_EQ(BrowserChrome::AgentEntryMetaText(status), "3/7");
    status.queue_done = 9;  // never "9/7"
    EXPECT_EQ(BrowserChrome::AgentEntryMetaText(status), "7/7");
    EXPECT_EQ(BrowserChrome::AgentEntryDescription(status), "Claude Code, working, task 7/7");
    status.queue_total = 0;
    status.queued_count = 2;
    EXPECT_EQ(BrowserChrome::AgentEntryMetaText(status), "2 queued");
    EXPECT_EQ(BrowserChrome::AgentEntryDescription(status), "Claude Code, working, 2 queued");
    const AgentEntryStatus longer{.provider_name = "A very long provider name"};
    // The full name; the view fits it to the measured room (see FitAgentEntryText).
    EXPECT_EQ(BrowserChrome::AgentEntryMetaText(longer), "A very long provider name");
    EXPECT_EQ(BrowserChrome::AgentEntryDescription({.activity = AgentActivity::kNeedsYou}),
              "Agent, needs you");
}

TEST(BrowserChromeNativeContractTest, GivenAgentActivityWhenColoredThenEachStateIsDistinct) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        const ArgbColor idle = BrowserChrome::AgentStatusDotColor(tokens, AgentActivity::kIdle);
        const ArgbColor working =
            BrowserChrome::AgentStatusDotColor(tokens, AgentActivity::kWorking);
        const ArgbColor needs =
            BrowserChrome::AgentStatusDotColor(tokens, AgentActivity::kNeedsYou);
        EXPECT_NE(idle.argb, working.argb);
        EXPECT_NE(working.argb, needs.argb);
        EXPECT_NE(idle.argb, needs.argb);
    }
}

}  // namespace
}  // namespace island

namespace island {
namespace {

// Fake measure: seven DIP per code point, so the expectations below count code points.
int CodePointWidth(std::string_view text) {
    int width = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) {
            width += 7;
        }
    }
    return width;
}

TEST(BrowserChromeNativeContractTest, GivenANameThatFitsWhenFittedThenItIsKept) {
    EXPECT_EQ(BrowserChrome::FitAgentEntryText("Claude Code", 77, CodePointWidth), "Claude Code");
}

TEST(BrowserChromeNativeContractTest, GivenANameThatDoesNotFitThenItEndsWithAnEllipsisInside) {
    // Nine code points plus the ellipsis is 70 DIP: "A very lo" and the ellipsis.
    EXPECT_EQ(BrowserChrome::FitAgentEntryText("A very long provider name", 70, CodePointWidth),
              "A very lo\xE2\x80\xA6");
    // A multi-byte character is never split.
    EXPECT_EQ(BrowserChrome::FitAgentEntryText("\xCE\xA9mega sphere", 49, CodePointWidth),
              "\xCE\xA9mega\xE2\x80\xA6");
}

TEST(BrowserChromeNativeContractTest, GivenNoRoomThenOnlyTheEllipsisRemains) {
    EXPECT_EQ(BrowserChrome::FitAgentEntryText("Claude Code", 0, CodePointWidth), "\xE2\x80\xA6");
}

}  // namespace
}  // namespace island
