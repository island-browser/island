#include "sidebar_collection.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>

#include "design_tokens.h"

namespace island {
namespace {

// The inner width of each look's sidebar: rail width less the 12-DIP side gutters and the 10-DIP
// scroll gutter (BrowserChrome::TabListScrollGutterDip). Native comfortable 248, Native compact
// 200, Arc and Graphite 286.
constexpr int kNativeComfortableStrip = 248 - 2 * 12 - 10;
constexpr int kNativeCompactStrip = 200 - 2 * 12 - 10;
constexpr int kArcStrip = 286 - 2 * 12 - 10;

TEST(GlobalStrip, GivenSixOrFewerFavoritesThenAllIconsShowAndNoMoreControl) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(6, kNativeComfortableStrip);

    EXPECT_EQ(layout.icons, 6u);
    EXPECT_EQ(layout.hidden, 0u);
}

TEST(GlobalStrip, GivenEightFavoritesThenSixIconsShowAndPlusTwoFollows) {
    // Ruling a: every look shows exactly min(6, count) icons, then "+N".
    for (const int width : {kNativeComfortableStrip, kArcStrip}) {
        const GlobalStripLayout layout = ComputeGlobalStripLayout(8, width);

        EXPECT_EQ(layout.icons, 6u) << "width " << width;
        EXPECT_EQ(layout.hidden, 2u) << "width " << width;
    }
}

TEST(GlobalStrip, GivenALookWithRoomThenThePlateStaysAt28AndTheGapAt4) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(8, kArcStrip);

    // 6 icons + "+2" = 7 controls: 7 * 28 + 6 * 4 = 220 <= 252.
    EXPECT_EQ(layout.plate_dip, 28);
    EXPECT_EQ(layout.gap_dip, 4);
}

TEST(GlobalStrip, GivenNativeComfortableThenTheGapShrinksBeforeTheIconCountDrops) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(8, kNativeComfortableStrip);

    // 7 controls at a 28-DIP plate need 7 * 28 + 6 * 3 = 214 DIP: the gap gives way, the plate
    // holds, and the icon count is unchanged.
    EXPECT_EQ(layout.icons, 6u);
    EXPECT_EQ(layout.plate_dip, 28);
    EXPECT_EQ(layout.gap_dip, 3);
    EXPECT_LE(7 * layout.plate_dip + 6 * layout.gap_dip, kNativeComfortableStrip);
}

TEST(GlobalStrip, GivenAnyFitThenThePlateAndGapStayWithinTheirFloorsAndCeilings) {
    for (std::size_t count = 1; count <= 12; ++count) {
        for (const int width : {kNativeCompactStrip, kNativeComfortableStrip, kArcStrip}) {
            const GlobalStripLayout layout = ComputeGlobalStripLayout(count, width);
            EXPECT_GE(layout.plate_dip, 24) << count << " favorites, width " << width;
            EXPECT_LE(layout.plate_dip, 28) << count << " favorites, width " << width;
            EXPECT_GE(layout.gap_dip, 2) << count << " favorites, width " << width;
            EXPECT_LE(layout.gap_dip, 4) << count << " favorites, width " << width;
        }
    }
}

TEST(GlobalStrip, GivenNativeCompactThenTheSixPlusNFloorsDoNotFitAndFewerIconsShow) {
    // Open decision (see REPORT): 6 icons + "+N" need 7 * 24 + 6 * 2 = 180 DIP at the floors,
    // and Native compact has 166. The strip keeps the floors and shows the largest count that
    // fits, 5 icons + "+3".
    const GlobalStripLayout layout = ComputeGlobalStripLayout(8, kNativeCompactStrip);

    EXPECT_EQ(layout.icons, 5u);
    EXPECT_EQ(layout.hidden, 3u);
    EXPECT_GE(layout.plate_dip, 24);
    EXPECT_LE(6 * layout.plate_dip + 5 * layout.gap_dip, kNativeCompactStrip);
}

TEST(GlobalStrip, GivenNoFavoritesThenTheStripHasNoIconsAndNoOverflow) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(0, kArcStrip);

    EXPECT_EQ(layout.icons, 0u);
    EXPECT_EQ(layout.hidden, 0u);
}

TEST(GlobalStrip, GivenNoRoomAtAllThenNoIconsAndTheOverflowStaysReachable) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(3, 10);

    EXPECT_EQ(layout.icons, 0u);
    EXPECT_EQ(layout.hidden, 3u);
}

