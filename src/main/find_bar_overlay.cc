#include "find_bar_overlay.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "search_palette_model.h"

namespace island {
namespace {

constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;
constexpr int kTabKeyCode = 0x09;

constexpr int kButtonSizeDip = 28;
constexpr int kCountWidthDip = 64;
constexpr int kVerticalInsetDip = 6;
constexpr int kFieldMinWidthDip = 120;

}  // namespace

class FindBarOverlay::SurfaceDelegate final : public CefPanelDelegate {
  public:
    explicit SurfaceDelegate(FindBarOverlay* overlay, CefSize preferred_size = CefSize())
        : overlay_(overlay), preferred_size_(preferred_size) {}

    void Detach() { overlay_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    // CEF resets view colors on a theme change; re-assert them from the current tokens.
    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (overlay_ != nullptr) {
            view->SetBackgroundColor(
                PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, overlay_->tokens_).argb);
        }
    }

  private:
    FindBarOverlay* overlay_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};

class FindBarOverlay::FieldDelegate final : public CefTextfieldDelegate {
  public:
    explicit FieldDelegate(FindBarOverlay* overlay) : overlay_(overlay) {}

    void Detach() { overlay_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override {
        return CefSize(kFieldMinWidthDip, kButtonSizeDip);
    }

    bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override {
        CEF_REQUIRE_UI_THREAD();
        return overlay_ != nullptr && overlay_->HandleFieldKeyEvent(textfield, event);
    }

    void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr && overlay_->host_ != nullptr) {
            overlay_->host_->OnFindQueryChanged(textfield->GetText().ToString());
        }
    }

  private:
    FindBarOverlay* overlay_;

    IMPLEMENT_REFCOUNTING(FieldDelegate);
};

class FindBarOverlay::ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(FindBarOverlay* overlay) : overlay_(overlay) {}

    void Detach() { overlay_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr) {
            overlay_->HandleButtonPressed(static_cast<ChromeViewId>(button->GetID()));
        }
    }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (overlay_ != nullptr) {
            overlay_->ApplyColors();
        }
    }

  private:
    FindBarOverlay* overlay_;

    IMPLEMENT_REFCOUNTING(ButtonDelegate);
};

DipRect FindBarOverlay::CardBounds(const DipRect& anchor) noexcept {
    const int width = std::max(0, std::min(kContentWidthDip, anchor.width - 2 * kAnchorInsetDip));
    return {.x = anchor.x + anchor.width - width - kAnchorInsetDip,
            .y = anchor.y + kAnchorInsetDip,
            .width = width,
            .height = kContentHeightDip};
}

FindBarOverlay::FindBarOverlay(FindBarOverlayHost& host, CefRefPtr<CefWindow> window,
                               ChromeTokens tokens, std::filesystem::path icon_resource_root)
    : host_(&host),
      window_(std::move(window)),
      tokens_(tokens),
      icons_(std::move(icon_resource_root)) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    ApplyColors();
}

FindBarOverlay::~FindBarOverlay() { Detach(); }

