#include "cheatsheet_overlay.h"

#include <algorithm>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "search_palette_model.h"

namespace island {
namespace {

constexpr int kCloseSizeDip = 24;

}  // namespace

class CheatsheetOverlay::SurfaceDelegate final : public CefPanelDelegate {
  public:
    explicit SurfaceDelegate(CheatsheetOverlay* overlay, CefSize preferred_size = CefSize())
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
    CheatsheetOverlay* overlay_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};

class CheatsheetOverlay::ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(CheatsheetOverlay* overlay) : overlay_(overlay) {}

    void Detach() { overlay_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton>) override {
        CEF_REQUIRE_UI_THREAD();
        // Only the close button is enabled; the row labels never fire.
        if (overlay_ != nullptr) {
            overlay_->HandleClosePressed();
        }
    }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (overlay_ != nullptr) {
            overlay_->ApplyColors();
        }
    }

  private:
    CheatsheetOverlay* overlay_;

    IMPLEMENT_REFCOUNTING(ButtonDelegate);
};

bool CheatsheetOverlay::ContainsDip(const CefRect& bounds, const CefPoint& point) noexcept {
    return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
           point.y < bounds.y + bounds.height;
}

DipRect CheatsheetOverlay::CardBounds(const DipRect& anchor, int content_height) noexcept {
    const int width = std::max(0, std::min(kContentWidthDip, anchor.width - 2 * kAnchorInsetDip));
    const int height = std::max(0, std::min(content_height, anchor.height - 2 * kAnchorInsetDip));
    return {.x = anchor.x + anchor.width - width - kAnchorInsetDip,
            .y = anchor.y + anchor.height - height - kAnchorInsetDip,
            .width = width,
            .height = height};
}

CheatsheetOverlay::CheatsheetOverlay(CheatsheetOverlayHost& host, CefRefPtr<CefWindow> window,
                                     ChromeTokens tokens, std::filesystem::path icon_resource_root)
    : host_(&host),
      window_(std::move(window)),
      tokens_(std::move(tokens)),
      icons_(std::move(icon_resource_root)) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    ApplyColors();
}

CheatsheetOverlay::~CheatsheetOverlay() { Detach(); }

void CheatsheetOverlay::BuildViews() {
    CefRefPtr<SurfaceDelegate> panel_delegate =
        new SurfaceDelegate(this, CefSize(kContentWidthDip, kContentHeightDip));
    surface_delegates_.push_back(panel_delegate);
    panel_ = CefPanel::CreatePanel(panel_delegate);
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = 0;
    settings.inside_border_insets =
        CefInsets(0, tokens_.spacing_3_dip, kBottomInsetDip, tokens_.spacing_3_dip);
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    panel_->SetToBoxLayout(settings);

    // CEF refuses to create a CefLabelButton without a delegate, so the rows share this one.
    button_delegate_ = new ButtonDelegate(this);

    CefRefPtr<SurfaceDelegate> header_delegate =
        new SurfaceDelegate(this, CefSize(kContentWidthDip, kHeaderHeightDip));
    surface_delegates_.push_back(header_delegate);
    header_ = CefPanel::CreatePanel(header_delegate);
    CefBoxLayoutSettings row_settings;
    row_settings.horizontal = static_cast<int>(true);
    row_settings.between_child_spacing = tokens_.spacing_2_dip;
    row_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    CefRefPtr<CefBoxLayout> header_layout = header_->SetToBoxLayout(row_settings);
    title_ = CefLabelButton::CreateLabelButton(button_delegate_, "Keyboard shortcuts");
    title_->SetEnabled(false);
    title_->SetFocusable(false);
    title_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    header_->AddChildView(title_);
    header_layout->SetFlexForView(title_, 1);
    close_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    close_->SetAccessibleName("Close shortcuts");
    close_->SetTooltipText("Close (esc)");
    close_->SetFocusable(true);
    close_->SetMinimumSize(CefSize(kCloseSizeDip, kCloseSizeDip));
    close_->SetInkDropEnabled(true);
    header_->AddChildView(close_);
    panel_->AddChildView(header_);

    for (RowViews& row : rows_) {
        CefRefPtr<SurfaceDelegate> row_delegate =
            new SurfaceDelegate(this, CefSize(kContentWidthDip, kRowHeightDip));
        surface_delegates_.push_back(row_delegate);
        row.panel = CefPanel::CreatePanel(row_delegate);
        CefRefPtr<CefBoxLayout> layout = row.panel->SetToBoxLayout(row_settings);
        row.label = CefLabelButton::CreateLabelButton(button_delegate_, "");
        row.keys = CefLabelButton::CreateLabelButton(button_delegate_, "");
        for (const CefRefPtr<CefLabelButton>& button : {row.label, row.keys}) {
            button->SetEnabled(false);
            button->SetFocusable(false);
            row.panel->AddChildView(button);
        }
        row.label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        row.keys->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_RIGHT);
        layout->SetFlexForView(row.label, 1);
        panel_->AddChildView(row.panel);
    }

    overlay_root_ = card_.Wrap(panel_, tokens_);
    card_.SetContentSize(kContentWidthDip, kContentHeightDip);
}

