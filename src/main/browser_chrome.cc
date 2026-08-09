#include "browser_chrome.h"

#include <string>
#include <utility>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

// allow: SIZE_OK — the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;

int ControlHeight(const ChromeTokens& tokens) {
    return tokens.spacing_6_dip + tokens.spacing_4_dip;
}

int DividerHeight(const ChromeTokens& tokens) {
    return tokens.spacing_1_dip / tokens.spacing_1_dip;
}

DipRect ToDipRect(const CefRect& bounds) {
    return {.x = bounds.x, .y = bounds.y, .width = bounds.width, .height = bounds.height};
}

void SelectAllWhenFocused(CefRefPtr<CefTextfield> textfield) {
    CEF_REQUIRE_UI_THREAD();
    if (textfield != nullptr && textfield->HasFocus()) {
        textfield->SelectAll(false);
    }
}

CefBoxLayoutSettings HorizontalLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_3_dip;
    settings.inside_border_vertical_spacing = tokens.spacing_2_dip;
    return settings;
}

CefBoxLayoutSettings HorizontalFillLayout(const ChromeTokens& tokens) {
    const int zero_dip = tokens.spacing_1_dip - tokens.spacing_1_dip;
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = zero_dip;
    settings.inside_border_horizontal_spacing = zero_dip;
    settings.inside_border_vertical_spacing = zero_dip;
    return settings;
}

CefBoxLayoutSettings VerticalLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_3_dip;
    settings.inside_border_vertical_spacing = tokens.spacing_3_dip;
    return settings;
}

std::string AddressErrorMessage(const std::optional<AddressError>& error) {
    if (!error.has_value()) {
        return "";
    }

    switch (*error) {
        case AddressError::kNotAbsolute:
            return "Enter an absolute http or https address.";
        case AddressError::kUnsupportedScheme:
            return "Only http and https addresses are supported.";
        case AddressError::kCredentialsNotAllowed:
            return "Addresses cannot include credentials.";
        case AddressError::kInvalidCharacter:
            return "Address contains an invalid character.";
        case AddressError::kInvalidHost:
            return "Enter an allowed address host.";
        case AddressError::kInvalidPort:
            return "Enter a valid address port.";
    }
}

}  // namespace

class BrowserChrome::PanelDelegate final : public CefPanelDelegate {
  public:
    PanelDelegate(CefSize preferred_size, CefSize minimum_size = CefSize(),
                  CefSize maximum_size = CefSize())
        : preferred_size_(preferred_size),
          minimum_size_(minimum_size),
          maximum_size_(maximum_size) {}

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return minimum_size_; }
    CefSize GetMaximumSize(CefRefPtr<CefView>) override { return maximum_size_; }

  private:
    CefSize preferred_size_;
    CefSize minimum_size_;
    CefSize maximum_size_;

    IMPLEMENT_REFCOUNTING(PanelDelegate);
};

class BrowserChrome::RootPanelDelegate final : public CefPanelDelegate {
  public:
    explicit RootPanelDelegate(ChromeTokens tokens) : tokens_(tokens) {}

    void SetChildren(CefRefPtr<CefPanel> rail, CefRefPtr<CefPanel> browser_content) {
        rail_ = rail;
        browser_content_ = browser_content;
    }

    void SetTokens(ChromeTokens tokens) { tokens_ = tokens; }

    void OnLayoutChanged(CefRefPtr<CefView>, const CefRect& new_bounds) override {
        if (rail_ == nullptr || browser_content_ == nullptr) {
            return;
        }

        const ChromeGeometrySnapshot geometry =
            BrowserChrome::LayoutForBounds({.x = new_bounds.x,
                                            .y = new_bounds.y,
                                            .width = new_bounds.width,
                                            .height = new_bounds.height},
                                           tokens_);
        ApplyBounds(rail_, geometry.rail_bounds);
        ApplyBounds(browser_content_, geometry.browser_content_bounds);
        rail_->Layout();
        browser_content_->Layout();
    }

  private:
    static void ApplyBounds(CefRefPtr<CefPanel> panel, const DipRect& bounds) {
        const CefRect target(bounds.x, bounds.y, bounds.width, bounds.height);
        if (panel->GetBounds() != target) {
            panel->SetBounds(target);
        }
    }

    ChromeTokens tokens_;
    CefRefPtr<CefPanel> rail_;
    CefRefPtr<CefPanel> browser_content_;

    IMPLEMENT_REFCOUNTING(RootPanelDelegate);
};

class BrowserChrome::ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(BrowserChrome* chrome) : chrome_(chrome) {}

    void Detach() { chrome_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleButtonPressed(static_cast<ChromeViewId>(button->GetID()));
        }
    }

  private:
    BrowserChrome* chrome_;

    IMPLEMENT_REFCOUNTING(ButtonDelegate);
};

