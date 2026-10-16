#include "browser_view_park.h"

#include <gtest/gtest.h>

namespace island {
namespace {

TEST(BrowserViewParkTest, GivenAHiddenViewStillInTheContentSlotWhenItsTimerFiresThenItIsRemoved) {
    EXPECT_TRUE(ShouldRemoveParkedView(/*visible=*/false, /*in_content=*/true));
}

TEST(BrowserViewParkTest, GivenAViewShownAgainWhenItsTimerFiresThenItStays) {
    // A superseded view that became the attached view again must never be removed under it.
    EXPECT_FALSE(ShouldRemoveParkedView(/*visible=*/true, /*in_content=*/true));
}

TEST(BrowserViewParkTest, GivenAViewAlreadyOutOfTheContentSlotWhenItsTimerFiresThenNothingHappens) {
    EXPECT_FALSE(ShouldRemoveParkedView(/*visible=*/false, /*in_content=*/false));
}

}  // namespace
}  // namespace island
