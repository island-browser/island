#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "active_tab_provider.h"
#include "address_indicators.h"
#include "address_input.h"
#include "agent_navigation.h"
#include "agent_providers.h"
#include "agent_session.h"
#include "bookmark_import.h"
#include "bookmark_store.h"
#include "browser_chrome.h"
#include "browser_command.h"
#include "browser_import.h"
#include "cef_content_blocker.h"
#include "cheatsheet_overlay.h"
#include "chrome_snapshot.h"
#include "clipboard.h"
#include "command_palette.h"
#include "command_palette_view.h"
#include "find_bar_model.h"
#include "find_bar_overlay.h"
#include "history_recorder.h"
#include "include/cef_client.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "keymap.h"
#include "local_page.h"
#include "navigation_state.h"
#include "peek_overlay.h"
#include "prefs_store.h"
#include "reader_mode.h"
#include "report_store.h"
#include "research_prompt.h"
#include "search_engine.h"
#include "search_palette.h"
#include "settings_focus.h"
#include "sidebar_state.h"
#include "space.h"
#include "space_rename_overlay.h"
#include "start_tiles.h"
#include "tab.h"
#include "tab_sleep_policy.h"
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
                      public CefDownloadHandler,
                      public CefFindHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefRequestHandler,
                      public CefResourceRequestHandler,
                      public CefContextMenuHandler,
                      public CefWindowDelegate,
                      public CefBrowserViewDelegate,
                      public BrowserChromeHost,
                      public ActiveTabProvider,
                      public SearchPaletteHost,
                      public CommandPaletteHost,
                      public SpaceRenameOverlayHost,
                      public PeekOverlayHost,
                      public FindBarOverlayHost,
                      public CheatsheetOverlayHost,
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

    // What the address bar does with typed text: a search on the configured
    // engine when it is not a URL (address_input.h), else the validator's
    // verdict on the URL (an https:// prefix is added to bare hosts).
    [[nodiscard]] ValidatedAddress ResolveAddressInput(std::string_view text) const;
    [[nodiscard]] const SearchEngine& search_engine() const noexcept {
        return SearchEngineForKey(prefs_.search_engine);
    }
    [[nodiscard]] std::string_view start_mode() const noexcept {
        return NormalizeStartMode(prefs_.start_mode);
    }
    // The data: URL of Island's own start page, which a tab with no startup
    // URL loads. Computed on first use (it needs CEF's base64 encoder).
    [[nodiscard]] const std::string& StartPageUrl() const;
    // The state the start page renders, exposed for tests.
    [[nodiscard]] std::string StartPageStateJson() const;
    // Applies one start page message ({search}, {ask_agent}, {research}, {set_start_mode},
    // {open_tile}, {ready}) coming from the tab `source` (nullptr in tests: the active tab).
    void HandleStartPageMessage(const json::Value& message, CefRefPtr<CefBrowser> source = nullptr);
    // Reloads the speed-dial tiles from the most visited history (the page's {ready}
    // message asks for it). The query runs on the history worker; the answer lands on the UI
    // thread in ApplyStartTiles, which re-renders the start pages.
    void RefreshStartTiles();
    void ApplyStartTiles(std::vector<HistoryResult> most_visited);
    // Test seam: installs the tiles a refresh would have produced.
    void SetStartTilesForTest(std::vector<StartTile> tiles) { start_tiles_ = std::move(tiles); }

    // --- Research mode (the start page's third mode) -------------------------
    // The start page shows this and drives it: it is also what a second start
    // page tab sees. `seq` changes with every transition so a page can tell a
    // fresh result from one the user already dismissed.
    struct ResearchStatus {
        std::uint64_t seq = 0;
        // idle | running | done | cancelled | failed | not_installed
        std::string state = "idle";
        std::string message;
        std::string question;
        std::string report_id;
        std::string report_title;
    };
    enum class ResearchStart : std::uint8_t {
        kStarted,
        kBusy,          // another research task or agent turn is running
        kNotInstalled,  // the selected agent is not on PATH (status carries the message)
        kFailed,        // the agent refused or could not be launched
    };
    struct ResearchStartResult {
        ResearchStart code = ResearchStart::kFailed;
        std::string message;
    };
    // Sends the research prompt (research_prompt.h) to the configured agent
    // through the panel's session and opens the panel. The agent creates the
    // research space and its tabs with the island-browser tools; the final
    // report is opened in a Report tab by FinishResearch.
    ResearchStartResult StartResearch(std::string_view question);
    // Stops the running task (the agent is asked to cancel; the outcome arrives
    // as a normal turn end).
    void CancelResearch();
    // Reads how the research turn ended and publishes it: a report becomes a
    // Reports entry plus a Report tab; a cancel or failure becomes the status.
    // Runs from a posted task after the session goes quiet; tests call it directly.
    void FinishResearch();
    void CompleteResearch(const ResearchOutcome& outcome);
    [[nodiscard]] bool research_running() const noexcept { return research_.has_value(); }
    [[nodiscard]] const ResearchStatus& research_status() const noexcept {
        return research_status_;
    }

    // --- Reports ----------------------------------------------------------------
    [[nodiscard]] const ReportState& reports() const noexcept { return reports_; }
    // Opens report `id` in a new tab, in the space named `space_name` when one
    // exists (the research space), else the active space. False for an unknown id.
    bool OpenReportTab(std::string_view id, std::string_view space_name = {});
    bool DeleteReport(std::string_view id);
    // The report page's fixed data: URL, and the URL of one report (the fixed
    // URL plus "#<id>"). Computed on first use (it needs CEF's base64 encoder).
    [[nodiscard]] const std::string& ReportPageUrl() const;
    [[nodiscard]] std::string ReportPageUrlFor(std::string_view id) const;
    // The report id a tab URL names, or std::nullopt for any other URL.
    [[nodiscard]] std::optional<std::string> ReportIdForUrl(std::string_view url) const;
    [[nodiscard]] std::string ReportPageStateJson(std::string_view id) const;
    // Applies one report page message for report `id` (taken from the tab's
    // URL, never from the message); `source` is the hosting tab (nullptr in tests).
    void HandleReportPageMessage(const json::Value& message, std::string_view id,
                                 CefRefPtr<CefBrowser> source = nullptr);
    // Test seams: where reports persist, and the report page URL (headless tests
    // cannot base64-encode).
    void SetReportsFileForTest(std::filesystem::path path);
    void SetReportPageUrlForTest(std::string url) { report_page_url_ = std::move(url); }
    // Marks a research task as running without sending anything, as if the
    // prompt had gone out after `turns_ended_before` turns.
    void SetResearchTaskForTest(std::string question, std::uint64_t turns_ended_before = 0) {
        research_ = ResearchTask{.question = question,
                                 .space_name = ResearchSpaceName(question),
                                 .turns_ended_before = turns_ended_before};
        research_status_.question = std::move(question);
        research_status_.state = "running";
    }

    // The ACP agent panel (Cmd/Ctrl+J): opens/closes the right-hand column,
    // creating the panel and its session on first use. No-op without chrome.
    void ToggleAgentPanel() override;
    void SetAgentPanelOpen(bool open);
    [[nodiscard]] bool agent_panel_open() const noexcept;
    // The agent the panel launches: ISLAND_AGENT_COMMAND when set, else the
    // selected provider's preset (or the custom command).
    [[nodiscard]] agent::ResolvedAgentCommand ResolvedAgent() const;
    [[nodiscard]] std::string ResolvedAgentCommand() const { return ResolvedAgent().command; }
    // The selected provider id (prefs `agent_provider`) and the custom
    // provider's command (prefs `agent_command`).
    [[nodiscard]] const std::string& agent_provider() const noexcept {
        return prefs_.agent_provider;
    }
    [[nodiscard]] const std::string& custom_agent_command() const noexcept {
        return prefs_.agent_command;
    }
    // Selects another provider and persists it. A running agent stops and
    // the panel starts a new chat with the new one; an idle session only
    // adopts it. False for an unknown id or while closing.
    bool SetAgentProvider(std::string_view id);
    // The agent session, once the panel or a message created it.
    [[nodiscard]] const agent::AgentSession* agent_session() const noexcept {
        return agent_session_.get();
    }
    // What the Native address field shows for the active tab (shield, Reader, agent dot).
    [[nodiscard]] const AddressIndicators& address_indicators() const noexcept {
        return address_indicators_;
    }
    // Counts one blocked request against `tab` (the resource handler resolves its browser id to
    // the tab first) and refreshes the indicators when it is the active tab.
    void RecordBlockedRequestForTab(TabId tab);
    // The state the agent panel renders, exposed for tests.
    [[nodiscard]] std::string AgentPanelStateJson() const;
    // Test seam: provider availability is searched in `dirs` instead of the
    // process PATH.
    void SetAgentSearchDirsForTest(std::vector<std::filesystem::path> dirs) {
        agent_search_dirs_for_test_ = std::move(dirs);
        provider_statuses_.reset();
    }
    void OnLocalPageMessage(LocalPageKind kind, const json::Value& message) override;

    // Built-in pages shown in the content area (Settings, the all-tabs
    // overview). Toggling the shown page hides it; selecting any tab hides it
    // too. No-op without chrome.
    void ToggleTabOverview() override;
    void ToggleSettings() override;
    // Opens Settings on one section (the Settings page names: appearance, shortcuts, ...).
    void OpenSettingsSection(std::string section);
    void OpenBlockerSettings() override;
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
    // Reopens the most recently closed tab (URL) in the active space. False
    // when nothing was closed or the window is closing.
    [[nodiscard]] bool ReopenClosedTab();
    // Stop-loading availability for the macOS menu validation. The Escape
    // variant also needs no overlay (palettes, Settings, address editing).
    [[nodiscard]] bool CanStopLoading() const;
    [[nodiscard]] bool CanStopLoadingWithEscape() const;
    // Closed-tab stack depth, for tests.
    [[nodiscard]] std::size_t closed_tab_count() const noexcept { return closed_tab_urls_.size(); }
    // Test seam: copy-URL writes go to `writer` instead of the system
    // clipboard.
    using ClipboardWriter = std::function<bool(std::string_view)>;
    void SetClipboardWriterForTest(ClipboardWriter writer) {
        clipboard_writer_ = std::move(writer);
    }

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
    // Tab sleep (see tab_sleep_policy.h). SleepInactiveTabsNow probes every tab the policy
    // allows to sleep right now and closes the ones whose page says they are idle (no unsaved
    // input, no playing media); it returns how many probes started. The timed sweep runs the
    // same path for tabs idle past the Settings timeout.
    std::size_t SleepInactiveTabsNow();
    // Puts |id| to sleep unconditionally (exclusions are the caller's job): the model keeps
    // its URL, title and favicon, the browser and view are released. False when it cannot.
    bool SleepTab(TabId id, double scroll_x = 0.0, double scroll_y = 0.0);
    [[nodiscard]] std::size_t sleeping_tab_count() const;
    [[nodiscard]] int tab_sleep_minutes() const noexcept { return prefs_.tab_sleep_minutes; }
    bool SetTabSleepMinutes(int minutes);

    // Peek (Little Arc): a floating card with its own browser over the current page.
    // OpenPeek validates |url| like any address and replaces an open peek's page.
    bool OpenPeek(std::string_view url);
    void ClosePeek();
    [[nodiscard]] bool peek_open() const noexcept { return peek_ != nullptr && peek_->open(); }
    // The ⌘/ shortcut cheatsheet. While it is open it owns Escape, ahead of a peek and of
    // stop-loading.
    [[nodiscard]] bool cheatsheet_open() const noexcept {
        return cheatsheet_ != nullptr && cheatsheet_->visible();
    }
    [[nodiscard]] const PeekOverlay* peek() const noexcept { return peek_.get(); }

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
    // Find in page: opens the find bar on the active tab (or reselects its field when it is open),
    // optionally running `query` at once. Headless windows keep only the model, so tests and
    // dev actions can drive it.
    void FindInPage(std::string_view query = {});
    [[nodiscard]] const FindBarModel& find_bar_model() const noexcept { return find_model_; }
    // Reader view: toggles the active tab between the page and Island's reader document built
    // from it. Extraction needs the DevTools bridge (persisted windows); otherwise a no-op.
    void ToggleReaderMode();
    // Builds the reader document for `tab` from the extractor's JSON (or an error page when it
    // found no article) and loads it. Returns the data: URL, empty when the tab is gone or
    // closing. Public so tests can drive the page contract without DevTools.
    std::string ShowReaderDocument(TabId tab, std::string_view origin_url,
                                   std::optional<std::string> extraction_json,
                                   std::string_view error);
    [[nodiscard]] bool ReaderActive(TabId tab) const;
    // Moves the active space by |delta| positions (-1 left, +1 right); false
    // when the move would leave the space list.
    [[nodiscard]] bool MoveActiveSpace(int delta);
    // Cmd/Ctrl+B: flips the sidebar between the full 286 DIP rail and the 52 DIP icon rail,
    // and saves the choice in the preferences.
    void ToggleSidebar();
    // Sets the persistent sidebar state (dev actions and the agent tool use this).
    void SetSidebarExpanded(bool expanded);
    // Developer-capture entry point (--island-dev-actions=appearance:<key>=<value> and
    // space_color:<index>=#RRGGBB): runs the same validated path as the Settings page.
    void ApplyAppearanceForDev(std::string_view key, std::string_view value);
    [[nodiscard]] bool sidebar_expanded() const noexcept { return sidebar_state_.expanded(); }
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
    void OnCommandPaletteQueryChanged(std::string_view query) override;

    void OnSpaceRenameCommitted(std::string name) override;
    void OnSpaceRenameCancelled() override;

    void OnPeekOpenAsTab(std::string url) override;
    void OnPeekClosed() override;
    void OnFindQueryChanged(std::string text) override;
    void OnFindStep(bool forward) override;
    void OnFindMatchCaseChanged(bool match_case) override;
    void OnFindCloseRequested() override;
    void OnCheatsheetCloseRequested(bool refocus_page) override;

    void OnWelcomeThemeChanged(ThemePreference preference) override;
    void OnWelcomeCompleted(const std::vector<ImportSource>& sources) override;

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
    CefRefPtr<CefFindHandler> GetFindHandler() override;
    // Counts for the find bar's "n of m" (only the find tab's results are used).
    void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier, int count,
                      const CefRect& selection_rect, int active_match_ordinal,
                      bool final_update) override;
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;

    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
        bool is_navigation, bool is_download, const CefString& request_initiator,
        bool& disable_default_handling) override;
    ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefCallback> callback) override;
    CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;

    bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          const CefString& target_url,
                          cef_window_open_disposition_t target_disposition,
                          bool user_gesture) override;
    void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             CefRefPtr<CefContextMenuParams> params,
                             CefRefPtr<CefMenuModel> model) override;
    bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefContextMenuParams> params, int command_id,
                              EventFlags event_flags) override;

    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
    // The start page's only channel back to native (see HandleStartPageMessage).
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                          const CefString& message, const CefString& source, int line) override;
    void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                            const std::vector<CefString>& icon_urls) override;
    // Delivered by the favicon download started in OnFaviconURLChange.
    void OnFaviconDownloaded(TabId tab, const std::string& icon_url, CefRefPtr<CefImage> image);

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                       const CefString& target_url, const CefString& target_frame_name,
                       cef_window_open_disposition_t target_disposition, bool user_gesture,
                       const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                       CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool CanDownload(CefRefPtr<CefBrowser> browser, const CefString& url,
                     const CefString& request_method) override;
    bool OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
                          const CefString& suggested_name,
                          CefRefPtr<CefBeforeDownloadCallback> callback) override;
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
    // Arc-style: on macOS the window is frameless with the traffic lights over the sidebar.
    bool IsFrameless(CefRefPtr<CefWindow> window) override;
    bool WithStandardWindowButtons(CefRefPtr<CefWindow> window) override;
    bool GetTitlebarHeight(CefRefPtr<CefWindow> window, float* titlebar_height) override;
    cef_runtime_style_t GetWindowRuntimeStyle() override;
    bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;

    void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
    void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                            CefRefPtr<CefBrowser> browser) override;
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView> browser_view) override;
    cef_runtime_style_t GetBrowserRuntimeStyle() override;
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
        // Escape: dismisses the welcome flow while it is visible, otherwise
        // stops loading when no overlay is open. It passes through otherwise.
        kEscapeAccelerator,
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
    void ApplyContentBlockerPolicy();
    [[nodiscard]] json::Value ContentBlockerStateJson() const;
    void RecordBlockedRequest(int browser_identifier);
    // Recomputes the active tab's address indicators from its blocked count, Reader availability
    // and the agent session, and pushes them to the chrome when they changed.
    void RefreshAddressIndicators();
    // Hands the current address indicators to the chrome, if it exists yet. The chrome is created
    // after early refreshes, so window setup calls this once it exists.
    void DeliverAddressIndicators();
    [[nodiscard]] bool ReaderAvailableForActiveTab() const;
    [[nodiscard]] AgentTabState AgentStateForActiveTab() const;
    // Navigates the active tab to a stored bookmark URL through the single
    // address-validation path; a URL the allow-list rejects is a no-op.
    void OpenBookmark(const PaletteBookmarkEntry& bookmark);
    void OpenHistory(const PaletteHistoryEntry& history);
    void ApplyHistoryResults(std::string query, std::vector<HistoryResult> results);
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
    // --- Find in page and Reader view (browser_window_find_reader.cc) ---
    void ApplyFindRequest(const FindRequest& request, CefRefPtr<CefBrowser> browser);
    void ShowFindOverlay();
    void CloseFindBar(bool refocus_page);
    void RefreshFindCount(bool final_update);
    [[nodiscard]] CefRect FindAnchorBounds() const;
    // Re-anchors the overlays pinned to the page card (the find bar and the cheatsheet).
    void UpdateAnchoredOverlayBounds();
    void ScheduleAnchoredOverlayBounds();
    void ToggleCheatsheet();
    // Escape, the close button and ⌘/ hand the keyboard back to the page; a click elsewhere
    // (a palette opening, a click in the page) keeps the focus it moved.
    void CloseCheatsheet(bool refocus_page);
    void StepFind(bool forward);
    [[nodiscard]] bool ReaderActiveFor(const Tab& tab) const;
    // The page address a tab should show and persist: the original page while its reader view
    // is up, else the committed URL.
    [[nodiscard]] std::string EffectiveUrl(const Tab& tab) const;
    void UpdateReaderButton();
    void ExitReaderMode(Tab& tab);
    void HandleReaderMessage(Tab& tab, const json::Value& message);
    void ApplyKeymap();
    void EnsureAgentSession();
    [[nodiscard]] agent::AgentSessionConfig AgentConfig() const;
    // Every provider with its availability, cached for a few seconds because
    // the pages re-render on every streamed chunk.
    [[nodiscard]] const std::vector<agent::AgentProviderStatus>& ProviderStatuses() const;
    // {provider, providers, command, env_override, env_command,
    //  effective_command}: what both the panel and Settings show.
    [[nodiscard]] json::Value AgentProvidersStateJson() const;
    // Shows Settings scrolled to `section` (e.g. "agent").
    void ShowSettingsSection(std::string section);
    [[nodiscard]] std::string ActiveTabAgentContext() const;
    // Coalesces panel re-renders (streaming chunks arrive in bursts).
    void ScheduleLocalPagesRender();
    void FlushLocalPagesRender();
    void HandleAgentPageMessage(const json::Value& message);
    // The panel's "send" path, shared by the start page's Ask agent mode.
    // Returns false when the session refused the turn (it is busy with another one).
    bool SubmitAgentPrompt(std::string text, bool include_context);
    // Pushes StartPageStateJson to every tab currently showing the start page
    // (and each open report page its report and theme).
    void RenderStartPages();
    void RenderReportFrame(CefRefPtr<CefFrame> frame, std::string_view id) const;
    void SetResearchStatus(std::string state, std::string message);
    void OnAgentSessionChanged();
    void SaveReports() const;
    // The "Continue in agent panel" button: opens the panel and, when its
    // conversation does not hold the report yet, hands the report to the agent.
    void ContinueReportInAgent(const ResearchReport& report, CefRefPtr<CefBrowser> source);
    void SaveReportToFile(const ResearchReport& report, CefRefPtr<CefBrowser> source);
    void ReportPageResult(CefRefPtr<CefBrowser> source, std::string_view kind,
                          std::string_view text) const;
    [[nodiscard]] json::Value ReportsListJson() const;
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
    void ApplySidebarState();
    // Frameless window: the sidebar's top band and the canvas margin above the cards drag it.
    void ApplyDragRegions();
    // Settings > Appearance: `key`/`value` come from the page and are validated here. Persists
    // the prefs and re-applies the tokens live.
    void HandleAppearanceMessage(const json::Value& message);
    void ApplyAppearance();
    bool SetSpaceColorFromHex(std::size_t space_index, std::string_view hex);
    // Rounds/shadows the page card's native views (see page_card.h); coalesced and retried because
    // a freshly attached browser view gets its native view a moment later.
    void RefreshPageCards();
    void SchedulePageCardRefresh();
    bool page_card_refresh_pending_ = false;
    std::vector<CornerPatch> corner_patches_;
    [[nodiscard]] std::vector<void*> NativeContentHandles() const;
    // Cmd/Ctrl+L while the icon rail is showing expands the sidebar for the length of the
    // edit; the end of the edit (submit, Escape, blur) collapses it again and returns the
    // keyboard to the page so focus never sits in a hidden field.
    void BeginTransientSidebarExpand();
    void EndTransientSidebarExpand();
    // Escape and blur end the edit after a short grace period, so a click on a row of the
    // temporarily expanded sidebar still lands before the layout moves under the pointer.
    void ScheduleTransientSidebarCollapse();
    void FinishTransientSidebarCollapse();
    void UpdateWindowTitle();
    void CloseNavigationAndQuitMessageLoop();
    // The URL a tab would reopen at: its committed page, else its startup URL.
    // Web URLs only; anything else reopens at the startup page.
    [[nodiscard]] std::string WebUrlOf(const Tab& tab) const;
    void RememberClosedTab(const Tab& tab);
    // True while a welcome flow, palette, rename overlay, Settings or All-tabs
    // page, or address editing owns the keyboard.
    [[nodiscard]] bool OverlayShown() const noexcept;

    // Tab sleep internals.
    struct SleepProbe {
        TabId tab;
        int browser_id = 0;
    };
    [[nodiscard]] std::vector<TabSleepInput> CollectSleepInputs() const;
    [[nodiscard]] Tab* FindTabById(TabId id) noexcept;
    bool BeginSleepProbe(Tab& tab);
    void ExpireSleepProbe(const std::string& token);
    bool HandleSleepProbeMessage(CefRefPtr<CefBrowser> browser, const std::string& text);
    void ScheduleSleepTick();
    void RunSleepTick();
    void TouchActiveTabs();
    void RestoreSavedScroll(Tab& tab, CefRefPtr<CefFrame> frame);
    void DeferPeek(const std::string& url);
    [[nodiscard]] PeekOverlay& EnsurePeek();
    [[nodiscard]] bool IsPeekLinkSource(CefRefPtr<CefBrowser> browser) const;

    std::string initial_url_;
    mutable std::string start_page_url_;
    // The start page's speed-dial tiles, refreshed from history when the page loads.
    std::vector<StartTile> start_tiles_;
    mutable std::string report_page_url_;
    // The research task in flight: what was asked, the space the agent was told
    // to create, and how many turns had ended before the prompt went out.
    struct ResearchTask {
        std::string question;
        std::string space_name;
        std::uint64_t turns_ended_before = 0;
    };
    std::optional<ResearchTask> research_;
    bool research_finish_posted_ = false;
    ResearchStatus research_status_;
    // The report whose text the agent session's conversation already holds.
    std::string report_in_session_;
    ReportState reports_;
    std::filesystem::path reports_path_;
    AddressBarModel address_bar_model_;
    std::unique_ptr<BrowserChrome> chrome_;
    std::unique_ptr<SearchPalette> search_palette_;
    std::unique_ptr<CommandPaletteView> command_palette_;
    std::unique_ptr<SpaceRenameOverlay> space_rename_overlay_;
    std::unique_ptr<PeekOverlay> peek_;
    TabSleepPolicy sleep_policy_;
    std::unordered_map<std::string, SleepProbe> sleep_probes_;
    // Identifiers of browsers closed on purpose for tab sleep: their OnBeforeClose is neither
    // a tab teardown nor, during shutdown, the window's final close.
    std::unordered_set<int> sleep_closing_browser_ids_;
    bool sleep_tick_scheduled_ = false;
    // Find in page (browser_window_find_reader.cc). The bar belongs to one tab: switching tabs
    // closes it.
    FindBarModel find_model_;
    std::unique_ptr<FindBarOverlay> find_overlay_;
    std::optional<TabId> find_tab_;
    // The ⌘/ cheatsheet card; its overlay exists only while it is shown.
    std::unique_ptr<CheatsheetOverlay> cheatsheet_;
    std::filesystem::path icon_resource_root_;
    // Reader view, per tab. The session records where the reader document came from and which
    // data: URL it is, so the window can tell when the tab is showing it (also after Back and
    // Forward) without storing the document twice.
    struct ReaderSession {
        std::string origin_url;
        std::string token;
        std::size_t url_hash = 0;
        std::size_t url_size = 0;
    };
    std::map<TabId, ReaderSession> reader_sessions_;
    SidebarState sidebar_state_;
    NavigationObserver* navigation_observer_ = nullptr;
    ChromeObserver* chrome_observer_ = nullptr;
    ChromeSnapshot chrome_snapshot_;
    AddressIndicators address_indicators_;
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
    std::unique_ptr<HistoryRecorder> history_;
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
    // A recorded chord that is already taken, waiting for Replace or Cancel
    // in Settings → Shortcuts.
    struct ShortcutConflict {
        KeyAction action;
        KeyBinding binding;
        KeyAction holder;
    };
    std::optional<ShortcutConflict> shortcut_conflict_;
    std::unique_ptr<agent::AgentSession> agent_session_;
    // Provider availability (see ProviderStatuses) and its test override.
    mutable std::optional<std::vector<agent::AgentProviderStatus>> provider_statuses_;
    mutable std::chrono::steady_clock::time_point provider_statuses_at_;
    std::optional<std::vector<std::filesystem::path>> agent_search_dirs_for_test_;
    // The Settings section requested by ShowSettingsSection; its sequence tells the page a
    // request is new even when the section name repeats.
    SettingsFocus settings_focus_;
    std::unique_ptr<update::Updater> updater_;
    CefRefPtr<CefContentBlocker> content_blocker_;
    // Most recent closed tabs, newest last (reopenable URLs), capped at
    // kClosedTabLimit.
    std::vector<std::string> closed_tab_urls_;
    ClipboardWriter clipboard_writer_ = CopyTextToClipboard;
    bool agent_panel_render_scheduled_ = false;
    // The window's current width in DIP; the snapshot's rail/content split is
    // derived from it (the agent panel column is not part of either).
    int window_width_dip_ = 1440;
    int window_height_dip_ = 900;
    // The space color the chrome is currently tinted with; a space switch
    // re-tints when it changes.
    std::optional<std::uint32_t> tinted_space_color_;

    IMPLEMENT_REFCOUNTING(BrowserWindow);
    DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace island

#endif  // ISLAND_BROWSER_WINDOW_H_