class BrowserChrome::TextfieldDelegate final : public CefTextfieldDelegate {
  public:
    TextfieldDelegate(BrowserChrome* chrome, CefSize preferred_size)
        : chrome_(chrome), preferred_size_(preferred_size) {}

    void Detach() { chrome_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override {
        CEF_REQUIRE_UI_THREAD();
        return chrome_ != nullptr && chrome_->HandleAddressKeyEvent(textfield, event);
    }

    void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressUserAction(textfield);
        }
    }

    void OnFocus(CefRefPtr<CefView>) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressFocus();
        }
    }

    void OnBlur(CefRefPtr<CefView>) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressBlur();
        }
    }

  private:
    BrowserChrome* chrome_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(TextfieldDelegate);
};

BrowserChrome::BrowserChrome(BrowserChromeHost& host, CefRefPtr<CefBrowserView> browser_view,
                             ChromeTokens tokens, std::filesystem::path icon_resource_root)
    : host_(&host),
      tokens_(tokens),
      icon_catalog_(std::move(icon_resource_root)),
      browser_view_(browser_view) {
    CEF_REQUIRE_UI_THREAD();

    const int control_height = ControlHeight(tokens_);
    root_delegate_ = new RootPanelDelegate(tokens_);
    root_ = CefPanel::CreatePanel(root_delegate_);
    root_->SetID(static_cast<int>(ChromeViewId::kRoot));

    const CefSize rail_size(tokens_.rail_width_dip, 0);
    sidebar_ = CefPanel::CreatePanel(new PanelDelegate(rail_size, rail_size, rail_size));
    sidebar_->SetID(static_cast<int>(ChromeViewId::kRail));
    CefRefPtr<CefBoxLayout> sidebar_layout = sidebar_->SetToBoxLayout(VerticalLayout(tokens_));

    CefRefPtr<CefPanel> navigation_row = CefPanel::CreatePanel(nullptr);
    navigation_row->SetID(static_cast<int>(ChromeViewId::kNavigationRow));
    navigation_row->SetToBoxLayout(HorizontalLayout(tokens_));

    button_delegate_ = new ButtonDelegate(this);
    back_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    back_button_->SetID(static_cast<int>(ChromeViewId::kBack));
    back_button_->SetAccessibleName("Back");
    back_button_->SetTooltipText("Back");
    back_button_->SetFocusable(true);
    back_button_->SetMinimumSize(CefSize(control_height, control_height));
    navigation_row->AddChildView(back_button_);

    forward_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    forward_button_->SetID(static_cast<int>(ChromeViewId::kForward));
    forward_button_->SetAccessibleName("Forward");
    forward_button_->SetTooltipText("Forward");
    forward_button_->SetFocusable(true);
    forward_button_->SetMinimumSize(CefSize(control_height, control_height));
    navigation_row->AddChildView(forward_button_);
    sidebar_->AddChildView(navigation_row);

    CefRefPtr<CefPanel> address_row = CefPanel::CreatePanel(nullptr);
    address_row->SetID(static_cast<int>(ChromeViewId::kAddressRow));
    CefRefPtr<CefBoxLayout> address_layout = address_row->SetToBoxLayout(HorizontalLayout(tokens_));

    address_location_icon_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    address_location_icon_->SetID(static_cast<int>(ChromeViewId::kAddressLocationIcon));
    address_location_icon_->SetAccessibleName("Location");
    address_location_icon_->SetTooltipText("Location");
    address_location_icon_->SetFocusable(false);
    address_location_icon_->SetMinimumSize(CefSize(control_height, control_height));
    address_row->AddChildView(address_location_icon_);

    textfield_delegate_ =
        new TextfieldDelegate(this, CefSize(tokens_.spacing_6_dip * 4, control_height));
    address_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    address_field_->SetID(static_cast<int>(ChromeViewId::kAddress));
    address_field_->SetAccessibleName("Address");
    address_field_->SetPlaceholderText("Enter address");
    address_field_->SetFocusable(true);
    address_row->AddChildView(address_field_);
    address_layout->SetFlexForView(address_field_, 1);

    reload_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    reload_button_->SetID(static_cast<int>(ChromeViewId::kReload));
    reload_button_->SetAccessibleName("Reload");
    reload_button_->SetTooltipText("Reload");
    reload_button_->SetFocusable(true);
    reload_button_->SetMinimumSize(CefSize(control_height, control_height));
    address_row->AddChildView(reload_button_);
    sidebar_->AddChildView(address_row);

    validation_message_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    validation_message_->SetID(static_cast<int>(ChromeViewId::kValidationMessage));
    validation_message_->SetFocusable(false);
    validation_message_->SetEnabled(false);
    validation_message_->SetVisible(false);
    sidebar_->AddChildView(validation_message_);

    CefRefPtr<CefPanel> spacer = CefPanel::CreatePanel(nullptr);
    spacer->SetID(static_cast<int>(ChromeViewId::kSpacer));
    sidebar_->AddChildView(spacer);
    sidebar_layout->SetFlexForView(spacer, 1);

    CefRefPtr<CefPanel> divider = CefPanel::CreatePanel(
        new PanelDelegate(CefSize(tokens_.rail_width_dip, DividerHeight(tokens_))));
    divider->SetID(static_cast<int>(ChromeViewId::kDivider));
    sidebar_->AddChildView(divider);

    active_page_ = CefPanel::CreatePanel(nullptr);
    active_page_->SetID(static_cast<int>(ChromeViewId::kActivePage));
    CefRefPtr<CefBoxLayout> active_page_layout =
        active_page_->SetToBoxLayout(HorizontalLayout(tokens_));

    active_page_fallback_favicon_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    active_page_fallback_favicon_->SetID(
        static_cast<int>(ChromeViewId::kActivePageFallbackFavicon));
    active_page_fallback_favicon_->SetAccessibleName("Current page favicon placeholder");
    active_page_fallback_favicon_->SetTooltipText("Current page favicon placeholder");
    active_page_fallback_favicon_->SetFocusable(false);
    active_page_fallback_favicon_->SetMinimumSize(CefSize(control_height, control_height));
    active_page_->AddChildView(active_page_fallback_favicon_);

    active_tab_ = CefLabelButton::CreateLabelButton(button_delegate_, "Island");
    active_tab_->SetID(static_cast<int>(ChromeViewId::kActiveTab));
    active_tab_->SetAccessibleName("Current page: Island");
    active_tab_->SetTooltipText("Current page title");
    active_tab_->SetFocusable(false);
    active_tab_->SetMinimumSize(CefSize(tokens_.spacing_6_dip * 4, control_height));
    active_page_->AddChildView(active_tab_);
    active_page_layout->SetFlexForView(active_tab_, 1);

    active_page_indicator_ =
        CefPanel::CreatePanel(new PanelDelegate(CefSize(tokens_.spacing_1_dip, control_height)));
    active_page_indicator_->SetID(static_cast<int>(ChromeViewId::kActivePageIndicator));
    active_page_->AddChildView(active_page_indicator_);
    sidebar_->AddChildView(active_page_);

    browser_content_ = CefPanel::CreatePanel(new PanelDelegate(
        CefSize(control_height, control_height), CefSize(control_height, control_height)));
    browser_content_->SetID(static_cast<int>(ChromeViewId::kBrowserContent));
    CefRefPtr<CefBoxLayout> browser_content_layout =
        browser_content_->SetToBoxLayout(HorizontalFillLayout(tokens_));
    browser_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    browser_content_->AddChildView(browser_view_);
    browser_content_layout->SetFlexForView(browser_view_, 1);

    root_->AddChildView(sidebar_);
    root_->AddChildView(browser_content_);
    root_delegate_->SetChildren(sidebar_, browser_content_);
    ApplyControlTheme();
}

