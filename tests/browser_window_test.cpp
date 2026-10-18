// The CEF view types must be complete before browser_window.h: its delegate
// base classes define inline methods taking CefRefPtr<CefWindow> by value, and
// the window itself holds CefRefPtr<CefPanel>/CefRefPtr<CefOverlayController>
// members whose scoped_refptr operations need the full definitions.
#include "browser_window.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
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
#include "row_raster.h"
#include "runtime_style.h"
#include "space.h"
#include "tab.h"
#include "tab_id.h"
#include "zoom_policy.h"

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

const ChromeViewTreeNode* FindChromeView(const ChromeViewTreeNode& node, ChromeViewId id) {
    if (node.id == id) {
        return &node;
    }
    for (const ChromeViewTreeNode& child : node.children) {
        if (const ChromeViewTreeNode* found = FindChromeView(child, id); found != nullptr) {
            return found;
        }
    }
    return nullptr;
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

TEST(BrowserWindowTest, GivenNativeTokensWhenTabRowsResolveThenSelectionUsesWhiteOnSelection) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);

    const RowColors selected =
        ResolveRowColors(tokens, /*selected=*/true, /*hovered=*/false, /*slept=*/false);
    const RowColors unselected =
        ResolveRowColors(tokens, /*selected=*/false, /*hovered=*/false, /*slept=*/false);

    EXPECT_EQ(selected.fill, tokens.selection);
    EXPECT_EQ(selected.title, (ArgbColor{.argb = 0xFFFFFFFFU}));
    EXPECT_EQ(unselected.title, tokens.text);
}

TEST(BrowserWindowTest, GivenNativeRowStatesWhenResolvedThenHoverAndSleepUseTheirSlots) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);

    const RowColors hovered =
        ResolveRowColors(tokens, /*selected=*/false, /*hovered=*/true, /*slept=*/false);
    const RowColors slept =
        ResolveRowColors(tokens, /*selected=*/false, /*hovered=*/false, /*slept=*/true);

    EXPECT_EQ(tokens.look, LookPreset::kNative);
    EXPECT_EQ(hovered.fill, tokens.hover);
    EXPECT_EQ(hovered.title, tokens.text);
    EXPECT_EQ(slept.title, tokens.text_tertiary);
}

TEST(BrowserWindowTest, GivenAGroupedTabThenItsSidebarMetricsMatchThePlanSpec) {
    EXPECT_EQ(BrowserChrome::GroupedTabIndentDip(), 16);
    EXPECT_EQ(BrowserChrome::GroupedTabKickerFontPx(), 10);
}

TEST(BrowserWindowTest, GivenNativeAppearanceWhenLaidOutThenItHasTheSourceListAndToolbarMetrics) {
    const ChromeTokens tokens =
        ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight)
            .Customized(kDefaultSpaceColor, ChromeTheme::kLight, AppearanceSettings{});
    const ChromeGeometrySnapshot geometry =
        BrowserChrome::LayoutForBounds({.x = 0, .y = 0, .width = 1440, .height = 900}, tokens);

    EXPECT_EQ(tokens.rail_width_dip, 248);
    EXPECT_EQ(tokens.control_height_dip, 34);
    EXPECT_EQ(BrowserChrome::NativeToolbarHeightDip(), 52);
    EXPECT_EQ(BrowserChrome::NativeToolbarButtonDip(), 28);
    EXPECT_EQ(BrowserChrome::NativeAddressHeightDip(), 30);
    EXPECT_EQ(geometry.rail_bounds, (DipRect{.x = 0, .y = 0, .width = 248, .height = 900}));
    EXPECT_EQ(geometry.toolbar_bounds, (DipRect{.x = 248, .y = 0, .width = 1192, .height = 52}));
    EXPECT_EQ(geometry.toolbar_leading_inset_dip, tokens.spacing_3_dip);
    EXPECT_EQ(geometry.browser_content_bounds,
              (DipRect{.x = 248, .y = 52, .width = 1192, .height = 848}));
    EXPECT_EQ(geometry.browser_view_bounds, geometry.browser_content_bounds);
}

