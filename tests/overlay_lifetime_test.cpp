#include "overlay_lifetime.h"

#include <gtest/gtest.h>

#include "cheatsheet_overlay.h"

namespace island {
namespace {

TEST(OverlayLifetimeTest, GivenANewSurfaceWhenInspectedThenNoOverlayIsExpected) {
    const OverlayLifetime lifetime;

    EXPECT_FALSE(lifetime.overlay_expected());
}

TEST(OverlayLifetimeTest, GivenAHiddenSurfaceWhenShownThenTheOverlayIsCreatedOnce) {
    OverlayLifetime lifetime;

    EXPECT_EQ(lifetime.Show(), OverlayStep::kCreate);
    EXPECT_TRUE(lifetime.overlay_expected());
    // Showing an already shown surface must not create a second overlay.
    EXPECT_EQ(lifetime.Show(), OverlayStep::kNone);
}

TEST(OverlayLifetimeTest, GivenAShownSurfaceWhenHiddenThenTheOverlayIsDestroyedOnce) {
    OverlayLifetime lifetime;
    ASSERT_EQ(lifetime.Show(), OverlayStep::kCreate);

    EXPECT_EQ(lifetime.Hide(), OverlayStep::kDestroy);
    EXPECT_FALSE(lifetime.overlay_expected());
    EXPECT_EQ(lifetime.Hide(), OverlayStep::kNone);
}

TEST(OverlayLifetimeTest, GivenAHiddenSurfaceWhenHiddenAgainThenNothingHappens) {
    OverlayLifetime lifetime;

    EXPECT_EQ(lifetime.Hide(), OverlayStep::kNone);
}

TEST(OverlayLifetimeTest, GivenRepeatedOpenAndCloseCyclesThenEachCycleCreatesAndDestroysOnce) {
    OverlayLifetime lifetime;

    for (int cycle = 0; cycle < 3; ++cycle) {
        EXPECT_EQ(lifetime.Show(), OverlayStep::kCreate) << "cycle " << cycle;
        EXPECT_TRUE(lifetime.overlay_expected());
        EXPECT_EQ(lifetime.Hide(), OverlayStep::kDestroy) << "cycle " << cycle;
        EXPECT_FALSE(lifetime.overlay_expected());
    }
}

TEST(OverlayLifetimeTest, GivenAShownSurfaceWhenDetachedThenTheOverlayIsDestroyedAndNeverReturns) {
    OverlayLifetime lifetime;
    ASSERT_EQ(lifetime.Show(), OverlayStep::kCreate);

    EXPECT_EQ(lifetime.Detach(), OverlayStep::kDestroy);
    EXPECT_FALSE(lifetime.overlay_expected());
    EXPECT_EQ(lifetime.Show(), OverlayStep::kNone);
    EXPECT_FALSE(lifetime.overlay_expected());
}

TEST(OverlayLifetimeTest, GivenAHiddenSurfaceWhenDetachedThenNoDestroyIsNeeded) {
    OverlayLifetime lifetime;

    EXPECT_EQ(lifetime.Detach(), OverlayStep::kNone);
    EXPECT_EQ(lifetime.Show(), OverlayStep::kNone);
}

// The ⌘/ cheatsheet: no live overlay may be left behind once it closes, whichever way it
// closes (the macOS mouse-input regression).
// Escape is routed by the window, not by the sheet: the cheatsheet comes first, then a peek,
// then stop-loading. The window feeds both the Escape predicate and the stop-loading command
// through this one function, so the two cannot disagree.
TEST(StopLoadingPriorityTest, GivenAnOpenCheatsheetWhenResolvedThenItWinsOverAPeekAndStop) {
    EXPECT_EQ(ResolveStopLoading(/*cheatsheet_open=*/true, /*peek_claims=*/true,
                                 /*can_stop_load=*/true),
              StopLoadingTarget::kCloseCheatsheet);
}

TEST(StopLoadingPriorityTest, GivenAnOpenCheatsheetWhenNotLoadingThenItStillWins) {
    EXPECT_EQ(ResolveStopLoading(/*cheatsheet_open=*/true, /*peek_claims=*/false,
                                 /*can_stop_load=*/false),
              StopLoadingTarget::kCloseCheatsheet);
}

TEST(StopLoadingPriorityTest, GivenAnOpenPeekWhenResolvedThenItWinsOverStop) {
    EXPECT_EQ(ResolveStopLoading(/*cheatsheet_open=*/false, /*peek_claims=*/true,
                                 /*can_stop_load=*/true),
              StopLoadingTarget::kClosePeek);
}

TEST(StopLoadingPriorityTest, GivenNothingOpenWhenLoadingThenItStopsLoading) {
    EXPECT_EQ(ResolveStopLoading(/*cheatsheet_open=*/false, /*peek_claims=*/false,
                                 /*can_stop_load=*/true),
              StopLoadingTarget::kStopLoad);
}

TEST(StopLoadingPriorityTest, GivenNothingOpenWhenNotLoadingThenNothingHappens) {
    EXPECT_EQ(ResolveStopLoading(/*cheatsheet_open=*/false, /*peek_claims=*/false,
                                 /*can_stop_load=*/false),
              StopLoadingTarget::kNone);
}

TEST(CheatsheetLifetimeTest, TheShortcutTogglesItClosedAndOpenAgain) {
    CheatsheetLifetime cheatsheet;
    for (int cycle = 0; cycle < 3; ++cycle) {
        EXPECT_EQ(cheatsheet.Toggle(), OverlayStep::kCreate) << "cycle " << cycle;
        EXPECT_TRUE(cheatsheet.visible());
        EXPECT_EQ(cheatsheet.Toggle(), OverlayStep::kDestroy) << "cycle " << cycle;
        EXPECT_FALSE(cheatsheet.overlay_expected());
    }
}

TEST(CheatsheetLifetimeTest, DetachDestroysAnOpenSheetAndItNeverReturns) {
    CheatsheetLifetime cheatsheet;
    ASSERT_EQ(cheatsheet.Toggle(), OverlayStep::kCreate);

    EXPECT_EQ(cheatsheet.Detach(), OverlayStep::kDestroy);
    EXPECT_EQ(cheatsheet.Toggle(), OverlayStep::kNone);
    EXPECT_FALSE(cheatsheet.overlay_expected());
}

// Showing the activatable overlay hands focus back to the page before the close button takes
// it; that page focus is not a click outside and must not close the sheet it just opened.
TEST(CheatsheetLifetimeTest, GivenAJustOpenedSheetWhenThePageTakesFocusThenItStaysOpen) {
    CheatsheetLifetime cheatsheet;
    ASSERT_EQ(cheatsheet.Show(), OverlayStep::kCreate);

    EXPECT_FALSE(cheatsheet.FocusLeftCloses());
    EXPECT_TRUE(cheatsheet.visible());
}

TEST(CheatsheetLifetimeTest, GivenAFocusedSheetWhenFocusLeavesThenItClosesOnce) {
    CheatsheetLifetime cheatsheet;
    ASSERT_EQ(cheatsheet.Show(), OverlayStep::kCreate);
    cheatsheet.FocusTaken();

    EXPECT_TRUE(cheatsheet.FocusLeftCloses());
    // The close button's blur and the page's focus both report the same click.
    EXPECT_FALSE(cheatsheet.FocusLeftCloses());
}

TEST(CheatsheetLifetimeTest, GivenAReopenedSheetWhenThePageTakesFocusEarlyThenItStaysOpen) {
    CheatsheetLifetime cheatsheet;
    ASSERT_EQ(cheatsheet.Show(), OverlayStep::kCreate);
    cheatsheet.FocusTaken();
    ASSERT_EQ(cheatsheet.Hide(), OverlayStep::kDestroy);
    ASSERT_EQ(cheatsheet.Show(), OverlayStep::kCreate);

    EXPECT_FALSE(cheatsheet.FocusLeftCloses());
}

TEST(CheatsheetLifetimeTest, GivenAClosedSheetWhenFocusIsTakenThenNothingCloses) {
    CheatsheetLifetime cheatsheet;
    cheatsheet.FocusTaken();

    EXPECT_FALSE(cheatsheet.FocusLeftCloses());
}

TEST(CheatsheetOverlayTest, TheCardSitsInTheBottomRightOfThePageCard) {
    const DipRect card =
        CheatsheetOverlay::CardBounds({.x = 300, .y = 50, .width = 900, .height = 700},
                                      /*content_height=*/400);
    EXPECT_EQ(card.width, CheatsheetOverlay::kContentWidthDip);
    EXPECT_EQ(card.width, 320);
    EXPECT_EQ(card.x + card.width, 300 + 900 - CheatsheetOverlay::kAnchorInsetDip);
    EXPECT_EQ(card.y + card.height, 50 + 700 - CheatsheetOverlay::kAnchorInsetDip);
    EXPECT_EQ(card.height, 400);
}

TEST(CheatsheetOverlayTest, AShortPageCardKeepsTheCardInsideIt) {
    const DipRect card =
        CheatsheetOverlay::CardBounds({.x = 0, .y = 40, .width = 200, .height = 150},
                                      /*content_height=*/400);
    EXPECT_GE(card.x, 0 + CheatsheetOverlay::kAnchorInsetDip);
    EXPECT_GE(card.y, 40 + CheatsheetOverlay::kAnchorInsetDip);
    EXPECT_LE(card.y + card.height, 40 + 150 - CheatsheetOverlay::kAnchorInsetDip);
    EXPECT_LE(card.x + card.width, 200 - CheatsheetOverlay::kAnchorInsetDip);
}

}  // namespace
}  // namespace island
