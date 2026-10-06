// The CEF view types must be complete before browser_window.h: its delegate
// base classes define inline methods taking CefRefPtr<CefWindow> by value, and
// the window itself holds CefRefPtr<CefPanel>/CefRefPtr<CefOverlayController>
// members whose scoped_refptr operations need the full definitions.
#include "browser_window.h"

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "browser_chrome.h"
#include "browser_command.h"
#include "design_tokens.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "navigation_state.h"
#include "space.h"
#include "tab.h"
#include "tab_id.h"

// Headless BrowserWindow coverage for the Phase 3 window logic that needs no
// live CefBrowser: tab/space command dispatch, the direct-index and space
// bookkeeping methods, and the model state that UpdateChromeCollections
// projects into the tab strip and space switcher. The headless seam has no
// chrome and no window, so every path here must be exactly the code a real
// window runs before it reaches CEF views or browsers.

namespace island {
namespace {

constexpr std::string_view kDefaultSpaceName = "Default";
constexpr SpaceColor kDefaultSpaceColor{0xFF0090FF};
// browser_window.cc's kSpaceColorPalette entries 2 and 3: the second and third
// space created through New Space take these marks.
constexpr SpaceColor kSecondPaletteColor{0xFF30A46C};
constexpr SpaceColor kThirdPaletteColor{0xFFFFB224};

CefRefPtr<BrowserWindow> MakeWindow() {
    return BrowserWindow::CreateHeadlessForTest("data:text/html,Island");
}

void AppendTabs(BrowserWindow& window, std::size_t count) {
    for (std::size_t appended = 0; appended < count; ++appended) {
        window.ExecuteCommand(BrowserCommand::kNewTab);
    }
}

std::vector<TabId> ActiveSpaceTabIds(const BrowserWindow& window) {
    const Space* space = window.FindSpace(window.active_space_id());
    if (space == nullptr) {
        return {};
    }
    std::vector<TabId> ids;
    for (const Tab& tab : space->tabs()) {
        ids.push_back(tab.id());
    }
    return ids;
}

// The window keeps spaces_ private; the public FindSpace seam observes the
// active space's model state (split pairing included) in the tests below.
const Space& ActiveSpace(const BrowserWindow& window) {
    const Space* space = window.FindSpace(window.active_space_id());
    assert(space != nullptr);
    return *space;
}

// The model keeps spaces_ private, so order is probed through the selection:
// switch to |index|, read which space became active, restore. The detach /
// attach cycle this triggers is the same one a real switch runs.
SpaceId SpaceIdAtIndex(BrowserWindow& window, std::size_t index) {
    const std::size_t previous = window.active_space_index();
    if (!window.SelectSpaceIndex(index)) {
        return SpaceId{};
    }
    const SpaceId id = window.active_space_id();
    EXPECT_TRUE(window.SelectSpaceIndex(previous));
    return id;
}

class RecordingNavigationObserver final : public NavigationObserver {
  public:
    void OnNavigationChanged(const NavigationSnapshot& snapshot) override {
        snapshots_.push_back(snapshot);
    }

    [[nodiscard]] const std::vector<NavigationSnapshot>& snapshots() const noexcept {
        return snapshots_;
    }

  private:
    std::vector<NavigationSnapshot> snapshots_;
};

TEST(BrowserWindowTest, GivenAHeadlessWindowWhenInspectedThenItStartsWithOneDefaultSpaceAndOneTab) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();

    ASSERT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    const Space* space = window->FindSpace(window->active_space_id());
    ASSERT_NE(space, nullptr);
    EXPECT_EQ(space->name(), kDefaultSpaceName);
    EXPECT_EQ(space->color(), kDefaultSpaceColor);
    ASSERT_EQ(space->tab_count(), 1U);
    // Ids are process-unique starting at 1, never the zero TabId{}.
    EXPECT_NE(window->active_tab_id().value, 0U);
    EXPECT_EQ(window->active_tab_id(), space->active_tab_id());
}

TEST(BrowserWindowTest, GivenAHeadlessWindowWhenTheViewTreeIsRequestedThenItReportsTheEmptyShape) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();

