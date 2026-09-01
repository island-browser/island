#include "sidebar_state.h"

#include <gtest/gtest.h>

namespace island {
namespace {

constexpr int kRailWidth = 286;

TEST(SidebarStateTest, GivenANewSidebarWhenInspectedThenItMatchesTheDefaultAndIsHidden) {
    const SidebarState state;

    EXPECT_EQ(state.pinned(), kSidebarRevealedByDefault);
    EXPECT_FALSE(state.revealed());
    EXPECT_FALSE(state.hover_revealed());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), 0);
    EXPECT_TRUE(state.sliver_visible());
}

TEST(SidebarStateTest, GivenAHiddenSidebarWhenToggledThenItRevealsAtFullRailWidth) {
    SidebarState state;

    state.Toggle();

    EXPECT_TRUE(state.pinned());
    EXPECT_TRUE(state.revealed());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), kRailWidth);
    EXPECT_FALSE(state.sliver_visible());

    state.Toggle();

    EXPECT_FALSE(state.revealed());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), 0);
}

TEST(SidebarStateTest, GivenTheEdgeBandWhenEnteredThenHoverReveals) {
    SidebarState state;

    state.OnPointerMoved(kHoverRevealBandDip, kRailWidth);
    EXPECT_FALSE(state.revealed());

    state.OnPointerMoved(kHoverRevealBandDip - 1, kRailWidth);
    EXPECT_TRUE(state.revealed());
    EXPECT_TRUE(state.hover_revealed());
    EXPECT_FALSE(state.pinned());
}

TEST(SidebarStateTest, GivenAHoverRevealWhenThePointerLeavesTheGraceBandThenItHides) {
    SidebarState state;
    state.OnPointerMoved(0, kRailWidth);
    ASSERT_TRUE(state.revealed());

    state.OnPointerMoved(kRailWidth + kHoverGraceBandDip, kRailWidth);
    EXPECT_TRUE(state.revealed());

    state.OnPointerMoved(kRailWidth + kHoverGraceBandDip + 1, kRailWidth);
    EXPECT_FALSE(state.revealed());
}

TEST(SidebarStateTest, GivenRepeatedHoverMovesWhenAppliedThenTheStateIsIdempotent) {
    SidebarState state;

    for (int repeat = 0; repeat < 5; ++repeat) {
        state.OnPointerMoved(3, kRailWidth);
        EXPECT_TRUE(state.revealed());
    }
    for (int repeat = 0; repeat < 5; ++repeat) {
        state.OnPointerMoved(900, kRailWidth);
        EXPECT_FALSE(state.revealed());
    }
}

TEST(SidebarStateTest, GivenAPinnedSidebarWhenHoveredAnywhereThenTheToggleStateWins) {
    SidebarState state;
    state.Toggle();
    ASSERT_TRUE(state.pinned());

    state.OnPointerMoved(900, kRailWidth);

    EXPECT_TRUE(state.revealed()) << "hover must never collapse a pinned sidebar";
    EXPECT_FALSE(state.hover_revealed());
}

TEST(SidebarStateTest, GivenAHoverRevealWhenToggledOffThenTheHoverRevealIsClearedToo) {
    SidebarState state;
    state.OnPointerMoved(0, kRailWidth);
    ASSERT_TRUE(state.hover_revealed());

    // The first Cmd/Ctrl+B pins it, the second collapses it outright.
    state.Toggle();
    EXPECT_TRUE(state.revealed());
    state.Toggle();

    EXPECT_FALSE(state.revealed());
    EXPECT_FALSE(state.hover_revealed());
}

TEST(SidebarStateTest, GivenAHoverRevealWhenThePointerLeavesTheWindowThenItHides) {
    SidebarState state;
    state.OnPointerMoved(0, kRailWidth);
    ASSERT_TRUE(state.revealed());

    state.OnPointerLeft();

    EXPECT_FALSE(state.revealed());
}

TEST(SidebarStateTest, GivenAPinnedSidebarWhenThePointerLeavesThenItStaysRevealed) {
    SidebarState state(/*pinned=*/true);

    state.OnPointerLeft();

    EXPECT_TRUE(state.revealed());
}

TEST(SidebarStateTest, GivenTheBandConstantsWhenInspectedThenTheyMatchTheSpec) {
    EXPECT_EQ(kHoverRevealBandDip, 12);
    EXPECT_EQ(kHoverGraceBandDip, 16);
    EXPECT_GE(kHoverSliverWidthDip, 1);
    EXPECT_LE(kHoverSliverWidthDip, 2);
}

}  // namespace
}  // namespace island
