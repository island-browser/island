#ifndef ISLAND_BROWSER_CHROME_H_
#define ISLAND_BROWSER_CHROME_H_

#if defined(_WIN32)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "address_bar_model.h"
#include "address_indicators.h"
#include "browser_command.h"
#include "chrome_snapshot.h"
#include "collapsed_rail.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "include/cef_image.h"
#include "navigation_state.h"
#include "page_card.h"
#include "row_raster.h"
#include "sidebar_state.h"
#include "tab_id.h"

class CefBoxLayout;
class CefView;
class CefBrowserView;
class CefButton;
class CefLabelButton;
class CefPanel;
class CefTextfield;

namespace island {

enum class ChromeViewId : int {
    kRoot = 1001,
    kRail = 1002,
    kNavigationRow = 1003,
    kBack = 1004,
    kForward = 1005,
    kAddressRow = 1006,
    kAddress = 1007,
    kReload = 1008,
    kValidationMessage = 1009,
    kSpacer = 1010,
    kDivider = 1011,
    kActiveTab = 1012,
    kBrowserContent = 1013,
    kBrowserView = 1014,
    kAddressLocationIcon = 1015,
    kActivePage = 1016,
    kActivePageFallbackFavicon = 1017,
    kActivePageIndicator = 1018,
    // Phase 3 collection regions start after the highest Phase 2 id; existing
    // values are never renumbered or reused.
    kTabStrip = 1019,
    kTabStripEntry = 1020,
    kTabStripEntryFavicon = 1021,
    kTabStripEntryTitle = 1022,
    kTabStripEntryClose = 1023,
    kSpaceSwitcher = 1024,
    kSpaceSwitcherEntry = 1025,
    kSpaceSwitcherEntryColorMark = 1026,
    kSpaceSwitcherEntryName = 1027,
    // Window-level regions. The palettes are reported through CefOverlayController;
    // all of them are deliberately absent from ViewTreeContract().
    // 1028 was the 2-DIP edge sliver shown while the sidebar was hidden. The sidebar
    // now collapses to an icon rail instead of disappearing, so the sliver is gone;
    // the value stays reserved and is never reused.
    kHoverSliver = 1028,
    kSearchPalette = 1029,
    kSearchPaletteQuery = 1030,
    kSearchPaletteProvider = 1031,
    kSearchPaletteProviderName = 1032,
    kCommandPalette = 1033,
    kCommandPaletteQuery = 1034,
    kCommandPaletteResult = 1035,
    kCommandPaletteResultName = 1036,
    kSpaceRenameOverlay = 1037,
    kSpaceRenameField = 1038,
    // U6 split view: the divider between the two panes lives inside
    // kBrowserContent between the two kBrowserView nodes, so the contract tree
    // (single view) is unchanged and only the live snapshot grows these nodes.
    kSplitDivider = 1039,
    // Welcome flow overlay (first-run import + appearance). Like the palettes,
    // it lives on the CefWindow, not in the rail/root contract tree.
    kWelcome = 1040,
    // The agent panel column at the right edge of the root (ACP chat). It is
    // a contract node with zero width while the panel is closed; the panel's
    // own CefBrowserView inside it is not a contract node.
    kAgentPanel = 1041,
    // Arc-style rail regions: the "New Tab" row above the tab list, and the
    // footer under the space bar holding the agent toggle and New Space.
    kRailNewTab = 1042,
    kRailFooter = 1043,
    kRailAgentButton = 1044,
    kRailNewSpaceButton = 1045,
    kRailTabsButton = 1046,
    kRailSettingsButton = 1047,
    // The collapsed icon rail (Cmd/Ctrl+B): a sibling of kRail inside the rail host, shown
    // instead of it. Like the palettes it is absent from ViewTreeContract(); its entries
    // are index-aligned with the tab and space projections.
    kCollapsedRail = 1048,
    kCollapsedRailTabList = 1049,
    kCollapsedRailTab = 1050,
    kCollapsedRailSeparator = 1051,
    kCollapsedRailNewTab = 1052,
    kCollapsedRailSpaceList = 1053,
    kCollapsedRailSpace = 1054,
    // The peek card (a floating page over the current one). Overlay-only, like the palettes,
    // so absent from ViewTreeContract().
    kPeekCard = 1055,
    kPeekTitleBar = 1056,
    kPeekAddress = 1057,
    kPeekOpenAsTab = 1058,
    kPeekClose = 1059,
    // Find-in-page bar overlay (like the palettes it lives on the CefWindow, not in the
    // contract tree) and the Reader button in the address pill (a contract node).
    kFindBar = 1060,
    kFindBarField = 1061,
    kFindBarCount = 1062,
    kFindBarPrevious = 1063,
    kFindBarNext = 1064,
    kFindBarMatchCase = 1065,
    kFindBarClose = 1066,
    kReaderButton = 1067,
    // Native-look toolbar. Existing IDs remain fixed; these append after the current range.
    kToolbar = 1068,
    kToolbarShare = 1069,
    // Retired from the Native toolbar on 2026-10-16: the sidebar Agent entry replaces it. The
    // id is kept so no later id moves; no view carries it any more.
    kToolbarAgent = 1070,
    kAddressIdleLabel = 1071,
    kFavoritesSectionHeader = 1072,
    kTabsSectionHeader = 1073,
    // Address indicators (shield, agent dot); Reader reuses kReaderButton.
    kAddressShield = 1074,
    kAddressAgentDot = 1075,
    // Native's bottom switcher row (the segment track plus the 24 DIP buttons). Added with the
    // 2026-10-16 Native sidebar; it sits inside kRailFooter in Native only.
    kRailSwitcherRow = 1076,
    // The branch label above the first grouped (research) tab row of a run; Native only.
    kTabGroupKicker = 1077,
};

struct ChromeViewTreeNode {
    ChromeViewId id;
    std::vector<ChromeViewTreeNode> children;

    bool operator==(const ChromeViewTreeNode&) const = default;
};

// One tab-strip row as projected from a Tab: the title label carries the
// truncating page title; favicon-or-fallback and the close affordance are the
// sibling affordance nodes in the contract tree. The active flag drives the
// row's active/inactive accessible state and surface tint.
struct TabStripEntrySnapshot {
    std::string title;
    bool active = false;
    // Pinned tabs form the top group of the list; their trailing affordance
    // unpins instead of closing.
    bool pinned = false;
    // The page's favicon once downloaded; null shows the globe fallback.
    CefRefPtr<CefImage> favicon;
    // A sleeping tab (its browser released, see tab_sleep_policy.h) is drawn dimmed: faded
    // title and a reduced-alpha favicon, in the rows and in the icon rail.
    bool sleeping = false;
    // A tab opened by research sits under its parent (the Plan tab) one level deep; the label
    // names the plan branch and is drawn as a kicker line above the first row of a run.
    std::optional<TabId> group_parent;
    std::string group_label;

