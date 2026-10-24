#include "sidebar_collection.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "design_tokens.h"

namespace island {
namespace {

constexpr int kPlate = 28;
constexpr int kGap = 4;

TEST(GlobalStrip, GivenSixOrFewerFavoritesWhenWideEnoughThenAllIconsShow) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(6, 252, kPlate, kGap, true);

    // 6 icons + "+" = 7 controls = 7*28 + 6*4 = 220 DIP; no "+N" when nothing is hidden.
    EXPECT_EQ(layout, (GlobalStripLayout{.icons = 6, .hidden = 0, .add_visible = true}));
}

TEST(GlobalStrip, GivenEightFavoritesInAWideSidebarThenSixShowAndPlusTwo) {
    // Arc and Graphite (286 DIP): inner width 286 - 2*12 - 10 = 252 fits 8 controls exactly.
    const GlobalStripLayout layout = ComputeGlobalStripLayout(8, 252, kPlate, kGap, true);

    EXPECT_EQ(layout, (GlobalStripLayout{.icons = 6, .hidden = 2, .add_visible = true}));
}

TEST(GlobalStrip, GivenANarrowNativeSidebarThenFewerIconsAndALargerOverflow) {
    // Native comfortable (248 DIP): inner width 214 cannot hold 6 icons + "+N" + "+" (252 DIP).
    const GlobalStripLayout comfortable = ComputeGlobalStripLayout(8, 214, kPlate, kGap, true);
    EXPECT_EQ(comfortable, (GlobalStripLayout{.icons = 4, .hidden = 4, .add_visible = true}));

    // Native compact (200 DIP): inner width 166.
    const GlobalStripLayout compact = ComputeGlobalStripLayout(8, 166, kPlate, kGap, true);
    EXPECT_EQ(compact, (GlobalStripLayout{.icons = 3, .hidden = 5, .add_visible = true}));
}

TEST(GlobalStrip, GivenNoAcceptablePageThenTheAddControlIsHidden) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(2, 252, kPlate, kGap, false);

    EXPECT_EQ(layout, (GlobalStripLayout{.icons = 2, .hidden = 0, .add_visible = false}));
}

TEST(GlobalStrip, GivenNoFavoritesThenOnlyTheAddControlCanShow) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(0, 252, kPlate, kGap, true);

    EXPECT_EQ(layout, (GlobalStripLayout{.icons = 0, .hidden = 0, .add_visible = true}));
}

TEST(GlobalStrip, GivenNoRoomAtAllThenNoIconsAndTheOverflowStaysReachable) {
    const GlobalStripLayout layout = ComputeGlobalStripLayout(3, 10, kPlate, kGap, true);

    EXPECT_EQ(layout.icons, 0u);
    EXPECT_EQ(layout.hidden, 3u);
}

TEST(SidebarAccessibleNames, UseTheSpecWording) {
    EXPECT_EQ(GlobalFavoriteAccessibleName("Island"), "Global favorite: Island");
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

TEST(SidebarHeaders, SpaceHeaderIsTheSpaceNameDotFavorites) {
    EXPECT_EQ(SpaceFavoritesHeaderTitle("Work"), "Work \xC2\xB7 Favorites");
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

}  // namespace
}  // namespace island
