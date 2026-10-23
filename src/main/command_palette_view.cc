#include "command_palette_view.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "include/cef_image.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "row_raster.h"
#include "search_palette_model.h"

// allow: SIZE_OK - the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kTabKeyCode = 0x09;
constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;
constexpr int kArrowUpKeyCode = 0x26;
constexpr int kArrowDownKeyCode = 0x28;

// Metrics in DIP: 36 DIP rows, a 40 DIP query field, and an 8 DIP card padding.
constexpr int kRowHeightDip = 36;
constexpr int kGroupHeaderHeightDip = 20;
constexpr int kQueryHeightDip = 40;
constexpr int kCardPaddingDip = 8;
constexpr int kRowGapDip = 2;
// Space the divider takes between the query and the rows: its hairline centered in 9 DIP.
constexpr int kDividerHeightDip = 9;
constexpr int kNativeRowRadiusDip = 10;
constexpr int kRowCapWidthDip = kNativeRowRadiusDip;
constexpr int kRowIconCellDip = 24;
constexpr int kKeycapHeightDip = 20;
constexpr std::uint32_t kScrimArgb = 0x33000000U;
constexpr std::uint32_t kTransparentArgb = 0x00000000U;

int CommandRowHeight(const ChromeTokens&) { return kRowHeightDip; }

CefBoxLayoutSettings CommandPaletteLayout(const ChromeTokens&) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = kRowGapDip;
    settings.inside_border_insets =
        CefInsets(kCardPaddingDip, kCardPaddingDip, kCardPaddingDip, kCardPaddingDip);
    return settings;
}

CefBoxLayoutSettings CommandRowLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_1_dip;
    settings.inside_border_vertical_spacing = 0;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    return settings;
}

void SetAllStateImages(CefRefPtr<CefLabelButton> button, CefRefPtr<CefImage> image) {
    for (const cef_button_state_t state : {CEF_BUTTON_STATE_NORMAL, CEF_BUTTON_STATE_HOVERED,
                                           CEF_BUTTON_STATE_PRESSED, CEF_BUTTON_STATE_DISABLED}) {
        button->SetImage(state, image);
    }
}

