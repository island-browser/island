#include "command_palette_view.h"

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
#include "search_palette_model.h"

// allow: SIZE_OK - the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kTabKeyCode = 0x09;
constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;
constexpr int kArrowUpKeyCode = 0x26;
constexpr int kArrowDownKeyCode = 0x28;

int CommandRowHeight(const ChromeTokens& tokens) {
    return tokens.spacing_6_dip + tokens.spacing_4_dip;
}

CefBoxLayoutSettings CommandPaletteLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_insets = CefInsets(tokens.spacing_3_dip, tokens.spacing_3_dip,
                                              tokens.spacing_3_dip, tokens.spacing_3_dip);
    return settings;
}

CefBoxLayoutSettings CommandRowLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_2_dip;
    settings.inside_border_vertical_spacing = tokens.spacing_1_dip;
    return settings;
}

// The accessible name for one result row. Every row states the action it
// performs, matching the palette accessibility bar of the search palette.
std::string CommandResultAccessibleName(const PaletteEntry& entry) {
    switch (entry.kind) {
        case PaletteEntryKind::kTab:
            return "Switch to tab: " + entry.tab.title + ", URL " + entry.tab.url;
        case PaletteEntryKind::kSpace:
            return "Switch to space: " + entry.space.name;
        case PaletteEntryKind::kUrl:
            return "Go to URL: " + entry.url_query;
        case PaletteEntryKind::kBookmark: {
            const std::string& title =
                entry.bookmark.title.empty() ? entry.bookmark.url : entry.bookmark.title;
            return "Open bookmark: " + title + ", URL " + entry.bookmark.url;
        }
    }
    return "Palette result";
}

// The visible row text. A tab row leads with its page title, a space row with
// its name, and the go-to-URL row echoes the typed query with its action
// prefix.
std::string PaletteRowDisplayText(const PaletteEntry& entry) {
    switch (entry.kind) {
        case PaletteEntryKind::kTab:
            return entry.tab.title;
        case PaletteEntryKind::kSpace:
            return entry.space.name;
        case PaletteEntryKind::kUrl:
            return "Go to " + entry.url_query;
        case PaletteEntryKind::kBookmark:
            // An untitled bookmark falls back to its URL so the row is never
            // blank.
            return entry.bookmark.title.empty() ? entry.bookmark.url : entry.bookmark.title;
    }
    return "";
}

std::string PaletteRowTooltipText(const PaletteEntry& entry) {
    switch (entry.kind) {
        case PaletteEntryKind::kTab:
            return entry.tab.url;
        case PaletteEntryKind::kSpace:
            return "Switch space";
        case PaletteEntryKind::kUrl:
            return "Open the typed URL in the active tab";
        case PaletteEntryKind::kBookmark:
            return entry.bookmark.url;
    }
    return "";
}

}  // namespace

// Shares SearchPalette's surface delegate: CefView drops any custom background
// color when the window's ThemeChanged() cascade reaches it, so every palette
// surface re-asserts its token color from OnThemeChanged.
class CommandPaletteView::PaletteSurfaceDelegate final : public CefPanelDelegate {
  public:
    PaletteSurfaceDelegate(CommandPaletteView* palette, PaletteSurfaceSlot slot,
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
    CommandPaletteView* palette_;
    PaletteSurfaceSlot slot_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(PaletteSurfaceDelegate);
};

class CommandPaletteView::RowButtonDelegate final : public CefButtonDelegate {
  public:
    explicit RowButtonDelegate(CommandPaletteView* palette) : palette_(palette) {}

    void Detach() { palette_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (palette_ == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < palette_->result_labels_.size(); ++index) {
            if (palette_->result_labels_[index] != nullptr &&
                palette_->result_labels_[index]->IsSame(button)) {
                palette_->HandleRowPressed(index);
                return;
            }
        }
    }

  private:
    CommandPaletteView* palette_;