    bool operator==(const TabStripEntrySnapshot&) const = default;
};

// One space-switcher row as projected from a Space: the color mark carries the
// space's color and the name label carries the truncating space name. The
// active flag drives the row's active/inactive accessible state and surface
// tint the same way.
struct SpaceSwitcherEntrySnapshot {
    ArgbColor color;
    std::string name;
    bool active = false;

    bool operator==(const SpaceSwitcherEntrySnapshot&) const = default;
};

struct ChromeGeometrySnapshot {
    DipRect root_bounds;
    DipRect rail_bounds;
    DipRect toolbar_bounds;
    int toolbar_leading_inset_dip = 0;
    DipRect browser_content_bounds;
    DipRect browser_view_bounds;
    // Zero width (at the right edge) while the agent panel is closed.
    DipRect agent_panel_bounds;

    bool operator==(const ChromeGeometrySnapshot&) const = default;
};

// Bounds of the four chrome regions managed by RootPanelDelegate. A layout pass may overwrite
// these without changing the requested ChromeGeometrySnapshot.
struct ManagedChromeBounds {
    DipRect rail;
    DipRect toolbar;
    DipRect browser_content;
    DipRect agent_panel;

    bool operator==(const ManagedChromeBounds&) const = default;
};

struct AddressSelectionSnapshot {
    bool has_focus = false;
    bool has_selection = false;

    bool operator==(const AddressSelectionSnapshot&) const = default;
};

// What a tab-strip or space-switcher entry button did. Declared at namespace
// scope so the chrome's private handler can name it without pulling the
// delegate's definition into the header.
enum class CollectionButtonAction : std::uint8_t {
    kActivateTab,
    kCloseTab,
    kActivateSpace,
    kUnpinTab,
};

// What the collapsed rail draws for a tab projection: the tab order (source indices into
// the projection, pinned tabs first and otherwise in list order) and the position in that
// order where the unpinned group starts, or npos when only one group exists.
struct CollapsedRailTabLayout {
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::vector<std::size_t> order;
    std::size_t separator_before = npos;
};

enum class ChromeFontWeight : std::uint8_t {
    kRegular,
    kMedium,
    kSemiBold,
    kBold,
};

struct AddressPlateStyle {
    ArgbColor fill;
    ArgbColor outside;
    int outline_dip = 0;
    int radius_dip = 0;

    bool operator==(const AddressPlateStyle&) const = default;
};

class BrowserChromeHost {
  public:
    virtual ~BrowserChromeHost() = default;

    virtual void ExecuteBrowserCommand(BrowserCommand command) = 0;
    // Collection-entry activation, reported by entry position in the current
    // projection. Hosts must keep these safe to call headless (no chrome).
    virtual void SelectTab(std::size_t index) = 0;
    virtual void CloseTab(std::size_t index) = 0;
    virtual void SelectSpace(std::size_t index) = 0;
    virtual void BeginAddressEditing() = 0;
    virtual void CancelAddressEditing() = 0;
    virtual void SubmitAddressDraft(std::string_view draft) = 0;
    virtual void FocusBrowserView() = 0;
    // Arc-style rail affordances.
    virtual void SetTabPinned(std::size_t index, bool pinned) = 0;
    virtual void ToggleAgentPanel() = 0;
    virtual void ToggleTabOverview() = 0;
    virtual void ToggleSettings() = 0;
    // Settings scrolled to Privacy, where the blocker and its per-site allowlist live.
    virtual void OpenBlockerSettings() = 0;
};

class BrowserChrome final : public NavigationObserver {
  public:
    BrowserChrome(BrowserChromeHost& host, CefRefPtr<CefBrowserView> browser_view,
                  ChromeTokens tokens, std::filesystem::path icon_resource_root);
    ~BrowserChrome() override;

    BrowserChrome(const BrowserChrome&) = delete;
    BrowserChrome& operator=(const BrowserChrome&) = delete;

    [[nodiscard]] CefRefPtr<CefPanel> root() const;
    [[nodiscard]] CefRefPtr<CefPanel> sidebar() const;
    // The browser views currently in the page card slot (one, or two in split view).
    [[nodiscard]] std::vector<CefRefPtr<CefBrowserView>> visible_browser_views() const;
    [[nodiscard]] ChromeViewTreeNode view_tree_snapshot() const;
    // The projection view_tree_snapshot() returns: the look's collection contract, plus the
    // divider and second pane under kBrowserContent while a split is shown.
    [[nodiscard]] static ChromeViewTreeNode LiveViewTree(
        const std::vector<TabStripEntrySnapshot>& tabs,
        const std::vector<SpaceSwitcherEntrySnapshot>& spaces, LookPreset look, bool split_active);
    [[nodiscard]] ChromeGeometrySnapshot view_bounds_snapshot() const;
    [[nodiscard]] AddressSelectionSnapshot address_selection_snapshot() const;

    // The Reader button in the address pill: `available` for an article-capable (http/https)
    // page, `active` while the tab shows Island's reader view of it.
    void SetReaderState(bool available, bool active);
    [[nodiscard]] bool reader_available() const noexcept { return reader_available_; }
    [[nodiscard]] bool reader_active() const noexcept { return reader_active_; }
    // The shield, Reader and agent-dot indicators inside the Native address field.
    void SetAddressIndicators(const AddressIndicators& indicators);

    // Focus-ring thickness of the address pill and ActivePageIndicator thickness in
    // DIP; both are 2 so the accent marks read as one weight.
    [[nodiscard]] static constexpr int AddressFocusLeadingEdgeDip() { return 2; }
    [[nodiscard]] static constexpr int ActivePageIndicatorWidthDip() {
        return AddressFocusLeadingEdgeDip();
    }

    // Top inset above the navigation row on platforms that keep their own title bar, which
    // already clears the window controls: the rail starts on the same spacing_3 (12) gutter as
    // its sides. With a frameless window (ChromeTokens::titlebar_band_dip > 0) the navigation row
    // is vertically centered in that band instead, next to the traffic lights.
    [[nodiscard]] static constexpr int RailTopInsetDip() { return 12; }
    [[nodiscard]] static int RailTopInsetDip(const ChromeTokens& tokens) {
        return tokens.titlebar_band_dip > 0
                   ? std::max(0, (tokens.titlebar_band_dip - tokens.control_height_dip) / 2)
                   : RailTopInsetDip();
    }
    // Height of the frameless titlebar band (macOS traffic lights centered in it) and the width
    // the three buttons occupy from the window's left edge, plus a gap.
    [[nodiscard]] static constexpr int TrafficLightBandDip() { return 40; }
    [[nodiscard]] static constexpr int TrafficLightClearDip() { return 92; }
    // Extra leading space of the navigation row (past the rail's own gutter) that clears them.
    [[nodiscard]] static constexpr int NavigationLeadDip() { return TrafficLightClearDip() - 12; }

