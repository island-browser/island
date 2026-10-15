#ifndef ISLAND_FIND_BAR_OVERLAY_H_
#define ISLAND_FIND_BAR_OVERLAY_H_

#include <filesystem>
#include <string>
#include <string_view>

#include "browser_chrome.h"
#include "design_tokens.h"
#include "icon_catalog.h"
#include "overlay_card.h"
#include "overlay_lifetime.h"

class CefLabelButton;
class CefPanel;
class CefTextfield;
class CefWindow;

namespace island {

// The window-side seam the find bar reports through. BrowserWindow owns the FindBarModel and
// turns these gestures into CefBrowserHost::Find / StopFinding calls.
class FindBarOverlayHost {
  public:
    virtual ~FindBarOverlayHost() = default;

    virtual void OnFindQueryChanged(std::string text) = 0;
    // Enter / Shift+Enter in the field and the previous/next buttons.
    virtual void OnFindStep(bool forward) = 0;
    virtual void OnFindMatchCaseChanged(bool match_case) = 0;
    // Escape or the close button; the window hides the bar and refocuses the page.
    virtual void OnFindCloseRequested() = 0;
};

// The in-page find bar: one small card at the top-right of the page card. Like every Island
// overlay it exists only while shown (see overlay_lifetime.h), never as a second top-level
// window, so the page and the rail stay clickable.
class FindBarOverlay final {
  public:
    FindBarOverlay(FindBarOverlayHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                   std::filesystem::path icon_resource_root);
    ~FindBarOverlay();

    FindBarOverlay(const FindBarOverlay&) = delete;
    FindBarOverlay& operator=(const FindBarOverlay&) = delete;

    // Shows the bar over `anchor` (the page card, window-relative DIP) and focuses the field.
    void Show(std::string_view query, bool match_case, const CefRect& anchor);
    void Hide();
    void UpdateBounds(const CefRect& anchor);
    void ApplyTheme(ChromeTokens tokens);
    void Detach();
    // Selects the field text again (Cmd+F while the bar is open).
    void FocusField();
    // "3 of 12" ("" hides it); `no_matches` tints it so a failed search is visible.
    void SetCount(std::string_view label, bool no_matches);

    [[nodiscard]] bool visible() const noexcept { return visible_; }

    static constexpr int kContentWidthDip = 360;
    static constexpr int kContentHeightDip = 40;
    static constexpr int kAnchorInsetDip = 12;

    // The card's position (content rect, window-relative) for a page card at `anchor`: pinned to
    // its top-right corner, narrower when the page card is. Pure so tests can pin the placement.
    [[nodiscard]] static DipRect CardBounds(const DipRect& anchor) noexcept;

  private:
    class SurfaceDelegate;
    class FieldDelegate;
    class ButtonDelegate;

    void BuildViews();
    void CreateOverlay();
    void ApplyColors();
    void ApplyMatchCaseVisual();
    bool HandleFieldKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void HandleButtonPressed(ChromeViewId id);

    FindBarOverlayHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    IconCatalog icons_;
    OverlayCard card_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefPanel> overlay_root_;
    CefRefPtr<CefTextfield> field_;
    CefRefPtr<CefLabelButton> count_;
    CefRefPtr<CefLabelButton> previous_;
    CefRefPtr<CefLabelButton> next_;
    CefRefPtr<CefLabelButton> match_case_button_;
    CefRefPtr<CefLabelButton> close_;
    OverlaySlot overlay_;
    OverlayLifetime overlay_lifetime_;
    CefRefPtr<SurfaceDelegate> panel_delegate_;
    CefRefPtr<FieldDelegate> field_delegate_;
    CefRefPtr<ButtonDelegate> button_delegate_;
    std::vector<CefRefPtr<SurfaceDelegate>> surface_delegates_;
    bool match_case_ = false;
    bool no_matches_ = false;
    bool visible_ = false;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_FIND_BAR_OVERLAY_H_