    IMPLEMENT_REFCOUNTING(RowButtonDelegate);
};

class CommandPaletteView::QueryTextfieldDelegate final : public CefTextfieldDelegate {
  public:
    QueryTextfieldDelegate(CommandPaletteView* palette, CefSize preferred_size)
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
    CommandPaletteView* palette_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(QueryTextfieldDelegate);
};

CommandPaletteView::CommandPaletteView(CommandPaletteHost& host, CefRefPtr<CefWindow> window,
                                       ChromeTokens tokens, PaletteUrlValidator validator)
    : host_(&host), window_(std::move(window)), tokens_(tokens), model_(std::move(validator)) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    // can_activate is true so the query field can take keyboard focus; the
    // overlay starts hidden and is only ever shown and hidden from here on.
    overlay_ = window_->AddOverlayView(panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true);
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    ApplySurfaceColors();
}

CommandPaletteView::~CommandPaletteView() { Detach(); }

void CommandPaletteView::BuildViews() {
    panel_delegate_ = new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kPanel);
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kCommandPalette));
    panel_->SetToBoxLayout(CommandPaletteLayout(tokens_));

    textfield_delegate_ = new QueryTextfieldDelegate(
        this, CefSize(SearchPaletteWidthDip(), CommandRowHeight(tokens_)));
    query_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    query_field_->SetID(static_cast<int>(ChromeViewId::kCommandPaletteQuery));
    query_field_->SetAccessibleName("Command palette");
    query_field_->SetPlaceholderText("Switch to tab, space, or type a URL");
    query_field_->SetFocusable(true);
    panel_->AddChildView(query_field_);
}

void CommandPaletteView::RebuildResultRows(std::size_t row_count) {
    for (const CefRefPtr<PaletteSurfaceDelegate>& delegate : row_delegates_) {
        delegate->Detach();
    }
    surface_delegates_.erase(surface_delegates_.begin() + 1, surface_delegates_.end());
    row_delegates_.clear();
    result_rows_.clear();
    result_labels_.clear();
    panel_->RemoveAllChildViews();
    panel_->AddChildView(query_field_);

    const int row_height = CommandRowHeight(tokens_);
    result_rows_.reserve(row_count);
    row_delegates_.reserve(row_count);
    result_labels_.reserve(row_count);
    for (std::size_t index = 0; index < row_count; ++index) {
        CefRefPtr<PaletteSurfaceDelegate> row_delegate =
            new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kRow, CefSize(0, row_height));
        surface_delegates_.push_back(row_delegate);
        row_delegates_.push_back(row_delegate);
        CefRefPtr<CefPanel> row = CefPanel::CreatePanel(row_delegate);
        row->SetID(static_cast<int>(ChromeViewId::kCommandPaletteResult));
        CefRefPtr<CefBoxLayout> row_layout = row->SetToBoxLayout(CommandRowLayout(tokens_));

        CefRefPtr<CefLabelButton> label = CefLabelButton::CreateLabelButton(button_delegate_, "");
        label->SetID(static_cast<int>(ChromeViewId::kCommandPaletteResultName));
        label->SetFocusable(false);
        label->SetMinimumSize(CefSize(0, row_height));
        row->AddChildView(label);
        row_layout->SetFlexForView(label, 1);

        panel_->AddChildView(row);
        result_rows_.push_back(row);
        result_labels_.push_back(label);
    }
}

int CommandPaletteView::PreferredContentHeightDip() const {
    const int row_height = CommandRowHeight(tokens_);
    const std::size_t row_count = result_rows_.size();
    const auto rows = static_cast<int>(row_count);
    const int spacing = rows > 0 ? tokens_.spacing_2_dip * (rows - 1) : 0;
    return row_height * rows + spacing + tokens_.spacing_3_dip * 2;
}

void CommandPaletteView::Show(std::vector<PaletteTabEntry> tabs,
                              std::vector<PaletteSpaceEntry> spaces,
                              std::vector<PaletteBookmarkEntry> bookmarks) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr) {
        return;
    }
    const std::size_t pool_size = tabs.size() + spaces.size() + 1;
    model_.SetTabs(std::move(tabs));
    model_.SetSpaces(std::move(spaces));
    model_.SetBookmarks(std::move(bookmarks));
    // Worst case: every tab and every space matches plus the go-to-URL row
    // that a non-empty query appends.
    RebuildResultRows(pool_size);
    model_.Open();
    query_field_->SetText("");
    UpdateBounds();
    overlay_->SetVisible(true);
    ProjectResults();
    query_field_->RequestFocus();
}