TEST(SidebarAccessibleNames, UseTheSpecWording) {
    // Spec §6: the name carries the title plus its position and total.
    EXPECT_EQ(GlobalFavoriteAccessibleName("Island", 2, 8), "Global favorite: Island (2 of 8)");
    EXPECT_EQ(MoreGlobalFavoritesAccessibleName(2), "Show 2 more global favorites");
    EXPECT_EQ(MoreGlobalFavoritesLabel(2), "+2");
    EXPECT_EQ(std::string(AddGlobalFavoriteAccessibleName()),
              "Add current page as a global favorite");
    EXPECT_EQ(SpaceFavoriteAccessibleName("Project home"), "Favorite: Project home");
}

TEST(SidebarHeaders, GlobalHeaderFollowsEachLook) {
    EXPECT_EQ(GlobalFavoritesHeaderTitle(LookPreset::kNative), "Global favorites");
    EXPECT_EQ(GlobalFavoritesHeaderTitle(LookPreset::kArc), "Favorites");
    EXPECT_EQ(GlobalFavoritesHeaderTitle(LookPreset::kGraphite), "Global");
}

TEST(SidebarHeaders, SpaceFavoritesHeaderIsTheSpaceNameDotFavoritesExceptGraphite) {
    EXPECT_EQ(SpaceFavoritesHeaderTitle(LookPreset::kNative, "Work"), "Work \xC2\xB7 Favorites");
    EXPECT_EQ(SpaceFavoritesHeaderTitle(LookPreset::kArc, "Work"), "Work \xC2\xB7 Favorites");
    // Spec §3.1 table: Graphite's space favorites read "Favorites".
    EXPECT_EQ(SpaceFavoritesHeaderTitle(LookPreset::kGraphite, "Work"), "Favorites");
    EXPECT_EQ(SpaceFavoritesHeaderTitle(LookPreset::kNative, ""), "Favorites");
}

TEST(SidebarHeaders, TabsHeaderNamesTheSpaceOnlyInNative) {
    EXPECT_EQ(TabsHeaderTitle(LookPreset::kNative, "Work"), "Work \xC2\xB7 Tabs");
    EXPECT_EQ(TabsHeaderTitle(LookPreset::kNative, ""), "Tabs");
    // Ruling c: Arc and Graphite show a small "Tabs" label.
    EXPECT_EQ(TabsHeaderTitle(LookPreset::kArc, "Work"), "Tabs");
    EXPECT_EQ(TabsHeaderTitle(LookPreset::kGraphite, "Work"), "Tabs");
}

TEST(SidebarHeaders, NativeAlwaysHeadsTheTabsBeforeTheirFirstRow) {
    EXPECT_TRUE(TabsHeaderBeforeFirstRow(LookPreset::kNative, /*favorites_precede=*/false,
                                         /*has_tabs=*/true));
    EXPECT_TRUE(TabsHeaderBeforeFirstRow(LookPreset::kNative, /*favorites_precede=*/true,
                                         /*has_tabs=*/true));
}

TEST(SidebarHeaders, ArcAndGraphiteHeadTheTabsOnlyWhenFavoritesPrecedeThem) {
    for (const LookPreset look : {LookPreset::kArc, LookPreset::kGraphite}) {
        EXPECT_FALSE(TabsHeaderBeforeFirstRow(look, /*favorites_precede=*/false,
                                              /*has_tabs=*/true));
        EXPECT_TRUE(TabsHeaderBeforeFirstRow(look, /*favorites_precede=*/true,
                                             /*has_tabs=*/true));
    }
}

TEST(SidebarHeaders, NoLookEverShowsATrailingTabsHeaderOverNoRows) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const bool favorites : {false, true}) {
            EXPECT_FALSE(TabsHeaderBeforeFirstRow(look, favorites, /*has_tabs=*/false));
        }
    }
}

TEST(SidebarRows, SpaceFavoriteRowsFollowSpecSection31Heights) {
    // Native: 28 compact, 34 comfortable. Arc and Graphite: 32 in both densities.
    EXPECT_EQ(SpaceFavoriteRowHeightDip(LookPreset::kNative, Density::kCompact), 28);
    EXPECT_EQ(SpaceFavoriteRowHeightDip(LookPreset::kNative, Density::kComfortable), 34);
    for (const LookPreset look : {LookPreset::kArc, LookPreset::kGraphite}) {
        EXPECT_EQ(SpaceFavoriteRowHeightDip(look, Density::kCompact), 32);
        EXPECT_EQ(SpaceFavoriteRowHeightDip(look, Density::kComfortable), 32);
    }
}

