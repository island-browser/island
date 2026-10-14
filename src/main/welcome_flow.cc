#include "welcome_flow.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace island {
namespace {

// Metrics in DIP. The card is a hairline-outlined surface; its text and controls sit on
// the spacing grid (4 / 8 / 24) with 32 DIP controls, like the rail.
constexpr int kHairlineDip = 1;
constexpr int kCardPaddingVerticalDip = 24;
// 24 DIP of padding plus the 6 DIP of internal inset a label button adds on its
// leading edge, so text and the outlined controls share one left edge.
constexpr int kCardPaddingHorizontalDip = 30;
constexpr int kSectionGapDip = 24;
constexpr int kItemGapDip = 8;
// Overlay width minus the card padding; the row stretches to the card, this only makes
// the preferred size non-empty so CEF honors the fixed height.
constexpr int kTextRowWidthDip = 480;
constexpr int kTitleHeightDip = 36;
constexpr int kLedeHeightDip = 24;
constexpr int kLabelHeightDip = 16;
constexpr int kControlHeightDip = 32;
constexpr int kNoteLineHeightDip = 20;
constexpr std::size_t kNoteLineChars = 64;
// The dim layer behind the card (alpha black): lighter over the light canvas.
constexpr std::uint32_t kScrimLightArgb = 0x47000000U;
constexpr std::uint32_t kScrimDarkArgb = 0x99000000U;

CefBoxLayoutSettings VerticalLayout(int spacing, const CefInsets& insets = CefInsets()) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = spacing;
    settings.inside_border_insets = insets;
    return settings;
}

CefBoxLayoutSettings HorizontalLayout(int spacing, const CefInsets& insets = CefInsets()) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = spacing;
    settings.inside_border_insets = insets;
    return settings;
}

CefInsets Uniform(int dip) { return CefInsets(dip, dip, dip, dip); }

// Posts one repaint after a burst of theme resets; the shared handle makes a task
// that outlives the flow a no-op.
class ReapplyTask final : public CefTask {
  public:
    ReapplyTask(std::shared_ptr<WelcomeFlow*> alive, void (*run)(WelcomeFlow*))
        : alive_(std::move(alive)), run_(run) {}

    void Execute() override {
        if (WelcomeFlow* flow = *alive_) {
            run_(flow);
        }
    }

  private:
    std::shared_ptr<WelcomeFlow*> alive_;
    void (*run_)(WelcomeFlow*);

    IMPLEMENT_REFCOUNTING(ReapplyTask);
};

}  // namespace

std::vector<std::string> WelcomeFlow::WrapUnreadableNote(const std::vector<std::string>& names,
                                                         std::size_t max_chars) {
    std::vector<std::string> lines;
    if (names.empty()) {
        return lines;
    }
    std::string current = "Not readable on this machine: ";
    bool line_has_name = false;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::string piece = names[index] + (index + 1 < names.size() ? "," : "");
        const std::size_t needed = current.size() + (line_has_name ? 1 : 0) + piece.size();
        if (line_has_name && needed > max_chars) {
            lines.push_back(current);
            current = piece;
        } else {
            current += (line_has_name ? " " : "") + piece;
        }
        line_has_name = true;
    }
    lines.push_back(current);
    return lines;
}

int WelcomeFlow::ContentHeightDip(int readable_sources, int note_lines) {
    // Import section: label, one 34 DIP outlined row per readable source, and the
    // wrapped note block, separated by kItemGapDip.
    int items = 1;
    int import_height = kLabelHeightDip;
    for (int row = 0; row < readable_sources; ++row) {
        import_height += kControlHeightDip + 2 * kHairlineDip;
        ++items;
    }
    if (note_lines > 0) {
        import_height += note_lines * kNoteLineHeightDip;
        ++items;
    }
    import_height += kItemGapDip * (items - 1);

    const int header = kTitleHeightDip + kLedeHeightDip;
    const int appearance = kLabelHeightDip + kItemGapDip + kControlHeightDip + 2 * kHairlineDip;
    const int actions = kControlHeightDip + 2 * kHairlineDip;
    return 2 * kHairlineDip + 2 * kCardPaddingVerticalDip + header + kSectionGapDip + appearance +
           kSectionGapDip + import_height + kSectionGapDip + actions;
}

WelcomeFlow::ButtonColors WelcomeFlow::SegmentColors(const ChromeTokens& tokens, bool selected) {
    // The selected segment steps to the secondary surface and takes the accent: the
    // accent marks active state only. Unselected segments are plain surface.
    if (selected) {
        return {.fill = tokens.surface_secondary, .text = tokens.accent};
    }
    return {.fill = tokens.surface, .text = tokens.text};
}

