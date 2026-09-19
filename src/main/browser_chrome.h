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
#include <string>
#include <string_view>
#include <vector>

#include "address_bar_model.h"
#include "browser_command.h"
#include "chrome_snapshot.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "navigation_state.h"
#include "sidebar_state.h"

class CefBoxLayout;
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
    // Window-level overlay regions. These are reported through
    // CefOverlayController and are deliberately absent from ViewTreeContract():
    // they are children of the CefWindow, not of the rail or the root panel.
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
    DipRect browser_content_bounds;
    DipRect browser_view_bounds;

    bool operator==(const ChromeGeometrySnapshot&) const = default;
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
    [[nodiscard]] ChromeViewTreeNode view_tree_snapshot() const;
    [[nodiscard]] ChromeGeometrySnapshot view_bounds_snapshot() const;
    [[nodiscard]] AddressSelectionSnapshot address_selection_snapshot() const;

    // Address-editing leading edge and ActivePageIndicator thickness in DIP;
    // both are 2 so the rail accent and the address accent read as one mark.
    [[nodiscard]] static constexpr int AddressFocusLeadingEdgeDip() { return 2; }
    [[nodiscard]] static constexpr int ActivePageIndicatorWidthDip() {
        return AddressFocusLeadingEdgeDip();
    }

    // Top inset reserving the platform title-bar / traffic-light region so the
    // navigation row clears the window controls instead of colliding with them. The
    // window uses a hidden title bar (content extends into the title region), so the
    // controls overlay the top of the rail. Token-derived: spacing_6 (24) +
    // spacing_3 (12) = 36 DIP.
    [[nodiscard]] static constexpr int RailTopInsetDip() { return 36; }

    // Vertical cadence between rail sections. A calm spacing_4 (16) step instead of
    // the cramped spacing_2 (8) so the cluster reads as composed ledger sections.
    [[nodiscard]] static constexpr int RailSectionSpacingDip() { return 16; }

    // Uniform gutter padding the browser_content panel on every side so the single
    // CefBrowserView reads as a floating card over the tinted canvas. spacing_3 (12)
    // is the calm edge: wide enough to separate the card from the rail and window
    // edge, tight enough to keep the canvas dominant at the 800x560 minimum.
    // CEF cannot round or clip the CefBrowserView, so the floating read comes from
    // this rectangular inset, not from a corner radius.
    [[nodiscard]] static constexpr int BrowserContentPaddingDip() { return 12; }

    enum class SurfaceSlot : std::uint8_t {
        kRoot,
        kRail,
        kBrowserContent,
        kHairline,
        kAddressWell,
        kNavControl,
        kActivePage,
        kAccent,
    };
    [[nodiscard]] static ArgbColor ChromeSurfaceRole(SurfaceSlot slot, ChromeTheme theme) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        switch (slot) {
            case SurfaceSlot::kRoot:
            case SurfaceSlot::kBrowserContent:
                return tokens.background;
            case SurfaceSlot::kRail:
                // The calm tinted rail sits one step off the canvas so the floating
                // browser card separates from it.
                return tokens.surface_secondary;
            case SurfaceSlot::kHairline:
                return tokens.border;
            case SurfaceSlot::kAddressWell:
                // The address pill lifts to the primary surface so it reads as a raised
                // rounded field against the tinted rail, matching the canonical
                // .rail-address anatomy (surface fill, hairline edge).
                return tokens.surface;
            case SurfaceSlot::kNavControl:
                // Nav icon-button hover/pressed fill stays on the quiet surface step so
                // the tinted rail keeps the controls calm until targeted.
                return tokens.surface;
            case SurfaceSlot::kActivePage:
                // The active-page pill lifts to the primary surface against the tinted
                // rail so the current page reads as the one raised card.
                return tokens.surface;
            case SurfaceSlot::kAccent:
                return tokens.accent;
        }
        return tokens.background;
    }
    [[nodiscard]] ArgbColor ChromeSurfaceRoleForResolvedTokens(SurfaceSlot slot) const {
        switch (slot) {
            case SurfaceSlot::kRoot:
            case SurfaceSlot::kBrowserContent:
                return tokens_.background;
            case SurfaceSlot::kRail:
                return tokens_.surface_secondary;
            case SurfaceSlot::kHairline:
                return tokens_.border;
            case SurfaceSlot::kAddressWell:
                return tokens_.surface;
            case SurfaceSlot::kNavControl:
                return tokens_.surface;
            case SurfaceSlot::kActivePage:
                return tokens_.surface;
            case SurfaceSlot::kAccent:
                return tokens_.accent;
        }
        return tokens_.background;
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
    [[nodiscard]] static ChromeGeometrySnapshot LayoutForBounds(
        DipRect root_bounds, const ChromeTokens& tokens) noexcept {
        return LayoutForBounds(root_bounds, tokens, /*sidebar_revealed=*/true);
    }
    // The hidden sidebar occupies 0 DIP of layout width; the rail stays in the
    // view tree, so the contract shape is unchanged and only the content
    // x-offset moves. This is the single layout path for both states.
    [[nodiscard]] static ChromeGeometrySnapshot LayoutForBounds(DipRect root_bounds,
                                                                const ChromeTokens& tokens,
                                                                bool sidebar_revealed) noexcept {
        const int rail_width =
            sidebar_revealed ? std::min(tokens.rail_width_dip, root_bounds.width) : 0;
        const DipRect rail_bounds = {
            .x = root_bounds.x,
            .y = root_bounds.y,
            .width = rail_width,
            .height = root_bounds.height,
        };
        // The floating-canvas gutter insets the browser card on every side; it shrinks
        // the content region symmetrically and is clamped so the card never inverts on
        // very narrow windows.
        const int pad = BrowserContentPaddingDip();
        const int content_x = root_bounds.x + rail_width;
        const int content_width = std::max(0, root_bounds.width - rail_width);
        const DipRect browser_content_bounds = {
            .x = content_x,
            .y = root_bounds.y,
            .width = content_width,
            .height = root_bounds.height,
        };
        const int inset_width = std::max(0, content_width - 2 * pad);
        const int inset_height = std::max(0, root_bounds.height - 2 * pad);
        const DipRect browser_view_bounds = {
            .x = content_x + std::min(pad, content_width / 2),
            .y = root_bounds.y + std::min(pad, root_bounds.height / 2),
            .width = inset_width,
            .height = inset_height,
        };
        return {
            .root_bounds = root_bounds,
            .rail_bounds = rail_bounds,
            .browser_content_bounds = browser_content_bounds,
            .browser_view_bounds = browser_view_bounds,
        };
    }
    // The fixed rail regions keep their Phase 2 shape; the collection regions
    // (kTabStrip, kSpaceSwitcher) are present but empty because Phase 3 U3
    // wires no real tab/space data yet. Per-entry structure is asserted
    // through CollectionCountContract below, not by absolute rail index.
    [[nodiscard]] static ChromeViewTreeNode ViewTreeContract() {
        return {ChromeViewId::kRoot,
                {{ChromeViewId::kRail,
                  {{ChromeViewId::kNavigationRow,
                    {{ChromeViewId::kBack, {}}, {ChromeViewId::kForward, {}}}},
                   {ChromeViewId::kAddressRow,
                    {{ChromeViewId::kAddressLocationIcon, {}},
                     {ChromeViewId::kAddress, {}},
                     {ChromeViewId::kReload, {}}}},
                   {ChromeViewId::kValidationMessage, {}},
                   {ChromeViewId::kTabStrip, {}},
                   {ChromeViewId::kSpacer, {}},
                   {ChromeViewId::kSpaceSwitcher, {}},
                   {ChromeViewId::kDivider, {}},
                   {ChromeViewId::kActivePage,
                    {{ChromeViewId::kActivePageFallbackFavicon, {}},
                     {ChromeViewId::kActiveTab, {}},
                     {ChromeViewId::kActivePageIndicator, {}}}}}},
                 {ChromeViewId::kBrowserContent, {{ChromeViewId::kBrowserView, {}}}}}};
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
        const std::vector<SpaceSwitcherEntrySnapshot>& spaces) {
        ChromeViewTreeNode tree = ViewTreeContract();
        ChromeViewTreeNode& rail = tree.children[0];
        for (ChromeViewTreeNode& child : rail.children) {
            if (child.id == ChromeViewId::kTabStrip) {
                child.children.reserve(tabs.size());
                for (std::size_t i = 0; i < tabs.size(); ++i) {
                    child.children.push_back({ChromeViewId::kTabStripEntry,
                                              {{ChromeViewId::kTabStripEntryFavicon, {}},
                                               {ChromeViewId::kTabStripEntryTitle, {}},
                                               {ChromeViewId::kTabStripEntryClose, {}}}});
                }
            } else if (child.id == ChromeViewId::kSpaceSwitcher) {
                child.children.reserve(spaces.size());
                for (std::size_t i = 0; i < spaces.size(); ++i) {
                    child.children.push_back({ChromeViewId::kSpaceSwitcherEntry,
                                              {{ChromeViewId::kSpaceSwitcherEntryColorMark, {}},
                                               {ChromeViewId::kSpaceSwitcherEntryName, {}}}});
                }
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

    // Split divider geometry. The divider is keyboard-adjustable through the
    // kMoveDividerLeft/Right commands (drag is a documented deviation: the
    // pinned CEF distribution exposes no mouse events on custom views).
    [[nodiscard]] static constexpr double SplitRatioMin() { return 0.2; }
    [[nodiscard]] static constexpr double SplitRatioMax() { return 0.8; }
    [[nodiscard]] static constexpr double SplitRatioDefault() { return 0.5; }
    [[nodiscard]] static constexpr double SplitRatioStep() { return 0.05; }
    [[nodiscard]] static constexpr int SplitDividerWidthDip() { return 2; }

    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;
    void OnAddressChanged(const AddressBarSnapshot& snapshot);
    void ApplyTheme(ChromeTokens tokens);
    // Applies the reveal state to the rail's layout width. Runtime geometry only:
    // the rail is never removed from the tree.
    void SetSidebarRevealed(bool revealed);
    [[nodiscard]] bool sidebar_revealed() const noexcept { return sidebar_revealed_; }
    void BeginAddressEditing();
    void Detach();

    // Resolves the current tokens' color for a chrome surface slot. Exposed so the
    // surface-panel delegate can re-assert the color when CEF resets custom view
    // backgrounds on theme change.
    [[nodiscard]] ArgbColor ResolveSurfaceColor(SurfaceSlot slot) const {
        return ChromeSurfaceRoleForResolvedTokens(slot);
    }

  private:
    class PanelDelegate;
    class RootPanelDelegate;
    class SurfacePanelDelegate;
    class ButtonDelegate;
    class CollectionButtonDelegate;
    class TextfieldDelegate;

    void HandleButtonPressed(ChromeViewId view_id);
    void HandleCollectionButtonPressed(CollectionButtonAction action, std::size_t index);
    bool HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleAddressUserAction(CefRefPtr<CefTextfield> textfield);
    void HandleAddressFocus();
    void HandleAddressBlur();
    void ProjectNavigation(const NavigationSnapshot& snapshot);
    void ProjectAddress(const AddressBarSnapshot& snapshot);
    void ScheduleAddressSelection();
    void ApplyControlTheme();
    void ApplyCollectionTheme();
    void UpdateAddressFocusLeadingEdge();

    BrowserChromeHost* host_;
    ChromeTokens tokens_;
    IconCatalog icon_catalog_;
    CefRefPtr<CefPanel> root_;
    CefRefPtr<CefPanel> sidebar_;
    CefRefPtr<CefPanel> browser_content_;
    CefRefPtr<CefBoxLayout> browser_content_layout_;
    CefRefPtr<CefBrowserView> browser_view_;
    // U6 split view state; both stay null while a single view is attached.
    CefRefPtr<CefBrowserView> split_view_;
    CefRefPtr<CefPanel> split_divider_;
    double split_ratio_ = SplitRatioDefault();
    CefRefPtr<CefPanel> navigation_row_;
    CefRefPtr<CefPanel> address_row_;
    CefRefPtr<CefPanel> spacer_;
    CefRefPtr<CefPanel> address_focus_leading_edge_;
    CefRefPtr<CefPanel> divider_;
    CefRefPtr<CefLabelButton> back_button_;
    CefRefPtr<CefLabelButton> forward_button_;
    CefRefPtr<CefLabelButton> reload_button_;
    CefRefPtr<CefLabelButton> address_location_icon_;
    CefRefPtr<CefTextfield> address_field_;
    CefRefPtr<CefLabelButton> validation_message_;
    CefRefPtr<CefPanel> tab_strip_;
    CefRefPtr<CefPanel> space_switcher_;
    // Per-entry runtime views, index-aligned with tab_entries_/space_entries_.
    struct TabEntryViews {
        CefRefPtr<CefPanel> row;
        CefRefPtr<CefLabelButton> favicon;
        CefRefPtr<CefLabelButton> title;
        CefRefPtr<CefLabelButton> close;
    };
    struct SpaceEntryViews {
        CefRefPtr<CefPanel> row;
        CefRefPtr<CefPanel> color_mark;
        CefRefPtr<CefLabelButton> name;
    };
    std::vector<TabStripEntrySnapshot> tab_entries_;
    std::vector<SpaceSwitcherEntrySnapshot> space_entries_;
    std::vector<TabEntryViews> tab_entry_views_;
    std::vector<SpaceEntryViews> space_entry_views_;
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
    CefRefPtr<ButtonDelegate> button_delegate_;
    CefRefPtr<TextfieldDelegate> textfield_delegate_;
    std::vector<CefRefPtr<SurfacePanelDelegate>> surface_delegates_;
    bool sidebar_revealed_ = kSidebarRevealedByDefault;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_BROWSER_CHROME_H_