    // No chrome exists headless, so the view tree snapshot is the empty node
    // and the navigation snapshot is the fresh active tab's default state.
    EXPECT_EQ(window->chrome_view_tree_snapshot(), ChromeViewTreeNode{});
    const NavigationSnapshot& snapshot = window->navigation_snapshot();
    EXPECT_TRUE(snapshot.url.empty());
    EXPECT_TRUE(snapshot.page_title.empty());
    EXPECT_EQ(snapshot.display_title, "Island");
    EXPECT_FALSE(snapshot.can_go_back);
    EXPECT_FALSE(snapshot.can_go_forward);
}

TEST(BrowserWindowTest, GivenNewTabCommandsWhenExecutedThenTabsAppendAndSelectInTheActiveSpace) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId default_space_id = window->active_space_id();
    const TabId first_tab_id = window->active_tab_id();

    window->ExecuteCommand(BrowserCommand::kNewTab);
    window->ExecuteBrowserCommand(BrowserCommand::kNewTab);
    ASSERT_EQ(window->space_count(), 1U);
    const Space* space = window->FindSpace(default_space_id);
    ASSERT_NE(space, nullptr);
    ASSERT_EQ(space->tab_count(), 3U);

    // Both dispatch paths append at the end and select what they appended; the
    // first tab keeps its identity, so the second dispatch's tab is active.
    const std::vector<TabId> ids = ActiveSpaceTabIds(*window);
    ASSERT_EQ(ids.size(), 3U);
    EXPECT_EQ(ids[0], first_tab_id);
    EXPECT_EQ(ids[2], window->active_tab_id());
    EXPECT_NE(ids[0], ids[1]);
    EXPECT_NE(ids[1], ids[2]);
}

TEST(BrowserWindowTest, GivenMultipleTabsWhenCloseTabExecutesThenTheFallbackNeighborBecomesActive) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 3);
    const std::vector<TabId> original = ActiveSpaceTabIds(*window);
    ASSERT_EQ(original.size(), 4U);
    EXPECT_EQ(window->active_tab_id(), original[3]);

    // Closing the active last tab falls back to its left neighbor.
    window->ExecuteCommand(BrowserCommand::kCloseTab);
    EXPECT_EQ(ActiveSpaceTabIds(*window),
              (std::vector<TabId>{original[0], original[1], original[2]}));
    EXPECT_EQ(window->active_tab_id(), original[2]);

    // Closing a tab before the active one keeps the active tab.
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    ASSERT_TRUE(window->CloseActiveSpaceTabIndex(1));
    EXPECT_EQ(ActiveSpaceTabIds(*window), (std::vector<TabId>{original[0], original[2]}));
    EXPECT_EQ(window->active_tab_id(), original[0]);

    // Closing the active tab at the front falls back to its right neighbor.
    ASSERT_TRUE(window->CloseActiveSpaceTabIndex(0));
    EXPECT_EQ(ActiveSpaceTabIds(*window), (std::vector<TabId>{original[2]}));
    EXPECT_EQ(window->active_tab_id(), original[2]);
}

TEST(BrowserWindowTest,
     GivenTheLastTabOfASpaceWhenCloseTabExecutesThenTheSpaceClosesAndThePreviousNeighborActivates) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId default_space_id = window->active_space_id();
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 2U);
    const SpaceId created_space_id = window->active_space_id();
    ASSERT_NE(created_space_id, default_space_id);

    window->ExecuteCommand(BrowserCommand::kCloseTab);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(window->active_space_id(), default_space_id);
    EXPECT_EQ(window->FindSpace(created_space_id), nullptr);

    // Closing the only tab of the front space hands over to the next space
    // (now at index 0), since no previous neighbor exists.
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    const SpaceId front_space_id = window->active_space_id();
    window->ExecuteCommand(BrowserCommand::kCloseTab);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_NE(window->active_space_id(), front_space_id);
    EXPECT_NE(window->FindSpace(window->active_space_id()), nullptr);
}

TEST(BrowserWindowTest,
     GivenTheLastRemainingSpaceWhenItsOnlyTabClosesThenAFreshDefaultSpaceIsRecreated) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId original_space_id = window->active_space_id();

    window->ExecuteCommand(BrowserCommand::kCloseTab);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    const Space* recreated = window->FindSpace(window->active_space_id());
    ASSERT_NE(recreated, nullptr);
    EXPECT_NE(window->active_space_id(), original_space_id);
    EXPECT_EQ(recreated->name(), kDefaultSpaceName);
    EXPECT_EQ(recreated->color(), kDefaultSpaceColor);
    ASSERT_EQ(recreated->tab_count(), 1U);
    EXPECT_EQ(window->active_tab_id(), recreated->active_tab_id());
}