ChromeIcon ChromeIconForPalette(PaletteRowIcon icon) {
    switch (icon) {
        case PaletteRowIcon::kGlobe:
            return ChromeIcon::kLocation;
        case PaletteRowIcon::kSpaces:
            return ChromeIcon::kGrid;
        case PaletteRowIcon::kBookmark:
            return ChromeIcon::kStar;
        case PaletteRowIcon::kHistory:
            return ChromeIcon::kNewspaper;
        case PaletteRowIcon::kCommand:
            return ChromeIcon::kSettings;
        case PaletteRowIcon::kReport:
            return ChromeIcon::kBookOpen;
    }
    return ChromeIcon::kLocation;
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
        case PaletteEntryKind::kHistory: {
            const std::string& title =
                entry.history.title.empty() ? entry.history.url : entry.history.title;
            return "Open history: " + title + ", URL " + entry.history.url;
        }
        case PaletteEntryKind::kCommand:
            return "Run command: " + entry.command.title;
        case PaletteEntryKind::kReport:
            return (entry.report.kind == "collection" ? "Open collected table: "
                                                      : "Open research report: ") +
                   entry.report.title;
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
        case PaletteEntryKind::kHistory:
            return (entry.history.title.empty() ? entry.history.url : entry.history.title) +
                   (entry.history.host.empty() ? "" : "  —  " + entry.history.host);
        case PaletteEntryKind::kCommand:
            return entry.command.title;
        case PaletteEntryKind::kReport:
            return entry.report.title;
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
        case PaletteEntryKind::kHistory:
            return entry.history.url;
        case PaletteEntryKind::kCommand:
            return "Run this command";
        case PaletteEntryKind::kReport:
            return entry.report.question.empty() ? "Open the report in a new tab"
                                                 : entry.report.question;
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
            const bool name_pressed = palette_->result_labels_[index] != nullptr &&
                                      palette_->result_labels_[index]->IsSame(button);
            const bool kind_pressed = index < palette_->result_kinds_.size() &&
                                      palette_->result_kinds_[index] != nullptr &&
                                      palette_->result_kinds_[index]->IsSame(button);
            const bool icon_pressed = index < palette_->result_icons_.size() &&
                                      palette_->result_icons_[index] != nullptr &&
                                      palette_->result_icons_[index]->IsSame(button);
            const bool shortcut_pressed = index < palette_->result_shortcuts_.size() &&
                                          palette_->result_shortcuts_[index] != nullptr &&
                                          palette_->result_shortcuts_[index]->IsSame(button);
            const bool left_pressed = index < palette_->result_left_caps_.size() &&
                                      palette_->result_left_caps_[index] != nullptr &&
                                      palette_->result_left_caps_[index]->IsSame(button);
            const bool right_pressed = index < palette_->result_right_caps_.size() &&
                                       palette_->result_right_caps_[index] != nullptr &&
                                       palette_->result_right_caps_[index]->IsSame(button);
            if (name_pressed || kind_pressed || icon_pressed || shortcut_pressed || left_pressed ||
                right_pressed) {
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
                                       ChromeTokens tokens, PaletteUrlValidator validator,
                                       std::filesystem::path icon_resource_root)
    : host_(&host),
      window_(std::move(window)),
      tokens_(tokens),
      icon_catalog_(std::move(icon_resource_root)),
      model_(std::move(validator)) {
    CEF_REQUIRE_UI_THREAD();
    BuildViews();
    ApplySurfaceColors();
}

void CommandPaletteView::CreateOverlay() {
    // can_activate is true so the text field can take keyboard focus. The overlay only exists
    // while shown (see overlay_lifetime.h): a live overlay widget blocks mouse input on macOS.
    overlay_.Adopt(
        window_->AddOverlayView(scrim_panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true));
    if (overlay_.has_overlay()) {
        overlay_.get()->SetVisible(false);
    }
}

CommandPaletteView::~CommandPaletteView() { Detach(); }

void CommandPaletteView::BuildViews() {
    panel_delegate_ = new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kPanel);
    surface_delegates_.push_back(panel_delegate_);
    panel_ = CefPanel::CreatePanel(panel_delegate_);
    panel_->SetID(static_cast<int>(ChromeViewId::kCommandPalette));
    panel_->SetToBoxLayout(CommandPaletteLayout(tokens_));

    textfield_delegate_ =
        new QueryTextfieldDelegate(this, CefSize(SearchPaletteWidthDip(), kQueryHeightDip));
    query_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    query_field_->SetID(static_cast<int>(ChromeViewId::kCommandPaletteQuery));
    query_field_->SetAccessibleName("Command palette");
    query_field_->SetPlaceholderText("Search tabs, bookmarks, history, or type a URL");
    query_field_->SetFocusable(true);
    panel_->AddChildView(query_field_);

    // The hairline between the query and the rows, centered in a 9 DIP band.
    {
        CefRefPtr<PaletteSurfaceDelegate> band_delegate = new PaletteSurfaceDelegate(
            this, PaletteSurfaceSlot::kRow, CefSize(SearchPaletteWidthDip(), kDividerHeightDip));
        surface_delegates_.push_back(band_delegate);
        divider_ = CefPanel::CreatePanel(band_delegate);
        CefBoxLayoutSettings band_settings;
        band_settings.horizontal = static_cast<int>(false);
        band_settings.between_child_spacing = 0;
        const int inset = (kDividerHeightDip - 1) / 2;
        band_settings.inside_border_insets = CefInsets(inset, 0, inset, 0);
        divider_->SetToBoxLayout(band_settings);
        CefRefPtr<PaletteSurfaceDelegate> line_delegate = new PaletteSurfaceDelegate(
            this, PaletteSurfaceSlot::kBorder, CefSize(SearchPaletteWidthDip(), 1));
        surface_delegates_.push_back(line_delegate);
        divider_->AddChildView(CefPanel::CreatePanel(line_delegate));
        panel_->AddChildView(divider_);
    }
    overlay_root_ = card_.Wrap(panel_, tokens_);

    // One full-window overlay owns both the Native 20% scrim and the card. Keeping them in the
    // same lifetime is required on macOS, where any live overlay swallows page mouse input.
    const ArgbColor scrim{.argb =
                              tokens_.look == LookPreset::kNative ? kScrimArgb : kTransparentArgb};
    scrim_delegate_ = new OverlayFillDelegate(scrim);
    scrim_panel_ = CefPanel::CreatePanel(scrim_delegate_);
    CefBoxLayoutSettings scrim_settings;
    scrim_settings.horizontal = static_cast<int>(false);
    scrim_settings.between_child_spacing = 0;
    CefRefPtr<CefBoxLayout> scrim_layout = scrim_panel_->SetToBoxLayout(scrim_settings);

    top_spacer_delegate_ = new OverlayFillDelegate(
        scrim, CefSize(1, std::max(0, SearchPaletteTopOffsetDip() - OverlayShadow::kTopDip)));
    scrim_panel_->AddChildView(CefPanel::CreatePanel(top_spacer_delegate_));

    card_row_ = CefPanel::CreatePanel(scrim_delegate_);
    CefBoxLayoutSettings row_settings;
    row_settings.horizontal = static_cast<int>(true);
    row_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    row_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_START;
    card_row_->SetToBoxLayout(row_settings);
    card_row_->AddChildView(overlay_root_);
    scrim_panel_->AddChildView(card_row_);

    CefRefPtr<CefPanel> bottom = CefPanel::CreatePanel(scrim_delegate_);
    scrim_panel_->AddChildView(bottom);
    scrim_layout->SetFlexForView(bottom, 1);

    // Every result row is a CefLabelButton, and CEF refuses to create one
    // without a delegate (CreateLabelButton returns null), so this must exist
    // before the first RebuildResultRows.
    button_delegate_ = new RowButtonDelegate(this);
}

