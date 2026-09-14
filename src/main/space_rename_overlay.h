#ifndef ISLAND_SPACE_RENAME_OVERLAY_H_
#define ISLAND_SPACE_RENAME_OVERLAY_H_

#include <string>

#include "browser_chrome.h"
#include "design_tokens.h"

class CefOverlayController;
class CefPanel;
class CefTextfield;
class CefWindow;

namespace island {

// The window-side seam the rename overlay reports through. BrowserWindow
// implements it: a commit renames the space, a cancellation only restores
// focus.
class SpaceRenameOverlayHost {
  public:
    virtual ~SpaceRenameOverlayHost() = default;

    // The trimmed, non-empty replacement name. Empty names can never arrive:
    // the overlay keeps them non-submittable.
    virtual void OnSpaceRenameCommitted(std::string name) = 0;
    virtual void OnSpaceRenameCancelled() = 0;
};

// A small single-textfield overlay for renaming the active space, built like
// the palettes: one overlay on the one existing CefWindow, created lazily and
// then only shown and hidden, never a second top-level window. Enter commits,
// Escape cancels, and Tab is ignored so focus stays trapped in the field.
class SpaceRenameOverlay final {
  public:
    SpaceRenameOverlay(SpaceRenameOverlayHost& host, CefRefPtr<CefWindow> window,
                       ChromeTokens tokens, std::string current_name);
    ~SpaceRenameOverlay();

    SpaceRenameOverlay(const SpaceRenameOverlay&) = delete;
    SpaceRenameOverlay& operator=(const SpaceRenameOverlay&) = delete;

    void Show();
    void Hide();
    void UpdateBounds();
    void ApplyTheme(ChromeTokens tokens);
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return visible_; }

  private:
    class RenameSurfaceDelegate;
    class RenameTextfieldDelegate;

    void BuildViews();
    bool HandleFieldKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event);
    void Commit();
    void ApplySurfaceColors();

    SpaceRenameOverlayHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    std::string current_name_;
    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefTextfield> name_field_;
    CefRefPtr<CefOverlayController> overlay_;
    CefRefPtr<RenameSurfaceDelegate> panel_delegate_;
    CefRefPtr<RenameTextfieldDelegate> textfield_delegate_;
    bool visible_ = false;
    bool detached_ = false;
};

}  // namespace island

#endif  // ISLAND_SPACE_RENAME_OVERLAY_H_