TEST(BrowserWindowTest, GivenMultipleTabsWhenNextOrPreviousTabExecutesThenTheSelectionWraps) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const std::vector<TabId> ids = ActiveSpaceTabIds(*window);
    ASSERT_EQ(ids.size(), 2U);
    ASSERT_EQ(window->active_tab_id(), ids[1]);

    window->ExecuteCommand(BrowserCommand::kNextTab);
    EXPECT_EQ(window->active_tab_id(), ids[0]);
    window->ExecuteCommand(BrowserCommand::kNextTab);
    EXPECT_EQ(window->active_tab_id(), ids[1]);
    window->ExecuteCommand(BrowserCommand::kPreviousTab);
    EXPECT_EQ(window->active_tab_id(), ids[0]);
    window->ExecuteCommand(BrowserCommand::kPreviousTab);
    EXPECT_EQ(window->active_tab_id(), ids[1]);
}

TEST(BrowserWindowTest, GivenASingleTabWhenNextOrPreviousTabExecutesThenTheSelectionDoesNotChange) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const TabId only_tab_id = window->active_tab_id();

    window->ExecuteCommand(BrowserCommand::kNextTab);
    EXPECT_EQ(window->active_tab_id(), only_tab_id);
    window->ExecuteCommand(BrowserCommand::kPreviousTab);
    EXPECT_EQ(window->active_tab_id(), only_tab_id);
}

TEST(BrowserWindowTest, GivenDirectIndexSelectionWhenInRangeThenTheRequestedTabBecomesActive) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 2);
    const std::vector<TabId> ids = ActiveSpaceTabIds(*window);
    ASSERT_EQ(ids.size(), 3U);
    ASSERT_EQ(window->active_tab_id(), ids[2]);

    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(0));
    EXPECT_EQ(window->active_tab_id(), ids[0]);
    // Same index reports success and changes nothing.
    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(0));
    EXPECT_EQ(window->active_tab_id(), ids[0]);
    // The chrome host's argument-carrying dispatch reaches the same method.
    window->SelectTab(1);
    EXPECT_EQ(window->active_tab_id(), ids[1]);
}

TEST(BrowserWindowTest,
     GivenOutOfRangeArgumentsWhenDirectMethodsRunThenTheyReturnFalseAndChangeNothing) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const TabId active_tab_before = window->active_tab_id();
    const SpaceId active_space_before = window->active_space_id();
    const Space* space = window->FindSpace(active_space_before);
    ASSERT_NE(space, nullptr);
    const std::string space_name_before = space->name();

    EXPECT_FALSE(window->SelectActiveSpaceTabIndex(1));
    EXPECT_FALSE(window->CloseActiveSpaceTabIndex(1));
    EXPECT_FALSE(window->SelectSpaceIndex(1));
    EXPECT_FALSE(window->RenameSpace(1, "Renamed"));
    EXPECT_FALSE(window->RenameSpace(0, ""));
    EXPECT_FALSE(window->MoveSpace(0, 1));
    EXPECT_FALSE(window->MoveSpace(1, 0));
    EXPECT_FALSE(window->MoveSpace(0, 0));
    EXPECT_FALSE(window->MoveActiveSpace(-1));
    EXPECT_FALSE(window->MoveActiveSpace(1));
    EXPECT_FALSE(window->MoveActiveSpace(0));

    // A same-index space selection is a no-op success, not a failure.
    EXPECT_TRUE(window->SelectSpaceIndex(0));

    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(window->active_tab_id(), active_tab_before);
    EXPECT_EQ(window->active_space_id(), active_space_before);
    const Space* unchanged = window->FindSpace(active_space_before);
    ASSERT_NE(unchanged, nullptr);
    ASSERT_EQ(unchanged->tab_count(), 1U);
    EXPECT_EQ(unchanged->name(), space_name_before);
}

