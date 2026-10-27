#include "favorite_tabs.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "session_store.h"
#include "tab_id.h"

namespace island {
namespace {

constexpr std::uint64_t kSpaceA = 100;
constexpr std::uint64_t kSpaceB = 200;

FavoriteTabKey GlobalKey(std::uint64_t bookmark_id) {
    return FavoriteTabKey{.scope = std::nullopt, .bookmark_id = bookmark_id};
}

FavoriteTabKey SpaceKey(std::uint64_t space, std::uint64_t bookmark_id) {
    return FavoriteTabKey{.scope = space, .bookmark_id = bookmark_id};
}

TEST(FavoriteTabRegistry, EnsureCreatesAtResetUrlAndKeepsExistingRecord) {
    FavoriteTabRegistry registry;
    const FavoriteTabRecord& created = registry.Ensure(GlobalKey(1), "https://a.test/");
    EXPECT_EQ(created.reset_url, "https://a.test/");
    EXPECT_EQ(created.last_url, "https://a.test/");

    registry.SetLastUrl(GlobalKey(1), "https://a.test/page");
    const FavoriteTabRecord& again = registry.Ensure(GlobalKey(1), "https://a.test/");
    EXPECT_EQ(again.last_url, "https://a.test/page");
}

TEST(FavoriteTabRegistry, GlobalAndSpaceRecordsForTheSameBookmarkAreDifferentTabs) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(1), "https://a.test/");
    registry.Ensure(SpaceKey(kSpaceA, 1), "https://a.test/");
    EXPECT_EQ(registry.records().size(), 2U);
    EXPECT_NE(GlobalKey(1), SpaceKey(kSpaceA, 1));
}

TEST(FavoriteTabRegistry, SetLastUrlIgnoresMissingRecord) {
    FavoriteTabRegistry registry;
    registry.SetLastUrl(GlobalKey(9), "https://ghost.test/");
    EXPECT_TRUE(registry.records().empty());
}

TEST(FavoriteTabRegistry, ResetToFavoriteReturnsLastUrlToResetUrl) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(1), "https://a.test/");
    registry.SetLastUrl(GlobalKey(1), "https://a.test/deep/page");
    registry.ResetToFavorite(GlobalKey(1));
    EXPECT_EQ(registry.Find(GlobalKey(1))->last_url, "https://a.test/");
    EXPECT_EQ(registry.Find(GlobalKey(1))->reset_url, "https://a.test/");
}

TEST(FavoriteTabRegistry, DisposeRemovesOnlyThatKey) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(1), "https://a.test/");
    registry.Ensure(GlobalKey(2), "https://b.test/");
    EXPECT_TRUE(registry.Dispose(GlobalKey(1)));
    EXPECT_FALSE(registry.Dispose(GlobalKey(1)));
    EXPECT_EQ(registry.Find(GlobalKey(1)), nullptr);
    EXPECT_NE(registry.Find(GlobalKey(2)), nullptr);
}

TEST(FavoriteTabRegistry, DisposeBookmarkRemovesEveryScopeOfThatBookmark) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(1), "https://a.test/");
    registry.Ensure(SpaceKey(kSpaceA, 1), "https://a.test/");
    registry.Ensure(SpaceKey(kSpaceB, 2), "https://b.test/");

    const std::vector<FavoriteTabKey> removed = registry.DisposeBookmark(1);
    EXPECT_EQ(removed.size(), 2U);
    EXPECT_EQ(registry.records().size(), 1U);
    EXPECT_NE(registry.Find(SpaceKey(kSpaceB, 2)), nullptr);
}

TEST(FavoriteTabRegistry, DisposeScopeRemovesOnlyThatSpacesRecords) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(1), "https://a.test/");
    registry.Ensure(SpaceKey(kSpaceA, 1), "https://a.test/");
    registry.Ensure(SpaceKey(kSpaceA, 2), "https://b.test/");
    registry.Ensure(SpaceKey(kSpaceB, 2), "https://b.test/");

    const std::vector<FavoriteTabKey> removed = registry.DisposeScope(FavoriteScope{kSpaceA});
    EXPECT_EQ(removed.size(), 2U);
    EXPECT_EQ(registry.records().size(), 2U);
    EXPECT_NE(registry.Find(GlobalKey(1)), nullptr);
    EXPECT_NE(registry.Find(SpaceKey(kSpaceB, 2)), nullptr);
}

TEST(FavoriteTabRegistry, SessionFormRoundTripsGlobalAndScopedRecords) {
    FavoriteTabRegistry registry;
    registry.Ensure(GlobalKey(7), "https://a.test/");
    registry.SetLastUrl(GlobalKey(7), "https://a.test/moved");
    registry.Ensure(SpaceKey(kSpaceB, 8), "https://b.test/");

    const std::vector<FavoriteTabState> session = ToSessionFavoriteTabs(registry);
    ASSERT_EQ(session.size(), 2U);
    EXPECT_FALSE(session[0].space_id.has_value());
    EXPECT_EQ(session[0].bookmark_id, 7U);
    EXPECT_EQ(session[0].last_url, "https://a.test/moved");
    ASSERT_TRUE(session[1].space_id.has_value());
    EXPECT_EQ(session[1].space_id->value, kSpaceB);

    const FavoriteTabRegistry restored = FavoriteTabsFromSession(session);
    EXPECT_EQ(restored.records(), registry.records());
}

}  // namespace
}  // namespace island