TEST(BrowserWindowTest, GivenNativeAddressWhenStyledThenItIsARoundedFillPlateWithATwoDipRingInset) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);

    const AddressPlateStyle style = BrowserChrome::AddressPlateStyleFor(tokens);

    EXPECT_EQ(style.fill, tokens.fill);
    EXPECT_EQ(style.outside, tokens.surface_secondary);
    EXPECT_EQ(style.outline_dip, 2);
    EXPECT_EQ(style.radius_dip, 8);
}

TEST(BrowserWindowTest,
     GivenNativeAddressWhenPatchedThenItIsRoundedOnTheToolbarInEverySidebarState) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const DipRect address = {.x = 320, .y = 11, .width = 600, .height = 30};

    for (const bool expanded : {true, false}) {
        const std::optional<CornerPatch> patch =
            BrowserChrome::AddressCornerPatch(tokens, address, expanded);
        ASSERT_TRUE(patch.has_value());
        EXPECT_EQ(*patch, (CornerPatch{.x = 320,
                                       .y = 11,
                                       .width = 600,
                                       .height = 30,
                                       .radius = 8,
                                       .outside = tokens.surface_secondary}));
    }
}

TEST(BrowserWindowTest, GivenArcAddressWhenPatchedThenItKeepsTheRailPillOnlyWhileExpanded) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const DipRect address = {.x = 12, .y = 40, .width = 260, .height = 32};

    const std::optional<CornerPatch> expanded =
        BrowserChrome::AddressCornerPatch(tokens, address, /*sidebar_expanded=*/true);
    ASSERT_TRUE(expanded.has_value());
    EXPECT_EQ(expanded->radius, BrowserChrome::AddressPlateStyleFor(tokens).radius_dip);
    EXPECT_EQ(expanded->outside, ArgbColor{});
    EXPECT_FALSE(
        BrowserChrome::AddressCornerPatch(tokens, address, /*sidebar_expanded=*/false).has_value());
}

TEST(BrowserWindowTest, GivenNativeAddressWhenFocusedThenATwoDipAccentRingIsPainted) {
    const ChromeTokens native = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    const ChromeTokens arc = ChromeTokens::ForTheme(ChromeTheme::kDark);

    EXPECT_EQ(BrowserChrome::AddressOutlineColor(native, /*focused=*/false), native.fill);
    EXPECT_EQ(BrowserChrome::AddressOutlineColor(native, /*focused=*/true), native.accent);
    EXPECT_EQ(BrowserChrome::AddressOutlineColor(arc, /*focused=*/false), arc.border);
    EXPECT_EQ(BrowserChrome::AddressOutlineColor(arc, /*focused=*/true), arc.accent);
}

TEST(BrowserWindowTest, GivenNativeAddressFocusWhenTextfieldRingResolvedThenItMatchesThePlate) {
    const ChromeTokens native = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    const ChromeTokens arc = ChromeTokens::ForTheme(ChromeTheme::kDark);

    EXPECT_EQ(BrowserChrome::TextfieldFocusRingColor(native, /*address_focused=*/true),
              native.fill);
    EXPECT_EQ(BrowserChrome::TextfieldFocusRingColor(native, /*address_focused=*/false),
              native.accent);
    EXPECT_EQ(BrowserChrome::TextfieldFocusRingColor(arc, /*address_focused=*/true), arc.accent);
}

TEST(BrowserWindowTest, CornerPatchPollingStopsAfterAQuietStreakAndResetsOnChange) {
    BrowserChrome::CornerPatchPollState state;
    for (std::size_t tick = 0; tick < BrowserChrome::CornerPatchPollUnchangedLimit(); ++tick) {
        state = BrowserChrome::AdvanceCornerPatchPoll(state.unchanged_ticks,
                                                      /*patches_changed=*/false);
        EXPECT_EQ(state.continue_polling,
                  tick + 1 < BrowserChrome::CornerPatchPollUnchangedLimit());
    }

    EXPECT_EQ(BrowserChrome::AdvanceCornerPatchPoll(state.unchanged_ticks,
                                                    /*patches_changed=*/true),
              (BrowserChrome::CornerPatchPollState{.unchanged_ticks = 0,
                                                   .continue_polling = true}));
}

