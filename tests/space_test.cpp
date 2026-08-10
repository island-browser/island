#include "space.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "design_tokens.h"
#include "tab.h"
#include "tab_id.h"

namespace island {
namespace {

constexpr SpaceColor kSpaceAccent{0xFF4488FF};

Space MakeSpace(std::string name = "Work") {
    return Space(NextSpaceId(), std::move(name), kSpaceAccent);
}

Tab MakeTab() { return Tab(NextTabId()); }

std::vector<TabId> TabIds(const Space& space) {
    std::vector<TabId> ids;
    for (const Tab& tab : space.tabs()) {
        ids.push_back(tab.id());
    }
    return ids;
}

TEST(SpaceIdGenerator, IssuesStrictlyIncreasingUniqueIds) {
    const SpaceId first = NextSpaceId();
    const SpaceId second = NextSpaceId();
    const SpaceId third = NextSpaceId();

    EXPECT_LT(first.value, second.value);
    EXPECT_LT(second.value, third.value);
    EXPECT_NE(first, second);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
}

TEST(SpaceIdGenerator, NeverReusesAnIssuedId) {
    const SpaceId reference = NextSpaceId();

    for (int allocation = 0; allocation < 100; ++allocation) {
        EXPECT_GT(NextSpaceId().value, reference.value);
    }

    EXPECT_GT(NextSpaceId().value, reference.value);
}

TEST(Space, HoldsItsIdentityNameAndAccentColor) {
    const SpaceId id = NextSpaceId();
    const Space space(id, "Research", kSpaceAccent);

    EXPECT_EQ(space.id(), id);
    EXPECT_EQ(space.name(), "Research");
    EXPECT_EQ(space.color(), kSpaceAccent);
    EXPECT_EQ(space.tab_count(), 0U);
    EXPECT_FALSE(space.has_active_tab());
    EXPECT_FALSE(space.split().has_value());
}

TEST(Space, RenamesAndRecolors) {
    Space space = MakeSpace();

    space.Rename("Travel");
    space.SetColor(ArgbColor{0xFF00FF00U});

    EXPECT_EQ(space.name(), "Travel");
    EXPECT_EQ(space.color(), ArgbColor{0xFF00FF00U});
}

TEST(Space, AppendedTabBecomesTheActiveTab) {
    Space space = MakeSpace();

    Tab first = MakeTab();
    const TabId first_id = first.id();
    space.AppendTab(std::move(first));

    ASSERT_TRUE(space.has_active_tab());
    EXPECT_EQ(space.active_tab_index(), 0U);
    EXPECT_EQ(space.active_tab_id(), first_id);

    Tab second = MakeTab();
    const TabId second_id = second.id();
    space.AppendTab(std::move(second));

    EXPECT_EQ(space.tab_count(), 2U);
    EXPECT_EQ(space.active_tab_index(), 1U);
    EXPECT_EQ(space.active_tab_id(), second_id);
}

TEST(Space, FindsTabsByIdentityNotJustPosition) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    const TabId first_id = first.id();
    space.AppendTab(std::move(first));

    ASSERT_NE(space.FindTab(first_id), nullptr);
    EXPECT_EQ(space.FindTab(first_id)->id(), first_id);
    EXPECT_EQ(space.IndexOfTab(first_id), 0U);

    EXPECT_EQ(space.FindTab(TabId{}), nullptr);
    EXPECT_FALSE(space.IndexOfTab(TabId{}).has_value());
}

TEST(Space, SelectsTabsByIdAndIndex) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));

    EXPECT_TRUE(space.SelectTab(first_id));
    EXPECT_EQ(space.active_tab_id(), first_id);

    EXPECT_TRUE(space.SelectTabIndex(1U));
    EXPECT_EQ(space.active_tab_id(), second_id);

    EXPECT_FALSE(space.SelectTab(TabId{}));
    EXPECT_FALSE(space.SelectTabIndex(3U));
    EXPECT_EQ(space.active_tab_id(), second_id);
}