void FindBarOverlay::BuildViews() {
    panel_delegate_ = new SurfaceDelegate(this, CefSize(kContentWidthDip, kContentHeightDip));
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kFindBar));
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens_.spacing_1_dip;
    settings.inside_border_insets =
        CefInsets(kVerticalInsetDip, tokens_.spacing_2_dip, kVerticalInsetDip, tokens_.spacing_2_dip);
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    CefRefPtr<CefBoxLayout> layout = panel_->SetToBoxLayout(settings);

    // Every button is a CefLabelButton, and CEF refuses to create one without a delegate.
    button_delegate_ = new ButtonDelegate(this);

    field_delegate_ = new FieldDelegate(this);
    field_ = CefTextfield::CreateTextfield(field_delegate_);
    field_->SetID(static_cast<int>(ChromeViewId::kFindBarField));
    field_->SetAccessibleName("Find in page");
    field_->SetPlaceholderText("Find in page");
    field_->SetFocusable(true);
    field_->SetFontList("Geist, 13px");
    panel_->AddChildView(field_);
    layout->SetFlexForView(field_, 1);

    const auto add_button = [&](ChromeViewId id, const char* text, const char* name,
                                bool focusable) {
        CefRefPtr<CefLabelButton> button = CefLabelButton::CreateLabelButton(button_delegate_, text);
        button->SetID(static_cast<int>(id));
        button->SetAccessibleName(name);
        button->SetTooltipText(name);
        button->SetFocusable(focusable);
        button->SetMinimumSize(CefSize(kButtonSizeDip, kButtonSizeDip));
        button->SetInkDropEnabled(true);
        panel_->AddChildView(button);
        return button;
    };

    count_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    count_->SetID(static_cast<int>(ChromeViewId::kFindBarCount));
    count_->SetAccessibleName("Match count");
    count_->SetFocusable(false);
    count_->SetEnabled(false);
    count_->SetMinimumSize(CefSize(kCountWidthDip, kButtonSizeDip));
    count_->SetFontList("Geist Mono, 11px");
    panel_->AddChildView(count_);

    previous_ = add_button(ChromeViewId::kFindBarPrevious, "", "Previous match", false);
    next_ = add_button(ChromeViewId::kFindBarNext, "", "Next match", false);
    match_case_button_ = add_button(ChromeViewId::kFindBarMatchCase, "Aa", "Match case", false);
    match_case_button_->SetFontList("Geist, Medium 12px");
    close_ = add_button(ChromeViewId::kFindBarClose, "", "Close find bar", false);

    overlay_root_ = card_.Wrap(panel_, tokens_);
    card_.SetContentSize(kContentWidthDip, kContentHeightDip);
}

void FindBarOverlay::CreateOverlay() {
    // can_activate is true so the text field can take keyboard focus. The overlay only exists
    // while shown (see overlay_lifetime.h): a live overlay widget blocks mouse input on macOS.
    overlay_.Adopt(
        window_->AddOverlayView(overlay_root_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true));
    if (overlay_.has_overlay()) {
        overlay_.get()->SetVisible(false);
    }
}

void FindBarOverlay::Show(std::string_view query, bool match_case, const CefRect& anchor) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || window_ == nullptr) {
        return;
    }
    if (overlay_lifetime_.Show() == OverlayStep::kCreate) {
        CreateOverlay();
    }
    if (!overlay_.has_overlay()) {
        return;
    }
    match_case_ = match_case;
    ApplyMatchCaseVisual();
    field_->SetText(std::string(query));
    SetCount("", false);
    UpdateBounds(anchor);
    overlay_.get()->SetVisible(true);
    visible_ = true;
    FocusField();
}

void FindBarOverlay::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
        overlay_.Release();
    }
    visible_ = false;
}

void FindBarOverlay::FocusField() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !visible_) {
        return;
    }
    field_->RequestFocus();
    field_->SelectAll(/*reversed=*/false);
}

void FindBarOverlay::UpdateBounds(const CefRect& anchor) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !overlay_.has_overlay() || window_ == nullptr) {
        return;
    }
    const DipRect card = CardBounds(
        {.x = anchor.x, .y = anchor.y, .width = anchor.width, .height = anchor.height});
    card_.SetContentSize(card.width, card.height);
    overlay_.get()->SetBounds(
        OverlayCard::OverlayBoundsForContent(CefRect(card.x, card.y, card.width, card.height)));
}

void FindBarOverlay::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    card_.Apply(tokens_);
    ApplyColors();
}

void FindBarOverlay::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    if (field_delegate_ != nullptr) {
        field_delegate_->Detach();
    }
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    for (const CefRefPtr<SurfaceDelegate>& delegate : surface_delegates_) {
        delegate->Detach();
    }
    static_cast<void>(overlay_lifetime_.Detach());
    overlay_.DestroyNow();
    window_ = nullptr;
}

