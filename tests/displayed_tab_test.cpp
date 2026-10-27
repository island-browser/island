#include "displayed_tab.h"

#include <chrono>
#include <optional>
#include <vector>

#include "gtest/gtest.h"

namespace island {
namespace {

constexpr TabId kFirst{101};
constexpr TabId kSecond{102};
constexpr TabId kFavorite{201};
constexpr FavoriteTabKey kFavoriteKey{std::nullopt, 7};

TEST(DisplayedTabModelTest, FavoriteActivatedInPaneTwoLeavesPaneOneUnchanged) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kSecond, DisplayedSplit{.first = kFirst, .second = kSecond});

    ASSERT_TRUE(model.ActivateFavorite(kFavoriteKey, kFavorite));

    const std::optional<DisplayedPanes> panes = model.panes();
    ASSERT_TRUE(panes.has_value());
    EXPECT_EQ(panes->first, kFirst);
    EXPECT_EQ(panes->second, kFavorite);
    EXPECT_EQ(model.focused_tab(), kFavorite);
}

TEST(DisplayedTabModelTest, DeactivatingFavoriteRestoresTheOrdinaryPane) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kSecond, DisplayedSplit{.first = kFirst, .second = kSecond});
    ASSERT_TRUE(model.ActivateFavorite(kFavoriteKey, kFavorite));

    EXPECT_TRUE(model.DeactivateFavorite());

    const std::optional<DisplayedPanes> panes = model.panes();
    ASSERT_TRUE(panes.has_value());
    EXPECT_EQ(panes->first, kFirst);
    EXPECT_EQ(panes->second, kSecond);
    EXPECT_EQ(model.focused_tab(), kSecond);
}

TEST(DisplayedTabModelTest, ActivatingTheOtherSplitSideMovesOnlyTheFavoriteSide) {
    DisplayedTabModel model;
    const DisplayedSplit split{.first = kFirst, .second = kSecond};
    model.ActivateOrdinary(kSecond, split);
    ASSERT_TRUE(model.ActivateFavorite(kFavoriteKey, kFavorite));
    ASSERT_TRUE(model.DeactivateFavorite());

    model.ActivateOrdinary(kFirst, split);
    ASSERT_TRUE(model.ActivateFavorite(kFavoriteKey, kFavorite));

    const std::optional<DisplayedPanes> panes = model.panes();
    ASSERT_TRUE(panes.has_value());
    EXPECT_EQ(panes->first, kFavorite);
    EXPECT_EQ(panes->second, kSecond);
}

TEST(DisplayedTabModelTest, AttachGateRefusesToRemoveATooYoungView) {
    DisplayedTabModel model;
    using namespace std::chrono_literals;
    const DisplayedTabModel::Clock::time_point created_at{1000ms};
    const DisplayedSurface attached = DisplayedSurface::Tab(kFirst);
    const DisplayedSurface replacement = DisplayedSurface::Tab(kSecond);
    model.DidAttach({attached});
    const std::vector<AttachedSurfaceState> state = {{.surface = attached,
                                                      .live = true,
                                                      .browser_created = true,
                                                      .browser_created_at = created_at}};

    const AttachDecision too_young =
        model.EvaluateReplacement(state, {replacement}, created_at + 99ms,
                                  /*allow_uncreated=*/false);
    EXPECT_FALSE(too_young.can_replace);
    EXPECT_EQ(too_young.retry_after, 1ms);

    const AttachDecision old_enough =
        model.EvaluateReplacement(state, {replacement}, created_at + 100ms,
                                  /*allow_uncreated=*/false);
    EXPECT_TRUE(old_enough.can_replace);
    EXPECT_EQ(old_enough.retry_after, 0ms);
}

}  // namespace
}  // namespace island
