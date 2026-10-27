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

    model.ActivateFavorite(kFavoriteKey, kFavorite);

    const std::optional<DisplayedPanes> panes = model.panes();
    ASSERT_TRUE(panes.has_value());
    EXPECT_EQ(panes->first, kFirst);
    EXPECT_EQ(panes->second, kFavorite);
    EXPECT_EQ(model.focused_tab(), kFavorite);
}

TEST(DisplayedTabModelTest, DeactivatingFavoriteRestoresTheOrdinaryPane) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kSecond, DisplayedSplit{.first = kFirst, .second = kSecond});
    model.ActivateFavorite(kFavoriteKey, kFavorite);

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
    model.ActivateFavorite(kFavoriteKey, kFavorite);
    ASSERT_TRUE(model.DeactivateFavorite());

    model.ActivateOrdinary(kFirst, split);
    model.ActivateFavorite(kFavoriteKey, kFavorite);

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

TEST(DisplayedTabModelTest, UnchangedSurfaceIsNeverDelayed) {
    DisplayedTabModel model;
    const DisplayedSurface attached = DisplayedSurface::Tab(kFirst);
    model.DidAttach({attached});

    const AttachDecision decision = model.EvaluateReplacement(
        {{.surface = attached, .live = true}}, {attached}, DisplayedTabModel::Clock::time_point{},
        /*allow_uncreated=*/false);

    EXPECT_TRUE(decision.can_replace);
    EXPECT_FALSE(decision.waiting_for_creation);
    EXPECT_EQ(decision.retry_after, std::chrono::milliseconds::zero());
}

TEST(DisplayedTabModelTest, UncreatedSurfaceWaitsUntilTheFallbackAllowsIt) {
    DisplayedTabModel model;
    const DisplayedSurface attached = DisplayedSurface::Tab(kFirst);
    model.DidAttach({attached});
    const std::vector<AttachedSurfaceState> states = {
        {.surface = attached, .live = true, .browser_created = false}};

    const AttachDecision waiting = model.EvaluateReplacement(
        states, {DisplayedSurface::Tab(kSecond)}, DisplayedTabModel::Clock::time_point{},
        /*allow_uncreated=*/false);
    EXPECT_FALSE(waiting.can_replace);
    EXPECT_TRUE(waiting.waiting_for_creation);
    EXPECT_EQ(waiting.retry_after, std::chrono::milliseconds::zero());

    const AttachDecision allowed = model.EvaluateReplacement(
        states, {DisplayedSurface::Tab(kSecond)}, DisplayedTabModel::Clock::time_point{},
        /*allow_uncreated=*/true);
    EXPECT_TRUE(allowed.can_replace);
    EXPECT_FALSE(allowed.waiting_for_creation);
}

TEST(DisplayedTabModelTest, CreatedSurfaceWithUnknownAgeUsesTheFallback) {
    DisplayedTabModel model;
    const DisplayedSurface attached = DisplayedSurface::Tab(kFirst);
    model.DidAttach({attached});
    const std::vector<AttachedSurfaceState> states = {
        {.surface = attached, .live = true, .browser_created = true}};

    const AttachDecision waiting = model.EvaluateReplacement(
        states, {DisplayedSurface::Tab(kSecond)}, DisplayedTabModel::Clock::time_point{},
        /*allow_uncreated=*/false);
    EXPECT_FALSE(waiting.can_replace);
    EXPECT_TRUE(waiting.waiting_for_creation);
    EXPECT_EQ(waiting.retry_after, std::chrono::milliseconds::zero());

    // Only a browser that does not exist yet is released by the fallback; a created one of
    // unknown age keeps waiting, as the old gate did.
    EXPECT_FALSE(model
                     .EvaluateReplacement(states, {DisplayedSurface::Tab(kSecond)},
                                          DisplayedTabModel::Clock::time_point{},
                                          /*allow_uncreated=*/true)
                     .can_replace);
}

TEST(DisplayedTabModelTest, FavoriteCanBeShownWithoutAnOrdinaryTab) {
    DisplayedTabModel model;

    model.ActivateFavorite(kFavoriteKey, kFavorite);
    EXPECT_EQ(model.focused_tab(), kFavorite);
    EXPECT_EQ(model.panes(), (DisplayedPanes{.first = kFavorite}));
}

TEST(DisplayedTabModelTest, ClearingTheOrdinarySelectionKeepsAShownFavorite) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kFirst);
    model.ActivateFavorite(kFavoriteKey, kFavorite);

    model.ClearOrdinary();

    EXPECT_EQ(model.panes(), (DisplayedPanes{.first = kFavorite}));
    EXPECT_EQ(model.shutdown_plan().navigation_tab, std::nullopt);
    EXPECT_EQ(model.shutdown_plan().graceful_tabs, (std::vector<TabId>{kFavorite}));
}

TEST(DisplayedTabModelTest, ClearingSplitWhileFavoriteIsActiveKeepsOnlyTheFavoritePane) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kSecond, DisplayedSplit{.first = kFirst, .second = kSecond});
    model.ActivateFavorite(kFavoriteKey, kFavorite);

    model.UpdateOrdinaryLayout(kSecond);

    EXPECT_EQ(model.panes(), (DisplayedPanes{.first = kFavorite}));
    EXPECT_EQ(model.ordinary_focused(), kSecond);
}

TEST(DisplayedTabModelTest, SinglePaneIdentityIgnoresItsFormerSplitSlot) {
    DisplayedTabModel model;
    const DisplayedSurface attached = DisplayedSurface::Tab(kSecond, /*second_slot=*/true);
    model.DidAttach({attached});

    const AttachDecision decision = model.EvaluateReplacement(
        {{.surface = attached, .live = true, .browser_created = false}},
        {DisplayedSurface::Tab(kSecond)}, DisplayedTabModel::Clock::time_point{},
        /*allow_uncreated=*/false);

    EXPECT_TRUE(decision.can_replace);
    EXPECT_FALSE(decision.waiting_for_creation);
}

TEST(DisplayedTabModelTest, SplitIdentityStillIncludesTheSlot) {
    DisplayedTabModel model;
    const DisplayedSurface attached = DisplayedSurface::Tab(kFirst);
    model.DidAttach({attached});

    const AttachDecision decision = model.EvaluateReplacement(
        {{.surface = attached, .live = true, .browser_created = false}},
        {DisplayedSurface::Tab(kSecond), DisplayedSurface::Tab(kFirst, /*second_slot=*/true)},
        DisplayedTabModel::Clock::time_point{}, /*allow_uncreated=*/false);

    EXPECT_FALSE(decision.can_replace);
    EXPECT_TRUE(decision.waiting_for_creation);
}

TEST(DisplayedTabModelTest, ShutdownKeepsOrdinaryAndFavoriteBrowsersGraceful) {
    DisplayedTabModel model;
    model.ActivateOrdinary(kFirst);
    model.ActivateFavorite(kFavoriteKey, kFavorite);

    const DisplayedShutdownPlan plan = model.shutdown_plan();

    EXPECT_EQ(plan.navigation_tab, kFirst);
    EXPECT_EQ(plan.graceful_tabs, (std::vector<TabId>{kFirst, kFavorite}));
}

}  // namespace
}  // namespace island