BrowserChrome::~BrowserChrome() { Detach(); }

CefRefPtr<CefPanel> BrowserChrome::root() const {
    CEF_REQUIRE_UI_THREAD();
    return root_;
}

CefRefPtr<CefPanel> BrowserChrome::sidebar() const {
    CEF_REQUIRE_UI_THREAD();
    return sidebar_;
}

ChromeViewTreeNode BrowserChrome::view_tree_snapshot() const {
    return BrowserChrome::ViewTreeContract();
}

ChromeGeometrySnapshot BrowserChrome::view_bounds_snapshot() const {
    CEF_REQUIRE_UI_THREAD();
    return {
        .root_bounds = ToDipRect(root_->GetBounds()),
        .rail_bounds = ToDipRect(sidebar_->GetBounds()),
        .browser_content_bounds = ToDipRect(browser_content_->GetBounds()),
        .browser_view_bounds = ToDipRect(browser_view_->GetBounds()),
    };
}

AddressSelectionSnapshot BrowserChrome::address_selection_snapshot() const {
    CEF_REQUIRE_UI_THREAD();
    return {
        .has_focus = address_field_->HasFocus(),
        .has_selection = address_field_->HasSelection(),
    };
}

void BrowserChrome::OnNavigationChanged(const NavigationSnapshot& snapshot) {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        ProjectNavigation(snapshot);
    }
}

void BrowserChrome::OnAddressChanged(const AddressBarSnapshot& snapshot) {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        ProjectAddress(snapshot);
    }
}

void BrowserChrome::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    root_delegate_->SetTokens(tokens_);
    ApplyControlTheme();
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
}

void BrowserChrome::BeginAddressEditing() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    host_->BeginAddressEditing();
    address_field_->RequestFocus();
    ScheduleAddressSelection();
}

