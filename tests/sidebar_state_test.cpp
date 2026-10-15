#include "sidebar_state.h"

#include <gtest/gtest.h>

namespace island {
namespace {

constexpr int kRailWidth = 286;

TEST(SidebarStateTest, GivenANewSidebarWhenInspectedThenItIsExpandedAtFullWidth) {
    const SidebarState state;

    EXPECT_EQ(state.persistent_expanded(), kSidebarExpandedByDefault);
    EXPECT_TRUE(state.expanded());
    EXPECT_FALSE(state.transient_expanded());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), kRailWidth);
}

TEST(SidebarStateTest, GivenACollapsedSidebarWhenMeasuredThenItKeepsTheIconRailWidth) {
    const SidebarState state(/*expanded=*/false);

    EXPECT_FALSE(state.expanded());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), kCollapsedRailWidthDip);
    EXPECT_EQ(kCollapsedRailWidthDip, 52);
    // A window narrower than the icon rail never reports more than it has.
    EXPECT_EQ(state.RailWidthDip(30), 30);
}

TEST(SidebarStateTest, GivenAToggleWhenAppliedThenItFlipsBetweenExpandedAndCollapsed) {
    SidebarState state;

    state.Toggle();
    EXPECT_FALSE(state.expanded());
    EXPECT_FALSE(state.persistent_expanded());

    state.Toggle();
    EXPECT_TRUE(state.expanded());
    EXPECT_TRUE(state.persistent_expanded());
}

TEST(SidebarStateTest, GivenACollapsedSidebarWhenTheAddressIsEditedThenItExpandsTransiently) {
    SidebarState state(/*expanded=*/false);

    EXPECT_TRUE(state.BeginTransientExpand());
    EXPECT_TRUE(state.expanded());
    EXPECT_TRUE(state.transient_expanded());
    // The persistent choice (the one saved in the preferences) is untouched.
    EXPECT_FALSE(state.persistent_expanded());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), kRailWidth);

    EXPECT_TRUE(state.EndTransientExpand());
    EXPECT_FALSE(state.expanded());
    EXPECT_EQ(state.RailWidthDip(kRailWidth), kCollapsedRailWidthDip);
}

TEST(SidebarStateTest, GivenAnExpandedSidebarWhenTheAddressIsEditedThenNothingChanges) {
    SidebarState state;

    EXPECT_FALSE(state.BeginTransientExpand());
    EXPECT_FALSE(state.transient_expanded());
    EXPECT_FALSE(state.EndTransientExpand());
    EXPECT_TRUE(state.expanded());
}

TEST(SidebarStateTest, GivenATransientExpansionWhenToggledThenItCollapsesAndStaysCollapsed) {
    SidebarState state(/*expanded=*/false);
    ASSERT_TRUE(state.BeginTransientExpand());

    state.Toggle();

    EXPECT_FALSE(state.expanded());
    EXPECT_FALSE(state.transient_expanded());
    EXPECT_FALSE(state.persistent_expanded());
}

TEST(SidebarStateTest, GivenASetExpandedWhenAppliedThenAnyTransientExpansionEnds) {
    SidebarState state(/*expanded=*/false);
    ASSERT_TRUE(state.BeginTransientExpand());

    state.SetExpanded(true);

    EXPECT_TRUE(state.persistent_expanded());
    EXPECT_FALSE(state.transient_expanded());
}

}  // namespace
}  // namespace island
