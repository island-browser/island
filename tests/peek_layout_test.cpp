#include "peek_layout.h"

#include <gtest/gtest.h>

namespace island {
namespace {

TEST(PeekLayoutTest, GivenALargeContentAreaThenTheCardIs80PercentByAnd85PercentCentered) {
    const DipRect card = PeekCardBounds({.x = 0, .y = 40, .width = 1440, .height = 900});

    EXPECT_EQ(card, (DipRect{.x = 144, .y = 107, .width = 1152, .height = 765}));
}

TEST(PeekLayoutTest, GivenAWindowResizeThenTheCardFollowsTheNewContentArea) {
    const DipRect before = PeekCardBounds({.x = 0, .y = 40, .width = 1440, .height = 900});
    const DipRect after = PeekCardBounds({.x = 0, .y = 40, .width = 1000, .height = 700});

    EXPECT_NE(before, after);
    EXPECT_EQ(after.width, 800);
    EXPECT_EQ(after.height, 595);
    EXPECT_EQ(after.x, 100);
}

TEST(PeekLayoutTest, GivenAContentAreaNarrowerThanTheMinimumThenTheCardFillsItsWidth) {
    const DipRect card = PeekCardBounds({.x = 0, .y = 0, .width = 300, .height = 500});

    EXPECT_EQ(card.width, 300);
    EXPECT_EQ(card.x, 0);
}

TEST(PeekLayoutTest, GivenAMediumContentAreaThenTheMinimumsApply) {
    // 80% of 400 is 320, below the 360 minimum width; 85% of 300 is 255, below 280.
    const DipRect card = PeekCardBounds({.x = 0, .y = 0, .width = 400, .height = 300});

    EXPECT_EQ(card.width, 360);
    EXPECT_EQ(card.height, 280);
}

TEST(PeekLayoutTest, GivenATinyContentAreaThenTheCardNeverExceedsIt) {
    const DipRect card = PeekCardBounds({.x = 10, .y = 20, .width = 200, .height = 100});

    EXPECT_EQ(card, (DipRect{.x = 10, .y = 20, .width = 200, .height = 100}));
}

TEST(PeekLayoutTest, GivenANegativeContentAreaThenTheCardIsEmpty) {
    const DipRect card = PeekCardBounds({.x = 0, .y = 0, .width = -5, .height = -5});

    EXPECT_EQ(card.width, 0);
    EXPECT_EQ(card.height, 0);
}

}  // namespace
}  // namespace island
