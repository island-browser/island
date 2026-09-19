#ifndef ISLAND_WELCOME_FLOW_H_
#define ISLAND_WELCOME_FLOW_H_

#include <vector>

#include "bookmark_import.h"
#include "browser_chrome.h"
#include "design_tokens.h"
#include "prefs_store.h"

class CefLabelButton;
class CefOverlayController;
class CefPanel;
class CefWindow;

namespace island {

// The window-side seam the welcome flow reports through. BrowserWindow
// implements it: a theme change persists and re-applies immediately; a
// completion runs the checked imports, marks onboarding done, and hides the
// flow. The overlay itself never touches a CefBrowser or the file system —
// the sources list arrives detected, and imports run in the host.
class WelcomeFlowHost {
  public:
    virtual ~WelcomeFlowHost() = default;

    virtual void OnWelcomeThemeChanged(ThemePreference preference) = 0;
    virtual void OnWelcomeCompleted(const std::vector<ImportSource>& sources) = 0;
};

// The first-run welcome flow: one cef_views overlay on the one existing
// CefWindow (AddOverlayView, shown and hidden, never a second top-level
// window, no web content), governed by the same rules as the Phase 3
// palettes. Presents an appearance choice and the detected import sources;
// Escape dismisses through the window accelerator, which treats it as
// "just start browsing".
class WelcomeFlow final {
  public:
    WelcomeFlow(WelcomeFlowHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens,
                ThemePreference current_theme, std::vector<ImportSourceInfo> sources);
    ~WelcomeFlow();

    WelcomeFlow(const WelcomeFlow&) = delete;
    WelcomeFlow& operator=(const WelcomeFlow&) = delete;

    void Show();
    void Hide();
    void ApplyTheme(ChromeTokens tokens);
    // Drops the window/overlay references and detaches every delegate. Safe to
    // call more than once.
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] ThemePreference chosen_theme() const noexcept { return chosen_theme_; }

  private:
    enum class SurfaceSlot : std::uint8_t { kPanel, kSection, kRow };
    // Element ids offset from the panel's ChromeViewId so the button delegate
    // can route presses by view id.
    static constexpr int kThemeSystemId = static_cast<int>(ChromeViewId::kWelcome) + 1;
    static constexpr int kThemeLightId = static_cast<int>(ChromeViewId::kWelcome) + 2;
    static constexpr int kThemeDarkId = static_cast<int>(ChromeViewId::kWelcome) + 3;
    static constexpr int kSourceIdBase = static_cast<int>(ChromeViewId::kWelcome) + 10;
    static constexpr int kImportButtonId = static_cast<int>(ChromeViewId::kWelcome) + 40;
    static constexpr int kStartButtonId = static_cast<int>(ChromeViewId::kWelcome) + 41;

    class WelcomeSurfaceDelegate;
    class WelcomeButtonDelegate;

    void BuildViews();
    void HandleButtonPressed(int view_id);
    void UpdateImportButtonState();
    [[nodiscard]] int PreferredContentHeightDip() const;
    void UpdateBounds();
    void ApplySurfaceColors();
    void ProjectThemeChoice();
    void ProjectSourceChecks();
    [[nodiscard]] ArgbColor ResolveColor(SurfaceSlot slot) const;

    WelcomeFlowHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    std::vector<ImportSourceInfo> sources_;
    std::vector<bool> source_checked_;
    ThemePreference chosen_theme_;
    bool visible_ = false;
    bool detached_ = false;

    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefLabelButton> theme_buttons_[3];
    std::vector<CefRefPtr<CefLabelButton>> source_buttons_;
    CefRefPtr<CefLabelButton> import_button_;
    CefRefPtr<CefLabelButton> start_button_;
    CefRefPtr<CefOverlayController> overlay_;
    CefRefPtr<WelcomeSurfaceDelegate> panel_delegate_;
    std::vector<CefRefPtr<WelcomeSurfaceDelegate>> surface_delegates_;
    CefRefPtr<WelcomeButtonDelegate> button_delegate_;
};

}  // namespace island

#endif  // ISLAND_WELCOME_FLOW_H_
