#include "space_rename_overlay.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "search_palette_model.h"

// allow: SIZE_OK - the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;
constexpr int kTabKeyCode = 0x09;

constexpr int kSpaceRenameWidthDip = 320;
constexpr int kSpaceRenameTopOffsetDip = 96;

constexpr int kSpaceRenameFieldHeightDip = 28;

DipRect SpaceRenameBounds(const DipRect& window_bounds) noexcept {
    const int width = std::min(kSpaceRenameWidthDip, window_bounds.width);
    return {.x = (window_bounds.width - width) / 2,
            .y = std::min(kSpaceRenameTopOffsetDip,
                          std::max(0, window_bounds.height - kSpaceRenameFieldHeightDip)),
            .width = width,
            .height = kSpaceRenameFieldHeightDip};
}

// Trims ASCII whitespace; a fully-whitespace name commits nothing.
std::string TrimSpaceName(std::string_view text) {
    const auto is_space = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    std::size_t begin = 0;
    while (begin < text.size() && is_space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

// Same theme re-assertion contract as the palette surface delegates.
class SpaceRenameOverlay::RenameSurfaceDelegate final : public CefPanelDelegate {
  public:
    RenameSurfaceDelegate(SpaceRenameOverlay* overlay, CefSize preferred_size)
        : overlay_(overlay), preferred_size_(preferred_size) {}

    void Detach() { overlay_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (overlay_ != nullptr) {
            view->SetBackgroundColor(
                PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, overlay_->tokens_).argb);
        }
    }

  private:
    SpaceRenameOverlay* overlay_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(RenameSurfaceDelegate);
};

class SpaceRenameOverlay::RenameTextfieldDelegate final : public CefTextfieldDelegate {
  public:
    RenameTextfieldDelegate(SpaceRenameOverlay* overlay, CefSize preferred_size)
        : overlay_(overlay), preferred_size_(preferred_size) {}

    void Detach() { overlay_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override {
        CEF_REQUIRE_UI_THREAD();
        return overlay_ != nullptr && overlay_->HandleFieldKeyEvent(textfield, event);
    }

  private:
    SpaceRenameOverlay* overlay_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(RenameTextfieldDelegate);
};

SpaceRenameOverlay::SpaceRenameOverlay(SpaceRenameOverlayHost& host, CefRefPtr<CefWindow> window,
                                       ChromeTokens tokens, std::string current_name)
    : host_(&host),
      window_(std::move(window)),
      tokens_(tokens),
      current_name_(std::move(current_name)) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    overlay_ = window_->AddOverlayView(panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true);
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    ApplySurfaceColors();
}

SpaceRenameOverlay::~SpaceRenameOverlay() { Detach(); }

void SpaceRenameOverlay::BuildViews() {
    panel_delegate_ =
        new RenameSurfaceDelegate(this, CefSize(kSpaceRenameWidthDip, kSpaceRenameFieldHeightDip));
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kSpaceRenameOverlay));
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.inside_border_insets = CefInsets(tokens_.spacing_2_dip, tokens_.spacing_2_dip,
                                              tokens_.spacing_2_dip, tokens_.spacing_2_dip);
    panel_->SetToBoxLayout(settings);

    textfield_delegate_ = new RenameTextfieldDelegate(
        this, CefSize(kSpaceRenameWidthDip, kSpaceRenameFieldHeightDip));
    name_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    name_field_->SetID(static_cast<int>(ChromeViewId::kSpaceRenameField));
    name_field_->SetAccessibleName("Space name");
    name_field_->SetPlaceholderText("Space name");
    name_field_->SetFocusable(true);
    name_field_->SetFontList("Geist, 14px");
    panel_->AddChildView(name_field_);
}

void SpaceRenameOverlay::Show() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr) {
        return;
    }
    name_field_->SetText(current_name_);
    UpdateBounds();
    overlay_->SetVisible(true);
    visible_ = true;
    name_field_->RequestFocus();
    name_field_->SelectAll(/*reversed=*/false);
}

void SpaceRenameOverlay::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    visible_ = false;
}

void SpaceRenameOverlay::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    const DipRect bounds = SpaceRenameBounds(
        {.x = 0, .y = 0, .width = window_size.width, .height = window_size.height});
    overlay_->SetBounds(CefRect(bounds.x, bounds.y, bounds.width, bounds.height));
}

void SpaceRenameOverlay::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    ApplySurfaceColors();
}

void SpaceRenameOverlay::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    if (textfield_delegate_ != nullptr) {
        textfield_delegate_->Detach();
    }
    if (panel_delegate_ != nullptr) {
        panel_delegate_->Detach();
    }
    if (overlay_ != nullptr) {
        if (overlay_->IsValid()) {
            overlay_->SetVisible(false);
            overlay_->Destroy();
        }
        overlay_ = nullptr;
    }
    window_ = nullptr;
}

bool SpaceRenameOverlay::HandleFieldKeyEvent(CefRefPtr<CefTextfield> textfield,
                                             const CefKeyEvent& event) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || event.type != KEYEVENT_RAWKEYDOWN) {
        return false;
    }

    switch (event.windows_key_code) {
        case kReturnKeyCode:
            Commit();
            return true;
        case kEscapeKeyCode:
            Hide();
            if (host_ != nullptr) {
                host_->OnSpaceRenameCancelled();
            }
            return true;
        case kTabKeyCode:
            // The rename overlay is a single field; Tab must not move focus out
            // of it while it is open.
            textfield->RequestFocus();
            return true;
        default:
            return false;
    }
}

void SpaceRenameOverlay::Commit() {
    CEF_REQUIRE_UI_THREAD();
    const std::string name = TrimSpaceName(name_field_->GetText().ToString());
    if (name.empty()) {
        // An all-whitespace name is not a rename: keep the overlay open with
        // the previous name so the field is never committed empty.
        name_field_->SetText(current_name_);
        return;
    }
    Hide();
    if (host_ != nullptr) {
        host_->OnSpaceRenameCommitted(name);
    }
}

void SpaceRenameOverlay::ApplySurfaceColors() {
    panel_->SetBackgroundColor(
        PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kPanel, tokens_).argb);
    name_field_->SetBackgroundColor(
        PaletteSurfaceRoleForTokens(PaletteSurfaceSlot::kQueryWell, tokens_).argb);
}

}  // namespace island
