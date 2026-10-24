#ifndef ISLAND_COMMAND_PALETTE_VIEW_H_
#define ISLAND_COMMAND_PALETTE_VIEW_H_

#include <filesystem>
#include <string_view>
#include <vector>

#include "browser_chrome.h"
#include "command_palette.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "overlay_card.h"
#include "overlay_lifetime.h"
#include "search_palette_model.h"

class CefLabelButton;
class CefImage;
class CefPanel;
class CefTextfield;
class CefWindow;

namespace island {

struct PaletteTabVisual {
    TabId id;
    CefRefPtr<CefImage> favicon;
};

// The window-side seam the command palette reports through. BrowserWindow
// implements it: a submission activates the selected tab/space or navigates the
// validated URL, a dismissal restores focus to the invocation point. The view
// never touches a CefBrowser. For kUrl selections the raw query is passed
// alongside the validated address because PaletteSelection carries only the
// validation result.
class CommandPaletteHost {
  public:
    virtual ~CommandPaletteHost() = default;

    virtual void OnCommandPaletteSubmitted(const PaletteSelection& selection,
                                           std::string_view url_query) = 0;
    virtual void OnCommandPaletteDismissed() = 0;
    virtual void OnCommandPaletteQueryChanged(std::string_view query) = 0;
    // The palette showed or hid: the host repaints the window's focus ring (the shared neutral
    // ring while an overlay is shown; see BrowserWindow::ApplyAddressFocusColors).
    virtual void OnOverlayFocusRingChanged() = 0;
};

// The Cmd/Ctrl+K command palette: the CEF projection of CommandPaletteModel,
// built exactly like SearchPalette — one overlay on the one existing CefWindow,
// created on show and destroyed on hide so it cannot block page input on macOS,
// never re-parented, and never a second top-level window. It embeds no
// web content. Unlike the search palette's fixed provider rows, the result list
// is a bounded pool of row views sized to the worst case (tabs, spaces,
// bookmarks, eight history results, and the go-to-URL row); rows the current
// Results() does not fill stay hidden.
class CommandPaletteView final {
  public:
    CommandPaletteView(CommandPaletteHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                       PaletteUrlValidator validator, std::filesystem::path icon_resource_root);
    ~CommandPaletteView();

    CommandPaletteView(const CommandPaletteView&) = delete;
    CommandPaletteView& operator=(const CommandPaletteView&) = delete;

    // Shows the palette with fresh read-only snapshots of the open tabs of the
    // active space, the full space list, and bookmarks; resets the draft query
    // and focuses the query field. History arrives asynchronously per query.
    void Show(std::vector<PaletteTabEntry> tabs, std::vector<PaletteSpaceEntry> spaces,
              std::vector<PaletteBookmarkEntry> bookmarks,
              std::vector<PaletteCommandEntry> commands = {},
              std::vector<PaletteReportEntry> reports = {},
              std::vector<PaletteTabVisual> tab_visuals = {});
    // Applies a background history answer only when it still belongs to the
    // visible draft that requested it.
    void SetHistoryResults(std::string_view query, std::vector<PaletteHistoryEntry> history);
    // Hides the palette without acting on the highlighted row.
    void Hide();
    // Developer capture only (`palette_query:<text>`): sets the query field as if typed.
    void SetQueryForDev(std::string_view query);
    // Re-positions the overlay for the window's current bounds.
    void UpdateBounds();
    void ApplyTheme(ChromeTokens tokens);
    // Drops the window/overlay references and detaches every delegate. Safe to
    // call more than once.
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return model_.visible(); }

  private:
    class PaletteSurfaceDelegate;
    class RowButtonDelegate;
    class QueryTextfieldDelegate;

    void BuildViews();
    void CreateOverlay();
    void RebuildResultRows(std::size_t row_count);
    [[nodiscard]] int PreferredContentHeightDip() const;
    [[nodiscard]] std::size_t VisibleRowCount() const;
    bool HandleQueryKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleQueryChanged(CefRefPtr<CefTextfield> textfield);
    void HandleRowPressed(std::size_t index);
    void SubmitHighlighted();
    void ProjectResults();
    void ApplySurfaceColors();
    [[nodiscard]] CefRefPtr<CefImage> RowIcon(const PaletteEntry& entry) const;
    [[nodiscard]] CefRefPtr<CefImage> PlateCap(ArgbColor fill, bool left, int width, int height,
                                               int radius) const;
    [[nodiscard]] ArgbColor ResolvePaletteColor(PaletteSurfaceSlot slot) const;

    CommandPaletteHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    IconCatalog icon_catalog_;
    CommandPaletteModel model_;
    std::vector<PaletteTabVisual> tab_visuals_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefTextfield> query_field_;
    std::vector<CefRefPtr<CefPanel>> result_rows_;
    std::vector<CefRefPtr<CefPanel>> result_bodies_;
    std::vector<CefRefPtr<CefPanel>> result_headers_;
    std::vector<CefRefPtr<CefLabelButton>> result_header_labels_;
    std::vector<CefRefPtr<CefPanel>> result_left_cap_panels_;
    std::vector<CefRefPtr<CefLabelButton>> result_left_caps_;
    std::vector<CefRefPtr<CefPanel>> result_right_cap_panels_;
    std::vector<CefRefPtr<CefLabelButton>> result_right_caps_;
    std::vector<CefRefPtr<CefLabelButton>> result_icons_;
    std::vector<CefRefPtr<CefLabelButton>> result_labels_;
    // The muted secondary text after a row's title (a bookmark's folder and favorite scopes).
    std::vector<CefRefPtr<CefLabelButton>> result_details_;
    OverlaySlot overlay_;
    OverlayLifetime overlay_lifetime_;
    // The right-aligned kind tag (Native sentence case; legacy looks keep their uppercase tone).
    std::vector<CefRefPtr<CefLabelButton>> result_kinds_;
    std::vector<CefRefPtr<CefPanel>> result_shortcut_columns_;
    std::vector<CefRefPtr<CefLabelButton>> result_shortcut_left_caps_;
    std::vector<CefRefPtr<CefLabelButton>> result_shortcut_right_caps_;
    std::vector<CefRefPtr<CefLabelButton>> result_shortcuts_;
    // Hairline + shadow around panel_; overlay_root_ is what the overlay hosts.
    OverlayCard card_;
    CefRefPtr<CefPanel> overlay_root_;
    CefRefPtr<CefPanel> scrim_panel_;
    CefRefPtr<OverlayFillDelegate> scrim_delegate_;
    CefRefPtr<CefPanel> card_row_;
    CefRefPtr<OverlayFillDelegate> top_spacer_delegate_;
    CefRefPtr<CefPanel> top_spacer_;
    CefRefPtr<CefPanel> divider_;
    CefRefPtr<PaletteSurfaceDelegate> panel_delegate_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> surface_delegates_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> row_delegates_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> body_delegates_;
    CefRefPtr<RowButtonDelegate> button_delegate_;
    CefRefPtr<QueryTextfieldDelegate> textfield_delegate_;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_COMMAND_PALETTE_VIEW_H_
