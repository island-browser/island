#ifndef ISLAND_WELCOME_FLOW_H_
#define ISLAND_WELCOME_FLOW_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bookmark_import.h"
#include "browser_chrome.h"
#include "design_tokens.h"
#include "overlay_card.h"
#include "overlay_lifetime.h"
#include "prefs_store.h"

class CefLabelButton;
class CefImage;
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
    // The welcome showed or hid: the host repaints the window's focus ring for the overlay state
    // (see BrowserWindow::ApplyAddressFocusColors).
    virtual void OnOverlayFocusRingChanged() = 0;
};

// The first-run welcome flow: one cef_views overlay on the one existing
// CefWindow (AddOverlayView, shown and hidden, never a second top-level
// window, no web content), governed by the same rules as the Phase 3
// palettes. Presents an appearance choice and the detected import sources;
// Escape dismisses through the window accelerator, which treats it as
// "just start browsing".
//
// Structure (DESIGN.md §5): a `--surface` card holding a title and lede, an appearance
// control, an import section with one toggle row per readable browser plus a wrapped note for a
// detected-but-unreadable profile, and a right-aligned footer with a secondary Import button and
// the primary "Start browsing" button. Native uses radius-12 caps, sentence-case labels and the
// sidebar's compact segment shape; Arc and Graphite retain their square hairline card and uppercase
// labels.
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
    // Keeps the card's selected plate in sync when the theme changes through a
    // menu, shortcut, or developer action rather than one of its own buttons.
    void SetChosenTheme(ThemePreference preference);
    // Re-centers the card and re-fits the scrim to the window.
    void UpdateBounds();
    // Drops the window/overlay references and detaches every delegate. Safe to
    // call more than once.
    void Detach();

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] ThemePreference chosen_theme() const noexcept { return chosen_theme_; }

    // Greedy word wrap of "Not readable on this machine: A, B, C" into lines of at
    // most |max_chars| characters, breaking after list commas, so the note shows in
    // full instead of being ellipsized. Pure; exposed for tests.
    [[nodiscard]] static std::vector<std::string> WrapUnreadableNote(
        const std::vector<std::string>& names, std::size_t max_chars);
    // The hairline-outlined card's overall height in DIP for the given source mix.
    [[nodiscard]] static int ContentHeightDip(int readable_sources, int note_lines);
    [[nodiscard]] static constexpr int WidthDip() { return 560; }

    // Every chrome view's color roles resolve through this seam so the card works in
    // both themes: the selected segment/row, the secondary button, and the primary
    // button (`--text` fill, `--surface` foreground) are asserted by tests.
    struct ButtonColors {
        ArgbColor fill;
        ArgbColor text;
    };
    [[nodiscard]] static ButtonColors SegmentColors(const ChromeTokens& tokens, bool selected);
    [[nodiscard]] static ButtonColors SecondaryButtonColors(const ChromeTokens& tokens);
    [[nodiscard]] static ButtonColors PrimaryButtonColors(const ChromeTokens& tokens);

  private:
    enum class SurfaceSlot : std::uint8_t { kTransparent, kTrack, kOutline, kCard };
    enum class TextRole : std::uint8_t { kTitle, kLede, kSectionLabel, kNote };
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

    struct StaticText {
        CefRefPtr<CefLabelButton> view;
        TextRole role;
        std::string text;
    };
    struct SourceRow {
        CefRefPtr<CefPanel> outline;
        CefRefPtr<CefLabelButton> button;
        std::size_t source_index = 0;
    };

    void BuildViews();
    void CreateOverlay();
    CefRefPtr<CefPanel> MakePanel(SurfaceSlot slot);
    // A text line is a label button inside a fixed-height row: CEF label buttons add their
    // own vertical padding, so the row (not the button) sets the line pitch.
    CefRefPtr<CefPanel> MakeText(const std::string& text, TextRole role,
                                 CefRefPtr<CefLabelButton>* label_out = nullptr);
    void HandleButtonPressed(int view_id);
    // CEF wipes custom view colors when a theme change is delivered; the delegates
    // report it here and one coalesced pass repaints everything.
    void HandleViewThemeReset();
    void ReapplyColors();
    void ApplyColors();
    void NotifyFocusRingChanged();
    void UpdateImportButtonState();
    [[nodiscard]] bool AnyCheckedAvailableSource() const;
    [[nodiscard]] ArgbColor ResolveColor(SurfaceSlot slot) const;
    [[nodiscard]] CefRefPtr<CefImage> CardCap(bool top) const;

    WelcomeFlowHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    std::vector<ImportSourceInfo> sources_;
    std::vector<bool> source_checked_;
    ThemePreference chosen_theme_;
    bool visible_ = false;
    bool detached_ = false;
    bool reapply_pending_ = false;
    int note_line_count_ = 0;
    int readable_count_ = 0;
    std::shared_ptr<WelcomeFlow*> alive_;

    CefRefPtr<CefPanel> panel_;
    CefRefPtr<CefPanel> card_top_cap_;
    CefRefPtr<CefLabelButton> card_top_cap_image_;
    CefRefPtr<CefPanel> card_bottom_cap_;
    CefRefPtr<CefLabelButton> card_bottom_cap_image_;
    CefRefPtr<CefPanel> card_body_outline_;
    CefRefPtr<CefPanel> card_;
    std::vector<std::pair<CefRefPtr<CefPanel>, SurfaceSlot>> panels_;
    std::vector<StaticText> texts_;
    CefRefPtr<CefLabelButton> theme_buttons_[3];
    CefRefPtr<CefPanel> theme_outlines_[3];
    std::vector<SourceRow> source_rows_;
    CefRefPtr<CefPanel> import_outline_;
    CefRefPtr<CefLabelButton> import_button_;
    CefRefPtr<CefLabelButton> start_button_;
    OverlaySlot overlay_;
    OverlayLifetime overlay_lifetime_;
    // The overlay root: a translucent full-window scrim that centers the shadowed card. The
    // scrim is part of the one overlay (a second, always-present overlay would swallow
    // mouse input on macOS), so it exists exactly while the card does.
    CefRefPtr<CefPanel> scrim_panel_;
    CefRefPtr<OverlayFillDelegate> scrim_delegate_;
    OverlayShadow shadow_;
    CefRefPtr<WelcomeSurfaceDelegate> panel_delegate_;
    std::vector<CefRefPtr<WelcomeSurfaceDelegate>> surface_delegates_;
    CefRefPtr<WelcomeButtonDelegate> button_delegate_;
};

}  // namespace island

#endif  // ISLAND_WELCOME_FLOW_H_
