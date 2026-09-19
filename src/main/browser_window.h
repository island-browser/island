#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <memory>
#include <string>
#include <string_view>

#include "active_tab_provider.h"
#include "bookmark_import.h"
#include "bookmark_store.h"
#include "browser_chrome.h"
#include "browser_command.h"
#include "chrome_snapshot.h"
#include "command_palette.h"
#include "command_palette_view.h"
#include "include/cef_client.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "navigation_state.h"
#include "prefs_store.h"
#include "search_palette.h"
#include "sidebar_state.h"
#include "space.h"
#include "space_rename_overlay.h"
#include "tab.h"
#include "welcome_flow.h"

class CefBrowser;
class CefBrowserView;
class CefFrame;
class CefOverlayController;
class CefWindow;

namespace island {

[[nodiscard]] inline ChromeTheme ClassifyChromeTheme(cef_color_t primary_background) noexcept {
    constexpr std::uint32_t kLuminanceScale = 10000;
    constexpr std::uint32_t kRedWeight = 2126;
    constexpr std::uint32_t kGreenWeight = 7152;
    constexpr std::uint32_t kBlueWeight = 722;
    constexpr std::uint32_t kDarkThemeLuminance = 128;

    const std::uint32_t luminance = (kRedWeight * CefColorGetR(primary_background) +
                                     kGreenWeight * CefColorGetG(primary_background) +
                                     kBlueWeight * CefColorGetB(primary_background)) /
                                    kLuminanceScale;
    return luminance < kDarkThemeLuminance ? ChromeTheme::kDark : ChromeTheme::kLight;
}

class BrowserWindow : public CefClient,
                      public CefDisplayHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefWindowDelegate,
                      public CefBrowserViewDelegate,
                      public BrowserChromeHost,
                      public ActiveTabProvider,
                      public SearchPaletteHost,
                      public CommandPaletteHost,
                      public SpaceRenameOverlayHost,
                      public WelcomeFlowHost,
                      public NavigationObserver {
  public:
    // |persist_session| stays false for the smoke run (a deterministic fixed
    // page, never reading or overwriting the real session file) and the
    // headless test seam (tests pin the fresh-install shape).
    static CefRefPtr<BrowserWindow> Create(std::string initial_url, bool persist_session = true);
    // Test seam: constructs the window headless — no CefWindow, no chrome, no
    // CefBrowserView — so command dispatch, space/tab bookkeeping, and the
    // fallbacks below are exercisable without a CEF runtime. Production entry
    // points remain Create/OnWindowCreated; every command must behave the same
    // in both shapes.
    static CefRefPtr<BrowserWindow> CreateHeadlessForTest(std::string initial_url);

    void ExecuteCommand(BrowserCommand command);
    // Direct-index switching and space bookkeeping take explicit methods
    // instead of enum commands so the argument-carrying paths stay
    // unit-testable. All return false and change nothing when the index is out
    // of range.
    [[nodiscard]] bool SelectActiveSpaceTabIndex(std::size_t index);
    [[nodiscard]] bool SelectSpaceIndex(std::size_t index);
    // Closes the tab at |index| in the active space. Closing the last tab of a
    // space closes that space; closing the last remaining space recreates a
    // single fresh default space.
    [[nodiscard]] bool CloseActiveSpaceTabIndex(std::size_t index);
    // Renames the space at |space_index|; an empty name is rejected.
    [[nodiscard]] bool RenameSpace(std::size_t space_index, std::string name);
    // Moves the space at |from_index| to |to_index|; the active space follows.
    [[nodiscard]] bool MoveSpace(std::size_t from_index, std::size_t to_index);

    // U6 split view. Both tabs must live in the active space (tabs from other
    // spaces are rejected, never silently pulled in) and must be distinct;
    // a successful call replaces any previous pairing. UnsplitActiveSpace
    // clears the pairing; both reattach the chrome.
    [[nodiscard]] bool SplitTabs(TabId first, TabId second);
    [[nodiscard]] bool UnsplitActiveSpace();
    // Nudges the split divider; the ratio clamps to
    // [BrowserChrome::SplitRatioMin, SplitRatioMax]. False while closing.
    [[nodiscard]] bool SetSplitRatio(double ratio);
    [[nodiscard]] double split_ratio() const noexcept { return split_ratio_; }

    // Welcome flow (first-run import + appearance). ShowWelcomeFlow is a
    // no-op without a window, like the palette seams.
    void ShowWelcomeFlow();
    // False while closing; a real change persists prefs and re-applies theme.
    bool SetThemePreference(ThemePreference preference);
    [[nodiscard]] ThemePreference theme_preference() const noexcept { return prefs_.theme; }

    // Observation seams for tests and palette snapshots. FindSpace returns
    // nullptr for unknown ids; ids are never derived from vector position.
    [[nodiscard]] std::size_t space_count() const noexcept { return spaces_.size(); }
    [[nodiscard]] std::size_t active_space_index() const noexcept { return active_space_index_; }
    [[nodiscard]] SpaceId active_space_id() const noexcept { return active_space().id(); }
    [[nodiscard]] TabId active_tab_id() const noexcept { return active_space().active_tab_id(); }
    [[nodiscard]] const Space* FindSpace(SpaceId id) const noexcept;
    void SetNavigationObserver(NavigationObserver* observer);
    void SetChromeObserver(ChromeObserver* observer);
    [[nodiscard]] const NavigationSnapshot& navigation_snapshot() const noexcept;
    [[nodiscard]] const ChromeSnapshot& chrome_snapshot() const noexcept;
    [[nodiscard]] ChromeViewTreeNode chrome_view_tree_snapshot() const;
    void RequestClose();
    // Opens the Cmd/Ctrl+K search palette, creating it on first use. Public so the
    // macOS main menu can reach it: CefWindow::SetAccelerator never dispatches on
    // macOS, where NSMenu key equivalents own the command keys.
    void ShowSearchPalette();
    // Opens the Cmd/Ctrl+Shift+K command palette (tabs, spaces, go-to-URL),
    // creating it on first use. The Phase 3 design fixes Cmd/Ctrl+K on the
    // command palette; the later search palette keeps Cmd/Ctrl+Shift+K.
    void ShowCommandPalette();
    // Opens the rename overlay for the active space. Menu-driven on macOS (no
    // key equivalent); F2 is the cross-platform accelerator.
    void BeginSpaceRenaming();
    // Moves the active space by |delta| positions (-1 left, +1 right); false
    // when the move would leave the space list.
    [[nodiscard]] bool MoveActiveSpace(int delta);
    // Flips the explicit Cmd/Ctrl+B pin. Available on every platform; hover is a
    // macOS-only enhancement layered on the same state.
    void ToggleSidebar();
    // The macOS hover seam's entry point: a pointer position in window DIP.
    void OnSidebarHoverPointer(int x_dip);
    // Navigates the active tab to the provider URL for `query`. A blank query is
    // rejected and a null ActiveBrowser() is a defined no-op; neither navigates.
    void SubmitSearchQuery(std::string_view query, SearchProviderId provider);

    void ExecuteBrowserCommand(BrowserCommand command) override;
    void SelectTab(std::size_t index) override;
    void CloseTab(std::size_t index) override;
    void SelectSpace(std::size_t index) override;
    void BeginAddressEditing() override;
    void CancelAddressEditing() override;
    void SubmitAddressDraft(std::string_view draft) override;
    void FocusBrowserView() override;
    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;

    [[nodiscard]] CefRefPtr<CefBrowser> ActiveBrowser() override;

    void OnSearchPaletteSubmitted(const SearchSubmission& submission) override;
    void OnSearchPaletteDismissed() override;

    void OnCommandPaletteSubmitted(const PaletteSelection& selection,
                                   std::string_view url_query) override;
    void OnCommandPaletteDismissed() override;

    void OnSpaceRenameCommitted(std::string name) override;
    void OnSpaceRenameCancelled() override;

    void OnWelcomeThemeChanged(ThemePreference preference) override;
    void OnWelcomeCompleted(const std::vector<ImportSource>& sources) override;

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;

    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                       const CefString& target_url, const CefString& target_frame_name,
                       WindowOpenDisposition target_disposition, bool user_gesture,
                       const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                       CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
                              bool can_go_forward) override;
    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     TransitionType transition_type) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int http_status_code) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                     const CefString& error_text, const CefString& failed_url) override;

    void OnWindowCreated(CefRefPtr<CefWindow> window) override;
    void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
    void OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect& new_bounds) override;
    void OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool chrome_theme) override;
    CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
    bool CanClose(CefRefPtr<CefWindow> window) override;
    bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;

    void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
    void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                            CefRefPtr<CefBrowser> browser) override;
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView> browser_view) override;
    void OnFocus(CefRefPtr<CefView> view) override;

  private:
    enum AcceleratorId {
        kBackAccelerator = 1,
        kForwardAccelerator,
        kReloadAccelerator,
        kReloadWithControlAccelerator,
        kFocusAddressAccelerator,
        kOpenPaletteAccelerator,
        kToggleSidebarAccelerator,
        // Phase 3 tab commands. The direct-index block is contiguous so
        // OnAccelerator can range-check the nine tab positions.
        kNewTabAccelerator,
        kCloseTabAccelerator,
        kNextTabAccelerator,
        kPreviousTabAccelerator,
        kOpenSearchPaletteAccelerator,
        kRenameSpaceAccelerator,
        kSelectTab1Accelerator,
        kSelectTab2Accelerator,
        kSelectTab3Accelerator,
        kSelectTab4Accelerator,
        kSelectTab5Accelerator,
        kSelectTab6Accelerator,
        kSelectTab7Accelerator,
        kSelectTab8Accelerator,
        kSelectTab9Accelerator,
        // U6 split view. On macOS these never dispatch — the NSMenu owns the
        // command keys — but registration stays uniform across platforms.
        kToggleSplitAccelerator,
        kMoveDividerLeftAccelerator,
        kMoveDividerRightAccelerator,
        // Welcome flow: Escape dismisses ("just start browsing") while the
        // overlay is visible and is otherwise untouched.
        kWelcomeDismissAccelerator,
    };

    explicit BrowserWindow(std::string initial_url, bool persist_session = true);

    [[nodiscard]] Space& active_space() noexcept { return spaces_[active_space_index_]; }
    [[nodiscard]] const Space& active_space() const noexcept {
        return spaces_[active_space_index_];
    }
    [[nodiscard]] Tab* FindTabByBrowser(CefRefPtr<CefBrowser> browser) noexcept;
    [[nodiscard]] const Tab* FindTabByBrowser(CefRefPtr<CefBrowser> browser) const noexcept;
    [[nodiscard]] Tab* FindTabByBrowserView(CefRefPtr<CefBrowserView> browser_view) noexcept;
    [[nodiscard]] Tab* active_tab() noexcept;
    [[nodiscard]] const Tab* active_tab() const noexcept;
    // Creates the tab's CefBrowserView on first use (same creation path as
    // kNewTab: the active space's request context and the tab's startup URL).
    // False when headless; true when the tab already had a view. Only valid
    // for tabs of the active space.
    bool EnsureTabBrowserView(Tab& tab);
    // Selecting a tab outside the current split pairing tears the split down,
    // so the attached views always include the active tab's view.
    void BreakSplitIfSelectionLeftPair(Space& space, TabId selected);
    // The theme the window should run in right now: the stored preference when
    // it forces a theme, else the OS theme classified from the window.
    [[nodiscard]] ChromeTheme ResolvedChromeTheme() const;
    // Persists prefs best-effort (logs via error code, never throws).
    void SavePrefs() const;
    // Navigates the active tab to a stored bookmark URL through the single
    // address-validation path; a URL the allow-list rejects is a no-op.
    void OpenBookmark(const PaletteBookmarkEntry& bookmark);
    // Detaches the projection from the current active tab before a mutation
    // changes which tab/space is active; Attach re-subscribes to the (possibly
    // new) active tab, pushes its snapshot through the address model and
    // chrome, re-attaches its browser view into the content slot, and refreshes
    // the collection rows. Both are no-ops while closing.
    void DetachActiveTabObservers();
    void AttachActiveTabObservers();
    void AttachActiveTabBrowserView();
    void UpdateChromeCollections();
    [[nodiscard]] static Space CreateDefaultSpace();
    // Session restore: rebuilds spaces, tabs, active selections, and split
    // pairings from the session file. Returns false and leaves the model in
    // its fresh-install shape for every non-recoverable outcome (missing,
    // unreadable, or schema-invalid file), which the caller treats as the
    // fixed-startup-page fallback.
    bool RestoreSession();
    // Serializes the current model for the next launch. Clean quit only — no
    // autosave, no crash hook.
    void SaveSession() const;
    // The URL a restored tab's browser view loads: the tab's validated startup
    // URL, else the window's fixed startup page.
    [[nodiscard]] std::string StartupUrlForTab(const Tab& tab) const;
    // Removes the space at |index| from the model and applies the encoded
    // fallbacks: the previous neighbor becomes active (the next one at index 0),
    // and an empty window recreates a single fresh default space. Returns the
    // removed space so its browsers can be closed after the model mutation.
    Space RemoveSpaceAtIndex(std::size_t index);
    // Closes every CefBrowser owned by |space|'s tabs. Must run after the
    // model mutation, never while iterating spaces_.
    void CloseSpaceBrowsers(Space& space);
    // Palette routing: the selection is resolved against the current model by
    // identity (TabId/SpaceId), never by the position it was listed at.
    void ActivatePaletteTab(TabId id);
    void ActivatePaletteSpace(SpaceId id);
    void SubmitPaletteUrl(std::string_view query, const ValidatedAddress& address);
    void ApplyTheme(CefRefPtr<CefWindow> window, ChromeTheme theme, bool notify_views);
    void PublishChromeSnapshot();
    void DetachChromeAndObservers();
    void CreateHoverSliver();
    void ApplySidebarState();
    void UninstallHoverSeam();
    void UpdateWindowTitle();
    void CloseNavigationAndQuitMessageLoop();

    std::string initial_url_;
    AddressBarModel address_bar_model_;
    std::unique_ptr<BrowserChrome> chrome_;
    std::unique_ptr<SearchPalette> search_palette_;
    std::unique_ptr<CommandPaletteView> command_palette_;
    std::unique_ptr<SpaceRenameOverlay> space_rename_overlay_;
    SidebarState sidebar_state_;
    CefRefPtr<CefPanel> hover_sliver_;
    CefRefPtr<CefOverlayController> hover_sliver_overlay_;
    void* hover_seam_ = nullptr;
    NavigationObserver* navigation_observer_ = nullptr;
    ChromeObserver* chrome_observer_ = nullptr;
    ChromeSnapshot chrome_snapshot_;
    CefRefPtr<CefWindow> window_;
    std::vector<Space> spaces_;
    std::size_t active_space_index_ = 0;
    // Monotonic source of "Space N" display names so removals never produce a
    // duplicate name later.
    std::uint32_t created_space_count_ = 1;
    // The active tab's page title last projected into the collection rows, so
    // title-only navigation changes avoid rebuilding both collections.
    std::string projected_page_title_;
    bool browser_was_created_ = false;
    bool closing_ = false;
    bool message_loop_quit_ = false;
    // False for the smoke run and the headless test seam: the session file is
    // neither read at startup nor written on clean quit.
    bool persist_session_ = true;
    // U6 split view: first pane's share of the content width. A window-level
    // transient (the design calls a split a view arrangement, not persisted
    // state; the pairing itself persists through the session file).
    double split_ratio_ = BrowserChrome::SplitRatioDefault();
    // Welcome flow state. prefs_ keeps the fresh-install defaults in the
    // headless/no-persistence shapes, which never touch the prefs file.
    PrefsState prefs_;
    BookmarkState bookmarks_;
    std::unique_ptr<WelcomeFlow> welcome_;

    IMPLEMENT_REFCOUNTING(BrowserWindow);
    DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace island

#endif  // ISLAND_BROWSER_WINDOW_H_