TEST(Space, SelectNextAndPreviousWrapAround) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_EQ(space.active_tab_id(), third_id);

    space.SelectPreviousTab();
    EXPECT_EQ(space.active_tab_id(), second_id);

    space.SelectNextTab();
    EXPECT_EQ(space.active_tab_id(), third_id);

    space.SelectNextTab();
    EXPECT_EQ(space.active_tab_id(), first_id);

    space.SelectPreviousTab();
    EXPECT_EQ(space.active_tab_id(), third_id);
}

TEST(Space, NextAndPreviousAreNoOpsForASingleTab) {
    Space space = MakeSpace();
    Tab only = MakeTab();
    const TabId only_id = only.id();
    space.AppendTab(std::move(only));

    space.SelectNextTab();
    space.SelectPreviousTab();

    EXPECT_EQ(space.active_tab_id(), only_id);
    EXPECT_EQ(space.active_tab_index(), 0U);
}

TEST(Space, RemovingAnEarlierTabKeepsTheSameActiveTab) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_TRUE(space.SelectTabIndex(1U));

    ASSERT_TRUE(space.RemoveTab(first_id).has_value());

    EXPECT_EQ(space.tab_count(), 2U);
    EXPECT_EQ(space.active_tab_index(), 0U);
    EXPECT_EQ(space.active_tab_id(), second_id);
    EXPECT_EQ(TabIds(space), (std::vector<TabId>{second_id, third_id}));
}

TEST(Space, RemovingTheActiveTabSelectsItsRightNeighbor) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_TRUE(space.SelectTabIndex(1U));

    ASSERT_TRUE(space.RemoveTab(second_id).has_value());

    EXPECT_EQ(space.active_tab_index(), 1U);
    EXPECT_EQ(space.active_tab_id(), third_id);
    EXPECT_EQ(TabIds(space), (std::vector<TabId>{first_id, third_id}));
}

TEST(Space, RemovingTheActiveLastTabSelectsItsLeftNeighbor) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_EQ(space.active_tab_id(), third_id);

    ASSERT_TRUE(space.RemoveTab(third_id).has_value());

    EXPECT_EQ(space.active_tab_index(), 1U);
    EXPECT_EQ(space.active_tab_id(), second_id);
}

TEST(Space, RemovingTheOnlyTabLeavesTheSpaceWithoutAnActiveTab) {
    Space space = MakeSpace();
    Tab only = MakeTab();
    const TabId only_id = only.id();
    space.AppendTab(std::move(only));

    ASSERT_TRUE(space.RemoveTab(only_id).has_value());

    EXPECT_EQ(space.tab_count(), 0U);
    EXPECT_FALSE(space.has_active_tab());
}

TEST(Space, RemovingAnUnknownTabChangesNothing) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    const TabId first_id = first.id();
    space.AppendTab(std::move(first));

    EXPECT_FALSE(space.RemoveTab(TabId{}).has_value());

    EXPECT_EQ(space.tab_count(), 1U);
    EXPECT_EQ(space.active_tab_id(), first_id);
}

TEST(Space, RemovedTabKeepsItsOwnStateWhenReturned) {
    Space space = MakeSpace();
    Tab tab = MakeTab();
    const TabId id = tab.id();
    tab.navigation_state().OnLoadStart("https://example.test/kept");
    space.AppendTab(std::move(tab));

    std::optional<Tab> removed = space.RemoveTab(id);

    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->id(), id);
    EXPECT_EQ(removed->navigation_state().snapshot().url, "https://example.test/kept");
}

TEST(Space, ReorderingKeepsTabIdentiesAndTheActiveTab) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_TRUE(space.SelectTabIndex(1U));

    ASSERT_TRUE(space.MoveTab(0U, 2U));

    EXPECT_EQ(TabIds(space), (std::vector<TabId>{second_id, third_id, first_id}));
    EXPECT_EQ(space.active_tab_id(), second_id);
    EXPECT_EQ(space.active_tab_index(), 0U);
}