    // Vertical cadence between rail sections: one spacing_2 (8) step, so the
    // controls read as one dense, even column (Graphite, "Linear-calm").
    [[nodiscard]] static constexpr int RailSectionSpacingDip() { return 8; }
    [[nodiscard]] static constexpr int NativeToolbarHeightDip() { return 52; }
    [[nodiscard]] static constexpr int NativeToolbarButtonDip() { return 28; }
    [[nodiscard]] static constexpr int NativeAddressHeightDip() { return 30; }
    [[nodiscard]] static constexpr int NativeAddressRadiusDip() { return 8; }
    [[nodiscard]] static constexpr int NativeSectionHeaderHeightDip() { return 20; }
    [[nodiscard]] static constexpr int GroupedTabIndentDip() { return 16; }
    [[nodiscard]] static constexpr int GroupedTabKickerFontPx() { return 10; }
    [[nodiscard]] static constexpr int GroupedTabKickerHeightDip() { return 16; }
    // True when entry `index` is the first row of a run of grouped tabs with one label, which
    // gets a kicker line above it.
    [[nodiscard]] static bool NeedsGroupKicker(const std::vector<TabStripEntrySnapshot>& entries,
                                               std::size_t index) {
        const TabStripEntrySnapshot& entry = entries[index];
        if (!entry.group_parent.has_value() || entry.group_label.empty()) {
            return false;
        }
        if (index == 0) {
            return true;
        }
        const TabStripEntrySnapshot& previous = entries[index - 1];
        return previous.group_parent != entry.group_parent ||
               previous.group_label != entry.group_label;
    }
    // The address field's plate: Native draws a `fill` plate with radius 8 and no hairline on the
    // toolbar; the other looks keep the hairline-outlined well on the canvas.
    [[nodiscard]] static AddressPlateStyle AddressPlateStyleFor(const ChromeTokens& tokens);
    // The color the address row paints around its children: the hairline (accent while focused),
    // or the plate fill itself in Native so no outline or ring shows.
    [[nodiscard]] static ArgbColor AddressOutlineColor(const ChromeTokens& tokens, bool focused);
    // The corner patch that rounds the address plate at `bounds` (window DIP). Native's address
    // sits in the always-shown toolbar; the other looks' address lives in the expanded sidebar
    // only, so it has no patch while the sidebar is collapsed.
    [[nodiscard]] static std::optional<CornerPatch> AddressCornerPatch(const ChromeTokens& tokens,
                                                                       const DipRect& bounds,
                                                                       bool sidebar_expanded);
    // A CEF font list ("Family, [Medium |Semi-Bold |Bold ]13px") for the resolved interface font.
    // The weight is explicit. The tokens are those gfx::FontList::ParseDescription accepts in
    // the pinned Chromium (150.0.7871.129); an unknown token ("SemiBold") fails the whole list.
    [[nodiscard]] static std::string FontListForTokens(const ChromeTokens& tokens, int size_px,
                                                       ChromeFontWeight weight);
    // Native's sentence-case section headers ("Favorites", "<Space> · Tabs"): 11 px, weight 600.
    [[nodiscard]] static std::string SectionHeaderFontList(const ChromeTokens& tokens) {
        return FontListForTokens(tokens, 11, ChromeFontWeight::kSemiBold);
    }

    // Default gutter around the page card (ChromeTokens::card_inset_dip is the live value): the
    // card floats on the sidebar-colored canvas, inset on its top, outer side and bottom and flush
    // with the sidebar on the fourth side. CEF cannot round or clip the CefBrowserView, so the
    // macOS helper (macos/page_card_mac.mm) rounds it natively; elsewhere the corners stay square.
    [[nodiscard]] static constexpr int BrowserContentPaddingDip() { return 8; }

    // The agent panel column: a fixed 380 DIP that yields to the page, so the
    // browser content never drops below MinimumBrowserContentWidthDip.
    [[nodiscard]] static constexpr int AgentPanelWidthDip() { return 380; }
    [[nodiscard]] static constexpr int MinimumBrowserContentWidthDip() { return 320; }

    enum class SurfaceSlot : std::uint8_t {
        kRoot,
        kRail,
        kBrowserContent,
        kToolbar,
        kHairline,
        kAddressWell,
        // The hairline (accent while focused) the address pill paints around its
        // surface-colored children.
        kAddressOutline,
        kNavControl,
        kActivePage,
        kAccent,
    };
    [[nodiscard]] static ArgbColor ChromeSurfaceRole(SurfaceSlot slot, ChromeTheme theme) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        switch (slot) {
            case SurfaceSlot::kRoot:
            case SurfaceSlot::kBrowserContent:
            case SurfaceSlot::kRail:
                // One surface: the sidebar and the window margin around the page card share
                // the canvas, so the card floats on it (Arc).
                return tokens.canvas;
            case SurfaceSlot::kToolbar:
                return tokens.surface_secondary;
            case SurfaceSlot::kHairline:
            case SurfaceSlot::kAddressOutline:
                return tokens.border;
            case SurfaceSlot::kAddressWell:
                // The address pill is a soft well on the canvas.
                return tokens.well;
            case SurfaceSlot::kNavControl:
                return tokens.hover;
            case SurfaceSlot::kActivePage:
                return tokens.selected;
            case SurfaceSlot::kAccent:
                return tokens.accent;
        }
        return tokens.background;
    }
    [[nodiscard]] ArgbColor ChromeSurfaceRoleForResolvedTokens(SurfaceSlot slot) const {
        switch (slot) {
            case SurfaceSlot::kRoot:
            case SurfaceSlot::kBrowserContent:
            case SurfaceSlot::kRail:
                return tokens_.canvas;
            case SurfaceSlot::kToolbar:
                return tokens_.surface_secondary;
            case SurfaceSlot::kHairline:
            case SurfaceSlot::kAddressOutline:
                return tokens_.border;
            case SurfaceSlot::kAddressWell:
                return tokens_.well;
            case SurfaceSlot::kNavControl:
                return tokens_.hover;
            case SurfaceSlot::kActivePage:
                return tokens_.selected;
            case SurfaceSlot::kAccent:
                return tokens_.accent;
        }
        return tokens_.background;
    }

