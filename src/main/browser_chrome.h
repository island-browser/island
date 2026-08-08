#ifndef ISLAND_BROWSER_CHROME_H_
#define ISLAND_BROWSER_CHROME_H_

#include <filesystem>
#include <string_view>
#include <vector>

#include "address_bar_model.h"
#include "browser_command.h"
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
};

struct ChromeViewTreeNode {
    ChromeViewId id;
    std::vector<ChromeViewTreeNode> children;

    bool operator==(const ChromeViewTreeNode&) const = default;
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
    [[nodiscard]] static ChromeViewTreeNode ViewTreeContract() {
        return {ChromeViewId::kRoot,
                {{ChromeViewId::kRail,
                  {{ChromeViewId::kNavigationRow,
                    {{ChromeViewId::kBack, {}}, {ChromeViewId::kForward, {}}}},
                   {ChromeViewId::kAddressRow,
                    {{ChromeViewId::kAddress, {}}, {ChromeViewId::kReload, {}}}},
                   {ChromeViewId::kValidationMessage, {}},
                   {ChromeViewId::kSpacer, {}},
                   {ChromeViewId::kDivider, {}},
                   {ChromeViewId::kActiveTab, {}}}},
                 {ChromeViewId::kBrowserContent, {{ChromeViewId::kBrowserView, {}}}}}};
    }

    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;
    void OnAddressChanged(const AddressBarSnapshot& snapshot);
    void ApplyTheme(ChromeTokens tokens);
    void BeginAddressEditing();
    void Detach();

  private:
    class PanelDelegate;
    class ButtonDelegate;
    class TextfieldDelegate;

    void HandleButtonPressed(ChromeViewId view_id);
    bool HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleAddressUserAction(CefRefPtr<CefTextfield> textfield);
    void HandleAddressFocus();
    void HandleAddressBlur();
    void ProjectNavigation(const NavigationSnapshot& snapshot);
    void ProjectAddress(const AddressBarSnapshot& snapshot);
    void ApplyControlTheme();
    [[nodiscard]] ChromeIconTone IconTone() const;

    BrowserChromeHost* host_;
    ChromeTokens tokens_;
    IconCatalog icon_catalog_;
    CefRefPtr<CefPanel> root_;
    CefRefPtr<CefPanel> sidebar_;
    CefRefPtr<CefPanel> browser_content_;
    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefLabelButton> back_button_;
    CefRefPtr<CefLabelButton> forward_button_;
    CefRefPtr<CefLabelButton> reload_button_;
    CefRefPtr<CefTextfield> address_field_;
    CefRefPtr<CefLabelButton> validation_message_;
    CefRefPtr<CefLabelButton> active_tab_;
    CefRefPtr<ButtonDelegate> button_delegate_;
    CefRefPtr<TextfieldDelegate> textfield_delegate_;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_BROWSER_CHROME_H_