void CommandPaletteView::RebuildResultRows(std::size_t row_count) {
    for (const CefRefPtr<PaletteSurfaceDelegate>& delegate : row_delegates_) {
        delegate->Detach();
    }
    // Index 0 is the card, 1 and 2 are the divider band and its line: only row delegates go.
    surface_delegates_.erase(surface_delegates_.begin() + 3, surface_delegates_.end());
    row_delegates_.clear();
    body_delegates_.clear();
    result_rows_.clear();
    result_bodies_.clear();
    result_headers_.clear();
    result_header_labels_.clear();
    result_left_caps_.clear();
    result_right_caps_.clear();
    result_icons_.clear();
    result_labels_.clear();
    result_kinds_.clear();
    result_shortcuts_.clear();
    panel_->RemoveAllChildViews();
    panel_->AddChildView(query_field_);
    panel_->AddChildView(divider_);

    const int row_height = CommandRowHeight(tokens_);
    result_rows_.reserve(row_count);
    result_bodies_.reserve(row_count);
    result_headers_.reserve(row_count);
    result_header_labels_.reserve(row_count);
    result_left_caps_.reserve(row_count);
    result_right_caps_.reserve(row_count);
    result_icons_.reserve(row_count);
    row_delegates_.reserve(row_count);
    body_delegates_.reserve(row_count);
    result_labels_.reserve(row_count);
    result_kinds_.reserve(row_count);
    result_shortcuts_.reserve(row_count);
    for (std::size_t index = 0; index < row_count; ++index) {
        CefRefPtr<PaletteSurfaceDelegate> header_delegate =
            new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kRow,
                                       CefSize(SearchPaletteWidthDip(), kGroupHeaderHeightDip));
        surface_delegates_.push_back(header_delegate);
        CefRefPtr<CefPanel> header = CefPanel::CreatePanel(header_delegate);
        CefRefPtr<CefBoxLayout> header_layout = header->SetToBoxLayout(CommandRowLayout(tokens_));
        CefRefPtr<CefLabelButton> header_label =
            CefLabelButton::CreateLabelButton(button_delegate_, "");
        header_label->SetFocusable(false);
        header_label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        header->AddChildView(header_label);
        header_layout->SetFlexForView(header_label, 1);
        panel_->AddChildView(header);

        CefRefPtr<PaletteSurfaceDelegate> row_delegate = new PaletteSurfaceDelegate(
            this, PaletteSurfaceSlot::kRow, CefSize(SearchPaletteWidthDip(), row_height));
        surface_delegates_.push_back(row_delegate);
        row_delegates_.push_back(row_delegate);
        CefRefPtr<CefPanel> row = CefPanel::CreatePanel(row_delegate);
        row->SetID(static_cast<int>(ChromeViewId::kCommandPaletteResult));
        CefBoxLayoutSettings cap_layout_settings;
        cap_layout_settings.horizontal = static_cast<int>(true);
        cap_layout_settings.between_child_spacing = 0;
        cap_layout_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
        CefRefPtr<CefBoxLayout> row_layout = row->SetToBoxLayout(cap_layout_settings);

        CefRefPtr<CefLabelButton> left_cap =
            CefLabelButton::CreateLabelButton(button_delegate_, "");
        left_cap->SetFocusable(false);
        left_cap->SetMinimumSize(CefSize(kRowCapWidthDip, row_height));
        row->AddChildView(left_cap);

        CefRefPtr<PaletteSurfaceDelegate> body_delegate =
            new PaletteSurfaceDelegate(this, PaletteSurfaceSlot::kRow);
        surface_delegates_.push_back(body_delegate);
        body_delegates_.push_back(body_delegate);
        CefRefPtr<CefPanel> body = CefPanel::CreatePanel(body_delegate);
        CefRefPtr<CefBoxLayout> body_layout = body->SetToBoxLayout(CommandRowLayout(tokens_));

        CefRefPtr<CefLabelButton> icon = CefLabelButton::CreateLabelButton(button_delegate_, "");
        icon->SetFocusable(false);
        icon->SetMinimumSize(CefSize(kRowIconCellDip, row_height));
        body->AddChildView(icon);

        CefRefPtr<CefLabelButton> label = CefLabelButton::CreateLabelButton(button_delegate_, "");
        label->SetID(static_cast<int>(ChromeViewId::kCommandPaletteResultName));
        label->SetFocusable(false);
        label->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        body->AddChildView(label);
        body_layout->SetFlexForView(label, 1);

        CefRefPtr<CefLabelButton> kind = CefLabelButton::CreateLabelButton(button_delegate_, "");
        kind->SetFocusable(false);
        kind->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_RIGHT);
        body->AddChildView(kind);

        CefRefPtr<CefLabelButton> shortcut =
            CefLabelButton::CreateLabelButton(button_delegate_, "");
        shortcut->SetFocusable(false);
        shortcut->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
        shortcut->SetMinimumSize(CefSize(0, kKeycapHeightDip));
        body->AddChildView(shortcut);

        row->AddChildView(body);
        row_layout->SetFlexForView(body, 1);

        CefRefPtr<CefLabelButton> right_cap =
            CefLabelButton::CreateLabelButton(button_delegate_, "");
        right_cap->SetFocusable(false);
        right_cap->SetMinimumSize(CefSize(kRowCapWidthDip, row_height));
        row->AddChildView(right_cap);

        panel_->AddChildView(row);
        result_headers_.push_back(header);
        result_header_labels_.push_back(header_label);
        result_rows_.push_back(row);
        result_bodies_.push_back(body);
        result_left_caps_.push_back(left_cap);
        result_right_caps_.push_back(right_cap);
        result_icons_.push_back(icon);
        result_labels_.push_back(label);
        result_kinds_.push_back(kind);
        result_shortcuts_.push_back(shortcut);
    }
}