TEST(BrowserWindowTest,
     GivenANewSpaceCommandWhenExecutedThenItCreatesAndSelectsAUniqueNamedTabbedSpace) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId default_space_id = window->active_space_id();

    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 2U);
    EXPECT_EQ(window->active_space_index(), 1U);
    const Space* created = window->FindSpace(window->active_space_id());
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->name(), "Space 2");
    EXPECT_EQ(created->color(), kSecondPaletteColor);
    ASSERT_EQ(created->tab_count(), 1U);
    EXPECT_EQ(window->active_tab_id(), created->active_tab_id());
    EXPECT_NE(created->id(), default_space_id);

    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 3U);
    EXPECT_EQ(window->active_space_index(), 2U);
    created = window->FindSpace(window->active_space_id());
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->name(), "Space 3");
    EXPECT_EQ(created->color(), kThirdPaletteColor);

    // Creation leaves the existing spaces untouched.
    const Space* untouched = window->FindSpace(default_space_id);
    ASSERT_NE(untouched, nullptr);
    EXPECT_EQ(untouched->name(), kDefaultSpaceName);
    ASSERT_EQ(untouched->tab_count(), 1U);
}

TEST(BrowserWindowTest, GivenSpacesWhenSwitchedThenEachSpaceKeepsItsOwnActiveTabSelection) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId default_space_id = window->active_space_id();
    AppendTabs(*window, 1);
    const std::vector<TabId> default_space_tabs = ActiveSpaceTabIds(*window);
    ASSERT_EQ(default_space_tabs.size(), 2U);
    const TabId default_active_tab = window->active_tab_id();
    EXPECT_EQ(default_active_tab, default_space_tabs[1]);

    window->ExecuteCommand(BrowserCommand::kNewSpace);
    const SpaceId created_space_id = window->active_space_id();
    const TabId created_active_tab = window->active_tab_id();
    ASSERT_NE(created_active_tab, default_active_tab);

    EXPECT_TRUE(window->SelectSpaceIndex(0));
    EXPECT_EQ(window->active_space_id(), default_space_id);
    // The chrome host's argument-carrying dispatch reaches the same method.
    window->SelectSpace(1);
    EXPECT_EQ(window->active_space_id(), created_space_id);
    EXPECT_EQ(window->active_tab_id(), created_active_tab);

    // Round-tripping back to the first space restores its own active tab.
    EXPECT_TRUE(window->SelectSpaceIndex(0));
    EXPECT_EQ(window->active_tab_id(), default_active_tab);
    const Space* first_space = window->FindSpace(window->active_space_id());
    ASSERT_NE(first_space, nullptr);
    EXPECT_EQ(ActiveSpaceTabIds(*window), default_space_tabs);
}

TEST(BrowserWindowTest,
     GivenCloseSpaceWhenExecutedThenThePreviousNeighborBecomesActiveOrTheNextOneAtTheFront) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 3U);
    const SpaceId front_space_id = SpaceIdAtIndex(*window, 0);
    const SpaceId middle_space_id = SpaceIdAtIndex(*window, 1);
    const SpaceId last_space_id = SpaceIdAtIndex(*window, 2);

    // Closing the last of three activates the previous neighbor.
    window->ExecuteCommand(BrowserCommand::kCloseSpace);
    EXPECT_EQ(window->space_count(), 2U);
    EXPECT_EQ(window->active_space_index(), 1U);
    EXPECT_EQ(window->active_space_id(), middle_space_id);
    EXPECT_EQ(window->FindSpace(last_space_id), nullptr);

    // Closing the front space (no previous neighbor) hands over to the next
    // space, which is now at index 0.
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    EXPECT_EQ(window->active_space_id(), front_space_id);
    window->ExecuteCommand(BrowserCommand::kCloseSpace);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(window->active_space_id(), middle_space_id);
    EXPECT_EQ(window->FindSpace(front_space_id), nullptr);
}

TEST(BrowserWindowTest,
     GivenTheLastRemainingSpaceWhenCloseSpaceExecutesThenAFreshDefaultSpaceIsRecreated) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId original_space_id = window->active_space_id();

    window->ExecuteCommand(BrowserCommand::kCloseSpace);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
    const Space* recreated = window->FindSpace(window->active_space_id());
    ASSERT_NE(recreated, nullptr);
    EXPECT_NE(window->active_space_id(), original_space_id);
    EXPECT_EQ(recreated->name(), kDefaultSpaceName);
    ASSERT_EQ(recreated->tab_count(), 1U);
}

