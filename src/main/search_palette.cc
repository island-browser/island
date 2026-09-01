#include "search_palette.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

// allow: SIZE_OK - the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kTabKeyCode = 0x09;
constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;
constexpr int kArrowUpKeyCode = 0x26;
constexpr int kArrowDownKeyCode = 0x28;

int PaletteRowHeight(const ChromeTokens& tokens) {
    return tokens.spacing_6_dip + tokens.spacing_4_dip;
}

CefBoxLayoutSettings PaletteLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_insets =
        CefInsets(tokens.spacing_3_dip, tokens.spacing_3_dip, tokens.spacing_3_dip,
                  tokens.spacing_3_dip);
    return settings;
}

CefBoxLayoutSettings PaletteRowLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_2_dip;
    settings.inside_border_vertical_spacing = tokens.spacing_1_dip;
    return settings;
}

}  // namespace

// CefView drops any custom background color when the window's ThemeChanged()
// cascade reaches it, so every palette surface re-asserts its token color from
// OnThemeChanged, mirroring BrowserChrome::SurfacePanelDelegate.
class SearchPalette::PaletteSurfaceDelegate final : public CefPanelDelegate {
  public:
    PaletteSurfaceDelegate(SearchPalette* palette, PaletteSurfaceSlot slot,
                           CefSize preferred_size = CefSize())
        : palette_(palette), slot_(slot), preferred_size_(preferred_size) {}

    void Detach() { palette_ = nullptr; }
    void SetSlot(PaletteSurfaceSlot slot) { slot_ = slot; }
    [[nodiscard]] PaletteSurfaceSlot slot() const { return slot_; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (palette_ != nullptr) {
            view->SetBackgroundColor(palette_->ResolvePaletteColor(slot_).argb);
        }
    }

  private:
    SearchPalette* palette_;
    PaletteSurfaceSlot slot_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(PaletteSurfaceDelegate);
};

class SearchPalette::PaletteButtonDelegate final : public CefButtonDelegate {
  public:
    explicit PaletteButtonDelegate(SearchPalette* palette) : palette_(palette) {}

    void Detach() { palette_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (palette_ == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < palette_->provider_labels_.size(); ++index) {
            if (palette_->provider_labels_[index] != nullptr &&
                palette_->provider_labels_[index]->IsSame(button)) {
                palette_->HandleRowPressed(index);
                return;
            }
        }
    }

  private:
    SearchPalette* palette_;

    IMPLEMENT_REFCOUNTING(PaletteButtonDelegate);
};

class SearchPalette::PaletteTextfieldDelegate final : public CefTextfieldDelegate {
  public:
    PaletteTextfieldDelegate(SearchPalette* palette, CefSize preferred_size)
        : palette_(palette), preferred_size_(preferred_size) {}

    void Detach() { palette_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override {
        CEF_REQUIRE_UI_THREAD();
        return palette_ != nullptr && palette_->HandleQueryKeyEvent(textfield, event);
    }

    void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override {
        CEF_REQUIRE_UI_THREAD();
        if (palette_ != nullptr) {
            palette_->HandleQueryChanged(textfield);
        }
    }

  private:
    SearchPalette* palette_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(PaletteTextfieldDelegate);
};

SearchPalette::SearchPalette(SearchPaletteHost& host, CefRefPtr<CefWindow> window,
                             ChromeTokens tokens)
    : host_(&host), window_(std::move(window)), tokens_(tokens) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    // The palette is one overlay on the one existing CefWindow. can_activate is
    // true so the query field can take keyboard focus; the overlay starts hidden
    // and is only ever shown and hidden from here on.
    overlay_ = window_->AddOverlayView(panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true);
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    ApplySurfaceColors();
    ProjectHighlight();
}

SearchPalette::~SearchPalette() { Detach(); }

void SearchPalette::BuildViews() {
    const int row_height = PaletteRowHeight(tokens_);

    panel_delegate_ = new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kPanel);
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kSearchPalette));
    panel_->SetToBoxLayout(PaletteLayout(tokens_));

    textfield_delegate_ =
        new PaletteTextfieldDelegate(this, CefSize(SearchPaletteWidthDip(), row_height));
    query_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    query_field_->SetID(static_cast<int>(ChromeViewId::kSearchPaletteQuery));
    query_field_->SetAccessibleName("Search query");
    query_field_->SetPlaceholderText("Search the web");
    query_field_->SetFocusable(true);
    panel_->AddChildView(query_field_);

    button_delegate_ = new PaletteButtonDelegate(this);
    const std::span<const SearchProvider> providers = SearchProviders();
    provider_rows_.reserve(providers.size());
    row_delegates_.reserve(providers.size());
    provider_labels_.reserve(providers.size());
    for (const SearchProvider& provider : providers) {
        CefRefPtr<PaletteSurfaceDelegate> row_delegate =
            new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kRow, CefSize(0, row_height));
        surface_delegates_.push_back(row_delegate);
        row_delegates_.push_back(row_delegate);
        CefRefPtr<CefPanel> row = CefPanel::CreatePanel(row_delegate);
        row->SetID(static_cast<int>(ChromeViewId::kSearchPaletteProvider));
        CefRefPtr<CefBoxLayout> row_layout = row->SetToBoxLayout(PaletteRowLayout(tokens_));

        CefRefPtr<CefLabelButton> label =
            CefLabelButton::CreateLabelButton(button_delegate_, std::string(provider.display_name));
        label->SetID(static_cast<int>(ChromeViewId::kSearchPaletteProviderName));
        label->SetAccessibleName(SearchProviderRowAccessibleName(provider, model_.query()));
        label->SetTooltipText(std::string(provider.base_url));
        label->SetFocusable(false);
        label->SetMinimumSize(CefSize(0, row_height));
        row->AddChildView(label);
        row_layout->SetFlexForView(label, 1);

        panel_->AddChildView(row);
        provider_rows_.push_back(row);
        provider_labels_.push_back(label);
    }
}

