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
#include <string_view>
#include <vector>

#include "address_bar_model.h"
#include "browser_command.h"
#include "chrome_snapshot.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "navigation_state.h"

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
};

struct ChromeViewTreeNode {
    ChromeViewId id;
    std::vector<ChromeViewTreeNode> children;

    bool operator==(const ChromeViewTreeNode&) const = default;
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

class BrowserChromeHost {
  public:
    virtual ~BrowserChromeHost() = default;

    virtual void ExecuteBrowserCommand(BrowserCommand command) = 0;
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

    enum class SurfaceSlot : std::uint8_t {
        kRoot,
        kRail,
        kBrowserContent,
        kHairline,
        kAddressWell,
        kAccent,
    };
    [[nodiscard]] static ArgbColor ChromeSurfaceRole(SurfaceSlot slot, ChromeTheme theme) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        switch (slot) {
            case SurfaceSlot::kRoot:
            case SurfaceSlot::kBrowserContent:
                return tokens.background;
            case SurfaceSlot::kRail:
                return tokens.surface;
            case SurfaceSlot::kHairline:
                return tokens.border;
            case SurfaceSlot::kAddressWell:
                return tokens.surface_secondary;
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
                return tokens_.surface;
            case SurfaceSlot::kHairline:
                return tokens_.border;
            case SurfaceSlot::kAddressWell:
                return tokens_.surface_secondary;
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
        const int rail_width = std::min(tokens.rail_width_dip, root_bounds.width);
        const DipRect rail_bounds = {
            .x = root_bounds.x,
            .y = root_bounds.y,
            .width = rail_width,
            .height = root_bounds.height,
        };
        const DipRect browser_content_bounds = {
            .x = root_bounds.x + rail_width,
            .y = root_bounds.y,
            .width = std::max(0, root_bounds.width - rail_width),
            .height = root_bounds.height,
        };
        return {
            .root_bounds = root_bounds,
            .rail_bounds = rail_bounds,
            .browser_content_bounds = browser_content_bounds,
            .browser_view_bounds = browser_content_bounds,
        };
    }
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
                   {ChromeViewId::kSpacer, {}},
                   {ChromeViewId::kDivider, {}},
                   {ChromeViewId::kActivePage,
                    {{ChromeViewId::kActivePageFallbackFavicon, {}},
                     {ChromeViewId::kActiveTab, {}},
                     {ChromeViewId::kActivePageIndicator, {}}}}}},
                 {ChromeViewId::kBrowserContent, {{ChromeViewId::kBrowserView, {}}}}}};
    }

    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;
    void OnAddressChanged(const AddressBarSnapshot& snapshot);
    void ApplyTheme(ChromeTokens tokens);
    void BeginAddressEditing();
    void Detach();

  private:
    class PanelDelegate;
    class RootPanelDelegate;
    class ButtonDelegate;
    class TextfieldDelegate;

    void HandleButtonPressed(ChromeViewId view_id);
    bool HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleAddressUserAction(CefRefPtr<CefTextfield> textfield);
    void HandleAddressFocus();
    void HandleAddressBlur();
    void ProjectNavigation(const NavigationSnapshot& snapshot);
    void ProjectAddress(const AddressBarSnapshot& snapshot);
    void ScheduleAddressSelection();
    void ApplyControlTheme();
    void UpdateAddressFocusLeadingEdge();

    BrowserChromeHost* host_;
    ChromeTokens tokens_;
    IconCatalog icon_catalog_;
    CefRefPtr<CefPanel> root_;
    CefRefPtr<CefPanel> sidebar_;
    CefRefPtr<CefPanel> browser_content_;
    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefPanel> address_focus_leading_edge_;
    CefRefPtr<CefPanel> divider_;
    CefRefPtr<CefLabelButton> back_button_;
    CefRefPtr<CefLabelButton> forward_button_;
    CefRefPtr<CefLabelButton> reload_button_;
    CefRefPtr<CefLabelButton> address_location_icon_;
    CefRefPtr<CefTextfield> address_field_;
    CefRefPtr<CefLabelButton> validation_message_;
    CefRefPtr<CefPanel> active_page_;
    CefRefPtr<CefLabelButton> active_page_fallback_favicon_;
    CefRefPtr<CefLabelButton> active_tab_;
    CefRefPtr<CefPanel> active_page_indicator_;
    CefRefPtr<RootPanelDelegate> root_delegate_;
    CefRefPtr<ButtonDelegate> button_delegate_;
    CefRefPtr<TextfieldDelegate> textfield_delegate_;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_BROWSER_CHROME_H_