TEST(BrowserWindowTest, GivenNativeTreeWhenInspectedThenToolbarAndAddressIdsMatchRuntime) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract(LookPreset::kNative);
    const ChromeViewTreeNode* rail = FindChromeView(tree, ChromeViewId::kRail);
    const ChromeViewTreeNode* toolbar = FindChromeView(tree, ChromeViewId::kToolbar);
    const ChromeViewTreeNode* address = FindChromeView(tree, ChromeViewId::kAddressRow);

    ASSERT_NE(rail, nullptr);
    EXPECT_EQ(FindChromeView(*rail, ChromeViewId::kNavigationRow), nullptr);
    EXPECT_EQ(FindChromeView(*rail, ChromeViewId::kAddressRow), nullptr);
    ASSERT_NE(toolbar, nullptr);
    // Native's toolbar holds navigation, the address field and Share; the Agent entry lives at
    // the sidebar bottom (2026-10-16), so the toolbar has no Agent button.
    ASSERT_EQ(toolbar->children.size(), 3U);
    EXPECT_EQ(toolbar->children[0].id, ChromeViewId::kNavigationRow);
    EXPECT_EQ(toolbar->children[1].id, ChromeViewId::kAddressRow);
    EXPECT_EQ(toolbar->children[2].id, ChromeViewId::kToolbarShare);
    EXPECT_EQ(FindChromeView(tree, ChromeViewId::kToolbarAgent), nullptr);
    ASSERT_NE(address, nullptr);
    ASSERT_EQ(address->children.size(), 5U);
    EXPECT_EQ(address->children[1].id, ChromeViewId::kAddress);
    EXPECT_EQ(address->children[2].id, ChromeViewId::kAddressIdleLabel);
    EXPECT_NE(static_cast<int>(ChromeViewId::kAddress),
              static_cast<int>(ChromeViewId::kAddressIdleLabel));
}

TEST(BrowserWindowTest, GivenNativeLiveTreeWithASplitWhenProjectedThenThePaneJoinsBrowserContent) {
    const ChromeViewTreeNode tree =
        BrowserChrome::LiveViewTree({}, {}, LookPreset::kNative, /*split_active=*/true);
    const ChromeViewTreeNode* toolbar = FindChromeView(tree, ChromeViewId::kToolbar);
    const ChromeViewTreeNode* content = FindChromeView(tree, ChromeViewId::kBrowserContent);

    ASSERT_NE(toolbar, nullptr);
    EXPECT_EQ(FindChromeView(*toolbar, ChromeViewId::kSplitDivider), nullptr);
    ASSERT_NE(content, nullptr);
    ASSERT_EQ(content->children.size(), 3U);
    EXPECT_EQ(content->children[1].id, ChromeViewId::kSplitDivider);
    EXPECT_EQ(content->children[2].id, ChromeViewId::kBrowserView);
}

TEST(BrowserWindowTest, GivenEachLookWhenTheContractIsWalkedThenNoViewIdRepeats) {
    for (const LookPreset look : {LookPreset::kArc, LookPreset::kGraphite, LookPreset::kNative}) {
        std::vector<int> ids;
        const std::function<void(const ChromeViewTreeNode&)> walk =
            [&](const ChromeViewTreeNode& node) {
                ids.push_back(static_cast<int>(node.id));
                for (const ChromeViewTreeNode& child : node.children) {
                    walk(child);
                }
            };
        walk(BrowserChrome::ViewTreeContract(look));
        std::sort(ids.begin(), ids.end());
        EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    }
}

TEST(BrowserWindowTest, GivenNativeCollectionsWhenProjectedThenSectionHeadersAreInTheContract) {
    const std::vector<TabStripEntrySnapshot> tabs = {
        {.title = "Pinned", .pinned = true},
        {.title = "Regular"},
    };

    const ChromeViewTreeNode tree =
        BrowserChrome::CollectionCountContract(tabs, {}, LookPreset::kNative);
    const ChromeViewTreeNode* tab_strip = FindChromeView(tree, ChromeViewId::kTabStrip);

    ASSERT_NE(tab_strip, nullptr);
    // Native's New Tab row closes the list, after the last tab.
    ASSERT_EQ(tab_strip->children.size(), 5U);
    EXPECT_EQ(tab_strip->children[0].id, ChromeViewId::kFavoritesSectionHeader);
    EXPECT_EQ(tab_strip->children[1].id, ChromeViewId::kTabStripEntry);
    EXPECT_EQ(tab_strip->children[2].id, ChromeViewId::kTabsSectionHeader);
    EXPECT_EQ(tab_strip->children[3].id, ChromeViewId::kTabStripEntry);
    EXPECT_EQ(tab_strip->children[4].id, ChromeViewId::kRailNewTab);
}