std::size_t CommandPaletteView::VisibleRowCount() const {
    std::size_t visible = 0;
    for (const CefRefPtr<CefPanel>& row : result_rows_) {
        if (row->IsVisible()) {
            ++visible;
        }
    }
    return visible;
}

// The card: padding, the query field, then (when there are rows) the divider band and the
// rows. Hidden pool rows take no space.
int CommandPaletteView::PreferredContentHeightDip() const {
    const auto rows = static_cast<int>(VisibleRowCount());
    const int headers = static_cast<int>(
        std::count_if(result_headers_.begin(), result_headers_.end(),
                      [](const CefRefPtr<CefPanel>& header) { return header->IsVisible(); }));
    int height = 2 * kCardPaddingDip + kQueryHeightDip;
    if (rows > 0) {
        height += kDividerHeightDip + rows * kRowHeightDip + headers * kGroupHeaderHeightDip +
                  (1 + rows + headers) * kRowGapDip;
    }
    return height;
}

void CommandPaletteView::Show(std::vector<PaletteTabEntry> tabs,
                              std::vector<PaletteSpaceEntry> spaces,
                              std::vector<PaletteBookmarkEntry> bookmarks,
                              std::vector<PaletteCommandEntry> commands,
                              std::vector<PaletteReportEntry> reports,
                              std::vector<PaletteTabVisual> tab_visuals) {
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
    constexpr std::size_t kHistoryRowLimit = 8;
    const std::size_t pool_size = tabs.size() + spaces.size() + commands.size() + reports.size() +
                                  bookmarks.size() + kHistoryRowLimit + 1;
    model_.SetTabs(std::move(tabs));
    tab_visuals_ = std::move(tab_visuals);
    model_.SetSpaces(std::move(spaces));
    model_.SetBookmarks(std::move(bookmarks));
    model_.SetCommands(std::move(commands));
    model_.SetReports(std::move(reports));
    // Worst case: every tab and every space matches plus the go-to-URL row
    // that a non-empty query appends.
    RebuildResultRows(pool_size);
    model_.Open();
    query_field_->SetText("");
    UpdateBounds();
    overlay_.get()->SetVisible(true);
    ProjectResults();
    query_field_->RequestFocus();
}