void FindBarOverlay::SetCount(std::string_view label, bool no_matches) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    no_matches_ = no_matches;
    count_->SetText(std::string(label));
    count_->SetVisible(!label.empty());
    count_->SetEnabledTextColors((no_matches_ ? tokens_.text : tokens_.text_secondary).argb);
    count_->SetTextColor(CEF_BUTTON_STATE_DISABLED,
                         (no_matches_ ? tokens_.text : tokens_.text_secondary).argb);
}

bool FindBarOverlay::HandleFieldKeyEvent(CefRefPtr<CefTextfield> textfield,
                                         const CefKeyEvent& event) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || event.type != KEYEVENT_RAWKEYDOWN || host_ == nullptr) {
        return false;
    }
    switch (event.windows_key_code) {
        case kReturnKeyCode:
            host_->OnFindStep(/*forward=*/(event.modifiers & EVENTFLAG_SHIFT_DOWN) == 0);
            return true;
        case kEscapeKeyCode:
            host_->OnFindCloseRequested();
            return true;
        case kTabKeyCode:
            // A single field: Tab must not move focus out of the find bar.
            textfield->RequestFocus();
            return true;
        default:
            break;
    }
#if !defined(__APPLE__)
    // On macOS the NSMenu owns Cmd+G; elsewhere the field sees Ctrl+G itself.
    if ((event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0 && event.windows_key_code == 'G') {
        host_->OnFindStep(/*forward=*/(event.modifiers & EVENTFLAG_SHIFT_DOWN) == 0);
        return true;
    }
#endif
    return false;
}

void FindBarOverlay::HandleButtonPressed(ChromeViewId id) {
    if (detached_ || host_ == nullptr) {
        return;
    }
    switch (id) {
        case ChromeViewId::kFindBarPrevious:
            host_->OnFindStep(/*forward=*/false);
            return;
        case ChromeViewId::kFindBarNext:
            host_->OnFindStep(/*forward=*/true);
            return;
        case ChromeViewId::kFindBarMatchCase:
            match_case_ = !match_case_;
            ApplyMatchCaseVisual();
            host_->OnFindMatchCaseChanged(match_case_);
            return;
        case ChromeViewId::kFindBarClose:
            host_->OnFindCloseRequested();
            return;
        default:
            return;
    }
}

void FindBarOverlay::ApplyMatchCaseVisual() {
    const ArgbColor surface = PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, tokens_);
    const ArgbColor on = MixColor(surface, tokens_.accent, 0.2);
    match_case_button_->SetBackgroundColor((match_case_ ? on : surface).argb);
    const ArgbColor text = match_case_ ? tokens_.accent : tokens_.text_secondary;
    match_case_button_->SetEnabledTextColors(text.argb);
    match_case_button_->SetAccessibleName(match_case_ ? "Match case, on" : "Match case, off");
    match_case_button_->SetTooltipText(match_case_ ? "Match case: on" : "Match case: off");
}

void FindBarOverlay::ApplyColors() {
    const ArgbColor surface = PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, tokens_);
    panel_->SetBackgroundColor(surface.argb);
    field_->SetBackgroundColor(
        PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kQueryWell, tokens_).argb);
    field_->SetFontList("Geist, 13px");
    for (const CefRefPtr<CefLabelButton>& button : {count_, previous_, next_, close_}) {
        button->SetBackgroundColor(surface.argb);
    }
    const auto set_icon = [&](const CefRefPtr<CefLabelButton>& button, ChromeIcon icon,
                              ChromeIconSize size) {
        const std::optional<CefRefPtr<CefImage>> image =
            icons_.Load(icon, ChromeIconTone::kSecondary, size);
        if (image.has_value()) {
            button->SetImage(CEF_BUTTON_STATE_NORMAL, *image);
        }
    };
    set_icon(previous_, ChromeIcon::kBack, ChromeIconSize::k16);
    set_icon(next_, ChromeIcon::kForward, ChromeIconSize::k16);
    set_icon(close_, ChromeIcon::kClose, ChromeIconSize::k13);
    SetCount(count_->GetText().ToString(), no_matches_);
    ApplyMatchCaseVisual();
}

}  // namespace island
