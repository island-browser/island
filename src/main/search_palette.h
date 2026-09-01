#ifndef ISLAND_SEARCH_PALETTE_H_
#define ISLAND_SEARCH_PALETTE_H_

#include <vector>

#include "browser_chrome.h"
#include "design_tokens.h"
#include "search_palette_model.h"
#include "search_provider.h"

class CefLabelButton;
class CefOverlayController;
class CefPanel;
class CefTextfield;
class CefWindow;

namespace island {

// The window-side seam the palette reports through. BrowserWindow implements it:
// a submission navigates the active tab, a dismissal restores focus to the
// invocation point. The palette itself never touches a CefBrowser.
class SearchPaletteHost {
  public:
    virtual ~SearchPaletteHost() = default;

    virtual void OnSearchPaletteSubmitted(const SearchSubmission& submission) = 0;
    virtual void OnSearchPaletteDismissed() = 0;
};

// The Cmd/Ctrl+K search palette: a single cef_views overlay on the one existing
// CefWindow, added with AddOverlayView(CEF_DOCKING_MODE_CUSTOM, can_activate =
// true) and positioned through the returned CefOverlayController. It is created
// once and then only shown and hidden -- never destroyed, never re-parented, and
// never a second top-level window. It embeds no web content.
class SearchPalette final {
  public:
    SearchPalette(SearchPaletteHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens);
    ~SearchPalette();

    SearchPalette(const SearchPalette&) = delete;
    SearchPalette& operator=(const SearchPalette&) = delete;

    // Shows the palette, resets the draft query, and focuses the query field.
    void Show();
    // Hides the palette without navigating.
    void Hide();
    // Re-positions the overlay for the window's current bounds.
    void UpdateBounds();
    void ApplyTheme(ChromeTokens tokens);
    // Drops the window/overlay references and detaches every delegate. Safe to
    // call more than once.
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return model_.visible(); }
    [[nodiscard]] const SearchPaletteModel& model() const noexcept { return model_; }

  private:
    class PaletteSurfaceDelegate;
    class PaletteButtonDelegate;
    class PaletteTextfieldDelegate;

    void BuildViews();
    [[nodiscard]] int PreferredContentHeightDip() const;
    bool HandleQueryKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleQueryChanged(CefRefPtr<CefTextfield> textfield);
    void HandleRowPressed(std::size_t index);
    void SubmitHighlighted();
    void ApplySurfaceColors();
    void ProjectHighlight();
    [[nodiscard]] ArgbColor ResolvePaletteColor(PaletteSurfaceSlot slot) const;

    SearchPaletteHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    SearchPaletteModel model_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefTextfield> query_field_;
    std::vector<CefRefPtr<CefPanel>> provider_rows_;
    std::vector<CefRefPtr<CefLabelButton>> provider_labels_;
    CefRefPtr<CefOverlayController> overlay_;
    CefRefPtr<PaletteSurfaceDelegate> panel_delegate_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> surface_delegates_;
    std::vector<CefRefPtr<PaletteSurfaceDelegate>> row_delegates_;
    CefRefPtr<PaletteButtonDelegate> button_delegate_;
    CefRefPtr<PaletteTextfieldDelegate> textfield_delegate_;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_SEARCH_PALETTE_H_