WelcomeFlow::ButtonColors WelcomeFlow::SecondaryButtonColors(const ChromeTokens& tokens) {
    return {.fill = tokens.surface, .text = tokens.text};
}

WelcomeFlow::ButtonColors WelcomeFlow::PrimaryButtonColors(const ChromeTokens& tokens) {
    // DESIGN.md §5: primary buttons use --text as background with --surface foreground.
    return {.fill = tokens.text, .text = tokens.surface};
}

// CefView drops custom background colors on theme changes, so every welcome
// surface re-asserts its token color from OnThemeChanged, mirroring the
// palettes, and reports the reset so the buttons repaint too.
class WelcomeFlow::WelcomeSurfaceDelegate final : public CefPanelDelegate {
  public:
    WelcomeSurfaceDelegate(WelcomeFlow* flow, SurfaceSlot slot, CefSize preferred_size)
        : flow_(flow), slot_(slot), preferred_size_(preferred_size) {}

    void Detach() { flow_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (flow_ != nullptr) {
            view->SetBackgroundColor(flow_->ResolveColor(slot_).argb);
            flow_->HandleViewThemeReset();
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

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (flow_ != nullptr) {
            flow_->HandleViewThemeReset();
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
      chosen_theme_(current_theme),
      alive_(std::make_shared<WelcomeFlow*>(this)) {
    CEF_REQUIRE_UI_THREAD();
    source_checked_.assign(sources_.size(), true);
    BuildViews();
    // The overlay covers the window: a translucent scrim that centers the shadowed card.
    scrim_delegate_ = new OverlayFillDelegate(ArgbColor{.argb = kScrimDarkArgb});
    scrim_panel_ = CefPanel::CreatePanel(scrim_delegate_);
    {
        CefBoxLayoutSettings settings;
        settings.horizontal = static_cast<int>(true);
        settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
        settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
        scrim_panel_->SetToBoxLayout(settings);
    }
    scrim_panel_->AddChildView(shadow_.Wrap(panel_, ThemeOfTokens(tokens_)));
    ApplyColors();
}

void WelcomeFlow::CreateOverlay() {
    // can_activate is true so the text field can take keyboard focus. The overlay only exists
    // while shown (see overlay_lifetime.h): a live overlay widget blocks mouse input on macOS.
    overlay_.Adopt(
        window_->AddOverlayView(scrim_panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true));
    if (overlay_.has_overlay()) {
        overlay_.get()->SetVisible(false);
    }
}

WelcomeFlow::~WelcomeFlow() { Detach(); }

CefRefPtr<CefPanel> WelcomeFlow::MakePanel(SurfaceSlot slot) {
    CefRefPtr<WelcomeSurfaceDelegate> delegate = new WelcomeSurfaceDelegate(this, slot, CefSize());
    surface_delegates_.push_back(delegate);
    CefRefPtr<CefPanel> panel = CefPanel::CreatePanel(delegate);
    panels_.emplace_back(panel, slot);
    return panel;
}

CefRefPtr<CefPanel> WelcomeFlow::MakeText(const std::string& text, TextRole role,
                                          CefRefPtr<CefLabelButton>* label_out) {
    CefRefPtr<CefLabelButton> label = CefLabelButton::CreateLabelButton(button_delegate_, text);
    label->SetID(kThemeSystemId - 1);
    label->SetAccessibleName(text);
    label->SetFocusable(false);
    label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    int height = kLabelHeightDip;
    switch (role) {
        case TextRole::kTitle:
            height = kTitleHeightDip;
            break;
        case TextRole::kLede:
            height = kLedeHeightDip;
            break;
        case TextRole::kSectionLabel:
            height = kLabelHeightDip;
            break;
        case TextRole::kNote:
            height = kNoteLineHeightDip;
            break;
    }
    // The row stretches the label to the row height, so the text is centered on a line
    // of exactly |height| DIP whatever padding the label button adds.
    CefRefPtr<WelcomeSurfaceDelegate> delegate =
        new WelcomeSurfaceDelegate(this, SurfaceSlot::kCard, CefSize(kTextRowWidthDip, height));
    surface_delegates_.push_back(delegate);
    CefRefPtr<CefPanel> row = CefPanel::CreatePanel(delegate);
    panels_.emplace_back(row, SurfaceSlot::kCard);
    CefRefPtr<CefBoxLayout> layout = row->SetToBoxLayout(HorizontalLayout(0));
    row->AddChildView(label);
    layout->SetFlexForView(label, 1);
    texts_.push_back({label, role});
    if (label_out != nullptr) {
        *label_out = label;
    }
    return row;
}

void WelcomeFlow::BuildViews() {
    panel_delegate_ = new WelcomeSurfaceDelegate(this, SurfaceSlot::kOutline, CefSize());
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panels_.emplace_back(panel_, SurfaceSlot::kOutline);
    panel_->SetID(static_cast<int>(ChromeViewId::kWelcome));
    // The overlay root paints the hairline and insets the card by its thickness.
    CefRefPtr<CefBoxLayout> outline_layout =
        panel_->SetToBoxLayout(VerticalLayout(0, Uniform(kHairlineDip)));

    button_delegate_ = new WelcomeButtonDelegate(this);

    card_ = MakePanel(SurfaceSlot::kCard);
    card_->SetToBoxLayout(VerticalLayout(
        kSectionGapDip, CefInsets(kCardPaddingVerticalDip, kCardPaddingHorizontalDip,
                                  kCardPaddingVerticalDip, kCardPaddingHorizontalDip)));
    panel_->AddChildView(card_);
    outline_layout->SetFlexForView(card_, 1);

    // Header: title and lede.
    CefRefPtr<CefPanel> header = MakePanel(SurfaceSlot::kCard);
    header->SetToBoxLayout(VerticalLayout(0));
    header->AddChildView(MakeText("Welcome to Island", TextRole::kTitle));
    header->AddChildView(
        MakeText("Choose how Island looks and import your bookmarks.", TextRole::kLede));
    card_->AddChildView(header);

    // Appearance: a three-segment control. The outline panel paints the hairline and
    // shows it through the 1 DIP gaps between equal-width segments.
    CefRefPtr<CefPanel> appearance = MakePanel(SurfaceSlot::kCard);
    appearance->SetToBoxLayout(VerticalLayout(kItemGapDip));
    appearance->AddChildView(MakeText("APPEARANCE", TextRole::kSectionLabel));
    CefRefPtr<CefPanel> segments = MakePanel(SurfaceSlot::kOutline);
    CefRefPtr<CefBoxLayout> segments_layout =
        segments->SetToBoxLayout(HorizontalLayout(kHairlineDip, Uniform(kHairlineDip)));
    const char* const theme_labels[3] = {"System", "Light", "Dark"};
    const int theme_ids[3] = {kThemeSystemId, kThemeLightId, kThemeDarkId};
    for (int index = 0; index < 3; ++index) {
        CefRefPtr<CefLabelButton> button =
            CefLabelButton::CreateLabelButton(button_delegate_, theme_labels[index]);
        button->SetID(theme_ids[index]);
        button->SetAccessibleName(std::string("Appearance: ") + theme_labels[index]);
        button->SetTooltipText(std::string("Appearance: ") + theme_labels[index]);
        button->SetFocusable(true);
        button->SetInkDropEnabled(true);
        button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
        button->SetMinimumSize(CefSize(150, kControlHeightDip));
        segments->AddChildView(button);
        segments_layout->SetFlexForView(button, 1);
        theme_buttons_[index] = button;
    }
    appearance->AddChildView(segments);
    card_->AddChildView(appearance);

    // Import: one outlined toggle row per READABLE browser; unreadable sources become a
    // wrapped, muted note (never ellipsized).
    CefRefPtr<CefPanel> import_section = MakePanel(SurfaceSlot::kCard);
    import_section->SetToBoxLayout(VerticalLayout(kItemGapDip));
    import_section->AddChildView(MakeText("IMPORT", TextRole::kSectionLabel));
    std::vector<std::string> unreadable;
    for (std::size_t index = 0; index < sources_.size(); ++index) {
        if (!sources_[index].available) {
            unreadable.push_back(sources_[index].display_name);
            continue;
        }
        SourceRow row;
        row.source_index = index;
        row.outline = MakePanel(SurfaceSlot::kOutline);
        CefRefPtr<CefBoxLayout> row_layout =
            row.outline->SetToBoxLayout(HorizontalLayout(0, Uniform(kHairlineDip)));
        row.button = CefLabelButton::CreateLabelButton(button_delegate_, "");
        row.button->SetID(kSourceIdBase + static_cast<int>(index));
        row.button->SetAccessibleName("Import bookmarks from " + sources_[index].display_name);
        row.button->SetTooltipText(sources_[index].bookmarks_path.generic_string());
        row.button->SetFocusable(true);
        row.button->SetInkDropEnabled(true);
        row.button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        row.button->SetMinimumSize(CefSize(0, kControlHeightDip));
        row.outline->AddChildView(row.button);
        row_layout->SetFlexForView(row.button, 1);
        import_section->AddChildView(row.outline);
        source_rows_.push_back(row);
    }
    readable_count_ = static_cast<int>(source_rows_.size());

    std::vector<std::string> note_lines = WrapUnreadableNote(unreadable, kNoteLineChars);
    if (readable_count_ == 0 && !sources_.empty()) {
        note_lines.insert(note_lines.begin(), "No readable bookmarks were found on this machine.");
    }
    note_line_count_ = static_cast<int>(note_lines.size());
    if (!note_lines.empty()) {
        CefRefPtr<CefPanel> notes = MakePanel(SurfaceSlot::kCard);
        notes->SetToBoxLayout(VerticalLayout(0));
        for (std::size_t line = 0; line < note_lines.size(); ++line) {
            CefRefPtr<CefLabelButton> note_label;
            CefRefPtr<CefPanel> note = MakeText(note_lines[line], TextRole::kNote, &note_label);
            if (line == 0) {
                // Screen readers get the whole note once, from its first line.
                std::string full;
                for (std::size_t i = 0; i < note_lines.size(); ++i) {
                    full += (i == 0 ? "" : " ") + note_lines[i];
                }
                note_label->SetAccessibleName(full);
            }
            notes->AddChildView(note);
        }
        import_section->AddChildView(notes);
    }
    card_->AddChildView(import_section);

    // Footer: secondary Import button, then the primary Start button, right-aligned.
    CefRefPtr<CefPanel> actions = MakePanel(SurfaceSlot::kCard);
    CefRefPtr<CefBoxLayout> actions_layout = actions->SetToBoxLayout(HorizontalLayout(kItemGapDip));
    CefRefPtr<CefPanel> spacer = MakePanel(SurfaceSlot::kCard);
    actions->AddChildView(spacer);
    actions_layout->SetFlexForView(spacer, 1);

    import_outline_ = MakePanel(SurfaceSlot::kOutline);
    import_outline_->SetToBoxLayout(HorizontalLayout(0, Uniform(kHairlineDip)));
    import_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "Import selected");
    import_button_->SetID(kImportButtonId);
    import_button_->SetAccessibleName("Import and start browsing");
    import_button_->SetFocusable(true);
    import_button_->SetInkDropEnabled(true);
    import_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    import_button_->SetMinimumSize(CefSize(128, kControlHeightDip));
    import_outline_->AddChildView(import_button_);
    actions->AddChildView(import_outline_);

    start_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "Start browsing");
    start_button_->SetID(kStartButtonId);
    start_button_->SetAccessibleName("Start browsing without importing");
    start_button_->SetFocusable(true);
    start_button_->SetInkDropEnabled(true);
    start_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    start_button_->SetMinimumSize(CefSize(128, kControlHeightDip + 2 * kHairlineDip));
    actions->AddChildView(start_button_);
    card_->AddChildView(actions);
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
        ApplyColors();
        if (host_ != nullptr) {
            host_->OnWelcomeThemeChanged(chosen_theme_);
        }
        return;
    }
    if (view_id >= kSourceIdBase && view_id < kSourceIdBase + static_cast<int>(sources_.size())) {
        const std::size_t index = static_cast<std::size_t>(view_id - kSourceIdBase);
        source_checked_[index] = !source_checked_[index];
        ApplyColors();
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
        if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
            overlay_.Release();
        }
        if (host_ != nullptr) {
            host_->OnWelcomeCompleted(checked);
        }
    }
}

