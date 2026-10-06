#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <memory>
#include <string>
#include <string_view>

#include "active_tab_provider.h"
#include "agent_navigation.h"
#include "agent_session.h"
#include "bookmark_import.h"
#include "bookmark_store.h"
#include "browser_chrome.h"
#include "browser_command.h"
#include "browser_import.h"
#include "chrome_snapshot.h"
#include "command_palette.h"
#include "command_palette_view.h"
#include "include/cef_client.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "keymap.h"
#include "local_page.h"
#include "navigation_state.h"
#include "prefs_store.h"
#include "search_palette.h"
#include "sidebar_state.h"
#include "space.h"
#include "space_rename_overlay.h"
#include "tab.h"
#include "updater.h"
#include "welcome_flow.h"

class CefBrowser;
class CefBrowserView;
class CefFrame;
class CefOverlayController;
class CefWindow;

namespace island {

class WindowAgentHost;

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
                      public LocalPageDelegate,
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

    // Agent seams (src/agent tools). OpenNewTab appends a tab to the active
    // space and loads `text` resolved through ResolveAgentNavigation (blank
    // text opens the startup page). NavigateTab targets a tab of the active
    // space (std::nullopt = the active tab); a tab without a live browser
    // keeps the URL as its startup URL. Both return false when closing, for
    // an unknown tab, or for text that resolves to no allowed URL.
    [[nodiscard]] bool OpenNewTab(std::string_view text);
    [[nodiscard]] bool NavigateTab(std::optional<std::size_t> index, std::string_view text);
    // The live CefBrowser of a tab of the active space, or nullptr.
    [[nodiscard]] CefRefPtr<CefBrowser> BrowserForTab(std::optional<std::size_t> index) const;
    // kNewSpace plus an optional name.
    [[nodiscard]] bool CreateSpace(std::string_view name);
    // Pins/unpins a tab of the active space (Arc-style pinned group).
    [[nodiscard]] bool SetActiveSpaceTabPinned(std::size_t index, bool pinned);
    [[nodiscard]] const std::vector<Space>& spaces() const noexcept { return spaces_; }
    // Test seam: headless tests have no CefParseURL, so they inject the
    // address validation the agent seams use.
    void SetAddressValidatorForTest(AddressValidator validator) {
        address_validator_ = std::move(validator);
    }

    // The ACP agent panel (Cmd/Ctrl+J): opens/closes the right-hand column,
    // creating the panel and its session on first use. No-op without chrome.
    void ToggleAgentPanel() override;
    void SetAgentPanelOpen(bool open);
    [[nodiscard]] bool agent_panel_open() const noexcept;
    // The agent command the panel launches: ISLAND_AGENT_COMMAND, else the
    // saved preference, else the built-in default.
    [[nodiscard]] std::string ResolvedAgentCommand() const;
    static constexpr std::string_view kDefaultAgentCommand =
        "npx -y @zed-industries/claude-code-acp";
    void OnLocalPageMessage(LocalPageKind kind, const json::Value& message) override;

    // Built-in pages shown in the content area (Settings, the all-tabs
    // overview). Toggling the shown page hides it; selecting any tab hides it
    // too. No-op without chrome.
    void ToggleTabOverview() override;
    void ToggleSettings() override;
    void ToggleInternalPage(LocalPageKind kind);
    [[nodiscard]] std::optional<LocalPageKind> internal_page() const noexcept {
        return internal_page_;
    }
    // The state the pages render, exposed for tests.
    [[nodiscard]] std::string SettingsStateJson() const;
    [[nodiscard]] std::string TabOverviewStateJson() const;

    // All-tabs overview operations, addressed by space and tab position.
    // Each returns false for out-of-range positions or while closing.
    [[nodiscard]] bool ActivateTabAt(std::size_t space, std::size_t tab);
    [[nodiscard]] bool CloseTabAt(std::size_t space, std::size_t tab);
    [[nodiscard]] bool SetTabPinnedAt(std::size_t space, std::size_t tab, bool pinned);
    // Reorders within a space; the target is clamped into the tab's group
    // (pinned or regular) so the pinned tray stays contiguous.
    [[nodiscard]] bool MoveTabWithinSpace(std::size_t space, std::size_t from, std::size_t to);
    // Spaces keep separate browsing contexts, so moving a tab reopens its URL
    // in the target space and closes the original.
    [[nodiscard]] bool MoveTabToSpace(std::size_t from_space, std::size_t tab,
                                      std::size_t to_space);
    bool ResetKeyBinding(KeyAction action);

    // In-browser updates (Settings > Updates). CheckForUpdates starts a
    // GitHub Releases check unless ISLAND_DISABLE_UPDATES is set; the
    // automatic one runs ~10 s after a persisted (non-smoke) window opens, at
    // most once per 24 h. RestartToUpdate launches the verified update's
    // apply script and closes the window so it can run.
    bool CheckForUpdates();
    void RestartToUpdate();
    [[nodiscard]] const update::Updater& updater() const noexcept { return *updater_; }
    // Replaces the updater (the constructor installs the CefURLRequest one;
    // tests inject an install location and a fake fetcher).
    void ConfigureUpdater(update::Updater::Config config,
                          std::unique_ptr<update::UpdateFetcher> fetcher);