void BrowserChrome::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    button_delegate_->Detach();
    textfield_delegate_->Detach();
    if (browser_view_ != nullptr && browser_content_ != nullptr) {
        browser_content_->RemoveChildView(browser_view_);
    }
    browser_view_ = nullptr;
}

void BrowserChrome::HandleButtonPressed(ChromeViewId view_id) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }

    switch (view_id) {
        case ChromeViewId::kBack:
            host_->ExecuteBrowserCommand(BrowserCommand::kBack);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kForward:
            host_->ExecuteBrowserCommand(BrowserCommand::kForward);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kReload:
            host_->ExecuteBrowserCommand(BrowserCommand::kReload);
            host_->FocusBrowserView();
            return;
        default:
            return;
    }
}

bool BrowserChrome::HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield,
                                          const CefKeyEvent& event) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || event.type != KEYEVENT_RAWKEYDOWN) {
        return false;
    }
    if (event.windows_key_code == kReturnKeyCode) {
        const std::string draft = textfield->GetText().ToString();
        host_->SubmitAddressDraft(draft);
        return true;
    }
    if (event.windows_key_code == kEscapeKeyCode) {
        host_->CancelAddressEditing();
        return true;
    }
    return false;
}

void BrowserChrome::HandleAddressUserAction(CefRefPtr<CefTextfield>) { CEF_REQUIRE_UI_THREAD(); }

void BrowserChrome::HandleAddressFocus() {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        host_->BeginAddressEditing();
        ScheduleAddressSelection();
    }
}

void BrowserChrome::HandleAddressBlur() {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        host_->CancelAddressEditing();
    }
}

void BrowserChrome::ProjectNavigation(const NavigationSnapshot& snapshot) {
    back_button_->SetEnabled(snapshot.can_go_back);
    forward_button_->SetEnabled(snapshot.can_go_forward);
    reload_button_->SetEnabled(true);
    active_tab_->SetText(snapshot.page_title.empty() ? "Island" : snapshot.page_title);
    active_tab_->SetAccessibleName("Current page: " + (snapshot.page_title.empty()
                                                           ? std::string("Island")
                                                           : snapshot.page_title));
}

void BrowserChrome::ProjectAddress(const AddressBarSnapshot& snapshot) {
    const bool editing = snapshot.mode != AddressBarMode::kResting;
    address_field_->SetReadOnly(!editing);
    address_field_->SetText(editing ? snapshot.edit_text : snapshot.display_text);
    const std::string validation_message = AddressErrorMessage(snapshot.validation_error);
    validation_message_->SetText(validation_message);
    validation_message_->SetAccessibleName(validation_message);
    validation_message_->SetVisible(!validation_message.empty());
    address_field_->SetAccessibleName(
        validation_message.empty() ? "Address" : "Address, invalid: " + validation_message);
}

void BrowserChrome::ScheduleAddressSelection() {
    CEF_REQUIRE_UI_THREAD();
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(SelectAllWhenFocused, address_field_)));
}

void BrowserChrome::ApplyControlTheme() {
    root_->SetBackgroundColor(tokens_.background.argb);
    sidebar_->SetBackgroundColor(tokens_.surface.argb);
    browser_content_->SetBackgroundColor(tokens_.background.argb);
    address_field_->SetBackgroundColor(tokens_.surface_secondary.argb);
    active_page_indicator_->SetBackgroundColor(tokens_.accent.argb);
    address_field_->SetFontList("Geist, 14px");
    back_button_->SetEnabledTextColors(tokens_.text.argb);
    forward_button_->SetEnabledTextColors(tokens_.text.argb);
    reload_button_->SetEnabledTextColors(tokens_.text.argb);
    address_location_icon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_page_fallback_favicon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_tab_->SetEnabledTextColors(tokens_.text.argb);
    validation_message_->SetEnabledTextColors(tokens_.accent.argb);

    const std::optional<CefRefPtr<CefImage>> back =
        icon_catalog_.Load(ChromeIcon::kBack, IconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> forward =
        icon_catalog_.Load(ChromeIcon::kForward, IconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> reload =
        icon_catalog_.Load(ChromeIcon::kReload, IconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> location =
        icon_catalog_.Load(ChromeIcon::kLocation, ChromeIconTone::kSecondary, ChromeIconSize::k16);
    if (back.has_value()) {
        back_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *back);
    }
    if (forward.has_value()) {
        forward_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *forward);
    }
    if (reload.has_value()) {
        reload_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *reload);
    }
    if (location.has_value()) {
        address_location_icon_->SetImage(CEF_BUTTON_STATE_NORMAL, *location);
        active_page_fallback_favicon_->SetImage(CEF_BUTTON_STATE_NORMAL, *location);
    }
}

ChromeIconTone BrowserChrome::IconTone() const { return ChromeIconTone::kText; }

}  // namespace island