TEST(FavoriteFill, GivenTheDisplayedFavoriteThenItIsNeutralRaisedNeverTheAccent) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);

            const ArgbColor displayed = FavoriteFillColor(tokens, /*displayed=*/true,
                                                          /*hovered=*/false);
            EXPECT_EQ(displayed.argb, tokens.segment_selected.argb);
            EXPECT_NE(displayed.argb, tokens.accent.argb);
            EXPECT_EQ(FavoriteFillColor(tokens, /*displayed=*/true, /*hovered=*/true).argb,
                      tokens.segment_selected.argb);
        }
    }
}

TEST(FavoriteFill, GivenAnOrdinaryFavoriteThenHoverUsesTheHoverFillAndRestIsTheSurface) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kArc, ChromeTheme::kLight);

    EXPECT_EQ(FavoriteFillColor(tokens, false, true).argb, tokens.hover.argb);
    EXPECT_EQ(FavoriteFillColor(tokens, false, false).argb, tokens.canvas.argb);
}

TEST(SectionDecisions, GivenTheAddControlAloneThenTheHeaderRowShowsButNoStrip) {
    const GlobalStripLayout empty = ComputeGlobalStripLayout(0, kArcStrip);

    EXPECT_TRUE(GlobalHeaderRowVisible(empty, /*add_visible=*/true));
    EXPECT_FALSE(GlobalStripVisible(empty));
    EXPECT_FALSE(GlobalHeaderRowVisible(empty, /*add_visible=*/false));
}

TEST(SectionDecisions, GivenIconsOrOverflowThenTheStripAndHeaderRowShow) {
    const GlobalStripLayout icons = ComputeGlobalStripLayout(2, kArcStrip);
    const GlobalStripLayout overflow = ComputeGlobalStripLayout(8, kArcStrip);

    EXPECT_TRUE(GlobalStripVisible(icons));
    EXPECT_TRUE(GlobalStripVisible(overflow));
    EXPECT_TRUE(GlobalHeaderRowVisible(icons, /*add_visible=*/false));
}

TEST(SectionDecisions, GivenOnlyTheAddControlThenItIsNotAFavorite) {
    // Ruling c: "+" does not make the tabs header a scope boundary; a favorite does.
    EXPECT_FALSE(FavoritesPrecedeTabs(0, 0));
    EXPECT_TRUE(FavoritesPrecedeTabs(1, 0));
    EXPECT_TRUE(FavoritesPrecedeTabs(0, 1));
}

TEST(SectionDecisions, GivenGlobalsThenTheRailHairlineNeedsSomethingBelowThem) {
    EXPECT_FALSE(CollapsedRailSeparatorVisible(0, /*below=*/true));
    EXPECT_FALSE(CollapsedRailSeparatorVisible(2, /*below=*/false));
    EXPECT_TRUE(CollapsedRailSeparatorVisible(2, /*below=*/true));
}

TEST(TabRowFill, GivenAShownFavoriteThenTheSelectedTabLosesTheAccentWhileItStaysSelected) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);

    const std::optional<ArgbColor> behind =
        TabRowFill(tokens, /*active=*/true, /*pinned=*/false, /*hovered=*/false,
                   /*behind_favorite=*/true);
    ASSERT_TRUE(behind.has_value());
    EXPECT_EQ(behind->argb, tokens.segment_selected.argb);
    EXPECT_NE(behind->argb, tokens.selection.argb);

    const std::optional<ArgbColor> selected =
        TabRowFill(tokens, /*active=*/true, /*pinned=*/false, /*hovered=*/false,
                   /*behind_favorite=*/false);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->argb, tokens.selection.argb);
}

TEST(BookmarkStatus, GivenTheFailureRowThenItsSurfaceIsTheWarningSurface) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kDark);
    const BookmarkStatusColors colors = BookmarkStatusRowColors(tokens);

    EXPECT_EQ(colors.row.argb, tokens.warning_bg.argb);
    EXPECT_EQ(colors.retry.argb, tokens.text.argb);
}

}  // namespace
}  // namespace island