TEST(BrowserWindowTest, GivenGroupedTabsWhenProjectedThenKickersSitUnderTheTabsHeader) {
    const TabId plan{1};
    const std::vector<TabStripEntrySnapshot> tabs = {
        {.title = "Plan", .pinned = true},
        {.title = "Favourite", .pinned = true},
        {.title = "A1", .group_parent = plan, .group_label = "Branch A"},
        {.title = "A2", .group_parent = plan, .group_label = "Branch A"},
        {.title = "B1", .group_parent = plan, .group_label = "Branch B"},
        {.title = "Regular"},
    };

    const ChromeViewTreeNode tree =
        BrowserChrome::CollectionCountContract(tabs, {}, LookPreset::kNative);
    const ChromeViewTreeNode* tab_strip = FindChromeView(tree, ChromeViewId::kTabStrip);

    ASSERT_NE(tab_strip, nullptr);
    std::vector<ChromeViewId> ids;
    for (const ChromeViewTreeNode& child : tab_strip->children) {
        ids.push_back(child.id);
    }
    const ChromeViewId entry = ChromeViewId::kTabStripEntry;
    const ChromeViewId kicker = ChromeViewId::kTabGroupKicker;
    // The children stay out of Favorites and the header precedes the first kicker.
    EXPECT_EQ(ids, (std::vector<ChromeViewId>{
                       ChromeViewId::kFavoritesSectionHeader, entry, entry,
                       ChromeViewId::kTabsSectionHeader, kicker, entry, entry, kicker, entry, entry,
                       ChromeViewId::kRailNewTab}));
}

TEST(BrowserWindowTest, GivenOnlyGroupedUnpinnedTabsThenTheTabsHeaderStillComesFirst) {
    const TabId plan{1};
    const std::vector<TabStripEntrySnapshot> tabs = {
        {.title = "Plan", .pinned = true},
        {.title = "A1", .group_parent = plan, .group_label = "Branch A"},
    };

    const ChromeViewTreeNode tree =
        BrowserChrome::CollectionCountContract(tabs, {}, LookPreset::kNative);
    const ChromeViewTreeNode* tab_strip = FindChromeView(tree, ChromeViewId::kTabStrip);

    ASSERT_NE(tab_strip, nullptr);
    ASSERT_EQ(tab_strip->children.size(), 6U);
    EXPECT_EQ(tab_strip->children[2].id, ChromeViewId::kTabsSectionHeader);
    EXPECT_EQ(tab_strip->children[3].id, ChromeViewId::kTabGroupKicker);
    EXPECT_EQ(tab_strip->children[5].id, ChromeViewId::kRailNewTab);
}

TEST(BrowserWindowTest, GivenFramelessNativeToolbarWhenRailDoesNotClearLightsThenInsetDoes) {
    ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    tokens.titlebar_band_dip = BrowserChrome::TrafficLightBandDip();
    const DipRect root = {.x = 0, .y = 0, .width = 1440, .height = 900};

    tokens.sidebar_side = SidebarSide::kRight;
    const ChromeGeometrySnapshot right = BrowserChrome::LayoutForBounds(root, tokens);
    EXPECT_EQ(right.toolbar_bounds.x, 0);
    EXPECT_EQ(right.toolbar_leading_inset_dip, BrowserChrome::TrafficLightClearDip());

    tokens.sidebar_side = SidebarSide::kLeft;
    const ChromeGeometrySnapshot collapsed =
        BrowserChrome::LayoutForBounds(root, tokens, /*sidebar_expanded=*/false);
    EXPECT_EQ(collapsed.toolbar_bounds.x, kCollapsedRailWidthDip);
    EXPECT_EQ(collapsed.toolbar_leading_inset_dip,
              BrowserChrome::TrafficLightClearDip() - kCollapsedRailWidthDip);
}