    // Resolved colors of one collection row. `outline` is the hairline painted
    // around the selected row (the rail color otherwise, so the row has no edge),
    // `fill` the row surface, `title` the label and `trailing` the favicon/close
    // glyph color. Resolved per entry state so a theme reset can repaint every row.
    struct EntryColors {
        ArgbColor outline;
        ArgbColor fill;
        ArgbColor title;
        ArgbColor trailing;
    };
    // The selected row is a soft `selected` fill with no outline (Arc); pinned rows sit half a
    // step toward it; every other row is the canvas.
    [[nodiscard]] static EntryColors TabRowColors(const ChromeTokens& tokens, bool active,
                                                  bool pinned) {
        const ArgbColor rail = tokens.canvas;
        RowColors row = ResolveRowColors(tokens, active, /*hovered=*/false, /*slept=*/false);
        if (tokens.look != LookPreset::kNative && pinned && !active) {
            row.fill = MixColor(rail, tokens.selected, 0.5);
        }
        return {
            .outline = row.fill,
            .fill = row.fill,
            .title = row.title,
            .trailing =
                tokens.look == LookPreset::kNative && active ? row.title : tokens.text_secondary,
        };
    }
    // Native's Agent entry (2026-10-16): full width at the sidebar bottom. Open takes the
    // accent_soft fill with accent_text; closed is the bare sidebar with an accent sparkle, and
    // hover takes the hover token.
    struct NativeAgentColors {
        ArgbColor fill;
        ArgbColor text;
        ArgbColor sparkle;
    };
    [[nodiscard]] static NativeAgentColors NativeAgentEntryColors(const ChromeTokens& tokens,
                                                                  bool open, bool hovered) {
        if (open) {
            return {.fill = tokens.accent_soft,
                    .text = tokens.accent_text,
                    .sparkle = tokens.accent};
        }
        return {.fill = hovered ? tokens.hover : tokens.canvas,
                .text = tokens.text,
                .sparkle = tokens.accent};
    }
    // Native's space segmented control: a `fill` track, the selected segment on `surface`,
    // the selected name in `text` and the others in `text_secondary`.
    struct SegmentPalette {
        ArgbColor track;
        ArgbColor fill;
        ArgbColor title;
    };
    [[nodiscard]] static SegmentPalette NativeSegmentColors(const ChromeTokens& tokens,
                                                            bool active) {
        return {.track = tokens.fill,
                .fill = active ? tokens.segment_selected : tokens.fill,
                .title = active ? tokens.text : tokens.text_secondary};
    }
    // Native's bottom switcher (spec §3, 2026-10-16): the segmented track and its three 24 DIP
    // icon buttons share one row; the segments sit inside the track's 2 DIP inset.
    struct NativeSwitcherMetrics {
        int row_dip;
        int button_dip;
        int track_inset_dip;
        int segment_dip;
    };
    [[nodiscard]] static constexpr NativeSwitcherMetrics NativeSwitcherSizes() {
        return {.row_dip = 24, .button_dip = 24, .track_inset_dip = 2, .segment_dip = 20};
    }
    // Space chips stay quiet: no fill and no outline (the dot, and the full name on the
    // active chip, carry the state); the active chip's label takes --text. Native segments
    // use NativeSegmentColors instead.
    [[nodiscard]] static EntryColors SpaceChipColors(const ChromeTokens& tokens, bool active) {
        const ArgbColor rail = tokens.canvas;
        if (tokens.look == LookPreset::kNative) {
            const SegmentPalette segment = NativeSegmentColors(tokens, active);
            return {
                .outline = segment.fill,
                .fill = segment.fill,
                .title = segment.title,
                .trailing = tokens.text_secondary,
            };
        }
        return {
            .outline = rail,
            .fill = rail,
            .title = active ? tokens.text : tokens.text_secondary,
            .trailing = tokens.text_secondary,
        };
    }

    // Justified: navigation, location, and the favicon placeholder use the
    // secondary tone so the canonical accent only signals an action or active
    // state (contract rules.accent_semantics); the accent would otherwise
    // claim idle affordances.
    [[nodiscard]] static constexpr ChromeIconTone NavigationIconTone() {
        return ChromeIconTone::kSecondary;
    }
    [[nodiscard]] static constexpr ChromeIconTone AddressLocationIconTone() {
        return ChromeIconTone::kSecondary;
    }
    [[nodiscard]] static constexpr ChromeIconTone FallbackFaviconIconTone() {
        return ChromeIconTone::kSecondary;
    }
    [[nodiscard]] static ChromeGeometrySnapshot LayoutForBounds(DipRect root_bounds,
                                                                const ChromeTokens& tokens) {
        return LayoutForBounds(root_bounds, tokens, /*sidebar_expanded=*/true);
    }
    [[nodiscard]] static ChromeGeometrySnapshot LayoutForBounds(DipRect root_bounds,
                                                                const ChromeTokens& tokens,
                                                                bool sidebar_expanded) {
        return LayoutForBounds(root_bounds, tokens, sidebar_expanded,
                               /*agent_panel_open=*/false);
    }
    // The collapsed sidebar occupies the icon rail's 52 DIP of layout width instead of the
    // full 286; the rail stays in the view tree, so the contract shape is unchanged and
    // only the content x-offset moves. This is the single layout path for both states.
    // The open agent panel takes its column from the window edge opposite the sidebar before
    // the card inset applies; closed, it is a zero-width rect at that edge.
    [[nodiscard]] static ChromeGeometrySnapshot LayoutForBounds(DipRect root_bounds,
                                                                const ChromeTokens& tokens,
                                                                bool sidebar_expanded,
                                                                bool agent_panel_open);
    // A repeated root snapshot is a no-op only while every managed region remains at its target.
    // The live-bounds input keeps this decision CEF-free and testable.
    [[nodiscard]] static bool NeedsRelayout(const std::optional<ChromeGeometrySnapshot>& applied,
                                            const ChromeGeometrySnapshot& requested,
                                            const ManagedChromeBounds& actual) {
        return !applied.has_value() || *applied != requested ||
               actual.rail != requested.rail_bounds || actual.toolbar != requested.toolbar_bounds ||
               actual.browser_content != requested.browser_content_bounds ||
               actual.agent_panel != requested.agent_panel_bounds;
    }

    // Insets of the page card (browser_view_bounds inside browser_content_bounds) and of the
    // agent panel's own card, resolved for the same inputs as LayoutForBounds.
    struct CardInsets {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };
    [[nodiscard]] static CardInsets ContentInsets(DipRect root_bounds, const ChromeTokens& tokens,
                                                  bool sidebar_expanded, bool agent_panel_open);
    [[nodiscard]] static CardInsets AgentPanelInsets(DipRect root_bounds,
                                                     const ChromeTokens& tokens,
                                                     bool sidebar_expanded);