void CommandPaletteView::SetHistoryResults(std::string_view query,
                                           std::vector<PaletteHistoryEntry> history) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !model_.visible() || model_.query() != query) {
        return;
    }
    model_.SetHistory(std::move(history));
    ProjectResults();
}

void CommandPaletteView::Hide() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    model_.Close();
    if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
        overlay_.Release();
    }
}

void CommandPaletteView::UpdateBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !overlay_.has_overlay() || window_ == nullptr) {
        return;
    }
    const CefSize window_size = window_->GetSize();
    // The card shares the search palette's width and top offset. Its one overlay now spans the
    // window because the Native look also paints a 20% scrim behind the card.
    const DipRect card = SearchPaletteBounds(
        {.x = 0, .y = 0, .width = window_size.width, .height = window_size.height},
        PreferredContentHeightDip());
    card_.SetContentSize(card.width, card.height);
    overlay_.get()->SetBounds(CefRect(0, 0, window_size.width, window_size.height));
    scrim_panel_->Layout();
}

void CommandPaletteView::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    card_.Apply(tokens_);
    const ArgbColor scrim{.argb =
                              tokens_.look == LookPreset::kNative ? kScrimArgb : kTransparentArgb};
    scrim_delegate_->SetColor(scrim);
    top_spacer_delegate_->SetColor(scrim);
    scrim_panel_->SetBackgroundColor(scrim.argb);
    card_row_->SetBackgroundColor(scrim.argb);
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
    static_cast<void>(overlay_lifetime_.Detach());
    overlay_.DestroyNow();
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
    model_.SetHistory({});
    ProjectResults();
    if (host_ != nullptr) {
        host_->OnCommandPaletteQueryChanged(model_.query());
    }
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
    if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
        overlay_.Release();
    }
    if (host_ != nullptr) {
        host_->OnCommandPaletteSubmitted(*selection, query);
    }
}

