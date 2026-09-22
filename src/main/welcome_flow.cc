#include "welcome_flow.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace island {
namespace {

constexpr int kWelcomeWidthDip = 560;
constexpr int kChoiceRowHeightDip = 32;
constexpr int kNoteRowHeightDip = 24;
constexpr int kActionRowHeightDip = 40;
constexpr int kTitleHeightDip = 44;
constexpr int kSectionGapDip = 12;

CefBoxLayoutSettings WelcomePanelLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_insets = CefInsets(tokens.spacing_6_dip, tokens.spacing_6_dip,
                                              tokens.spacing_6_dip, tokens.spacing_6_dip);
    return settings;
}

}  // namespace

// CefView drops custom background colors on theme changes, so every welcome
// surface re-asserts its token color from OnThemeChanged, mirroring the
// palettes.
class WelcomeFlow::WelcomeSurfaceDelegate final : public CefPanelDelegate {
  public:
    WelcomeSurfaceDelegate(WelcomeFlow* flow, SurfaceSlot slot, CefSize preferred_size)
        : flow_(flow), slot_(slot), preferred_size_(preferred_size) {}

    void Detach() { flow_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (flow_ != nullptr) {
            view->SetBackgroundColor(flow_->ResolveColor(slot_).argb);
        }
    }

  private:
    WelcomeFlow* flow_;
    SurfaceSlot slot_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(WelcomeSurfaceDelegate);
};

class WelcomeFlow::WelcomeButtonDelegate final : public CefButtonDelegate {
  public:
    explicit WelcomeButtonDelegate(WelcomeFlow* flow) : flow_(flow) {}

    void Detach() { flow_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (flow_ != nullptr) {
            flow_->HandleButtonPressed(button->GetID());
        }
    }

  private:
    WelcomeFlow* flow_;

    IMPLEMENT_REFCOUNTING(WelcomeButtonDelegate);
};

WelcomeFlow::WelcomeFlow(WelcomeFlowHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                         ThemePreference current_theme, std::vector<ImportSourceInfo> sources)
    : host_(&host),
      window_(std::move(window)),
      tokens_(tokens),
      sources_(std::move(sources)),
      chosen_theme_(current_theme) {
    CEF_REQUIRE_UI_THREAD();
    source_checked_.assign(sources_.size(), true);
    BuildViews();
    overlay_ = window_->AddOverlayView(panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true);
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    ApplySurfaceColors();
    ProjectThemeChoice();
    ProjectSourceChecks();
}

WelcomeFlow::~WelcomeFlow() { Detach(); }

