#include "tab.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

#include "tab_id.h"

namespace island {
namespace {

TEST(TabIdGenerator, IssuesStrictlyIncreasingUniqueIds) {
    const TabId first = NextTabId();
    const TabId second = NextTabId();
    const TabId third = NextTabId();

    EXPECT_LT(first.value, second.value);
    EXPECT_LT(second.value, third.value);
    EXPECT_NE(first, second);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
}

TEST(TabIdGenerator, NeverReusesAnIssuedId) {
    const TabId reference = NextTabId();

    for (int allocation = 0; allocation < 100; ++allocation) {
        EXPECT_GT(NextTabId().value, reference.value);
    }

    EXPECT_GT(NextTabId().value, reference.value);
}

TEST(Tab, HoldsItsIdAndAFreshNavigationState) {
    const TabId id = NextTabId();
    const Tab tab(id);

    EXPECT_EQ(tab.id(), id);
    EXPECT_EQ(tab.navigation_state().snapshot().revision, 0U);
    EXPECT_TRUE(tab.navigation_state().snapshot().url.empty());
    EXPECT_EQ(tab.navigation_state().snapshot().display_title, "Island");
}

TEST(Tab, IsMoveOnlySoItsNavigationStateCannotBeDuplicated) {
    static_assert(!std::is_copy_constructible_v<Tab>);
    static_assert(!std::is_copy_assignable_v<Tab>);
    static_assert(std::is_move_constructible_v<Tab>);
    static_assert(std::is_move_assignable_v<Tab>);
    static_assert(std::is_nothrow_move_constructible_v<Tab>);
}

TEST(Tab, KeepsItsIdAcrossMovesIndependentOfAnyCollectionPosition) {
    Tab tab(NextTabId());
    tab.navigation_state().OnLoadStart("https://example.test/");
    const TabId id = tab.id();

    Tab moved(std::move(tab));

    EXPECT_EQ(moved.id(), id);
    EXPECT_EQ(moved.navigation_state().snapshot().url, "https://example.test/");
}

}  // namespace
}  // namespace island