void WelcomeFlow::HandleViewThemeReset() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || reapply_pending_) {
        return;
    }
    reapply_pending_ = true;
    CefPostTask(TID_UI, new ReapplyTask(alive_, [](WelcomeFlow* flow) { flow->ReapplyColors(); }));
}

void WelcomeFlow::ReapplyColors() {
    CEF_REQUIRE_UI_THREAD();
    reapply_pending_ = false;
    if (!detached_) {
        ApplyColors();
    }
}

void WelcomeFlow::UpdateImportButtonState() {
    if (import_outline_ == nullptr) {
        return;
    }
    // Importing with nothing selected would just start browsing, so the button only
    // shows while at least one readable source is checked.
    import_outline_->SetVisible(AnyCheckedAvailableSource());
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
    if (detached_ || window_ == nullptr) {
        return;
    }
    if (overlay_lifetime_.Show() == OverlayStep::kCreate) {
        CreateOverlay();
    }
    if (!overlay_.has_overlay()) {
        return;
    }
    visible_ = true;
    UpdateBounds();
    overlay_.get()->SetVisible(true);
    ApplyColors();
    UpdateImportButtonState();
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
    if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
        overlay_.Release();
    }
}

void WelcomeFlow::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !overlay_.has_overlay() || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    shadow_.SetCardSize(WidthDip(), ContentHeightDip(readable_count_, note_line_count_));
    overlay_.get()->SetBounds(CefRect(0, 0, window_size.width, window_size.height));
    scrim_panel_->Layout();
}