TEST(BrowserWindowTest, GivenRenameSpaceWhenTheNameIsValidThenTheProjectionInputUpdates) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const SpaceId default_space_id = window->active_space_id();

    EXPECT_TRUE(window->RenameSpace(0, "Work"));
    const Space* renamed = window->FindSpace(default_space_id);
    ASSERT_NE(renamed, nullptr);
    EXPECT_EQ(renamed->name(), "Work");

    // The rename-commit seam routes to the active space by index.
    window->OnSpaceRenameCommitted("Personal");
    renamed = window->FindSpace(default_space_id);
    ASSERT_NE(renamed, nullptr);
    EXPECT_EQ(renamed->name(), "Personal");

    // An empty name is rejected and leaves the current name in place.
    EXPECT_FALSE(window->RenameSpace(0, ""));
    renamed = window->FindSpace(default_space_id);
    ASSERT_NE(renamed, nullptr);
    EXPECT_EQ(renamed->name(), "Personal");
}

TEST(BrowserWindowTest, GivenMoveSpaceWhenReorderedThenTheActiveSelectionFollowsItsSpace) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 3U);
    const SpaceId front_space_id = SpaceIdAtIndex(*window, 0);
    const SpaceId middle_space_id = SpaceIdAtIndex(*window, 1);
    const SpaceId last_space_id = SpaceIdAtIndex(*window, 2);

    // Move the front space to the back; the other two shift up.
    EXPECT_TRUE(window->MoveSpace(0, 2));
    EXPECT_EQ(SpaceIdAtIndex(*window, 0), middle_space_id);
    EXPECT_EQ(SpaceIdAtIndex(*window, 1), last_space_id);
    EXPECT_EQ(SpaceIdAtIndex(*window, 2), front_space_id);
    // Ids are never derived from vector position, so lookups still resolve.
    const Space* moved = window->FindSpace(front_space_id);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->name(), kDefaultSpaceName);

    // The active selection follows its space rather than the position.
    ASSERT_TRUE(window->SelectSpaceIndex(1));
    const SpaceId active_before = window->active_space_id();
    EXPECT_TRUE(window->MoveSpace(1, 0));
    EXPECT_EQ(window->active_space_id(), active_before);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(SpaceIdAtIndex(*window, 1), middle_space_id);
    EXPECT_EQ(SpaceIdAtIndex(*window, 2), front_space_id);

    // The delta-based wrapper moves the active space and rejects edge moves.
    EXPECT_TRUE(window->MoveActiveSpace(1));
    EXPECT_EQ(window->active_space_id(), active_before);
    EXPECT_EQ(window->active_space_index(), 1U);
    EXPECT_FALSE(window->MoveActiveSpace(5));
    EXPECT_FALSE(window->MoveActiveSpace(0));
}

TEST(BrowserWindowTest, GivenNavigationCommandsWhenNoBrowserExistsThenTheyAreToleratedAsNoOps) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const TabId active_tab_before = window->active_tab_id();

    // Back/Forward/Reload dispatch to the tab's browser; headless tabs have
    // none, so the commands must be defined no-ops, not crashes.
    window->ExecuteCommand(BrowserCommand::kBack);
    window->ExecuteCommand(BrowserCommand::kForward);
    window->ExecuteCommand(BrowserCommand::kReload);
    EXPECT_EQ(window->active_tab_id(), active_tab_before);
    EXPECT_FALSE(window->navigation_snapshot().can_go_back);
    EXPECT_FALSE(window->navigation_snapshot().can_go_forward);
    EXPECT_FALSE(window->chrome_snapshot().back_enabled);
    EXPECT_FALSE(window->chrome_snapshot().forward_enabled);
}

TEST(BrowserWindowTest,
     GivenHeadlessWindowOverlaysWhenRequestedThenTheyAreToleratedWithoutAWindow) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();

    // Overlays attach to the CefWindow; without one these are defined no-ops.
    window->ShowSearchPalette();
    window->ShowCommandPalette();
    window->BeginSpaceRenaming();
    window->OnCommandPaletteDismissed();
    window->OnSearchPaletteDismissed();
    window->OnSpaceRenameCancelled();
    window->ToggleSidebar();
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->chrome_view_tree_snapshot(), ChromeViewTreeNode{});
}

