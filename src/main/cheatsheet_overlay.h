#ifndef ISLAND_CHEATSHEET_OVERLAY_H_
#define ISLAND_CHEATSHEET_OVERLAY_H_

#include <array>
#include <filesystem>
#include <vector>

#include "browser_chrome.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "keymap.h"
#include "overlay_card.h"
#include "overlay_lifetime.h"

class CefLabelButton;
class CefPanel;
class CefWindow;

namespace island {

// The CEF-free open/close decisions of the shortcut cheatsheet. Every way the sheet closes
// (its shortcut again, Escape through the stop-loading route, a click outside, the window going
// away) ends in the one OverlayStep that keeps "an overlay exists only while the sheet is
// shown" true, so no live overlay is left behind to swallow mouse input on macOS (see
// overlay_lifetime.h).
class CheatsheetLifetime {
  public:
    // ⌘/ (Ctrl+/ elsewhere): opens a closed sheet and closes an open one.
    OverlayStep Toggle() { return lifetime_.overlay_expected() ? Hide() : Show(); }
    OverlayStep Show() { return lifetime_.Show(); }
    OverlayStep Hide() { return lifetime_.Hide(); }
    OverlayStep Detach() { return lifetime_.Detach(); }

    [[nodiscard]] bool visible() const noexcept { return lifetime_.overlay_expected(); }
    [[nodiscard]] bool overlay_expected() const noexcept { return lifetime_.overlay_expected(); }

  private:
    OverlayLifetime lifetime_;
};

// The window-side seam: the close button, Escape and a click outside the card all end here,
// and the window closes the sheet.
class CheatsheetOverlayHost {
  public:
    virtual ~CheatsheetOverlayHost() = default;

    // The close button was pressed (`refocus_page`), or the sheet lost focus to a click
    // elsewhere, which keeps the focus where the click put it.
    virtual void OnCheatsheetCloseRequested(bool refocus_page) = 0;
};

// The ⌘/ cheatsheet: a 320 DIP card in the bottom-right corner of the page card listing the
// ten most-used bindings with their current keys. It exists only while shown, like every
// Island overlay, and its close button holds focus so that a click anywhere else blurs it.
class CheatsheetOverlay final {
  public:
    CheatsheetOverlay(CheatsheetOverlayHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                      std::filesystem::path icon_resource_root);
    ~CheatsheetOverlay();

    CheatsheetOverlay(const CheatsheetOverlay&) = delete;
    CheatsheetOverlay& operator=(const CheatsheetOverlay&) = delete;

    // Opens the sheet with `rows` over `anchor` (the page card, window-relative DIP), or closes
    // an open one.
    void Toggle(const std::vector<CheatsheetRow>& rows, const CefRect& anchor);
    void Hide();
    void UpdateBounds(const CefRect& anchor);
    void ApplyTheme(ChromeTokens tokens);
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return lifetime_.visible(); }

    static constexpr int kContentWidthDip = 320;
    static constexpr int kAnchorInsetDip = 12;
    static constexpr int kHeaderHeightDip = 40;
    static constexpr int kRowHeightDip = 28;
    static constexpr int kRowCount = 10;
    static constexpr int kBottomInsetDip = 8;
    static constexpr int kContentHeightDip =
        kHeaderHeightDip + (kRowCount * kRowHeightDip) + kBottomInsetDip;

    // The card's content rect (window-relative) for a page card at `anchor`: pinned to its
    // bottom-right corner and shrunk to stay inside a small page card. Pure so tests can pin it.
    [[nodiscard]] static DipRect CardBounds(const DipRect& anchor, int content_height) noexcept;

  private:
    class SurfaceDelegate;
    class ButtonDelegate;

    struct RowViews {
        CefRefPtr<CefPanel> panel;
        CefRefPtr<CefLabelButton> label;
        CefRefPtr<CefLabelButton> keys;
    };

    void BuildViews();
    void Apply(OverlayStep step);
    void SetRows(const std::vector<CheatsheetRow>& rows);
    void ApplyColors();
    void HandleCloseBlurred();
    void HandleClosePressed();

    CheatsheetOverlayHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    IconCatalog icons_;
    OverlayCard card_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefPanel> header_;
    CefRefPtr<CefPanel> overlay_root_;
    CefRefPtr<CefLabelButton> title_;
    CefRefPtr<CefLabelButton> close_;
    std::array<RowViews, kRowCount> rows_;
    OverlaySlot overlay_;
    CheatsheetLifetime lifetime_;
    CefRefPtr<ButtonDelegate> button_delegate_;
    std::vector<CefRefPtr<SurfaceDelegate>> surface_delegates_;
    // A blur only counts once the close button has actually taken focus, so a stray early blur
    // while the overlay activates cannot close the sheet the moment it opens.
    bool close_focused_ = false;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_CHEATSHEET_OVERLAY_H_