void WelcomeFlow::BuildViews() {
    panel_delegate_ =
        new WelcomeSurfaceDelegate(this, SurfaceSlot::kPanel, CefSize(kWelcomeWidthDip, 0));
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kWelcome));
    panel_->SetToBoxLayout(WelcomePanelLayout(tokens_));

    button_delegate_ = new WelcomeButtonDelegate(this);

    CefRefPtr<CefLabelButton> title =
        CefLabelButton::CreateLabelButton(button_delegate_, "Welcome to Island");
    title->SetID(kThemeSystemId - 1);
    title->SetAccessibleName("Welcome to Island");
    title->SetFocusable(false);
    title->SetMinimumSize(CefSize(0, kTitleHeightDip));
    title->SetEnabledTextColors(tokens_.text.argb);
    panel_->AddChildView(title);

    CefRefPtr<CefLabelButton> lede = CefLabelButton::CreateLabelButton(
        button_delegate_, "Choose how Island looks and import your bookmarks.");
    lede->SetID(kThemeSystemId - 1);
    lede->SetAccessibleName(lede->GetText());
    lede->SetFocusable(false);
    lede->SetEnabledTextColors(tokens_.text.argb);
    panel_->AddChildView(lede);

    // Appearance: three radio-style rows; the selected one leads with a filled
    // dot and renders in the accent (LabelButton in this CEF distribution has
    // no native button chrome, so state lives in the glyph and the color).
    const char* const theme_labels[3] = {"System", "Light", "Dark"};
    const int theme_ids[3] = {kThemeSystemId, kThemeLightId, kThemeDarkId};
    for (int index = 0; index < 3; ++index) {
        CefRefPtr<CefLabelButton> button = CefLabelButton::CreateLabelButton(button_delegate_, "");
        button->SetID(theme_ids[index]);
        button->SetAccessibleName(std::string("Appearance: ") + theme_labels[index]);
        button->SetMinimumSize(CefSize(0, kChoiceRowHeightDip));
        panel_->AddChildView(button);
        theme_buttons_[index] = button;
    }

    // Import sources: one toggle row per READABLE browser. Unreadable sources
    // collapse into one muted note instead of dominating the card.
    std::vector<std::string> unreadable;
    source_buttons_.reserve(sources_.size());
    for (std::size_t index = 0; index < sources_.size(); ++index) {
        if (!sources_[index].available) {
            unreadable.push_back(sources_[index].display_name);
            continue;
        }
        CefRefPtr<CefLabelButton> source = CefLabelButton::CreateLabelButton(button_delegate_, "");
        source->SetID(kSourceIdBase + static_cast<int>(index));
        source->SetAccessibleName("Import bookmarks from " + sources_[index].display_name);
        source->SetTooltipText(sources_[index].bookmarks_path.generic_string());
        source->SetMinimumSize(CefSize(0, kChoiceRowHeightDip));
        panel_->AddChildView(source);
        source_buttons_.push_back(source);
    }
    if (!unreadable.empty()) {
        std::string note = "Not readable on this machine: ";
        for (std::size_t i = 0; i < unreadable.size(); ++i) {
            note += unreadable[i];
            if (i + 1 < unreadable.size()) {
                note += ", ";
            }
        }
        CefRefPtr<CefLabelButton> note_label =
            CefLabelButton::CreateLabelButton(button_delegate_, note);
        note_label->SetID(kThemeSystemId - 1);
        note_label->SetAccessibleName(note);
        note_label->SetFocusable(false);
        note_label->SetEnabledTextColors(tokens_.text.argb);
        panel_->AddChildView(note_label);
    }

    // Actions: glyph-prefixed rows; the import action carries the accent.
    import_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    import_button_->SetID(kImportButtonId);
    import_button_->SetAccessibleName("Import and start browsing");
    import_button_->SetMinimumSize(CefSize(0, kActionRowHeightDip));
    panel_->AddChildView(import_button_);

    start_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "›  Just start browsing");
    start_button_->SetID(kStartButtonId);
    start_button_->SetAccessibleName("Start browsing without importing");
    start_button_->SetMinimumSize(CefSize(0, kActionRowHeightDip));
    panel_->AddChildView(start_button_);
}

void WelcomeFlow::HandleButtonPressed(int view_id) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    if (view_id == kThemeSystemId || view_id == kThemeLightId || view_id == kThemeDarkId) {
        chosen_theme_ = view_id == kThemeLightId  ? ThemePreference::kLight
                        : view_id == kThemeDarkId ? ThemePreference::kDark
                                                  : ThemePreference::kSystem;
        ProjectThemeChoice();
        if (host_ != nullptr) {
            host_->OnWelcomeThemeChanged(chosen_theme_);
        }
        return;
    }
    if (view_id >= kSourceIdBase && view_id < kSourceIdBase + static_cast<int>(sources_.size())) {
        const std::size_t index = static_cast<std::size_t>(view_id - kSourceIdBase);
        source_checked_[index] = !source_checked_[index];
        ProjectSourceChecks();
        UpdateImportButtonState();
        return;
    }
    if (view_id == kImportButtonId || view_id == kStartButtonId) {
        std::vector<ImportSource> checked;
        if (view_id == kImportButtonId) {
            for (std::size_t index = 0; index < sources_.size(); ++index) {
                if (source_checked_[index] && sources_[index].available) {
                    checked.push_back(sources_[index].id);
                }
            }
        }
        visible_ = false;
        if (overlay_ != nullptr) {
            overlay_->SetVisible(false);
        }
        if (host_ != nullptr) {
            host_->OnWelcomeCompleted(checked);
        }
    }
}

void WelcomeFlow::UpdateImportButtonState() {
    if (import_button_ == nullptr) {
        return;
    }
    const bool any = AnyCheckedAvailableSource();
    import_button_->SetText(any ? "▸  Import and start" : "No readable bookmarks to import");
    import_button_->SetEnabledTextColors(any ? tokens_.accent.argb : tokens_.text.argb);
}