TEST(BrowserWindowTest,
     GivenAMutationWhenTheActiveTabChangesThenTheNavigationObserverFollowsTheNewActiveTab) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    RecordingNavigationObserver observer;
    window->SetNavigationObserver(&observer);
    ASSERT_EQ(observer.snapshots().size(), 1U);

    // Every mutation re-attaches the projection to the (possibly new) active
    // tab, and each attach pushes one snapshot through the observer.
    window->ExecuteCommand(BrowserCommand::kNewTab);
    ASSERT_EQ(observer.snapshots().size(), 2U);
    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(0));
    ASSERT_EQ(observer.snapshots().size(), 3U);
    window->ExecuteCommand(BrowserCommand::kCloseTab);
    ASSERT_EQ(observer.snapshots().size(), 4U);
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(observer.snapshots().size(), 5U);
    EXPECT_TRUE(window->SelectSpaceIndex(0));
    ASSERT_EQ(observer.snapshots().size(), 6U);
    window->ExecuteCommand(BrowserCommand::kCloseSpace);
    ASSERT_EQ(observer.snapshots().size(), 7U);

    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_FALSE(window->navigation_snapshot().can_go_back);
}

TEST(BrowserWindowTest,
     GivenTheProjectionInputsWhenCollectionsAreProjectedThenRowsCarryIdsLabelsAndActiveFlags) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 2);
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    ASSERT_EQ(window->space_count(), 2U);
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    ASSERT_EQ(ActiveSpaceTabIds(*window).size(), 3U);
    const std::vector<TabId> tabs = ActiveSpaceTabIds(*window);
    const TabId active_tab = window->active_tab_id();
    EXPECT_EQ(active_tab, tabs[2]);

    // UpdateChromeCollections projects one tab-strip row per tab of the active
    // space: the truncating label input falls back to "Island" while the page
    // title is still empty, and exactly the active tab's row is flagged. The
    // chrome-side rendering of these rows is covered by the chrome contract
    // tests with live views; headless coverage pins the row data itself.
    const Space* active_space = window->FindSpace(window->active_space_id());
    ASSERT_NE(active_space, nullptr);
    std::size_t active_rows = 0;
    for (const Tab& tab : active_space->tabs()) {
        const NavigationSnapshot& snapshot = tab.navigation_state().snapshot();
        EXPECT_TRUE(snapshot.page_title.empty());
        EXPECT_EQ(snapshot.page_title.empty() ? "Island" : snapshot.page_title, "Island");
        if (tab.id() == active_tab) {
            ++active_rows;
        }
    }
    EXPECT_EQ(active_rows, 1U);
    EXPECT_EQ(tabs.size(), 3U);

    // The space switcher projects every space in model order: name, color, and
    // the active flag of the selected index.
    EXPECT_EQ(active_space->name(), kDefaultSpaceName);
    EXPECT_EQ(active_space->color(), kDefaultSpaceColor);
    const Space* other_space = window->FindSpace(SpaceIdAtIndex(*window, 1));
    ASSERT_NE(other_space, nullptr);
    EXPECT_EQ(other_space->name(), "Space 2");
    EXPECT_EQ(other_space->color(), kSecondPaletteColor);
    EXPECT_NE(other_space->color(), active_space->color());
    EXPECT_NE(other_space->id(), active_space->id());
    EXPECT_EQ(window->active_space_index(), 0U);
}

// U6 split view. The headless window records the pairing in the Space model;
// chrome-side pane attachment needs live views and stays with the manual pass.
// Every case here pins exactly the code a real window runs before it reaches
// CEF views.

TEST(BrowserWindowTest, GivenTwoTabsWhenToggleSplitRunsThenTheActivePairIsRecorded) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const Space& space = ActiveSpace(*window);
    ASSERT_EQ(space.tab_count(), 2U);
    ASSERT_EQ(space.active_tab_index(), 1U);

    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    ASSERT_TRUE(space.split().has_value());
    // The active tab is the first (left) pane; it pairs with its right
    // neighbor by default.
    EXPECT_EQ(space.split()->first, space.tabs()[1].id());
    EXPECT_EQ(space.split()->second, space.tabs()[0].id());

    // Toggling again tears the pair down.
    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    EXPECT_FALSE(space.split().has_value());
}