void CommandPaletteView::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.Close();
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
}

void CommandPaletteView::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || overlay_ == nullptr || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    // The command palette shares the search palette's centered card geometry:
    // same width and top offset, clamped to the window.
    const DipRect bounds = SearchPaletteBounds(
        {.x = 0, .y = 0, .width = window_size.width, .height = window_size.height},
        PreferredContentHeightDip());
    overlay_->SetBounds(CefRect(bounds.x, bounds.y, bounds.width, bounds.height));
}

void CommandPaletteView::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    ApplySurfaceColors();
    ProjectResults();
}

void CommandPaletteView::Detach() {
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

bool CommandPaletteView::HandleQueryKeyEvent(CefRefPtr<CefTextfield> textfield,
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
                host_->OnCommandPaletteDismissed();
            }
            return true;
        case kArrowUpKeyCode:
            model_.MoveHighlight(-1);
            ProjectResults();
            return true;
        case kArrowDownKeyCode:
            model_.MoveHighlight(1);
            ProjectResults();
            return true;
        case kTabKeyCode:
            // Tab never leaves the palette: it advances the highlighted row and
            // keeps keyboard focus on the query field, which is the palette's
            // single focus anchor.
            model_.MoveHighlight(event.modifiers & EVENTFLAG_SHIFT_DOWN ? -1 : 1);
            ProjectResults();
            textfield->RequestFocus();
            return true;
        default:
            return false;
    }
}

void CommandPaletteView::HandleQueryChanged(CefRefPtr<CefTextfield> textfield) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.SetQuery(textfield->GetText().ToString());
    ProjectResults();
}

void CommandPaletteView::HandleRowPressed(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.SetQuery(query_field_->GetText().ToString());
    model_.SetHighlightedIndex(index);
    ProjectResults();
    SubmitHighlighted();
}

void CommandPaletteView::SubmitHighlighted() {
    CEF_REQUIRE_UI_THREAD();
    const std::string query = model_.query();
    const std::optional<PaletteSelection> selection = model_.Submit();
    if (!selection.has_value()) {
        // An invalid go-to-URL query: the rejection is surfaced by the model,
        // the palette stays open, and nothing is consumed.
        return;
    }
    if (overlay_ != nullptr) {
        overlay_->SetVisible(false);
    }
    if (host_ != nullptr) {
        host_->OnCommandPaletteSubmitted(*selection, query);
    }
}

void CommandPaletteView::ProjectResults() {
    const std::vector<PaletteEntry> results = model_.Results();
    for (std::size_t index = 0; index < result_rows_.size(); ++index) {
        if (index >= results.size()) {
            result_rows_[index]->SetVisible(false);
            continue;
        }
        result_rows_[index]->SetVisible(true);
        const bool highlighted = index == model_.highlighted_index();
        const PaletteSurfaceSlot slot =
            highlighted ? PaletteSurfaceSlot::kHighlightedRow : PaletteSurfaceSlot::kRow;
        result_rows_[index]->SetBackgroundColor(ResolvePaletteColor(slot).argb);
        // The row delegate remembers the slot so the theme-change re-assertion
        // restores the highlight rather than flattening every row.
        row_delegates_[index]->SetSlot(slot);
        result_labels_[index]->SetText(PaletteRowDisplayText(results[index]));
        result_labels_[index]->SetTooltipText(PaletteRowTooltipText(results[index]));
        result_labels_[index]->SetAccessibleName(CommandResultAccessibleName(results[index]));
    }
}

void CommandPaletteView::ApplySurfaceColors() {
    panel_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kPanel).argb);
    query_field_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kQueryWell).argb);
    query_field_->SetFontList("Geist Mono, 14px");
    for (const CefRefPtr<CefLabelButton>& label : result_labels_) {
        label->SetEnabledTextColors(tokens_.text.argb);
        label->SetFontList("Geist, 14px");
    }
}

ArgbColor CommandPaletteView::ResolvePaletteColor(PaletteSurfaceSlot slot) const {
    return PaletteSurfaceRoleForTokens(slot, tokens_);
}

}  // namespace island