    // The window regions that drag the window in a frameless window: the sidebar's top band
    // (minus the navigation buttons) and the canvas margin above the page card.
    struct DragRegion {
        DipRect bounds;
        bool draggable = true;
    };
    [[nodiscard]] static std::vector<DragRegion> DragRegionsForBounds(DipRect root_bounds,
                                                                      const ChromeTokens& tokens,
                                                                      bool sidebar_expanded,
                                                                      bool agent_panel_open);
    // The fixed rail regions keep their Phase 2 shape; the collection regions
    // (kTabStrip, kSpaceSwitcher) are present but empty because Phase 3 U3
    // wires no real tab/space data yet. Per-entry structure is asserted
    // through CollectionCountContract below, not by absolute rail index.
    [[nodiscard]] static ChromeViewTreeNode ViewTreeContract(LookPreset look = LookPreset::kArc) {
        const bool native = look == LookPreset::kNative;
        const ChromeViewTreeNode navigation = {
            ChromeViewId::kNavigationRow,
            {{ChromeViewId::kBack, {}}, {ChromeViewId::kForward, {}}}};
        ChromeViewTreeNode address = {ChromeViewId::kAddressRow,
                                      {{ChromeViewId::kAddressLocationIcon, {}},
                                       {ChromeViewId::kAddress, {}},
                                       {ChromeViewId::kReload, {}},
                                       {ChromeViewId::kReaderButton, {}}}};
        if (native) {
            address.children.insert(address.children.begin() + 2,
                                    {ChromeViewId::kAddressIdleLabel, {}});
        }
        std::vector<ChromeViewTreeNode> rail_children;
        if (!native) {
            rail_children.push_back(navigation);
            rail_children.push_back(address);
        }
        rail_children.push_back({ChromeViewId::kValidationMessage, {}});
        // Arc and Graphite list New Tab above the tabs; Native moves it to the list's end.
        if (!native) {
            rail_children.push_back({ChromeViewId::kRailNewTab, {}});
        }
        rail_children.insert(rail_children.end(), {{ChromeViewId::kTabStrip, {}},
                                                   {ChromeViewId::kSpacer, {}},
                                                   {ChromeViewId::kDivider, {}}});
        if (native) {
            // Bottom-up the owner's layout reads: the full-width Agent entry at the very bottom,
            // with the switcher row (segments plus the new-space, overview and settings buttons)
            // above it.
            rail_children.push_back(
                {ChromeViewId::kRailFooter,
                 {{ChromeViewId::kRailSwitcherRow,
                   {{ChromeViewId::kSpaceSwitcher, {}},
                    {ChromeViewId::kRailNewSpaceButton, {}},
                    {ChromeViewId::kRailTabsButton, {}},
                    {ChromeViewId::kRailSettingsButton, {}}}},
                  {ChromeViewId::kRailAgentButton, {}}}});
        } else {
            rail_children.push_back({ChromeViewId::kSpaceSwitcher, {}});
            rail_children.push_back({ChromeViewId::kRailFooter,
                                     {{ChromeViewId::kRailAgentButton, {}},
                                      {ChromeViewId::kRailTabsButton, {}},
                                      {ChromeViewId::kRailSettingsButton, {}},
                                      {ChromeViewId::kRailNewSpaceButton, {}}}});
        }
        rail_children.push_back({ChromeViewId::kActivePage,
                                 {{ChromeViewId::kActivePageFallbackFavicon, {}},
                                  {ChromeViewId::kActiveTab, {}},
                                  {ChromeViewId::kActivePageIndicator, {}}}});
        std::vector<ChromeViewTreeNode> root_children = {
            {ChromeViewId::kRail, std::move(rail_children)}};
        if (native) {
            root_children.push_back({ChromeViewId::kToolbar,
                                     {navigation, address, {ChromeViewId::kToolbarShare, {}}}});
        }
        root_children.push_back(
            {ChromeViewId::kBrowserContent, {{ChromeViewId::kBrowserView, {}}}});
        root_children.push_back({ChromeViewId::kAgentPanel, {}});
        return {ChromeViewId::kRoot, std::move(root_children)};
    }

    // Projects the collection regions the way U4/U5 wire them at runtime: one
    // entry node per tab/space snapshot, each with the fixed sub-shape the
    // design requires (tab: favicon-or-fallback, truncating title, close
    // affordance; space: color mark, truncating name). The count comes from
    // the snapshots, so no absolute positional index is ever asserted against
    // these regions. This is the single projection both the runtime view tree
    // snapshot and the contract tests go through.
    [[nodiscard]] static ChromeViewTreeNode CollectionCountContract(
        const std::vector<TabStripEntrySnapshot>& tabs,
        const std::vector<SpaceSwitcherEntrySnapshot>& spaces, LookPreset look = LookPreset::kArc) {
        ChromeViewTreeNode tree = ViewTreeContract(look);
        const bool native = look == LookPreset::kNative;
        // Native nests the switcher inside the footer's switcher row, so it is found by id
        // anywhere in the tree rather than only among the rail's direct children.
        const auto find = [](ChromeViewTreeNode& node, ChromeViewId id,
                             const auto& self) -> ChromeViewTreeNode* {
            for (ChromeViewTreeNode& child : node.children) {
                if (child.id == id) {
                    return &child;
                }
                if (ChromeViewTreeNode* match = self(child, id, self); match != nullptr) {
                    return match;
                }
            }
            return nullptr;
        };
        if (ChromeViewTreeNode* tab_strip = find(tree, ChromeViewId::kTabStrip, find);
            tab_strip != nullptr) {
            tab_strip->children.reserve(tabs.size() + 3);
            bool favorites_header_added = false;
            bool tabs_header_added = false;
            for (std::size_t i = 0; i < tabs.size(); ++i) {
                if (native && tabs[i].pinned && !favorites_header_added) {
                    tab_strip->children.push_back({ChromeViewId::kFavoritesSectionHeader, {}});
                    favorites_header_added = true;
                }
                if (native && !tabs[i].pinned && !tabs_header_added) {
                    tab_strip->children.push_back({ChromeViewId::kTabsSectionHeader, {}});
                    tabs_header_added = true;
                }
                if (native && NeedsGroupKicker(tabs, i)) {
                    tab_strip->children.push_back({ChromeViewId::kTabGroupKicker, {}});
                }
                tab_strip->children.push_back({ChromeViewId::kTabStripEntry,
                                               {{ChromeViewId::kTabStripEntryFavicon, {}},
                                                {ChromeViewId::kTabStripEntryTitle, {}},
                                                {ChromeViewId::kTabStripEntryClose, {}}}});
            }
            if (native && !tabs_header_added) {
                tab_strip->children.push_back({ChromeViewId::kTabsSectionHeader, {}});
            }
            // Native's New Tab row is the list's last row, after the last tab.
            if (native) {
                tab_strip->children.push_back({ChromeViewId::kRailNewTab, {}});
            }
        }
        if (ChromeViewTreeNode* switcher = find(tree, ChromeViewId::kSpaceSwitcher, find);
            switcher != nullptr) {
            switcher->children.reserve(spaces.size());
            for (std::size_t i = 0; i < spaces.size(); ++i) {
                // Native segments carry only the name: the space color is never shown there.
                ChromeViewTreeNode segment = {ChromeViewId::kSpaceSwitcherEntry, {}};
                if (!native) {
                    segment.children.push_back({ChromeViewId::kSpaceSwitcherEntryColorMark, {}});
                }
                segment.children.push_back({ChromeViewId::kSpaceSwitcherEntryName, {}});
                switcher->children.push_back(std::move(segment));
            }
        }
        return tree;
    }