void CheatsheetOverlay::Apply(OverlayStep step) {
    switch (step) {
        case OverlayStep::kCreate:
            // can_activate is true so the close button can hold focus for keyboard users. Focus
            // does not close the sheet; a press outside the card does (see HandleMouseDown). The
            // overlay only exists while shown (see overlay_lifetime.h): a live overlay widget
            // blocks mouse input on macOS.
            overlay_.Adopt(window_->AddOverlayView(overlay_root_, CEF_DOCKING_MODE_CUSTOM,
                                                   /*can_activate=*/true));
            if (overlay_.has_overlay()) {
                overlay_.get()->SetVisible(false);
                if (CefWindowHandle handle = window_->GetWindowHandle(); handle != nullptr) {
                    mouse_monitor_ = std::make_unique<MouseDownMonitor>(
                        handle,
                        [this](bool in_view, CefPoint point) { HandleMouseDown(in_view, point); });
                }
            }
            return;
        case OverlayStep::kDestroy:
            mouse_monitor_.reset();
            overlay_.Release();
            return;
        case OverlayStep::kNone:
            return;
    }
}

void CheatsheetOverlay::Toggle(const std::vector<CheatsheetRow>& rows, const CefRect& anchor) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || window_ == nullptr) {
        return;
    }
    const OverlayStep step = lifetime_.Toggle();
    Apply(step);
    if (step != OverlayStep::kCreate) {
        return;
    }
    if (!overlay_.has_overlay()) {
        static_cast<void>(lifetime_.Hide());
        return;
    }
    SetRows(rows);
    UpdateBounds(anchor);
    overlay_.get()->SetVisible(true);
    // Showing can re-enter the window (focus moves while the overlay activates); give the close
    // button focus only if the sheet is still open. Focus is for keyboard users; nothing closes
    // the sheet on focus loss.
    if (lifetime_.visible() && overlay_.has_overlay()) {
        close_->RequestFocus();
    }
}

void CheatsheetOverlay::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        Apply(lifetime_.Hide());
    }
}

void CheatsheetOverlay::UpdateBounds(const CefRect& anchor) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !overlay_.has_overlay()) {
        return;
    }
    const DipRect card =
        CardBounds({.x = anchor.x, .y = anchor.y, .width = anchor.width, .height = anchor.height},
                   kContentHeightDip);
    card_bounds_ = CefRect(card.x, card.y, card.width, card.height);
    card_.SetContentSize(card.width, card.height);
    overlay_.get()->SetBounds(
        OverlayCard::OverlayBoundsForContent(CefRect(card.x, card.y, card.width, card.height)));
}

void CheatsheetOverlay::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = std::move(tokens);
    card_.Apply(tokens_);
    ApplyColors();
}

void CheatsheetOverlay::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    for (const CefRefPtr<SurfaceDelegate>& delegate : surface_delegates_) {
        delegate->Detach();
    }
    static_cast<void>(lifetime_.Detach());
    mouse_monitor_.reset();
    overlay_.DestroyNow();
    window_ = nullptr;
}

void CheatsheetOverlay::SetRows(const std::vector<CheatsheetRow>& rows) {
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const bool present = index < rows.size();
        rows_[index].label->SetText(present ? rows[index].label : std::string());
        rows_[index].keys->SetText(present ? rows[index].keys : std::string());
        rows_[index].panel->SetVisible(present);
    }
}

void CheatsheetOverlay::HandleMouseDown(bool in_view, CefPoint point) {
    // Runs inside the event monitor, so the close is handed to the host, which posts it.
    const bool inside = in_view && ContainsDip(card_bounds_, point);
    if (!detached_ && host_ != nullptr && lifetime_.OutsideMouseDownCloses(inside)) {
        host_->OnCheatsheetCloseRequested(/*refocus_page=*/false);
    }
}

void CheatsheetOverlay::HandleClosePressed() {
    if (!detached_ && host_ != nullptr) {
        host_->OnCheatsheetCloseRequested(/*refocus_page=*/true);
    }
}

void CheatsheetOverlay::ApplyColors() {
    const ArgbColor surface = PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, tokens_);
    panel_->SetBackgroundColor(surface.argb);
    header_->SetBackgroundColor(surface.argb);
    title_->SetBackgroundColor(surface.argb);
    title_->SetFontList(BrowserChrome::FontListForTokens(tokens_, 13, ChromeFontWeight::kSemiBold));
    title_->SetTextColor(CEF_BUTTON_STATE_DISABLED, tokens_.text.argb);
    close_->SetBackgroundColor(surface.argb);
    const std::optional<CefRefPtr<CefImage>> close_icon =
        icons_.Load(ChromeIcon::kClose, ChromeIconTone::kSecondary, ChromeIconSize::k13);
    if (close_icon.has_value()) {
        close_->SetImage(CEF_BUTTON_STATE_NORMAL, *close_icon);
    }
    for (const RowViews& row : rows_) {
        row.panel->SetBackgroundColor(surface.argb);
        row.label->SetBackgroundColor(surface.argb);
        row.label->SetFontList(
            BrowserChrome::FontListForTokens(tokens_, 13, ChromeFontWeight::kRegular));
        row.label->SetTextColor(CEF_BUTTON_STATE_DISABLED, tokens_.text.argb);
        row.keys->SetBackgroundColor(surface.argb);
        row.keys->SetFontList(
            BrowserChrome::FontListForTokens(tokens_, 12, ChromeFontWeight::kMedium));
        row.keys->SetTextColor(CEF_BUTTON_STATE_DISABLED, tokens_.text_secondary.argb);
    }
}

}  // namespace island