bool WelcomeFlow::AnyCheckedAvailableSource() const {
    for (std::size_t index = 0; index < sources_.size(); ++index) {
        if (source_checked_[index] && sources_[index].available) {
            return true;
        }
    }
    return false;
}

void WelcomeFlow::Show() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr) {
        return;
    }
    visible_ = true;
    UpdateBounds();
    overlay_->SetVisible(true);
    ProjectThemeChoice();
    ProjectSourceChecks();
    if (theme_buttons_[0] != nullptr) {
        theme_buttons_[0]->RequestFocus();
    }
}

void WelcomeFlow::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    visible_ = false;
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
}

int WelcomeFlow::PreferredContentHeightDip() const {
    int readable = 0;
    int unreadable = 0;
    for (const ImportSourceInfo& info : sources_) {
        info.available ? ++readable : ++unreadable;
    }
    // Title + lede + 3 theme rows + readable source rows + optional unreadable
    // note + 2 action rows, plus the panel's vertical insets.
    const int rows = 1 + 3 + readable + (unreadable > 0 ? 1 : 0) + 2;
    return kTitleHeightDip + kChoiceRowHeightDip * (3 + readable) + kActionRowHeightDip * 2 +
           (unreadable > 0 ? kNoteRowHeightDip : 0) + kSectionGapDip * (rows + 1) +
           static_cast<int>(tokens_.spacing_6_dip) * 2;
}

void WelcomeFlow::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    const int height = PreferredContentHeightDip();
    const int x = std::max(0, (static_cast<int>(window_size.width) - kWelcomeWidthDip) / 2);
    const int y = std::max(0, (static_cast<int>(window_size.height) - height) / 2);
    overlay_->SetBounds(CefRect(x, y, kWelcomeWidthDip, height));
}

void WelcomeFlow::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    ApplySurfaceColors();
    ProjectThemeChoice();
    ProjectSourceChecks();
}

void WelcomeFlow::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    for (const CefRefPtr<WelcomeSurfaceDelegate>& delegate : surface_delegates_) {
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

void WelcomeFlow::ApplySurfaceColors() {
    CEF_REQUIRE_UI_THREAD();
    if (panel_ != nullptr) {
        panel_->SetBackgroundColor(ResolveColor(SurfaceSlot::kPanel).argb);
    }
}

void WelcomeFlow::ProjectThemeChoice() {
    // The filled dot + accent mark the active choice; the others use an open
    // dot on the text color.
    for (int index = 0; index < 3; ++index) {
        if (theme_buttons_[index] == nullptr) {
            continue;
        }
        const ThemePreference choice = index == 0   ? ThemePreference::kSystem
                                       : index == 1 ? ThemePreference::kLight
                                                    : ThemePreference::kDark;
        const bool selected = chosen_theme_ == choice;
        const char* const labels[3] = {"System", "Light", "Dark"};
        theme_buttons_[index]->SetText(std::string(selected ? "●  " : "○  ") + labels[index]);
        theme_buttons_[index]->SetEnabledTextColors(selected ? tokens_.accent.argb
                                                             : tokens_.text.argb);
    }
}

void WelcomeFlow::ProjectSourceChecks() {
    // source_buttons_ holds only the READABLE sources, in sources_ order.
    std::size_t row = 0;
    for (std::size_t index = 0; index < sources_.size() && row < source_buttons_.size(); ++index) {
        if (!sources_[index].available) {
            continue;
        }
        source_buttons_[row]->SetText(std::string(source_checked_[index] ? "☑  " : "☐  ") +
                                      sources_[index].display_name);
        source_buttons_[row]->SetEnabledTextColors(tokens_.text.argb);
        ++row;
    }
}

ArgbColor WelcomeFlow::ResolveColor(SurfaceSlot slot) const {
    const ChromeTokens tokens = tokens_;
    switch (slot) {
        case SurfaceSlot::kPanel:
            return tokens.surface;
        case SurfaceSlot::kSection:
        case SurfaceSlot::kRow:
            return tokens.surface_secondary;
    }
    return tokens.surface;
}

}  // namespace island