void WelcomeFlow::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    ApplyColors();
}

void WelcomeFlow::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    *alive_ = nullptr;
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    for (const CefRefPtr<WelcomeSurfaceDelegate>& delegate : surface_delegates_) {
        delegate->Detach();
    }
    static_cast<void>(overlay_lifetime_.Detach());
    overlay_.DestroyNow();
    scrim_delegate_ = nullptr;
    window_ = nullptr;
}

// Paints every welcome view from the tokens and the current selection state. Runs on
// construction, on every theme/selection change, and after CEF's theme reset.
void WelcomeFlow::ApplyColors() {
    CEF_REQUIRE_UI_THREAD();
    const ChromeTheme theme = ThemeOfTokens(tokens_);
    shadow_.Apply(theme);
    if (scrim_delegate_ != nullptr && scrim_panel_ != nullptr) {
        const ArgbColor scrim{.argb = theme == ChromeTheme::kDark ? kScrimDarkArgb
                                                                  : kScrimLightArgb};
        scrim_delegate_->SetColor(scrim);
        scrim_panel_->SetBackgroundColor(scrim.argb);
    }
    for (const std::pair<CefRefPtr<CefPanel>, SurfaceSlot>& entry : panels_) {
        entry.first->SetBackgroundColor(ResolveColor(entry.second).argb);
    }

    for (const StaticText& text : texts_) {
        switch (text.role) {
            case TextRole::kTitle:
                text.view->SetFontList("Geist, Bold 24px");
                text.view->SetEnabledTextColors(tokens_.text.argb);
                break;
            case TextRole::kLede:
                text.view->SetFontList("Geist, 14px");
                text.view->SetEnabledTextColors(tokens_.text_secondary.argb);
                break;
            case TextRole::kSectionLabel:
                // Mono uppercase labels, DESIGN.md §3.
                text.view->SetFontList("Geist Mono, Medium 11px");
                text.view->SetEnabledTextColors(tokens_.text_secondary.argb);
                break;
            case TextRole::kNote:
                text.view->SetFontList("Geist, 13px");
                text.view->SetEnabledTextColors(tokens_.text_secondary.argb);
                break;
        }
        text.view->SetBackgroundColor(tokens_.surface.argb);
    }

    for (int index = 0; index < 3; ++index) {
        if (theme_buttons_[index] == nullptr) {
            continue;
        }
        const ThemePreference choice = index == 0   ? ThemePreference::kSystem
                                       : index == 1 ? ThemePreference::kLight
                                                    : ThemePreference::kDark;
        const bool selected = chosen_theme_ == choice;
        const ButtonColors colors = SegmentColors(tokens_, selected);
        theme_buttons_[index]->SetBackgroundColor(colors.fill.argb);
        theme_buttons_[index]->SetEnabledTextColors(colors.text.argb);
        theme_buttons_[index]->SetFontList(selected ? "Geist, Bold 13px" : "Geist, 13px");
    }

    for (const SourceRow& row : source_rows_) {
        const bool checked = source_checked_[row.source_index];
        const ButtonColors colors = SegmentColors(tokens_, checked);
        // The check mark is text, so the state never rests on color alone.
        row.button->SetText(std::string(checked ? "\xE2\x9C\x93  " : "\xE2\x80\x83\xE2\x80\x83") +
                            sources_[row.source_index].display_name);
        row.button->SetBackgroundColor(colors.fill.argb);
        row.button->SetEnabledTextColors(colors.text.argb);
        row.button->SetFontList(checked ? "Geist, Bold 13px" : "Geist, 13px");
    }

    const ButtonColors secondary = SecondaryButtonColors(tokens_);
    import_button_->SetBackgroundColor(secondary.fill.argb);
    import_button_->SetEnabledTextColors(secondary.text.argb);
    import_button_->SetFontList("Geist, Medium 13px");
    const ButtonColors primary = PrimaryButtonColors(tokens_);
    start_button_->SetBackgroundColor(primary.fill.argb);
    start_button_->SetEnabledTextColors(primary.text.argb);
    start_button_->SetFontList("Geist, Medium 13px");
}

ArgbColor WelcomeFlow::ResolveColor(SurfaceSlot slot) const {
    switch (slot) {
        case SurfaceSlot::kOutline:
            return tokens_.border;
        case SurfaceSlot::kCard:
            return tokens_.surface;
    }
    return tokens_.surface;
}

}  // namespace island