TEST(BrowserWindowTest, GivenSectionHeaderWhenFormattedThenItUsesTheParsedSemiBoldToken) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const auto ends_with = [](const std::string& text, std::string_view suffix) {
        return text.size() >= suffix.size() &&
               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    // gfx::FontList::ParseDescription (Chromium 150.0.7871.129) accepts "Semi-Bold" for weight
    // 600 and rejects "SemiBold", which would drop the whole font list.
    const std::string header = BrowserChrome::SectionHeaderFontList(tokens);
    EXPECT_TRUE(ends_with(header, ", Semi-Bold 11px")) << header;
    EXPECT_EQ(header.find("SemiBold"), std::string::npos);

    // The weight comes from the parameter, never from the size.
    EXPECT_TRUE(ends_with(BrowserChrome::FontListForTokens(tokens, 13, ChromeFontWeight::kSemiBold),
                          ", Semi-Bold 13px"));
    EXPECT_TRUE(ends_with(BrowserChrome::FontListForTokens(tokens, 11, ChromeFontWeight::kMedium),
                          ", Medium 11px"));
    EXPECT_TRUE(ends_with(BrowserChrome::FontListForTokens(tokens, 11, ChromeFontWeight::kBold),
                          ", Bold 11px"));
    EXPECT_TRUE(ends_with(BrowserChrome::FontListForTokens(tokens, 11, ChromeFontWeight::kRegular),
                          ", 11px"));
}

TEST(BrowserWindowTest, GivenWindowsSystemFontsThenSegoeUiVariablePrecedesTheFallback) {
    EXPECT_EQ(BrowserChrome::WindowsSystemFontFamilies(), "Segoe UI Variable, Segoe UI");
}

TEST(BrowserWindowTest, GivenNativeSpaceChipsWhenResolvedThenTheyUseNoSpaceColorAccent) {
    const ChromeTokens tokens = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const BrowserChrome::EntryColors active = BrowserChrome::SpaceChipColors(tokens, true);
    const BrowserChrome::EntryColors idle = BrowserChrome::SpaceChipColors(tokens, false);

    // Native segments: the selected one is `surface` on the `fill` track, the others sit on it.
    EXPECT_EQ(active.fill, tokens.surface);
    EXPECT_EQ(active.outline, tokens.surface);
    EXPECT_EQ(active.title, tokens.text);
    EXPECT_EQ(idle.fill, tokens.fill);
    EXPECT_NE(active.fill, tokens.selection);
    EXPECT_NE(active.fill, tokens.accent);
}

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

TEST(BrowserWindowTest, ClosingThePlanTabConfirmsWhetherResearchStops) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest("data:text/html,plan");
    window->OpenPlanFixtureTab();
    ASSERT_TRUE(window->research_running());
    ASSERT_EQ(ActiveSpace(*window).tab_count(), 2U);

    int confirmations = 0;
    window->SetPlanCloseConfirmationForTest([&confirmations](std::string_view title,
                                                             std::string_view stop,
                                                             std::string_view keep_running) {
        ++confirmations;
        EXPECT_EQ(title, "Stop the research?");
        EXPECT_EQ(stop, "Stop");
        EXPECT_EQ(keep_running, "Keep running");
        return false;
    });
    EXPECT_FALSE(window->CloseActiveSpaceTabIndex(0));
    EXPECT_EQ(confirmations, 1);
    EXPECT_EQ(ActiveSpace(*window).tab_count(), 2U);
    EXPECT_TRUE(window->research_running());

    window->SetPlanCloseConfirmationForTest(
        [&confirmations](std::string_view, std::string_view, std::string_view) {
            ++confirmations;
            return true;
        });
    EXPECT_TRUE(window->CloseActiveSpaceTabIndex(0));
    EXPECT_EQ(confirmations, 2);
    EXPECT_EQ(ActiveSpace(*window).tab_count(), 1U);
    EXPECT_FALSE(window->research_running());
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