int SearchPalette::PreferredContentHeightDip() const {
    const int row_height = PaletteRowHeight(tokens_);
    const auto row_count = static_cast<int>(SearchProviders().size()) + 1;
    return row_height * row_count + tokens_.spacing_2_dip * (row_count - 1) +
           tokens_.spacing_3_dip * 2;
}

void SearchPalette::Show() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr) {
        return;
    }
    model_.Open();
    query_field_->SetText("");
    UpdateBounds();
    overlay_->SetVisible(true);
    ProjectHighlight();
    query_field_->RequestFocus();
}

void SearchPalette::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.Close();
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
}

void SearchPalette::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    const DipRect bounds = SearchPaletteBounds(
        {.x = 0, .y = 0, .width = window_size.width, .height = window_size.height},
        PreferredContentHeightDip());
    overlay_->SetBounds(CefRect(bounds.x, bounds.y, bounds.width, bounds.height));
}

void SearchPalette::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    ApplySurfaceColors();
    ProjectHighlight();
}

void SearchPalette::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    model_.Close();
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    if (textfield_delegate_ != nullptr) {
        textfield_delegate_->Detach();
    }
    for (const CefRefPtr<PaletteSurfaceDelegate>& delegate : surface_delegates_) {
        delegate->Detach();
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

bool SearchPalette::HandleQueryKeyEvent(CefRefPtr<CefTextfield> textfield,
                                        const CefKeyEvent& event) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || event.type != KEYEVENT_RAWKEYDOWN) {
        return false;
    }

    switch (event.windows_key_code) {
        case kReturnKeyCode:
            model_.SetQuery(textfield->GetText().ToString());
            SubmitHighlighted();
            return true;
        case kEscapeKeyCode:
            Hide();
            if (host_ != nullptr) {
                host_->OnSearchPaletteDismissed();
            }
            return true;
        case kArrowUpKeyCode:
            model_.MoveHighlight(-1);
            ProjectHighlight();
            return true;
        case kArrowDownKeyCode:
            model_.MoveHighlight(1);
            ProjectHighlight();
            return true;
        case kTabKeyCode:
            // Tab never leaves the palette: it advances the highlighted provider
            // and keeps keyboard focus on the query field, which is the palette's
            // single focus anchor.
            model_.MoveHighlight(event.modifiers & EVENTFLAG_SHIFT_DOWN ? -1 : 1);
            ProjectHighlight();
            textfield->RequestFocus();
            return true;
        default:
            return false;
    }
}

void SearchPalette::HandleQueryChanged(CefRefPtr<CefTextfield> textfield) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.SetQuery(textfield->GetText().ToString());
    ProjectHighlight();
}

void SearchPalette::HandleRowPressed(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.SetHighlightedIndex(index);
    model_.SetQuery(query_field_->GetText().ToString());
    ProjectHighlight();
    SubmitHighlighted();
}

void SearchPalette::SubmitHighlighted() {
    CEF_REQUIRE_UI_THREAD();
    const std::optional<SearchSubmission> submission = model_.Submit();
    if (!submission.has_value()) {
        // Empty or whitespace-only query: no navigation, no error page, palette
        // stays open.
        return;
    }
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    if (host_ != nullptr) {
        host_->OnSearchPaletteSubmitted(*submission);
    }
}

void SearchPalette::ApplySurfaceColors() {
    panel_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kPanel).argb);
    query_field_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kQueryWell).argb);
    query_field_->SetFontList("Geist Mono, 14px");
    for (const CefRefPtr<CefLabelButton>& label : provider_labels_) {
        label->SetEnabledTextColors(tokens_.text.argb);
        label->SetFontList("Geist, 14px");
    }
}

void SearchPalette::ProjectHighlight() {
    const std::span<const SearchProvider> providers = SearchProviders();
    for (std::size_t index = 0; index < provider_rows_.size(); ++index) {
        const bool highlighted = index == model_.highlighted_index();
        const PaletteSurfaceSlot slot =
            highlighted ? PaletteSurfaceSlot::kHighlightedRow : PaletteSurfaceSlot::kRow;
        provider_rows_[index]->SetBackgroundColor(ResolvePaletteColor(slot).argb);
        // The row delegate remembers the slot so the theme-change re-assertion
        // restores the highlight rather than flattening every row.
        row_delegates_[index]->SetSlot(slot);
        if (index < providers.size()) {
            provider_labels_[index]->SetAccessibleName(
                SearchProviderRowAccessibleName(providers[index], model_.query()));
        }
    }
}

ArgbColor SearchPalette::ResolvePaletteColor(PaletteSurfaceSlot slot) const {
    return PaletteSurfaceRoleForTokens(slot, tokens_);
}

}  // namespace island