void CommandPaletteView::ProjectResults() {
    // Rows are rebuilt on every Show(), so their text colors and fonts are applied here too.
    ApplySurfaceColors();
    const std::vector<PaletteEntry> results = model_.Results();
    // Results arrive grouped by kind, so a header starts wherever the group title changes.
    std::string_view previous_group;
    for (std::size_t index = 0; index < result_rows_.size(); ++index) {
        if (index >= results.size()) {
            result_headers_[index]->SetVisible(false);
            result_rows_[index]->SetVisible(false);
            continue;
        }
        const bool native = tokens_.look == LookPreset::kNative;
        const PaletteKindStyle& style = PaletteKindStyleFor(results[index].kind);
        const bool starts_group = native && style.group_header != previous_group;
        result_headers_[index]->SetVisible(starts_group);
        result_header_labels_[index]->SetText(std::string(style.group_header));
        previous_group = style.group_header;
        result_rows_[index]->SetVisible(true);
        const bool highlighted = index == model_.highlighted_index();
        const PaletteSurfaceSlot slot =
            highlighted ? PaletteSurfaceSlot::kHighlightedRow : PaletteSurfaceSlot::kRow;
        const ArgbColor fill = ResolvePaletteColor(slot);
        result_rows_[index]->SetBackgroundColor(tokens_.surface.argb);
        result_bodies_[index]->SetBackgroundColor(fill.argb);
        // The body delegate remembers the slot so the theme-change re-assertion restores the
        // highlight rather than flattening every row. The outer row remains the corner color.
        body_delegates_[index]->SetSlot(slot);
        const int radius = native ? kNativeRowRadiusDip : 0;
        SetAllStateImages(result_left_caps_[index],
                          RowCap(fill, /*left=*/true, kRowHeightDip, radius));
        SetAllStateImages(result_right_caps_[index],
                          RowCap(fill, /*left=*/false, kRowHeightDip, radius));
        result_left_caps_[index]->SetBackgroundColor(tokens_.surface.argb);
        result_right_caps_[index]->SetBackgroundColor(tokens_.surface.argb);

        result_icons_[index]->SetVisible(native);
        if (native) {
            SetAllStateImages(result_icons_[index], RowIcon(results[index]));
        }
        result_icons_[index]->SetBackgroundColor(fill.argb);
        result_labels_[index]->SetText(PaletteRowDisplayText(results[index]));
        result_labels_[index]->SetBackgroundColor(fill.argb);
        result_labels_[index]->SetEnabledTextColors(tokens_.text.argb);
        result_labels_[index]->SetFontList(BrowserChrome::FontListForTokens(
            tokens_, (tokens_.look == LookPreset::kNative ? 13 : 14),
            highlighted ? ChromeFontWeight::kMedium : ChromeFontWeight::kRegular));
        result_labels_[index]->SetTooltipText(PaletteRowTooltipText(results[index]));
        result_labels_[index]->SetAccessibleName(CommandResultAccessibleName(results[index]));
        result_kinds_[index]->SetText(PaletteKindLabel(results[index], native));
        result_kinds_[index]->SetBackgroundColor(fill.argb);
        result_kinds_[index]->SetEnabledTextColors(tokens_.text_secondary.argb);
        result_kinds_[index]->SetAccessibleName(CommandResultAccessibleName(results[index]));
        const std::string shortcut = results[index].kind == PaletteEntryKind::kCommand
                                         ? results[index].command.shortcut
                                         : std::string();
        const BrowserChrome::KeycapColors keycap = BrowserChrome::NativeKeycapColors(tokens_);
        result_shortcuts_[index]->SetVisible(native && !shortcut.empty());
        result_shortcuts_[index]->SetText(shortcut);
        result_shortcuts_[index]->SetBackgroundColor(keycap.fill.argb);
        result_shortcuts_[index]->SetEnabledTextColors(keycap.text.argb);
        result_shortcuts_[index]->SetAccessibleName(shortcut.empty() ? "" : "Shortcut " + shortcut);
    }
    if (divider_ != nullptr) {
        divider_->SetVisible(!results.empty());
    }
    // The card is exactly as tall as its visible rows.
    UpdateBounds();
}