TEST(Space, MovingTheActiveTabKeepsItActive) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_EQ(space.active_tab_id(), third_id);

    ASSERT_TRUE(space.MoveTab(2U, 0U));

    EXPECT_EQ(TabIds(space), (std::vector<TabId>{third_id, first_id, second_id}));
    EXPECT_EQ(space.active_tab_id(), third_id);
    EXPECT_EQ(space.active_tab_index(), 0U);
}

TEST(Space, RejectsOutOfBoundsMovesLeavingOrderUnchanged) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));

    EXPECT_FALSE(space.MoveTab(0U, 2U));
    EXPECT_FALSE(space.MoveTab(2U, 0U));

    EXPECT_EQ(TabIds(space), (std::vector<TabId>{first_id, second_id}));
    EXPECT_TRUE(space.MoveTab(0U, 0U));
    EXPECT_EQ(TabIds(space), (std::vector<TabId>{first_id, second_id}));
}

TEST(Space, SplitStoresThePairByTabIdentity) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));

    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, third_id}));

    ASSERT_TRUE(space.split().has_value());
    EXPECT_EQ(space.split()->first, first_id);
    EXPECT_EQ(space.split()->second, third_id);
}

TEST(Space, SplitSurvivesReorderingBecauseItIsStoredById) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, second_id}));

    ASSERT_TRUE(space.MoveTab(0U, 1U));

    ASSERT_TRUE(space.split().has_value());
    EXPECT_EQ(space.split()->first, first_id);
    EXPECT_EQ(space.split()->second, second_id);
}

TEST(Space, SplitRejectsUnknownOrDuplicateTabs) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));

    EXPECT_FALSE(space.SetSplit(SplitPairing{first_id, first_id}));
    EXPECT_FALSE(space.SetSplit(SplitPairing{first_id, TabId{}}));

    EXPECT_FALSE(space.split().has_value());
}

TEST(Space, SettingANewSplitReplacesThePreviousPairing) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    Tab third = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    const TabId third_id = third.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    space.AppendTab(std::move(third));
    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, second_id}));

    ASSERT_TRUE(space.SetSplit(SplitPairing{second_id, third_id}));

    EXPECT_EQ(space.split()->first, second_id);
    EXPECT_EQ(space.split()->second, third_id);
}

TEST(Space, ClosingASplitHalfClearsTheSplit) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, second_id}));

    ASSERT_TRUE(space.RemoveTab(second_id).has_value());

    EXPECT_FALSE(space.split().has_value());
}

TEST(Space, ClearSplitRemovesThePairing) {
    Space space = MakeSpace();
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, second_id}));

    space.ClearSplit();

    EXPECT_FALSE(space.split().has_value());
}

TEST(Space, IsMoveOnlyAndMovesItsWholeTabCollection) {
    static_assert(!std::is_copy_constructible_v<Space>);
    static_assert(!std::is_copy_assignable_v<Space>);
    static_assert(std::is_move_constructible_v<Space>);
    static_assert(std::is_move_assignable_v<Space>);
    static_assert(std::is_nothrow_move_constructible_v<Space>);

    Space space = MakeSpace("Moving");
    Tab first = MakeTab();
    Tab second = MakeTab();
    const TabId first_id = first.id();
    const TabId second_id = second.id();
    space.AppendTab(std::move(first));
    space.AppendTab(std::move(second));
    ASSERT_TRUE(space.SelectTabIndex(0U));
    ASSERT_TRUE(space.SetSplit(SplitPairing{first_id, second_id}));

    Space moved(std::move(space));

    EXPECT_EQ(moved.name(), "Moving");
    EXPECT_EQ(moved.color(), kSpaceAccent);
    EXPECT_EQ(TabIds(moved), (std::vector<TabId>{first_id, second_id}));
    EXPECT_EQ(moved.active_tab_id(), first_id);
    ASSERT_TRUE(moved.split().has_value());
    EXPECT_EQ(moved.split()->first, first_id);
}

}  // namespace
}  // namespace island
