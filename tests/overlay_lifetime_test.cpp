#include "overlay_lifetime.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace island