void CommandPaletteView::ApplySurfaceColors() {
    panel_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kPanel).argb);
    divider_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kRow).argb);
    divider_->GetChildViewAt(0)->SetBackgroundColor(
        ResolvePaletteColor(PaletteSurfaceSlot::kBorder).argb);
    query_field_->SetBackgroundColor(ResolvePaletteColor(PaletteSurfaceSlot::kQueryWell).argb);
    query_field_->SetFontList(
        BrowserChrome::FontListForTokens(tokens_, 15, ChromeFontWeight::kRegular));
    for (const CefRefPtr<CefLabelButton>& header : result_header_labels_) {
        header->SetBackgroundColor(tokens_.surface.argb);
        header->SetEnabledTextColors(tokens_.text_secondary.argb);
        header->SetFontList(
            BrowserChrome::FontListForTokens(tokens_, 11, ChromeFontWeight::kSemiBold));
    }
    for (const CefRefPtr<CefLabelButton>& label : result_labels_) {
        label->SetEnabledTextColors(tokens_.text.argb);
        label->SetFontList(BrowserChrome::FontListForTokens(
            tokens_, (tokens_.look == LookPreset::kNative ? 13 : 14), ChromeFontWeight::kRegular));
    }
    for (const CefRefPtr<CefLabelButton>& kind : result_kinds_) {
        kind->SetEnabledTextColors(tokens_.text_secondary.argb);
        kind->SetFontList(
            tokens_.look == LookPreset::kNative
                ? BrowserChrome::FontListForTokens(tokens_, 11, ChromeFontWeight::kSemiBold)
                : "Geist Mono, Medium 11px");
    }
    for (const CefRefPtr<CefLabelButton>& shortcut : result_shortcuts_) {
        shortcut->SetFontList(
            BrowserChrome::FontListForTokens(tokens_, 11, ChromeFontWeight::kMedium));
    }
}

CefRefPtr<CefImage> CommandPaletteView::RowIcon(const PaletteEntry& entry) const {
    if (entry.kind == PaletteEntryKind::kTab) {
        const auto visual = std::find_if(tab_visuals_.begin(), tab_visuals_.end(),
                                         [&](const auto& v) { return v.id == entry.tab.id; });
        if (visual != tab_visuals_.end() && visual->favicon != nullptr) {
            return visual->favicon;
        }
    }
    const ChromeIcon icon = ChromeIconForPalette(PaletteKindStyleFor(entry.kind).icon);
    return icon_catalog_.LoadTinted(icon, tokens_.text_secondary, ChromeIconSize::k16)
        .value_or(nullptr);
}

CefRefPtr<CefImage> CommandPaletteView::RowCap(ArgbColor fill, bool left, int height,
                                               int radius) const {
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    for (const int scale : {1, 2}) {
        RasterBitmap bitmap = NewRasterBitmap(kRowCapWidthDip, height, scale);
        const CornerRadii radii =
            left ? CornerRadii{radius, 0, 0, radius} : CornerRadii{0, radius, radius, 0};
        FillRoundedRect(bitmap, left ? 0 : -kRowCapWidthDip, 0, 2 * kRowCapWidthDip, height, radii,
                        fill);
        image->AddBitmap(static_cast<float>(scale), bitmap.width_px, bitmap.height_px,
                         CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, bitmap.bgra.data(),
                         bitmap.bgra.size());
    }
    return image;
}

ArgbColor CommandPaletteView::ResolvePaletteColor(PaletteSurfaceSlot slot) const {
    if (tokens_.look == LookPreset::kNative && slot == PaletteSurfaceSlot::kHighlightedRow) {
        // Native's highlight is the neutral raised plate, not the blue selection: blue stays for
        // the active tab and focus rings.
        return tokens_.segment_selected;
    }
    return PaletteSurfaceRoleForTokens(slot, tokens_);
}

}  // namespace island
