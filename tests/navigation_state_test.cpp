#include "navigation_state.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace island {
namespace {

constexpr int kHttpNotFound = 404;
constexpr int kNetworkNameNotResolved = -105;

class RecordingObserver : public NavigationObserver {
  public:
    void OnNavigationChanged(const NavigationSnapshot& snapshot) override {
        snapshots.push_back(snapshot);
    }

    std::vector<NavigationSnapshot> snapshots;
};

TEST(NavigationState, StartsWithAnIslandSnapshot) {
    NavigationState state;

    const NavigationSnapshot& snapshot = state.snapshot();

    EXPECT_EQ(snapshot.revision, 0U);
    EXPECT_TRUE(snapshot.url.empty());
    EXPECT_TRUE(snapshot.page_title.empty());
    EXPECT_EQ(snapshot.display_title, "Island");
    EXPECT_EQ(snapshot.load_phase, LoadPhase::kNotStarted);
    EXPECT_FALSE(snapshot.can_go_back);
    EXPECT_FALSE(snapshot.can_go_forward);
    EXPECT_EQ(snapshot.http_status, std::nullopt);
    EXPECT_EQ(snapshot.network_error, std::nullopt);
}

TEST(NavigationState, DeliversTheCurrentSnapshotWhenObserverIsAttached) {
    NavigationState state;
    state.OnLoadStart("https://example.test/");
    RecordingObserver observer;

    state.SetObserver(&observer);

    ASSERT_EQ(observer.snapshots.size(), 1U);
    EXPECT_EQ(observer.snapshots[0], state.snapshot());
}

TEST(NavigationState, PublishesOneRevisionForEachMaterialChange) {
    NavigationState state;
    RecordingObserver observer;
    state.SetObserver(&observer);

    state.OnLoadStart("https://example.test/");
    state.OnTitleChange("Example");
    state.OnLoadingStateChange(true, true);

    EXPECT_EQ(state.snapshot().revision, 3U);
    ASSERT_EQ(observer.snapshots.size(), 4U);
    EXPECT_EQ(observer.snapshots[1].revision, 1U);
    EXPECT_EQ(observer.snapshots[2].revision, 2U);
    EXPECT_EQ(observer.snapshots[3].revision, 3U);
}

TEST(NavigationState, DoesNotPublishIdenticalChanges) {
    NavigationState state;
    RecordingObserver observer;
    state.SetObserver(&observer);
    state.OnLoadStart("https://example.test/");
    const std::uint64_t revision = state.snapshot().revision;
    const std::size_t publications = observer.snapshots.size();

    state.OnLoadStart("https://example.test/");
    state.SetAddress("https://example.test/");
    state.OnTitleChange("");
    state.OnLoadingStateChange(false, false);

    EXPECT_EQ(state.snapshot().revision, revision);
    EXPECT_EQ(observer.snapshots.size(), publications);
}

TEST(NavigationState, SetsSameDocumentAddressWithoutResettingCompletedState) {
    NavigationState state;
    RecordingObserver observer;
    state.SetObserver(&observer);
    state.OnLoadStart("https://example.test/page");
    state.OnTitleChange("Example page");
    state.OnLoadEnd(kHttpNotFound);
    const std::uint64_t revision = state.snapshot().revision;
    const std::size_t publications = observer.snapshots.size();

    state.SetAddress("https://example.test/page#section");

    const NavigationSnapshot& snapshot = state.snapshot();
    EXPECT_EQ(snapshot.url, "https://example.test/page#section");
    EXPECT_EQ(snapshot.page_title, "Example page");
    EXPECT_EQ(snapshot.display_title, "Example page — Island");
    EXPECT_EQ(snapshot.load_phase, LoadPhase::kCompleted);
    EXPECT_EQ(snapshot.http_status, kHttpNotFound);
    EXPECT_EQ(snapshot.network_error, std::nullopt);
    EXPECT_EQ(snapshot.revision, revision + 1U);
    ASSERT_EQ(observer.snapshots.size(), publications + 1U);
    EXPECT_EQ(observer.snapshots.back(), snapshot);
}

TEST(NavigationState, SetsSameDocumentAddressWithoutResettingFailedState) {
    NavigationState state;
    state.OnLoadStart("https://example.test/page");
    state.OnTitleChange("Example page");
    state.OnLoadError(kNetworkNameNotResolved);

    state.SetAddress("https://example.test/page#section");

    const NavigationSnapshot& snapshot = state.snapshot();
    EXPECT_EQ(snapshot.url, "https://example.test/page#section");
    EXPECT_EQ(snapshot.page_title, "Example page");
    EXPECT_EQ(snapshot.load_phase, LoadPhase::kFailed);
    EXPECT_EQ(snapshot.http_status, std::nullopt);
    EXPECT_EQ(snapshot.network_error, kNetworkNameNotResolved);
}

TEST(NavigationState, SetsSameDocumentAddressWithoutResettingLoadingState) {
    NavigationState state;
    state.OnLoadStart("https://example.test/page");
    state.OnTitleChange("Example page");

    state.SetAddress("https://example.test/page#section");

    const NavigationSnapshot& snapshot = state.snapshot();
    EXPECT_EQ(snapshot.url, "https://example.test/page#section");
    EXPECT_EQ(snapshot.page_title, "Example page");
    EXPECT_EQ(snapshot.load_phase, LoadPhase::kLoading);
    EXPECT_EQ(snapshot.http_status, std::nullopt);
    EXPECT_EQ(snapshot.network_error, std::nullopt);
}

TEST(NavigationState, FormatsPageTitleForDisplay) {
    NavigationState state;
    state.OnTitleChange("Island homepage");

    EXPECT_EQ(state.snapshot().page_title, "Island homepage");
    EXPECT_EQ(state.snapshot().display_title, "Island homepage — Island");

    state.OnTitleChange("");

    EXPECT_EQ(state.snapshot().display_title, "Island");
}

TEST(NavigationState, StartingALoadClearsStaleResultState) {
    NavigationState state;
    state.OnTitleChange("Old page");
    state.OnLoadEnd(kHttpNotFound);
    state.OnLoadError(kNetworkNameNotResolved);

    state.OnLoadStart("https://example.test/new");

    const NavigationSnapshot snapshot = state.snapshot();
    EXPECT_EQ(snapshot.url, "https://example.test/new");
    EXPECT_TRUE(snapshot.page_title.empty());
    EXPECT_EQ(snapshot.display_title, "Island");
    EXPECT_EQ(snapshot.load_phase, LoadPhase::kLoading);
    EXPECT_EQ(snapshot.http_status, std::nullopt);
    EXPECT_EQ(snapshot.network_error, std::nullopt);
}

TEST(NavigationState, CompletesForHttpErrorsAndStoresTheStatus) {
    NavigationState state;
    state.OnLoadStart("https://example.test/missing");

    state.OnLoadEnd(kHttpNotFound);

    EXPECT_EQ(state.snapshot().load_phase, LoadPhase::kCompleted);
    EXPECT_EQ(state.snapshot().http_status, kHttpNotFound);
    EXPECT_EQ(state.snapshot().network_error, std::nullopt);
}

TEST(NavigationState, FailsForNetworkErrors) {
    NavigationState state;
    state.OnLoadStart("https://example.test/");

    state.OnLoadError(kNetworkNameNotResolved);

    EXPECT_EQ(state.snapshot().load_phase, LoadPhase::kFailed);
    EXPECT_EQ(state.snapshot().network_error, kNetworkNameNotResolved);
    EXPECT_EQ(state.snapshot().http_status, std::nullopt);
}

TEST(NavigationState, TracksBackAndForwardAvailability) {
    NavigationState state;

    state.OnLoadingStateChange(true, false);

    EXPECT_TRUE(state.snapshot().can_go_back);
    EXPECT_FALSE(state.snapshot().can_go_forward);
}

TEST(NavigationState, ClosePublishesOnceThenDetachesTheObserver) {
    NavigationState state;
    RecordingObserver observer;
    state.SetObserver(&observer);

    state.Close();
    state.OnLoadStart("https://example.test/");
    state.Close();

    ASSERT_EQ(observer.snapshots.size(), 2U);
    EXPECT_EQ(observer.snapshots.back().load_phase, LoadPhase::kClosed);
    EXPECT_EQ(state.snapshot().revision, 1U);
    EXPECT_EQ(state.snapshot().load_phase, LoadPhase::kClosed);
}

}  // namespace
}  // namespace island