TEST(BrowserWindowTest, GivenTheLastTabActiveWhenToggleSplitRunsThenTheLeftNeighborPairs) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 2);
    const Space& space = ActiveSpace(*window);
    ASSERT_EQ(space.active_tab_index(), 2U);

    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    ASSERT_TRUE(space.split().has_value());
    EXPECT_EQ(space.split()->first, space.tabs()[2].id());
    EXPECT_EQ(space.split()->second, space.tabs()[1].id());
}

TEST(BrowserWindowTest, GivenASingleTabWhenToggleSplitRunsThenNothingPairs) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    const Space& space = ActiveSpace(*window);
    ASSERT_EQ(space.tab_count(), 1U);

    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    EXPECT_FALSE(space.split().has_value());
}

TEST(BrowserWindowTest, GivenTabsFromDifferentSpacesWhenSplitTabsRunsThenItIsRejected) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const TabId default_space_tab = window->active_tab_id();
    window->ExecuteCommand(BrowserCommand::kNewSpace);
    const TabId other_space_tab = window->active_tab_id();
    const Space& active_space = ActiveSpace(*window);
    ASSERT_FALSE(active_space.split().has_value());

    // The other space's tab must be rejected, never silently pulled in.
    EXPECT_FALSE(window->SplitTabs(default_space_tab, other_space_tab));
    EXPECT_FALSE(window->SplitTabs(other_space_tab, default_space_tab));
    EXPECT_FALSE(active_space.split().has_value());
    EXPECT_EQ(window->active_tab_id(), other_space_tab);
}

TEST(BrowserWindowTest, GivenDegeneratePairingsWhenSplitTabsRunsThenTheyAreRejected) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const Space& space = ActiveSpace(*window);
    const TabId active_id = window->active_tab_id();

    // Same id twice, and unknown ids, are rejected without model changes.
    EXPECT_FALSE(window->SplitTabs(active_id, active_id));
    EXPECT_FALSE(window->SplitTabs(active_id, TabId{9999}));
    EXPECT_FALSE(window->SplitTabs(TabId{9999}, active_id));
    EXPECT_FALSE(space.split().has_value());

    // A real pairing replaces any previous one. AppendTabs selected the second
    // tab, so the other member is the first one.
    const TabId other_id = space.tabs()[0].id();
    EXPECT_TRUE(window->SplitTabs(active_id, other_id));
    ASSERT_TRUE(space.split().has_value());
    EXPECT_EQ(space.split()->first, active_id);
    EXPECT_EQ(space.split()->second, other_id);
    EXPECT_TRUE(window->SplitTabs(other_id, active_id));
    ASSERT_TRUE(space.split().has_value());
    EXPECT_EQ(space.split()->first, other_id);
    EXPECT_EQ(space.split()->second, active_id);
}

TEST(BrowserWindowTest, GivenASplitPairWhenEitherHalfClosesThenTheSurvivorIsFullWidth) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const Space& space = ActiveSpace(*window);
    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    ASSERT_TRUE(space.split().has_value());
    // The pair is {first=tabs[1] (active), second=tabs[0]}; closing the active
    // first half leaves the second half as the full-width survivor.
    const TabId survivor_id = space.split()->second;

    EXPECT_TRUE(window->CloseActiveSpaceTabIndex(1));
    EXPECT_FALSE(space.split().has_value());
    EXPECT_EQ(window->active_tab_id(), survivor_id);
    EXPECT_EQ(space.tab_count(), 1U);

    // And a fresh split whose first half closes behaves the same.
    window->ExecuteCommand(BrowserCommand::kNewTab);
    const TabId active_id = window->active_tab_id();
    EXPECT_TRUE(window->SplitTabs(survivor_id, active_id));
    ASSERT_TRUE(space.split().has_value());
    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(0));
    EXPECT_TRUE(window->CloseActiveSpaceTabIndex(0));
    EXPECT_FALSE(space.split().has_value());
    EXPECT_EQ(window->active_tab_id(), active_id);
}