    // Rail rows show at most this many characters of a tab title or space name;
    // longer text keeps the first characters and an ellipsis so the cut never
    // splits a UTF-8 sequence. The accessible names below always carry the full
    // untruncated text.
    [[nodiscard]] static constexpr std::size_t CollectionTitleCharacterLimit() { return 24; }
    [[nodiscard]] static std::string TruncateCollectionTitle(std::string title);
    // Accessible names announce the entry text and the active/inactive state,
    // matching the Phase 2 rail requirement for keyboard-focusable entries.
    [[nodiscard]] static std::string TabEntryAccessibleName(const TabStripEntrySnapshot& entry);
    [[nodiscard]] static std::string TabCloseAccessibleName(const TabStripEntrySnapshot& entry);
    // Inactive space pills show only an initial; the active one shows the name.
    [[nodiscard]] static std::string SpacePillLabel(const SpaceSwitcherEntrySnapshot& entry);
    [[nodiscard]] static std::string SpaceEntryAccessibleName(
        const SpaceSwitcherEntrySnapshot& entry);

    // Rebuilds the tab-strip collection from the active space's Tab list and
    // the space switcher from the window's space list. Rows are keyboard
    // focusable through their affordance buttons and announce active state.
    void SetTabStripEntries(const std::vector<TabStripEntrySnapshot>& entries);
    void SetSpaceSwitcherEntries(const std::vector<SpaceSwitcherEntrySnapshot>& entries);
    // Swaps the CefBrowserView living in the content slot when the active tab
    // or space changes. The previous view is detached, not destroyed; the new
    // one takes the kBrowserView contract id. Leaving split view (if active)
    // always tears the second pane and divider down first.
    void AttachBrowserView(CefRefPtr<CefBrowserView> browser_view);
    // U6 split view: attaches both panes side by side with the divider between
    // them. The first (active) view keeps the kBrowserView id; flex weights
    // follow the current split ratio. Both views must be non-null and distinct.
    void AttachSplitBrowserViews(CefRefPtr<CefBrowserView> first, CefRefPtr<CefBrowserView> second);
    // Nudges the divider. Ratios outside [SplitRatioMin, SplitRatioMax] are
    // clamped; the flex weights re-apply only while a split is attached.
    void SetSplitRatio(double ratio);
    [[nodiscard]] double split_ratio() const noexcept { return split_ratio_; }
    [[nodiscard]] bool split_active() const noexcept { return split_view_ != nullptr; }
    [[nodiscard]] std::size_t browser_view_size_change_count() const noexcept {
        return browser_view_size_change_count_;
    }
    [[nodiscard]] std::size_t relayout_count() const noexcept { return relayout_count_; }

    // Split divider geometry. The divider is keyboard-adjustable through the
    // kMoveDividerLeft/Right commands (drag is a documented deviation: the
    // pinned CEF distribution exposes no mouse events on custom views).
    [[nodiscard]] static constexpr double SplitRatioMin() { return 0.2; }
    [[nodiscard]] static constexpr double SplitRatioMax() { return 0.8; }
    [[nodiscard]] static constexpr double SplitRatioDefault() { return 0.5; }
    [[nodiscard]] static constexpr double SplitRatioStep() { return 0.05; }
    [[nodiscard]] static constexpr int SplitDividerWidthDip() { return 2; }

    // The collapsed icon rail lists the tabs pinned-first (the projection is already in that
    // order because Space keeps its pinned tabs on top; this re-asserts it without ever
    // reordering within a group) and reports each tab by its source index, which is what
    // the host's SelectTab needs. The separator sits where the unpinned group begins.
    [[nodiscard]] static CollapsedRailTabLayout CollapsedRailLayout(
        const std::vector<TabStripEntrySnapshot>& tabs);
    // The tooltip of a rail icon is the full page title (or space name); the accessible
    // names reuse TabEntryAccessibleName / SpaceEntryAccessibleName.
    [[nodiscard]] static std::string CollapsedRailTabTooltip(const TabStripEntrySnapshot& entry);
    [[nodiscard]] static std::string CollapsedRailSpaceTooltip(
        const SpaceSwitcherEntrySnapshot& entry);

    // Rail metrics: every control row is one 32 DIP height (spacing_6 + spacing_2),
    // so tab rows, space chips, footer buttons, and the address pill align.
    [[nodiscard]] static constexpr int TabRowHeightDip() { return 32; }
    [[nodiscard]] static constexpr int SpacePillHeightDip() { return 32; }
    // The tab list scrolls inside the rail; its rows stop short of the rail's
    // right inset by this gutter so a classic scrollbar never forces a
    // horizontal one.
    [[nodiscard]] static constexpr int TabListScrollGutterDip() { return 10; }

    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;
    void OnAddressChanged(const AddressBarSnapshot& snapshot);
    void ApplyTheme(ChromeTokens tokens);
    // Applies the expand state to the rail's layout width. Runtime geometry only:
    // the rail is never removed from the tree.
    // `expanded` selects the full 286 DIP sidebar; false shows the icon rail instead.
    void SetSidebarExpanded(bool expanded);
    [[nodiscard]] bool sidebar_expanded() const noexcept { return sidebar_expanded_; }
    // Hosts the agent panel's view in the kAgentPanel column (once) and opens
    // or closes the column.
    void SetAgentPanelView(CefRefPtr<CefView> view);
    void SetAgentPanelOpen(bool open);
    [[nodiscard]] bool agent_panel_open() const noexcept { return agent_panel_open_; }
    // A short updater notice ("Island 0.5.0 is available"): the rail's
    // Settings button takes the accent tint and carries it in its tooltip
    // and accessible name. "" restores the plain button.
    void SetUpdateNotice(std::string notice);
    [[nodiscard]] const std::string& update_notice() const noexcept { return update_notice_; }
    void BeginAddressEditing();
    void Detach();
    // The window host applies the corner patches (page_card.h) the chrome computes for its soft
    // rounded fills; the chrome re-pushes them when rows change, hover, scroll or relayout.
    void SetCornerPatchSink(std::function<void(const std::vector<CornerPatch>&)> sink);
    [[nodiscard]] std::vector<CornerPatch> ComputeCornerPatches() const;
    // Repaints every chrome view from the resolved tokens and each entry's current
    // state. Runs after CEF's asynchronous theme reset has wiped the custom colors.
    void ReapplyThemeColors();

