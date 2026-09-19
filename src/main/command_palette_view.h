#ifndef ISLAND_COMMAND_PALETTE_VIEW_H_
#define ISLAND_COMMAND_PALETTE_VIEW_H_

#include <string_view>
#include <vector>

#include "browser_chrome.h"
#include "command_palette.h"
#include "design_tokens.h"
#include "search_palette_model.h"

class CefLabelButton;
class CefOverlayController;
class CefPanel;
class CefTextfield;
class CefWindow;

namespace island {

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
};

// The Cmd/Ctrl+K command palette: the CEF projection of CommandPaletteModel,
// built exactly like SearchPalette — one overlay on the one existing CefWindow,
// created lazily on first invocation and then only shown and hidden, never
// destroyed or re-parented, and never a second top-level window. It embeds no
// web content. Unlike the search palette's fixed provider rows, the result list
// is a bounded pool of row views sized to the worst case (one per tab, one per
// space, plus the go-to-URL row); rows the current Results() does not fill stay
// hidden.
class CommandPaletteView final {
  public:
    CommandPaletteView(CommandPaletteHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                       PaletteUrlValidator validator);
    ~CommandPaletteView();

    CommandPaletteView(const CommandPaletteView&) = delete;
    CommandPaletteView& operator=(const CommandPaletteView&) = delete;

    // Shows the palette with fresh read-only snapshots of the open tabs of the
    // active space and the full space list, resets the draft query, and focuses
    // the query field.
    void Show(std::vector<PaletteTabEntry> tabs, std::vector<PaletteSpaceEntry> spaces,
              std::vector<PaletteBookmarkEntry> bookmarks);
    // Hides the palette without acting on the highlighted row.
    void Hide();
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
    void RebuildResultRows(std::size_t row_count);
    [[nodiscard]] int PreferredContentHeightDip() const;
    bool HandleQueryKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleQueryChanged(CefRefPtr<CefTextfield> textfield);
    void HandleRowPressed(std::size_t index);
    void SubmitHighlighted();
    void ProjectResults();
    void ApplySurfaceColors();
    [[nodiscard]] ArgbColor ResolvePaletteColor(PaletteSurfaceSlot slot) const;

    CommandPaletteHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    CommandPaletteModel model_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefTextfield> query_field_;
    std::vector<CefRefPtr<CefPanel>> result_rows_;
    std::vector<CefRefPtr<CefLabelButton>> result_labels_;
    CefRefPtr<CefOverlayController> overlay_;
    CefRefPtr<PaletteSurfaceDelegate> panel_delegate_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> surface_delegates_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> row_delegates_;
    CefRefPtr<RowButtonDelegate> button_delegate_;
    CefRefPtr<QueryTextfieldDelegate> textfield_delegate_;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_COMMAND_PALETTE_VIEW_H_