// Regression: the window hosts several CefBrowserViews (tabs, split view, agent
// panel, local pages), which CEF only supports for Alloy style views. The Chrome
// default crashed inside CEF when the app was deactivated (NSWindow resign-key)
// and on quit, so the window and its browser views must never fall back to it.
TEST(BrowserWindowTest, GivenAHeadlessWindowWhenTheRuntimeStyleIsRequestedThenItIsAlloyNotChrome) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(kIslandRuntimeStyle, CEF_RUNTIME_STYLE_ALLOY);
    EXPECT_EQ(window->GetWindowRuntimeStyle(), CEF_RUNTIME_STYLE_ALLOY);
    EXPECT_EQ(window->GetBrowserRuntimeStyle(), CEF_RUNTIME_STYLE_ALLOY);
}

// Accepts absolute https URLs without whitespace, standing in for
// CefParseURL, which is unavailable headless.
ValidatedAddress AcceptHttps(std::string_view text) {
    if (text.substr(0, 8) != "https://" || text.find(' ') != std::string_view::npos) {
        return {.url = {}, .error = AddressError::kNotAbsolute};
    }
    return {.url = std::string(text), .error = std::nullopt};
}

CefRefPtr<BrowserWindow> MakeWebWindow() {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetAddressValidatorForTest(&AcceptHttps);
    return window;
}

TEST(BrowserWindowTest, GivenAClosedWebTabWhenReopenedThenItReturnsWithItsURLInTheActiveSpace) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    ASSERT_TRUE(window->OpenNewTab("https://example.com/a"));
    ASSERT_EQ(ActiveSpaceTabIds(*window).size(), 2U);
    window->ExecuteCommand(BrowserCommand::kCloseTab);
    ASSERT_EQ(ActiveSpaceTabIds(*window).size(), 1U);
    EXPECT_EQ(window->closed_tab_count(), 1U);

    window->ExecuteCommand(BrowserCommand::kReopenClosedTab);
    EXPECT_EQ(ActiveSpaceTabIds(*window).size(), 2U);
    EXPECT_EQ(ActiveSpace(*window).tabs().back().startup_url(), "https://example.com/a");
    EXPECT_EQ(window->active_tab_id(), ActiveSpaceTabIds(*window).back());
    EXPECT_EQ(window->closed_tab_count(), 0U);

    // An empty stack changes nothing.
    window->ExecuteCommand(BrowserCommand::kReopenClosedTab);
    EXPECT_EQ(ActiveSpaceTabIds(*window).size(), 2U);
}

TEST(BrowserWindowTest, GivenMoreThanTwentyClosedTabsThenTheStackKeepsTheNewestTwenty) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 25);
    ASSERT_EQ(ActiveSpaceTabIds(*window).size(), 26U);
    for (int closed = 0; closed < 25; ++closed) {
        window->ExecuteCommand(BrowserCommand::kCloseTab);
    }
    EXPECT_EQ(ActiveSpaceTabIds(*window).size(), 1U);
    EXPECT_EQ(window->closed_tab_count(), 20U);
}

TEST(BrowserWindowTest, GivenSeveralTabsWhenSelectLastTabRunsThenTheLastTabBecomesActive) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 3);
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    window->ExecuteCommand(BrowserCommand::kSelectLastTab);
    EXPECT_EQ(window->active_tab_id(), ActiveSpaceTabIds(*window).back());
    // Already on the last tab: still the last tab.
    window->ExecuteCommand(BrowserCommand::kSelectLastTab);
    EXPECT_EQ(window->active_tab_id(), ActiveSpaceTabIds(*window).back());
}

TEST(BrowserWindowTest, GivenTheAlternateTabKeysWhenRunThenTheySelectNextAndPreviousTabs) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 2);
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    window->RunKeyAction(KeyAction::kNextTabAlternate);
    EXPECT_EQ(window->active_tab_id(), ActiveSpaceTabIds(*window)[1]);
    window->RunKeyAction(KeyAction::kPreviousTabAlternate);
    EXPECT_EQ(window->active_tab_id(), ActiveSpaceTabIds(*window)[0]);
}