    // Resolves the current tokens' color for a chrome surface slot. Exposed so the
    // surface-panel delegate can re-assert the color when CEF resets custom view
    // backgrounds on theme change.
    // The address outline resolves to the accent while the field has focus.
    [[nodiscard]] ArgbColor ResolveSurfaceColor(SurfaceSlot slot) const;

  private:
    class PanelDelegate;
    class RootPanelDelegate;
    class SurfacePanelDelegate;
    class ButtonDelegate;
    class CollectionButtonDelegate;
    class TextfieldDelegate;

  public:
    // Runs a collection-button action; posted from HandleCollectionButtonPressed so the
    // rebuild never happens inside the pressed button's own callback.
    void DispatchCollectionAction(CollectionButtonAction action, std::size_t index);

  private:
    // Called by every chrome delegate when CEF resets a view's colors on a theme
    // change; schedules one ReapplyThemeColors() pass.
    void HandleViewThemeReset();
    void HandleButtonPressed(ChromeViewId view_id);
    void HandleCollectionButtonPressed(CollectionButtonAction action, std::size_t index);
    void UpdateTabStripPreferredSize();
    void RebuildCollapsedTabs();
    void RebuildCollapsedSpaces();
    void UpdateCollapsedTabListSize();
    void ApplyRailButtonTheme();
    bool HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleAddressUserAction(CefRefPtr<CefTextfield> textfield);
    void HandleAddressFocus();
    void HandleAddressBlur();
    void ProjectNavigation(const NavigationSnapshot& snapshot);
    void ProjectAddress(const AddressBarSnapshot& snapshot);
    void ScheduleAddressSelection();
    void ApplyControlTheme();
    // Re-creates the box layouts whose insets depend on the tokens (sidebar side, titlebar band,
    // card inset, density) and the expand/agent state; a no-op while nothing relevant changed.
    void ApplyLayoutTokens();
    // Developer diagnostics: record root relayouts and changes to the attached page bounds.
    // The counters are read only by the existing --island-dev-keep-open SIGUSR2 report.
    void RecordRelayout();
    // The row/control heights follow the density token; rebuilds the rows when it changes.
    void ApplyDensity();
    void ApplyLookStructure();
    // FontListForTokens for the current tokens.
    [[nodiscard]] std::string FontList(int size_px, ChromeFontWeight weight) const;
    [[nodiscard]] int RowHeight() const { return tokens_.control_height_dip; }
    [[nodiscard]] int NavigationButtonSize() const {
        return tokens_.look == LookPreset::kNative ? NativeToolbarButtonDip()
                                                   : tokens_.control_height_dip;
    }
    [[nodiscard]] int AddressHeight() const {
        return tokens_.look == LookPreset::kNative ? NativeAddressHeightDip()
                                                   : tokens_.control_height_dip;
    }
    [[nodiscard]] std::string ActiveSpaceSectionTitle() const;
    void ApplyCollectionTheme();
    // Re-renders the soft rounded fill (selected / hovered / none) of one row's edge buttons.
    void ApplyTabRowVisual(std::size_t index);
    void ApplyCollapsedTabVisual(std::size_t position);
    void ApplyAddressPillImages();
    void ApplyAddressIndicators();
    // Called by the row buttons' delegates on every pointer-state change.
    void RefreshRowHover();
    void PushCornerPatches();
    void SchedulePatchPoll();
    void ApplyCollapsedRailTheme();
    void UpdateAddressFocusLeadingEdge();