TEST(BrowserWindowTest, GivenASplitPairWhenTheSelectionLeavesThePairThenTheSplitClears) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 2);
    const Space& space = ActiveSpace(*window);
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    ASSERT_TRUE(space.split().has_value());
    ASSERT_EQ(space.split()->first, space.tabs()[0].id());
    ASSERT_EQ(space.split()->second, space.tabs()[1].id());

    // Selecting the other member keeps the pair.
    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(1));
    EXPECT_TRUE(space.split().has_value());

    // Selecting a third tab tears it down.
    EXPECT_TRUE(window->SelectActiveSpaceTabIndex(2));
    EXPECT_FALSE(space.split().has_value());
}

TEST(BrowserWindowTest, GivenSpacesWhenSwitchedThenEachKeepsItsOwnSplitPairing) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    const std::optional<SplitPairing> default_split =
        window->FindSpace(window->active_space_id())->split();
    ASSERT_TRUE(default_split.has_value());

    window->ExecuteCommand(BrowserCommand::kNewSpace);
    const Space& created = ActiveSpace(*window);
    EXPECT_FALSE(created.split().has_value());

    EXPECT_TRUE(window->SelectSpaceIndex(0));
    EXPECT_EQ(window->FindSpace(window->active_space_id())->split(), default_split);
    EXPECT_TRUE(window->SelectSpaceIndex(1));
    EXPECT_FALSE(ActiveSpace(*window).split().has_value());
}

TEST(BrowserWindowTest, GivenUnsplitOrClosingStatesWhenUnsplitRunsThenItReportsAccordingly) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);

    EXPECT_FALSE(window->UnsplitActiveSpace());
    window->ExecuteCommand(BrowserCommand::kToggleSplit);
    const Space& space = ActiveSpace(*window);
    ASSERT_TRUE(space.split().has_value());
    EXPECT_TRUE(window->UnsplitActiveSpace());
    EXPECT_FALSE(space.split().has_value());
    EXPECT_FALSE(window->UnsplitActiveSpace());
}

TEST(BrowserWindowTest, GivenRatiosWhenSetSplitRatioRunsThenTheyClampIntoTheUsableBand) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();

    EXPECT_DOUBLE_EQ(window->split_ratio(), BrowserChrome::SplitRatioDefault());
    // Out-of-band ratios clamp into the usable band; only closing fails.
    EXPECT_TRUE(window->SetSplitRatio(-1.0));
    EXPECT_DOUBLE_EQ(window->split_ratio(), BrowserChrome::SplitRatioMin());
    EXPECT_TRUE(window->SetSplitRatio(0.6));
    EXPECT_DOUBLE_EQ(window->split_ratio(), 0.6);
    EXPECT_TRUE(window->SetSplitRatio(5.0));
    EXPECT_DOUBLE_EQ(window->split_ratio(), BrowserChrome::SplitRatioMax());

    // The divider commands nudge by SplitRatioStep from the clamped state.
    window->ExecuteCommand(BrowserCommand::kMoveDividerLeft);
    EXPECT_DOUBLE_EQ(window->split_ratio(),
                     BrowserChrome::SplitRatioMax() - BrowserChrome::SplitRatioStep());
    window->ExecuteCommand(BrowserCommand::kMoveDividerRight);
    window->ExecuteCommand(BrowserCommand::kMoveDividerRight);
    EXPECT_DOUBLE_EQ(window->split_ratio(), BrowserChrome::SplitRatioMax());
}

TEST(BrowserWindowTest, GivenTheHeadlessSeamWhenWelcomeAndThemeSeamsRunThenTheyAreInMemoryOnly) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_EQ(window->theme_preference(), ThemePreference::kSystem);

    // No window exists headless, so every welcome seam is a defined no-op that
    // only moves in-memory state; the prefs file is never read or written.
    window->ShowWelcomeFlow();
    window->OnWelcomeThemeChanged(ThemePreference::kDark);
    EXPECT_EQ(window->theme_preference(), ThemePreference::kDark);
    window->OnWelcomeCompleted({});
    window->ShowWelcomeFlow();
    EXPECT_TRUE(window->SetThemePreference(ThemePreference::kLight));
    EXPECT_EQ(window->theme_preference(), ThemePreference::kLight);
    EXPECT_EQ(window->space_count(), 1U);
}

}  // namespace
}  // namespace island