    // Runs one Settings import ("chrome", "firefox", "arc", ...) and returns
    // the notice the page shows.
    [[nodiscard]] std::string ImportFromBrowser(std::string_view source_id);
    // Appends Arc-style spaces of pinned tabs (URLs validated); spaces with
    // no allowed URL are skipped. Returns the number of spaces added.
    std::size_t AddImportedSpaces(const std::vector<ImportedSpace>& imported,
                                  std::size_t* tab_count = nullptr);

    // Keyboard shortcuts: the keymap (defaults + prefs overrides) becomes the
    // window's accelerators. SetKeyBinding persists and re-applies at once;
    // std::nullopt unbinds.
    void RunKeyAction(KeyAction action);
    [[nodiscard]] const Keymap& keymap() const noexcept { return keymap_; }
    bool SetKeyBinding(KeyAction action, std::optional<KeyBinding> binding);
    void ResetKeymap();

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
    void SetTabPinned(std::size_t index, bool pinned) override;
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
    void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                            const std::vector<CefString>& icon_urls) override;
    // Delivered by the favicon download started in OnFaviconURLChange.
    void OnFaviconDownloaded(TabId tab, CefRefPtr<CefImage> image);

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
    // Fixed accelerators; every configurable shortcut is registered from the
    // keymap at kKeymapAcceleratorBase + its KeyAction value.
    enum AcceleratorId {
        kReloadAccelerator = 1,  // F5
        kSelectTab1Accelerator,
        kSelectTab2Accelerator,
        kSelectTab3Accelerator,
        kSelectTab4Accelerator,
        kSelectTab5Accelerator,
        kSelectTab6Accelerator,
        kSelectTab7Accelerator,
        kSelectTab8Accelerator,
        kSelectTab9Accelerator,
        // Welcome flow: Escape dismisses ("just start browsing") while the
        // overlay is visible and is otherwise untouched.
        kWelcomeDismissAccelerator,
        kKeymapAcceleratorBase = 100,
    };

    explicit BrowserWindow(std::string initial_url, bool persist_session = true);
    // Out of line so the agent host's definition is only needed in the .cc.
    ~BrowserWindow() override;

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
    // The contract tokens for `theme`, washed with the active space's color
    // (Arc-style space theming).
    [[nodiscard]] ChromeTokens ResolvedTokens(ChromeTheme theme) const;
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
    // Appends a tab to the active space with `startup_url` (empty = the
    // startup page) and makes it active; kNewTab and OpenNewTab share it.
    void AppendTabToActiveSpace(std::string startup_url);
    void ShutdownAgentHost();
    void ApplyKeymap();
    void EnsureAgentSession();
    [[nodiscard]] agent::AgentSessionConfig AgentConfig(std::string command) const;
    [[nodiscard]] std::string AgentPanelStateJson() const;
    [[nodiscard]] std::string ActiveTabAgentContext() const;
    // Coalesces panel re-renders (streaming chunks arrive in bursts).
    void ScheduleLocalPagesRender();
    void FlushLocalPagesRender();
    void HandleAgentPageMessage(const json::Value& message);
    void HandleSettingsMessage(const json::Value& message);
    void HandleOverviewMessage(const json::Value& message);
    // Settings > Updates: the page's update messages, the state it renders,
    // the deferred startup check, and the updater's change notifications.
    [[nodiscard]] bool HandleUpdateMessage(std::string_view type, const json::Value& message);
    [[nodiscard]] json::Value UpdateStateJson() const;
    void RunStartupUpdateCheck();
    void OnUpdaterChanged();
    void HideInternalPage();
    [[nodiscard]] LocalPage* InternalPageFor(LocalPageKind kind) const;
    [[nodiscard]] json::Value ThemeJson() const;

    void CloseAgentPanelAndSession();
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
    Keymap keymap_ = Keymap::Defaults();
    BookmarkState bookmarks_;
    std::unique_ptr<WelcomeFlow> welcome_;
    // Agent integration: the MCP tools endpoint and its DevTools bridge.
    // Created in OnWindowCreated for persisted (non-smoke) windows only.
    std::unique_ptr<WindowAgentHost> agent_host_;
    AddressValidator address_validator_;
    // The sidebar agent: its panel view and the ACP session behind it.
    std::unique_ptr<LocalPage> agent_panel_;
    std::unique_ptr<LocalPage> settings_page_;
    std::unique_ptr<LocalPage> overview_page_;
    std::optional<LocalPageKind> internal_page_;
    // Set while an overview action mutates tabs, so the re-attach that
    // follows keeps the overview on screen instead of the active tab.
    bool keep_internal_page_ = false;
    // A one-shot notice for the Settings page (import results, errors).
    std::string settings_message_;
    std::unique_ptr<agent::AgentSession> agent_session_;
    std::unique_ptr<update::Updater> updater_;
    bool agent_panel_render_scheduled_ = false;
    // The window's current width in DIP; the snapshot's rail/content split is
    // derived from it (the agent panel column is not part of either).
    int window_width_dip_ = 1440;
    // The space color the chrome is currently tinted with; a space switch
    // re-tints when it changes.
    std::optional<std::uint32_t> tinted_space_color_;

    IMPLEMENT_REFCOUNTING(BrowserWindow);
    DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace island

#endif  // ISLAND_BROWSER_WINDOW_H_