    BrowserChromeHost* host_;
    ChromeTokens tokens_;
    IconCatalog icon_catalog_;
    // Shared with the delegates and the posted reapply task; nulled on Detach.
    std::shared_ptr<BrowserChrome*> alive_;
    bool theme_reapply_pending_ = false;
    CefRefPtr<CefPanel> root_;
    CefRefPtr<CefBoxLayout> root_layout_;
    CefRefPtr<CefPanel> rail_host_;
    CefRefPtr<PanelDelegate> rail_host_delegate_;
    CefRefPtr<CefPanel> content_column_;
    CefRefPtr<CefBoxLayout> content_column_layout_;
    CefRefPtr<CefPanel> sidebar_;
    CefRefPtr<CefPanel> browser_content_;
    CefRefPtr<PanelDelegate> browser_content_delegate_;
    CefRefPtr<CefPanel> toolbar_;
    CefRefPtr<CefPanel> toolbar_content_;
    CefRefPtr<CefPanel> toolbar_hairline_;
    CefRefPtr<CefBoxLayout> browser_content_layout_;
    CefRefPtr<CefBoxLayout> agent_panel_layout_;
    CefRefPtr<CefBoxLayout> navigation_row_layout_;
    CefRefPtr<CefBoxLayout> rail_layout_;
    CefRefPtr<CefBoxLayout> collapsed_layout_;
    CefRefPtr<CefPanel> agent_panel_;
    CefRefPtr<PanelDelegate> agent_panel_delegate_;
    CefRefPtr<CefView> agent_panel_view_;
    bool agent_panel_open_ = false;
    std::string update_notice_;
    CefRefPtr<CefBrowserView> browser_view_;
    // U6 split view state; both stay null while a single view is attached.
    CefRefPtr<CefBrowserView> split_view_;
    CefRefPtr<CefPanel> split_divider_;
    double split_ratio_ = SplitRatioDefault();
    CefRefPtr<CefPanel> navigation_row_;
    CefRefPtr<CefPanel> address_row_;
    CefRefPtr<CefPanel> spacer_;
    CefRefPtr<CefBoxLayout> address_layout_;
    CefRefPtr<CefPanel> address_field_holder_;
    CefRefPtr<CefLabelButton> address_idle_label_;
    CefRefPtr<CefPanel> divider_;
    CefRefPtr<CefLabelButton> back_button_;
    CefRefPtr<CefLabelButton> forward_button_;
    CefRefPtr<CefLabelButton> reload_button_;
    CefRefPtr<CefLabelButton> reader_button_;
    CefRefPtr<CefLabelButton> address_shield_button_;
    CefRefPtr<CefLabelButton> address_agent_dot_;
    AddressIndicators address_indicators_;
    CefRefPtr<CefLabelButton> share_button_;
    CefRefPtr<CefPanel> switcher_row_;
    bool reader_available_ = false;
    bool reader_active_ = false;
    CefRefPtr<CefLabelButton> address_location_icon_;
    CefRefPtr<CefImage> location_icon_source_;
    CefRefPtr<CefImage> reload_icon_source_;
    CefRefPtr<CefTextfield> address_field_;
    bool address_editing_ = false;
    CefRefPtr<CefLabelButton> validation_message_;
    CefRefPtr<CefPanel> tab_strip_;
    CefRefPtr<CefLabelButton> favorites_section_header_;
    CefRefPtr<CefLabelButton> tabs_section_header_;
    // Branch labels of grouped tab runs and their indent panels (rebuilt with the tab strip).
    std::vector<CefRefPtr<CefPanel>> group_kicker_panels_;
    std::vector<CefRefPtr<CefLabelButton>> group_kickers_;
    // The collapsed icon rail. `collapsed_rail_` is the panel shown instead of `sidebar_`;
    // the tab list scrolls inside `collapsed_tab_scroll_`, the "+" button and the space
    // dots sit in the bottom group.
    CefRefPtr<CefPanel> collapsed_rail_;
    CefRefPtr<SurfacePanelDelegate> collapsed_rail_delegate_;
    CefRefPtr<CefView> collapsed_tab_scroll_;
    CefRefPtr<CefPanel> collapsed_tab_list_;
    CefRefPtr<SurfacePanelDelegate> collapsed_tab_list_delegate_;
    CefRefPtr<CefPanel> collapsed_bottom_group_;
    CefRefPtr<CefPanel> collapsed_divider_wrapper_;
    CefRefPtr<CefPanel> collapsed_divider_;
    CefRefPtr<CefLabelButton> collapsed_new_tab_button_;
    CefRefPtr<CefPanel> collapsed_space_list_;
    CefRefPtr<SurfacePanelDelegate> tab_strip_delegate_;
    CefRefPtr<CefView> tab_list_scroll_;
    CefRefPtr<CefPanel> space_switcher_;
    CefRefPtr<CefLabelButton> new_tab_button_;
    CefRefPtr<CefPanel> rail_footer_;
    CefRefPtr<SurfacePanelDelegate> rail_footer_delegate_;
    CefRefPtr<CefLabelButton> agent_button_;
    // Native's Agent entry is hovered (its fill follows the hover token; see RefreshRowHover).
    bool native_agent_hovered_ = false;
    CefRefPtr<CefLabelButton> new_space_button_;
    CefRefPtr<CefLabelButton> tabs_button_;
    CefRefPtr<CefLabelButton> settings_button_;
    // Per-entry runtime views, index-aligned with tab_entries_/space_entries_.
    struct TabEntryViews {
        CefRefPtr<CefPanel> row;
        CefRefPtr<CefPanel> inner;
        CefRefPtr<CefLabelButton> favicon;
        CefRefPtr<CefLabelButton> title;
        CefRefPtr<CefLabelButton> close;
        // The glyph sources the row's edge-button images are re-rendered from (favicon or the
        // globe fallback, close or pin icon) and the live pointer state.
        CefRefPtr<CefImage> favicon_source;
        bool favicon_is_page = false;
        CefRefPtr<CefImage> trailing_source;
        bool hovered = false;
    };
    // Collapsed-rail entries: `row` paints the hairline outline of the active tab and
    // `inner` its fill; `button` carries the favicon (or the space dot) and the tooltip.
    struct CollapsedTabViews {
        CefRefPtr<CefPanel> row;
        CefRefPtr<CefPanel> inner;
        CefRefPtr<CefLabelButton> button;
        std::size_t source_index = 0;
        CefRefPtr<CefImage> favicon_source;
        bool favicon_is_page = false;
        bool hovered = false;
    };
    struct CollapsedSpaceViews {
        CefRefPtr<CefLabelButton> button;
        std::size_t source_index = 0;
    };
    struct SpaceEntryViews {
        CefRefPtr<CefPanel> row;
        CefRefPtr<CefPanel> inner;
        CefRefPtr<CefPanel> color_mark;
        CefRefPtr<CefLabelButton> name;
    };
    std::vector<TabStripEntrySnapshot> tab_entries_;
    std::vector<SpaceSwitcherEntrySnapshot> space_entries_;
    std::vector<TabEntryViews> tab_entry_views_;
    std::vector<SpaceEntryViews> space_entry_views_;
    std::vector<CollapsedTabViews> collapsed_tab_views_;
    std::vector<CollapsedSpaceViews> collapsed_space_views_;
    CefRefPtr<CefPanel> collapsed_pinned_separator_wrapper_;
    CefRefPtr<CefPanel> collapsed_pinned_separator_;
    std::vector<CefRefPtr<CollectionButtonDelegate>> collapsed_tab_delegates_;
    std::vector<CefRefPtr<CollectionButtonDelegate>> collapsed_space_delegates_;
    // Kept per region: rebuilding one collection must not detach the other's
    // still-live entry buttons.
    std::vector<CefRefPtr<CollectionButtonDelegate>> tab_button_delegates_;
    std::vector<CefRefPtr<CollectionButtonDelegate>> space_button_delegates_;
    CefRefPtr<CefPanel> active_page_;
    CefRefPtr<CefLabelButton> active_page_fallback_favicon_;
    CefRefPtr<CefLabelButton> active_tab_;
    CefRefPtr<CefPanel> active_page_indicator_;
    CefRefPtr<SurfacePanelDelegate> rail_delegate_;
    CefRefPtr<RootPanelDelegate> root_delegate_;
    CefRefPtr<SurfacePanelDelegate> navigation_row_delegate_;
    CefRefPtr<SurfacePanelDelegate> address_row_delegate_;
    int applied_control_height_ = 0;
    LookPreset applied_look_ = LookPreset::kGraphite;
    CefRefPtr<ButtonDelegate> button_delegate_;
    CefRefPtr<TextfieldDelegate> textfield_delegate_;
    std::vector<CefRefPtr<SurfacePanelDelegate>> surface_delegates_;
    bool sidebar_expanded_ = kSidebarExpandedByDefault;
    bool detached_ = false;
    // The inputs ApplyLayoutTokens() last laid out for.
    std::string layout_key_;
    std::function<void(const std::vector<CornerPatch>&)> corner_sink_;
    std::vector<CornerPatch> last_patches_;
    bool patch_poll_pending_ = false;
    std::size_t browser_view_size_change_count_ = 0;
    std::size_t relayout_count_ = 0;
    std::vector<CefRect> last_browser_view_bounds_;
};

}  // namespace island

#endif  // ISLAND_BROWSER_CHROME_H_