TEST(BrowserWindowTest, GivenSpacesWhenNextAndPreviousSpaceRunThenTheyWrapAround) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->CreateSpace("Work"));
    ASSERT_TRUE(window->CreateSpace("Notes"));
    ASSERT_EQ(window->space_count(), 3U);
    ASSERT_TRUE(window->SelectSpaceIndex(2));

    window->ExecuteCommand(BrowserCommand::kNextSpace);
    EXPECT_EQ(window->active_space_index(), 0U);
    window->ExecuteCommand(BrowserCommand::kPreviousSpace);
    EXPECT_EQ(window->active_space_index(), 2U);
    window->ExecuteCommand(BrowserCommand::kPreviousSpace);
    EXPECT_EQ(window->active_space_index(), 1U);
}

TEST(BrowserWindowTest, GivenASingleSpaceWhenNextSpaceRunsThenNothingChanges) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->ExecuteCommand(BrowserCommand::kNextSpace);
    window->ExecuteCommand(BrowserCommand::kPreviousSpace);
    EXPECT_EQ(window->space_count(), 1U);
    EXPECT_EQ(window->active_space_index(), 0U);
}

TEST(BrowserWindowTest, GivenAWebTabWhenCopyTabUrlRunsThenItsURLGoesToTheClipboardWriter) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    std::vector<std::string> copied;
    window->SetClipboardWriterForTest([&copied](std::string_view text) {
        copied.emplace_back(text);
        return true;
    });

    // The startup page has no web URL, so there is nothing to copy.
    window->ExecuteCommand(BrowserCommand::kCopyTabUrl);
    EXPECT_TRUE(copied.empty());

    ASSERT_TRUE(window->OpenNewTab("https://example.com/copy"));
    window->ExecuteCommand(BrowserCommand::kCopyTabUrl);
    ASSERT_EQ(copied.size(), 1U);
    EXPECT_EQ(copied[0], "https://example.com/copy");
}

TEST(BrowserWindowTest, GivenNoLoadingPageWhenStopLoadingIsAskedThenItIsUnavailable) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_FALSE(window->CanStopLoading());
    EXPECT_FALSE(window->CanStopLoadingWithEscape());
    // Stop with no browser is a tolerated no-op.
    window->ExecuteCommand(BrowserCommand::kStopLoading);
    EXPECT_FALSE(window->CanStopLoading());
}

TEST(BrowserWindowTest, GivenZoomStepsWhenSteppedThenTheyClampToTheUsableRange) {
    EXPECT_DOUBLE_EQ(SteppedZoomLevel(0.0, 1), 0.5);
    EXPECT_DOUBLE_EQ(SteppedZoomLevel(0.0, -1), -0.5);
    EXPECT_DOUBLE_EQ(SteppedZoomLevel(kZoomLevelMax, 1), kZoomLevelMax);
    EXPECT_DOUBLE_EQ(SteppedZoomLevel(kZoomLevelMin, -3), kZoomLevelMin);
    EXPECT_DOUBLE_EQ(SteppedZoomLevel(8.0, 1), kZoomLevelMax);
}
}  // namespace
}  // namespace island

namespace island {
namespace {

TEST(BrowserWindowTest, GivenTabsWithDifferentBlockedCountsWhenSelectingThenTheIndicatorsFollow) {
    const CefRefPtr<BrowserWindow> window = MakeWindow();
    AppendTabs(*window, 1);
    const std::vector<TabId> ids = ActiveSpaceTabIds(*window);
    ASSERT_EQ(ids.size(), 2U);
    for (int i = 0; i < 5; ++i) {
        window->RecordBlockedRequestForTab(ids[0]);
    }
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    EXPECT_TRUE(window->address_indicators().show_shield);
    EXPECT_EQ(window->address_indicators().shield_label, "5");

    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(1));
    EXPECT_FALSE(window->address_indicators().show_shield);

    window->RecordBlockedRequestForTab(ids[0]);
    EXPECT_FALSE(window->address_indicators().show_shield);
    ASSERT_TRUE(window->SelectActiveSpaceTabIndex(0));
    EXPECT_EQ(window->address_indicators().shield_label, "6");
}

}  // namespace
}  // namespace island
