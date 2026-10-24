#include "collapsed_rail.h"

#include <gtest/gtest.h>

#include "browser_chrome.h"

namespace island {
namespace {

TabStripEntrySnapshot Tab(std::string title, bool active = false, bool pinned = false) {
    return {.title = std::move(title), .active = active, .pinned = pinned};
}

TEST(CollapsedRailMetricsTest, GivenTheRailWidthWhenInspectedThenButtonsAndScrollbarFit) {
    EXPECT_EQ(kCollapsedRailWidthDip, 52);
    EXPECT_EQ(kCollapsedRailButtonDip, 32);
    EXPECT_EQ(kCollapsedRailGutterDip, 10);
    // A classic vertical scrollbar leaves the buttons room, so no horizontal bar can appear.
    EXPECT_LE(kCollapsedRailButtonDip + kCollapsedRailScrollbarAllowanceDip,
              kCollapsedRailWidthDip);
}

TEST(CollapsedRailMetricsTest, GivenTabsWhenStackedThenTheListHeightCountsGapsAndSeparator) {
    EXPECT_EQ(CollapsedRailTabListHeightDip(0, false), 0);
    EXPECT_EQ(CollapsedRailTabListHeightDip(1, false), 32);
    EXPECT_EQ(CollapsedRailTabListHeightDip(3, false), 3 * 32 + 2 * 2);
    // Separator: one more child (9 DIP) and one more gap.
    EXPECT_EQ(CollapsedRailTabListHeightDip(3, true), 3 * 32 + 9 + 3 * 2);
}

TEST(CollapsedRailLayoutTest, GivenLayoutBoundsWhenCollapsedThenTheContentStartsAfterTheIconRail) {
    ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const DipRect root = {.x = 0, .y = 0, .width = 1440, .height = 900};

    const ChromeGeometrySnapshot collapsed =
        BrowserChrome::LayoutForBounds(root, tokens, /*sidebar_expanded=*/false);
    const ChromeGeometrySnapshot expanded =
        BrowserChrome::LayoutForBounds(root, tokens, /*sidebar_expanded=*/true);

    EXPECT_EQ(collapsed.rail_bounds.width, 52);
    EXPECT_EQ(collapsed.browser_content_bounds.x, 52);
    EXPECT_EQ(collapsed.browser_content_bounds.width, 1440 - 52);
    EXPECT_EQ(expanded.rail_bounds.width, tokens.rail_width_dip);
    // A window narrower than the rail clamps it.
    EXPECT_EQ(BrowserChrome::LayoutForBounds({0, 0, 40, 300}, tokens, false).rail_bounds.width, 40);
}

TEST(CollapsedRailLayoutTest, GivenMixedTabsWhenLaidOutThenPinnedComeFirstWithSourceIndices) {
    const std::vector<TabStripEntrySnapshot> tabs = {Tab("a"), Tab("b", true, true), Tab("c"),
                                                     Tab("d", false, true)};

    const CollapsedRailTabLayout layout = BrowserChrome::CollapsedRailLayout(tabs);

    // Pinned (1, 3) first, then the rest in list order; indices stay the source indices so
    // SelectTab(index) activates the right tab.
    EXPECT_EQ(layout.order, (std::vector<std::size_t>{1, 3, 0, 2}));
    EXPECT_EQ(layout.separator_before, 2U);
}

TEST(CollapsedRailLayoutTest, GivenOneGroupOnlyWhenLaidOutThenThereIsNoSeparator) {
    const CollapsedRailTabLayout plain =
        BrowserChrome::CollapsedRailLayout({Tab("a"), Tab("b"), Tab("c")});
    EXPECT_EQ(plain.order, (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_EQ(plain.separator_before, CollapsedRailTabLayout::npos);

    const CollapsedRailTabLayout pinned =
        BrowserChrome::CollapsedRailLayout({Tab("a", false, true), Tab("b", false, true)});
    EXPECT_EQ(pinned.separator_before, CollapsedRailTabLayout::npos);

    EXPECT_TRUE(BrowserChrome::CollapsedRailLayout({}).order.empty());
}

TEST(CollapsedRailLayoutTest,
     GivenATabWhenLabelledThenTheTooltipIsTheFullTitleAndNameAnnouncesState) {
    const TabStripEntrySnapshot entry =
        Tab("A very long page title that the expanded row would truncate", true);

    EXPECT_EQ(BrowserChrome::CollapsedRailTabTooltip(entry), entry.title);
    EXPECT_EQ(BrowserChrome::TabEntryAccessibleName(entry), "Tab: " + entry.title + ", active");
}

TEST(CollapsedRailContractTest, GivenTheNewIdsWhenInspectedThenTheyFollow1047AndStayOutOfTheTree) {
    EXPECT_GT(static_cast<int>(ChromeViewId::kCollapsedRail),
              static_cast<int>(ChromeViewId::kRailSettingsButton));
    EXPECT_EQ(static_cast<int>(ChromeViewId::kCollapsedRail), 1048);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kCollapsedRailSpace), 1054);
    EXPECT_EQ(static_cast<int>(ChromeViewId::kHoverSliver), 1028);

    const ChromeViewTreeNode tree = BrowserChrome::CollectionCountContract(
        {Tab("a"), Tab("b")}, {{.color = {0xFF3B82F6}, .name = "Default", .active = true}});
    ASSERT_EQ(tree.children.size(), 3U);
    const auto contains = [&](auto&& self, const ChromeViewTreeNode& node,
                              ChromeViewId id) -> bool {
        if (node.id == id) {
            return true;
        }
        for (const ChromeViewTreeNode& child : node.children) {
            if (self(self, child, id)) {
                return true;
            }
        }
        return false;
    };
    for (const ChromeViewId id :
         {ChromeViewId::kCollapsedRail, ChromeViewId::kCollapsedRailTabList,
          ChromeViewId::kCollapsedRailTab, ChromeViewId::kCollapsedRailSpace,
          ChromeViewId::kCollapsedRailSpaceList}) {
        EXPECT_FALSE(contains(contains, tree, id));
    }
}

TEST(SpaceDotTest, GivenADotWhenRenderedThenItIsACenteredAntiAliasedCircle) {
    const ArgbColor fill = {0xFF3B82F6};
    const ArgbColor ring = {0xFF111111};

    const SpaceDotBitmap dot = RenderSpaceDot(fill, ring, /*active=*/false, 16, 2);

    ASSERT_EQ(dot.size_px, 32);
    ASSERT_EQ(dot.bgra.size(), 32U * 32U * 4U);
    const auto alpha_at = [&](int x, int y) { return dot.bgra[(y * 32 + x) * 4 + 3]; };
    EXPECT_EQ(alpha_at(16, 16), 255);  // center: opaque
    EXPECT_EQ(alpha_at(0, 0), 0);      // corner: transparent
    EXPECT_EQ(alpha_at(16, 2), 0);     // an inactive dot has no ring
    // Premultiplied BGRA of the fill at the center.
    EXPECT_EQ(dot.bgra[(16 * 32 + 16) * 4 + 0], 0xF6);
    EXPECT_EQ(dot.bgra[(16 * 32 + 16) * 4 + 2], 0x3B);
    // Some edge pixel is partially covered (anti-aliasing).
    bool partial = false;
    for (int x = 0; x < 32; ++x) {
        partial = partial || (alpha_at(x, 12) > 0 && alpha_at(x, 12) < 255);
    }
    EXPECT_TRUE(partial);
}

TEST(SpaceDotTest, GivenTheActiveSpaceWhenRenderedThenARingSurroundsTheDot) {
    const ArgbColor fill = {0xFF3B82F6};
    const ArgbColor ring = {0xFF111111};

    const SpaceDotBitmap active = RenderSpaceDot(fill, ring, /*active=*/true, 16, 2);
    const SpaceDotBitmap inactive = RenderSpaceDot(fill, ring, /*active=*/false, 16, 2);

    // At 6 DIP (12 px) from the center the active dot has opaque ring pixels, the inactive none:
    // the state is a shape, not a color.
    const auto alpha = [](const SpaceDotBitmap& bitmap, int x, int y) {
        return bitmap.bgra[(y * bitmap.size_px + x) * 4 + 3];
    };
    EXPECT_GT(alpha(active, 16 + 12, 16), 200);
    EXPECT_EQ(alpha(inactive, 16 + 12, 16), 0);
}

}  // namespace
}  // namespace island
