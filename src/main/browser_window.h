#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "active_tab_provider.h"
#include "address_indicators.h"
#include "address_input.h"
#include "agent_navigation.h"
#include "agent_providers.h"
#include "agent_session.h"
#include "agent_task_queue.h"
#include "bookmark_import.h"
#include "bookmark_store.h"
#include "browser_chrome.h"
#include "browser_command.h"
#include "browser_import.h"
#include "browser_tools.h"
#include "cef_content_blocker.h"
#include "cheatsheet_overlay.h"
#include "chrome_snapshot.h"
#include "clipboard.h"
#include "collect_prompt.h"
#include "command_palette.h"
#include "command_palette_view.h"
#include "favorite_tabs.h"
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
#include "plan_edits.h"
#include "plan_graph.h"
#include "prefs_store.h"
#include "reader_mode.h"
#include "report_store.h"
#include "research_prompt.h"
#include "search_engine.h"
#include "search_palette.h"
#include "session_store.h"
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
    [[nodiscard]] bool SetSpaceIcon(std::size_t space_index, std::string icon_id);
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

    // Agent seams (src/agent tools). OpenNewTab appends a regular tab to the active
    // space and loads `text` resolved through ResolveAgentNavigation (blank
    // text opens the startup page). NavigateTab targets a tab of the active
    // space (std::nullopt = the active tab); a tab without a live browser
    // keeps the URL as its startup URL. Both return false when closing, for
    // an unknown tab, or for text that resolves to no allowed URL.
    [[nodiscard]] bool OpenNewTab(std::string_view text);
    // The MCP browser_open_tab path. During research it inserts the page below the Plan tab,
    // labels it with the running branch and remembers which plan node opened it.
    [[nodiscard]] bool OpenAgentTab(std::string_view text);
    // Sites the agent may act on without asking. Normalized (see NormalizeTrustedSites) and saved
    // with the prefs; adding a site twice or removing an unknown one changes nothing.
    void AddTrustedSite(std::string host);
    void RemoveTrustedSite(std::string_view host);
    [[nodiscard]] const std::vector<std::string>& trusted_sites() const {
        return prefs_.agent_trusted_sites;
    }
    // Agent confirmations: the card the person answers before a consequential page action. The
    // window keeps each request until it is answered, times out after 120 s or the window closes,
    // and calls `done` exactly once. The card lives in the window, not the panel, so it survives
    // closing and reopening the panel; showing one opens the panel.
    void ShowConfirmation(agent::ConfirmationRequest request,
                          std::function<void(agent::ConfirmationAnswer)> done);
    // Answers a pending confirmation (unknown ids are ignored). "Trust this site" saves the host
    // when the card offered it; a trust or task answer to a card that did not offer them is
    // treated as a cancel.
    void AnswerConfirmation(std::string_view id, agent::ConfirmationAnswer answer);
    // Shorter than the HTTP handler budget, so an external caller always gets the card's own
    // timeout message rather than a 504.
    static constexpr std::chrono::seconds kConfirmationTimeout{120};
    [[nodiscard]] bool HasPendingConfirmation() const { return !pending_confirmations_.empty(); }
    // Moves the confirmation clock forward and fires the timeouts that came due.
    void AdvanceConfirmationClockForTest(std::chrono::seconds elapsed);

    // Agent tools (window_agent_host.cc). In ordinary chat the agent works with the window the
    // person sees: indices and calls without an index refer to the active space and tab. While a
    // research runs, the agent works in the Plan's space: indices refer to that space, and a call
    // without an index acts on the agent's own most recent page (never the person's tab).
    enum class AgentTabError { kNone, kNoAgentPage, kAgentPageClosed, kNoSuchTab };
    struct AgentTabRef {
        std::size_t space = 0;
        std::size_t tab = 0;
    };
    struct AgentTabLookup {
        std::optional<AgentTabRef> tab;
        AgentTabError error = AgentTabError::kNone;
        // True when the lookup resolved inside a running research's Plan space (the agent's pages).
        bool research = false;
    };
    [[nodiscard]] std::size_t AgentSpaceIndex() const;
    [[nodiscard]] AgentTabLookup ResolveAgentTab(std::optional<std::size_t> index) const;
    [[nodiscard]] bool IsAgentPlanTab(std::optional<std::size_t> index) const;
    [[nodiscard]] CefRefPtr<CefBrowser> AgentBrowser(AgentTabRef ref) const;
    [[nodiscard]] bool NavigateAgentTab(std::optional<std::size_t> index, std::string_view text);
    // Activating brings the agent's page to the front, switching the person to its space.
    [[nodiscard]] bool ActivateAgentTab(std::size_t index);
    // Closing is allowed only in the space on screen: the person's view is never changed to close.
    [[nodiscard]] bool CloseAgentTab(std::size_t index);
    [[nodiscard]] bool SetAgentTabPinned(std::size_t index, bool pinned);
    // Refuses (false) to navigate the Plan tab: it is Island's own page, not a web page.
    [[nodiscard]] bool NavigateTab(std::optional<std::size_t> index, std::string_view text);
    // The URL NavigateTab would load for `text`, or nullopt when it resolves to no allowed one.
    [[nodiscard]] std::optional<std::string> ResolveNavigationTarget(std::string_view text) const;
    // True when the tab of the active space (nullopt = the active tab) is the research Plan tab.
    [[nodiscard]] bool IsPlanTabIndex(std::optional<std::size_t> index) const;
    // The session as SaveSession would write it.
    [[nodiscard]] SessionState BuildSessionState() const;
    // Restores an already-loaded session through the production filtering path.
    // Tests use this to cover legacy internal-page entries without touching HOME.
    [[nodiscard]] bool RestoreSessionStateForTest(const SessionState& state);
    // The live CefBrowser of a tab of the active space, or nullptr.
    [[nodiscard]] CefRefPtr<CefBrowser> BrowserForTab(std::optional<std::size_t> index) const;
    // kNewSpace plus an optional name. `opened_by_agent` marks the space's start tab, which the
    // agent creates through NewSpace (see Tab::opened_by_agent).
    [[nodiscard]] bool CreateSpace(std::string_view name, bool opened_by_agent = false);
    // Pins/unpins a tab of the active space (Arc-style pinned group).
    [[nodiscard]] bool SetActiveSpaceTabPinned(std::size_t index, bool pinned);
    [[nodiscard]] const std::vector<Space>& spaces() const noexcept { return spaces_; }
    // Test seam: headless tests have no CefParseURL, so they inject the
    // address validation the agent seams use.
    void SetAddressValidatorForTest(AddressValidator validator) {
        address_validator_ = std::move(validator);
    }
    // Replaces the bookmark library the way the constructor loads it: its space IDs are reserved
    // before any space can be created.
    void ReplaceBookmarkLibraryForTest(BookmarkStore library);
    // Starts a favicon download for |tab| as OnFaviconURLChange does, then delivers |image|.
    void CompleteFaviconDownloadForTest(TabId tab, const std::string& icon_url,
                                        CefRefPtr<CefImage> image);

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
    void SetStartPageUrlForTest(std::string url) { start_page_url_ = std::move(url); }
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
        kBusy,          // research cannot be queued now (the window is closing)
        kQueued,        // another agent task is live; the research starts when it finishes
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
    // --- Plan tab (Plan mode, native-look spec 4.6) ----------------------------
    // The Plan tab is a tab whose document is plan.html at the fixed plan URL. The window
    // holds the plan (the newest plan the research agent sent), the person's edits and the
    // view; plan.html shows them and sends its edits back as messages.
    // Applies one plan page message from the tab `source` (nullptr in tests).
    void HandlePlanPageMessage(const json::Value& message, CefRefPtr<CefBrowser> source = nullptr);
    // The state plan.html renders, exposed for tests.
    [[nodiscard]] std::string PlanStateJson() const;
    // Developer capture: opens a Plan tab that shows the sample plan waiting for approval.
    void OpenPlanFixtureTab(bool include_child_tabs = false);
    // Developer capture only (`agent_fixture:confirm`): a sample confirmation card in the panel.
    void ShowConfirmationFixtureForDev();
    // Pushes the provider name, activity and shortcut keycap to the sidebar's Agent entry.
    void RefreshAgentEntry();
    // Opens `count` agent pages, one UI task each, as an agent calling browser_open_tab in a row
    // does (developer capture `agent_pages:<count>[:<gap>]`). `gap_ms` 0 posts every task at once;
    // a positive gap delays each next page by that many milliseconds; kSerialAgentPages posts the
    // next page only after the previous task returned, as the serial stdio MCP bridge does.
    static constexpr int kSerialAgentPages = -1;
    void OpenAgentPagesInQuickSuccessionForDev(int count, int gap_ms = 0);
    void OpenAgentPageStepForDev(int index, int count, int gap_ms);
    // How many times the active tab's view was attached; tests assert one swap per action.
    [[nodiscard]] std::size_t AttachCountForTest() const noexcept { return attach_count_; }
    // View swaps actually made (attaches the gate let through) and whether one is waiting for an
    // attached view to reach the minimum browser age.
    [[nodiscard]] std::size_t ViewSwapCountForTest() const noexcept { return view_swap_count_; }
    [[nodiscard]] bool ViewSwapPendingForTest() const noexcept { return view_swap_pending_; }
    // Space-icon picker: shows counted before the palette guard, so headless tests see them.
    [[nodiscard]] std::size_t SpaceIconPickerShowsForTest() const noexcept {
        return space_icon_picker_shows_;
    }
    [[nodiscard]] bool SpaceIconPickerScheduledForTest() const noexcept {
        return space_icon_picker_scheduled_;
    }
    void RunScheduledSpaceIconPickerForTest() { RunScheduledSpaceIconPicker(); }
    // Headless windows have no CEF task queue: scheduling records the task without posting it.
    void DisableUiTaskPostingForTest() { ui_tasks_enabled_for_test_ = false; }
    // Real-gate seams: headless tests identify the tab represented by the content slot and drive
    // the same creation-age, generation and fallback callbacks production uses.
    void SetViewSwapTimeForTest(std::chrono::milliseconds since_epoch);
    void AdvanceViewSwapTimeForTest(std::chrono::milliseconds elapsed);
    void SetAttachedViewForSwapTest(TabId tab);
    void SetAttachedViewsForSwapTest(std::vector<TabId> tabs);
    void SetAttachedInternalPageForSwapTest(LocalPageKind kind);
    void ToggleInternalPageForSwapTest(LocalPageKind kind);
    void BrowserCreatedForViewSwapTest(TabId tab);
    [[nodiscard]] std::uint64_t AttachedViewGenerationForTest() const noexcept {
        return attached_view_generation_;
    }
    void RecheckPendingViewSwapForTest(std::uint64_t generation);
    void RunUncreatedViewFallbackForTest(std::uint64_t generation);
    [[nodiscard]] std::size_t BrowserViewSizeChangeCountForDev() const noexcept;
    [[nodiscard]] std::size_t ChromeRelayoutCountForDev() const noexcept;
    // Test seams: the plan page URL, the agent send path (research and plan turns go through
    // it instead of the agent session), and the plan shown without an agent turn.
    void SetPlanPageUrlForTest(std::string url) { plan_page_url_ = std::move(url); }
    // Makes a tab the agent's most recent page, as if the agent had opened it.
    void TargetAgentPageForTest(std::size_t space, std::size_t tab) {
        agent_tab_id_ = spaces_[space].tabs()[tab].id();
    }
    void SetAgentSendForTest(std::function<bool(std::string)> send) {
        agent_send_for_test_ = std::move(send);
    }
    // Ends the turn of the task the window started last, as the session would (chat tasks).
    void FinishAgentTurnForTest(bool ok) { FinishInFlightTurn(ok); }
    // Makes the session count as busy (or idle) for the queue; unset follows the real session.
    void SetAgentBusyForTest(bool busy) { agent_busy_for_test_ = busy; }
    void CheckInFlightTurnForTest() { CheckInFlightTurn(); }
    bool SubmitAgentPromptForTest(std::string text) {
        return SubmitAgentPrompt(std::move(text), /*include_context=*/false);
    }
    [[nodiscard]] const agent::AgentTaskQueue& agent_queue() const noexcept { return agent_queue_; }
    // "Ask the agent from context": the page chip above the composer, optionally carrying the
    // text that was selected. Cleared by the chip's ✕, a send, a new chat, a tab change or closing
    // the panel.
    struct AgentContextChip {
        std::string title;
        std::string url;
        std::string selection;

        bool operator==(const AgentContextChip&) const = default;
    };
    [[nodiscard]] const std::optional<AgentContextChip>& agent_context_chip() const noexcept {
        return context_chip_;
    }
    // Queues an ask task with `context` (agent_context.h; empty for none). False when the agent
    // could not take it.
    bool AskAgent(std::string prompt, std::string context);
    // A palette command by id (the palette view routes its selection here).
    void RunPaletteCommand(std::string_view id);
    // One of the context menu's agent items (offsets from agent_context.h) for the tab at
    // `browser`'s page, `selection` and `link`. False when the item is not an agent item.
    bool RunAgentMenuCommand(int offset, const AgentContextChip& page, std::string_view link);
    // Dev capture (`agent_fixture:queue`): one running and two queued tasks, nothing sent.
    void ShowQueueFixtureForDev();
    // Dev capture (`agent_fixture:quote`): the panel with a page chip and a selection quote.
    void ShowQuoteFixtureForDev();
    // Dev capture (`agent_fixture:collect`): the Collect sheet open over the panel.
    void ShowCollectFixtureForDev();
    // Dev capture (`agent_fixture:thread`): a sample transcript (a user message, three tool rows,
    // a Markdown reply) and one queued message, nothing sent.
    void ShowThreadFixtureForDev();
    // Dev capture (`report_fixture:collection`): a collection Report tab with its Export menu open.
    void ShowCollectionReportFixtureForDev();
    // Queues a collection of the web tabs the sheet showed, for `what` (blank or no web tab:
    // refused). A choice is taken only when the active space still holds its URL, at its index or,
    // if the tabs moved, at another index; a closed or changed tab is left out. False when the
    // agent could not take it.
    bool CollectFromTabs(std::string_view what, const std::vector<CollectChoice>& choices);
    // Stores `markdown` as a collection report and opens its Report tab. False when it is blank.
    bool CompleteCollect(std::string_view question, std::string_view markdown);
    // Test seam: CSV and JSON exports are written to `path` instead of through the save dialog.
    void SetExportPathForTest(std::filesystem::path path) {
        export_path_for_test_ = std::move(path);
    }
    // The command palette's command list (the window's own commands).
    [[nodiscard]] static std::vector<PaletteCommandEntry> PaletteCommands();
    void SetPlanCloseConfirmationForTest(
        std::function<bool(std::string_view, std::string_view, std::string_view)> confirm) {
        plan_close_confirmation_for_test_ = std::move(confirm);
    }
    void SetPlanEntriesForTest(std::vector<agent::AcpPlanEntry> entries) {
        plan_entries_ = std::move(entries);
    }
    bool SetTabAddressForTest(std::size_t space, std::size_t tab, std::string url) {
        if (space >= spaces_.size() || tab >= spaces_[space].tab_count()) {
            return false;
        }
        spaces_[space].tabs()[tab].navigation_state().SetAddress(std::move(url));
        return true;
    }
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
    void OnLocalPageBrowserCreated(LocalPageKind kind) override;

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
    [[nodiscard]] const AddressBarSnapshot& address_bar_snapshot() const noexcept {
        return address_bar_model_.snapshot();
    }
    [[nodiscard]] const ChromeSnapshot& chrome_snapshot() const noexcept;
    [[nodiscard]] ChromeViewTreeNode chrome_view_tree_snapshot() const;
    // Favorites (0.9). A scope is std::nullopt for the global strip or a space's SpaceId value.
    // Every bookmark mutation runs through one helper: a failure is kept as the sidebar status
    // with its retry, and a success clears it.
    BookmarkResult AddFavoriteByUrl(FavoriteScope scope, std::string_view title,
                                    std::string_view url);
    BookmarkResult RemoveFavorite(FavoriteScope scope, std::uint64_t bookmark_id);
    // Runs the last failed bookmark mutation again. With no retryable failure it returns an error.
    BookmarkResult RetryLastBookmarkMutation();
    void DismissBookmarkStatus();
    // Shows the favorite's virtual tab in the focused content slot. The selected ordinary tab is
    // unchanged and returns when the virtual tab closes. False when the favorite is not in that
    // list, or it is a scoped list of a space that is not the active one (global favorites are
    // reachable from any space).
    bool ActivateFavorite(FavoriteScope scope, std::uint64_t bookmark_id);
    // Closes the displayed virtual tab: its record returns to the favorite URL and the content slot
    // shows the selected ordinary tab again. Does nothing when no favorite is displayed.
    void CloseFavoriteTab();
    [[nodiscard]] std::optional<FavoriteTabKey> displayed_favorite() const noexcept {
        return displayed_favorite_;
    }
    // The tab the person sees and that page commands, the address field and the agent target: the
    // displayed favorite's virtual tab, else the selected ordinary tab. Empty without a tab.
    [[nodiscard]] std::optional<TabId> shown_tab_id() const noexcept;
    [[nodiscard]] const FavoriteTabRecord* FavoriteTabRecordFor(const FavoriteTabKey& key) const {
        return favorite_tabs_.Find(key);
    }
    // The sidebar collection for the current state: global favorites, the active space's
    // favorites, its tab rows and the bookmark status. Built on demand; UpdateChromeCollections
    // pushes it to the chrome.
    [[nodiscard]] SidebarCollectionSnapshot BuildSidebarCollection() const;
    [[nodiscard]] const BookmarkStore& bookmarks() const noexcept { return bookmarks_; }

    void RequestClose();
    // Opens the Cmd/Ctrl+K search palette, creating it on first use. Public so the
    // macOS main menu can reach it: CefWindow::SetAccelerator never dispatches on
    // macOS, where NSMenu key equivalents own the command keys.
    void ShowSearchPalette();
    // Opens the Cmd/Ctrl+Shift+K command palette (tabs, spaces, go-to-URL),
    // creating it on first use. The Phase 3 design fixes Cmd/Ctrl+K on the
    // command palette; the later search palette keeps Cmd/Ctrl+Shift+K.
    void ShowCommandPalette();
    // Reopens the palette listing only the space icons (and None) for the active space.
    void ShowSpaceIconPicker();
    // Queues ShowSpaceIconPicker on the next UI task. The palette's own submit runs inside its
    // key/button dispatch and has just released its overlay, so the picker must not re-show
    // the palette there.
    void ScheduleSpaceIconPicker();
    void RunScheduledSpaceIconPicker();
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
    void SetAddressFocusRingSuppressed(bool suppressed) override;
    // What the last address focus change repainted: nothing, the three focus colors only, or a
    // full window re-theme (never, since focus changes no longer re-theme the window).
    enum class AddressFocusRepaint { kNone, kColorsOnly, kFullTheme };
    [[nodiscard]] AddressFocusRepaint LastAddressFocusRepaintForTest() const noexcept {
        return address_focus_repaint_;
    }
    void ApplyAddressFocusColors(CefRefPtr<CefWindow> window, const ChromeTokens& tokens) const;
    // True while the welcome or the command palette is shown (both paint the neutral ring).
    [[nodiscard]] bool OverlayFocusRingShown() const;
    // Hides or shows the attached page views for the welcome (Native only); called on every
    // attach and on every overlay focus-ring transition.
    void ApplyPageViewVisibility();
    void CancelAddressEditing() override;
    void SubmitAddressDraft(std::string_view draft) override;
    void FocusBrowserView() override;
    void SetTabPinned(std::size_t index, bool pinned) override;
    // Bookmarks 0.9 sidebar collection (BrowserChromeHost). OpenMoreGlobalFavorites is a stub until
    // the Bookmarks page exists; it logs and does nothing.
    void ActivateFavoriteFromSidebar(FavoriteScope scope, std::uint64_t bookmark_id) override;
    void OpenMoreGlobalFavorites() override;
    void AddCurrentPageToGlobalFavorites() override;
    void RetryFailedBookmarkMutation() override;
    void DismissFailedBookmarkStatus() override;
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
    void OnOverlayFocusRingChanged() override;

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
    // The displayed virtual tab when one is shown, else active_tab(). Page commands, find, the
    // address field and the agent read this; tab selection, sessions and sleep keep active_tab().
    [[nodiscard]] Tab* DisplayedTab() noexcept;
    [[nodiscard]] const Tab* DisplayedTab() const noexcept;
    // Clears the navigation observer of both candidate tabs, so only the tab on screen reports.
    void ClearShownTabObservers();
    // True when a favicon finished for |id| changes what the sidebar shows: an ordinary tab of the
    // active space, or a virtual tab in global scope or the active space's scope.
    [[nodiscard]] bool FaviconAffectsCollections(TabId id) const noexcept;
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

    struct PendingConfirmation {
        agent::ConfirmationRequest request;
        std::function<void(agent::ConfirmationAnswer)> done;
        std::chrono::steady_clock::time_point deadline;
        // The tab the card was raised for (identity, not position), its space, and its origin.
        TabId tab_id;
        SpaceId space_id;
        std::string origin;
    };
    [[nodiscard]] std::chrono::steady_clock::time_point ConfirmationNow() const;
    // Removes the entry at `index` and reports `answer` to its callback.
    void FinishConfirmation(std::size_t index, agent::ConfirmationAnswer answer);
    void CancelPendingConfirmations();
    // Answers kWithdrawn to every card whose target is no longer the tab it was raised for: closed,
    // moved to another index or space, or navigated to another origin.
    void CancelStaleConfirmations();
    [[nodiscard]] bool ConfirmationTargetIsCurrent(const PendingConfirmation& pending) const;
    void ExpireConfirmations();
    void ArmConfirmationTimer();
    void OnConfirmationTick();
    // After the pending list changed: indicators, the Agent button dot, the panel's state.
    void RefreshConfirmationUi();
    [[nodiscard]] json::Value ConfirmationsStateJson() const;
    [[nodiscard]] json::Value TrustedSitesJson() const;
    void HandleConfirmMessage(const json::Value& message);
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
    // A tab view that was attached is never removed until its browser exists and is at least
    // 100 ms old. Removing one about 10 ms after creation crashed CEF; the age margin is
    // independent of navigation or commit. A generation-guarded delayed task rechecks at the
    // age boundary. The 2 s cap applies only if that generation's browser is never created.
    // The model and address projection still follow every selection immediately.
    struct AttachedView {
        CefRefPtr<CefBrowserView> view;
        std::optional<TabId> tab;
        LocalPage* local_page = nullptr;
        // A logical local-page attachment for the headless view-swap tests. Production
        // attachments always use local_page instead.
        std::optional<LocalPageKind> local_page_for_test;
        std::optional<std::chrono::steady_clock::time_point> browser_created_at_for_test;
        // The view sits in the split's second slot (BrowserChrome::split_view_).
        bool second_slot = false;
    };
    using ViewSwapClock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kMinimumAttachedViewAge{100};
    static constexpr std::chrono::milliseconds kUncreatedViewFallbackDelay{2000};
    [[nodiscard]] ViewSwapClock::time_point ViewSwapNow() const;
    [[nodiscard]] Tab* TabForAttachedView(const AttachedView& attached) noexcept;
    [[nodiscard]] bool AttachedViewWillRemain(const AttachedView& attached) const;
    [[nodiscard]] bool AttachedViewIsLive(const AttachedView& attached);
    [[nodiscard]] bool AttachedViewBrowserWasCreated(const AttachedView& attached);
    [[nodiscard]] std::optional<ViewSwapClock::duration> AttachedViewAgeAt(
        const AttachedView& attached, ViewSwapClock::time_point now);
    [[nodiscard]] bool AttachedBrowserStillCreating();
    void SetAttachedViews(std::vector<AttachedView> attached);
    void SchedulePendingViewSwap();
    void RunUncreatedViewFallback(std::uint64_t generation);
    void RunPendingViewSwap(std::uint64_t generation);
    void RequestViewSwap();
    void FocusAttachedContent();
    // Selects `tab` of `space` by changing the model first and attaching its view once, so a
    // cross-space activation never creates a view that the next swap removes within the task.
    void SelectTabWithOneAttach(std::size_t space, std::size_t tab);
    void AttachActiveTabBrowserView();
    void UpdateChromeCollections();
    [[nodiscard]] static Space CreateDefaultSpace();
    // Appends a tab to the active space with `startup_url` (empty = the
    // startup page) and makes it active; kNewTab and OpenNewTab share it.
    // `opened_by_agent` marks the tab as one the agent opened (see Tab::opened_by_agent).
    void AppendTabToActiveSpace(std::string startup_url, bool opened_by_agent = false);
    // Adds an empty space (next palette color) at the end and returns it; the caller gives it
    // a tab and makes it active.
    Space& AppendSpace(std::string name);
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
    // The active tab as a chip (empty url when it is not a web page).
    [[nodiscard]] AgentContextChip ActiveTabChip() const;
    // Opens the panel with the chip set and the composer focused.
    void ShowAgentContextChip(AgentContextChip chip);
    // Coalesces panel re-renders (streaming chunks arrive in bursts).
    void ScheduleLocalPagesRender();
    void FlushLocalPagesRender();
    void HandleAgentPageMessage(const json::Value& message);
    // The panel's "send" path, shared by the start page's Ask agent mode.
    // Returns false when the session refused the turn (it is busy with another one).
    // Queues the prompt as a chat task (it starts at once when nothing else is live). Returns
    // false when the prompt is blank or the agent could not take it.
    bool SubmitAgentPrompt(std::string text, bool include_context);
    // Queue plumbing. StartQueuedTask is the queue's StartFn: it sends one task's prompt.
    void StartQueuedTask(const agent::AgentTask& task);
    // Stops the live task: cancels the turn, withdraws its confirmation cards and lets the
    // queue move on (after the stopped turn's end when one is still in flight).
    void StopAgentTask();
    // Reports the end of the turn started for the live chat task (ok: it was not an error).
    void FinishInFlightTurn(bool ok);
    // Compares the session with the in-flight chat turn after a session change.
    void CheckInFlightTurn();
    void FinishPostedTurn();
    // The body of a research task start (the queue's StartFn for kResearch).
    bool StartResearchTask(const agent::AgentTask& queued);
    void EndResearchTask(const std::string& task_id, bool ok);
    // For paths that drop the live turn without an end event (new chat, agent switch, teardown):
    // the live task is cancelled and the queue moves on at once.
    // Drops the live task without a turn end; `reset_session` replaces the conversation first
    // (while no turn is in flight, so its events report nothing), then the queue advances.
    void AbandonInFlightTurn(const std::function<void()>& reset_session = {});
    [[nodiscard]] bool AgentTurnOpen() const;
    // Ends the research that runs as the live task. With `turn_open` the turn's end completes
    // it; otherwise it is dropped now.
    void EndLiveResearch(bool turn_open);
    void StartDeferredTask();
    void PostStartDeferredTask();
    [[nodiscard]] bool OutsideResearchOwnsTurn() const;
    [[nodiscard]] std::optional<agent::TaskKind> TaskKindOf(std::string_view id) const;
    void ReportQueueProgress();
    // Edge-triggered OnWaiting: a card is open or the research waits for approval.
    void SyncQueueWaiting();
    void HandleTaskMessage(std::string_view type, const json::Value& message);
    [[nodiscard]] json::Value TasksStateJson() const;
    void RefreshAgentButtonLabel();
    // Pushes StartPageStateJson to every tab currently showing the start page
    // (and each open report page its report and theme).
    void RenderStartPages();
    void RenderReportFrame(CefRefPtr<CefFrame> frame, std::string_view id) const;
    // The plan page's fixed data: URL, and the Plan tab's rendering.
    [[nodiscard]] const std::string& PlanPageUrl() const;
    [[nodiscard]] bool IsPlanPageUrl(std::string_view url) const;
    void RenderPlanFrame(CefRefPtr<CefFrame> frame) const;
    // Pushes the plan state to every open Plan tab.
    void RenderPlanPages();
    // Shows the research's Plan tab: reuses it (and closes other Plan tabs) or opens it in the
    // active space, pinned and first.
    void OpenPlanTab();
    // Switches to the space named `name` (the newest one), or creates it with the Plan tab as
    // its only tab. Island makes the research space itself so the Plan hub and every page the
    // agent opens share one space.
    void OpenResearchSpace(const std::string& name);
    // Adds one browser page or result below the Plan tab and records the node -> tab mapping.
    // A background child (`activate` false) changes neither the selection nor the content view.
    // If another space is active, the child stays grouped in the Plan's space in the background.
    // Without a tracked Plan tab it opens as an ordinary tab in the active space.
    [[nodiscard]] TabId AppendPlanChildTab(std::string startup_url, std::string node_id,
                                           std::string label, bool activate);
    // The part of AppendPlanChildTab that needs the Plan's space: an activated child makes that
    // space active and is selected with one attach, wherever the person was. `opened_by_agent`
    // marks the child: true for a research page, false for the Report (the person's result).
    TabId AppendChildOfPlan(std::size_t space_index, std::string startup_url, std::string node_id,
                            std::string label, bool activate, bool opened_by_agent);
    // True while a research runs with its Plan tab in a space: the agent's space is then the
    // Plan's.
    [[nodiscard]] bool ResearchOwnsAgent() const;
    [[nodiscard]] std::optional<agent::PlanNode> RunningPlanNode() const;
    // The address a new tab opens (empty text: the start page), or nullopt when the text is not
    // an address Island allows.
    [[nodiscard]] std::optional<std::string> ResolveNewTabTarget(std::string_view text) const;
    // Plan close is asynchronous in the real page and synchronous through the test hook.
    [[nodiscard]] bool ConfirmPlanTabClose(TabId id);
    void ShowPlanCloseConfirmation();
    void CompletePlanCloseConfirmation(bool stop);
    void ForgetPlanTabMapping(TabId id);
    // Drops a duplicate Plan document without adding it to Reopen Closed Tab. A one-tab space
    // receives a fresh start tab instead of disappearing.
    void RemoveStalePlanTabAt(std::size_t space, std::size_t tab);
    // The space and tab index of `id`, when a tab still has it.
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> TabLocation(TabId id) const;
    // The view Plan mode shows: the person's choice, else Graph when some entry carries
    // `_meta.island` and List for a plain ACP plan.
    [[nodiscard]] std::string PlanView() const;
    // Takes the newest plan from the agent transcript (when it has one) and keeps the edits.
    void RefreshPlanFromTranscript();
    // Sends one user turn to the research agent (through the test seam when set).
    bool SendToAgent(std::string text, std::string context = {});
    // Sends the plan turn of a Run, a branch, or an expand: only while the plan waits for
    // approval, and it becomes the turn the research classifies next. `approves_plan` is true
    // for a Run, whose turn may end with a plain report. `continues_run` (a failed-step retry)
    // is not a plan edit: it skips the approval gate, keeps the approval the run had, and starts
    // a task when none is in flight.
    void SendPlanTurn(std::string text, bool approves_plan = false, bool continues_run = false);
    // Turns the agent session has ended so far (0 without a session).
    [[nodiscard]] std::uint64_t TurnsEnded() const;
    void SetResearchStatus(std::string state, std::string message);
    void OnAgentSessionChanged();
    void SaveReports() const;
    bool OpenReportTabImpl(std::string_view id, std::string_view space_name, bool group_with_plan);
    // The "Continue in agent panel" button: opens the panel and, when its
    // conversation does not hold the report yet, hands the report to the agent.
    void ContinueReportInAgent(const ResearchReport& report, CefRefPtr<CefBrowser> source);
    void SaveReportToFile(const ResearchReport& report, CefRefPtr<CefBrowser> source);
    // The Export menu: "md" saves the Markdown, "csv" and "json" save the report's first table.
    void ExportReport(const ResearchReport& report, std::string_view format,
                      CefRefPtr<CefBrowser> source);
    // The task the in-flight collect turn ended with: stores its table as a report when the turn
    // produced one. Called before the queue moves on.
    void FinishCollectTurn(std::string_view task_id);
    [[nodiscard]] json::Value CollectSheetStateJson() const;
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
    void UpdateAddressBarForNavigation(const NavigationSnapshot& snapshot);
    void UpdateAddressBarForCurrentPage();
    [[nodiscard]] json::Value ThemeJson() const;

    void CloseAgentPanelAndSession();
    // Session restore: rebuilds spaces, tabs, active selections, and split
    // pairings from the session file. Returns false and leaves the model in
    // its fresh-install shape for every non-recoverable outcome (missing,
    // unreadable, or schema-invalid file), which the caller treats as the
    // fixed-startup-page fallback.
    bool RestoreSession();
    bool RestoreSessionState(const SessionState& state);
    [[nodiscard]] bool IsSessionInternalTabUrl(std::string_view url) const;
    // Writes BuildSessionState() for the next launch. Clean quit only — no
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
    // The one path for bookmark mutations (see the favorites methods above). Reconciles virtual
    // tabs with the library afterwards and reprojects the sidebar.
    BookmarkResult RunBookmarkMutation(std::function<BookmarkResult()> mutation);
    // Disposes the records and virtual tabs of favorites the library no longer lists. Returns true
    // when the displayed virtual tab was one of them, so the caller re-attaches the content slot.
    bool ReconcileFavoriteTabs();
    [[nodiscard]] bool FavoriteListContains(const FavoriteTabKey& key) const;
    // Destroys a virtual tab and closes its browser. Its record is left to the caller.
    void DropVirtualTab(const FavoriteTabKey& key);
    [[nodiscard]] Tab* FindVirtualTab(const FavoriteTabKey& key) noexcept;
    // Returns the content slot to the selected ordinary tab when a virtual tab is displayed.
    void ReturnToOrdinaryTab();
    // Closes every CefBrowser owned by |space|'s tabs. Must run after the
    // model mutation, never while iterating spaces_.
    void CloseSpaceBrowsers(Space& space);
    // Palette routing: the selection is resolved against the current model by
    // identity (TabId/SpaceId), never by the position it was listed at.
    void ActivatePaletteTab(TabId id);
    void ActivatePaletteSpace(SpaceId id);
    void SubmitPaletteUrl(std::string_view query, const ValidatedAddress& address);
    void ApplyTheme(CefRefPtr<CefWindow> window, ChromeTheme theme, bool notify_views);
    // Sets every space's web color mode (prefers-color-scheme) from the theme preference.
    void ApplyWebColorScheme();
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
    mutable std::string plan_page_url_;
    // The newest plan of the research agent, the person's edits to it, and the view
    // (graph | list) and selected node the Plan tab shows.
    std::vector<agent::AcpPlanEntry> plan_entries_;
    agent::PlanEdits plan_edits_;
    // The view the person picked (graph | list); unset means PlanView() decides.
    std::optional<std::string> plan_view_;
    std::string plan_selected_;
    // The Plan tab of the current research (one per research).
    std::optional<TabId> plan_tab_;
    // Plan node ids come from BuildPlanGraph, matching the ids plan.html posts.
    std::map<std::string, TabId> plan_node_tabs_;
    // True while the command palette is the space-icon picker (no tabs, no history rows).
    bool space_icon_picker_open_ = false;
    // The agent's most recent page during a research (runtime only; never persisted).
    std::optional<TabId> agent_tab_id_;
    bool space_icon_picker_scheduled_ = false;
    bool ui_tasks_enabled_for_test_ = true;
    std::size_t space_icon_picker_shows_ = 0;
    bool closing_plan_after_confirmation_ = false;
    // Why the last plan action was refused (depth limit, agent busy); empty when none.
    std::string plan_notice_;
    std::function<bool(std::string)> agent_send_for_test_;
    std::function<bool(std::string_view, std::string_view, std::string_view)>
        plan_close_confirmation_for_test_;
    // The research task in flight: what was asked, the space the agent was told
    // to create, and how many turns had ended before the prompt went out.
    struct ResearchTask {
        std::string question;
        std::string space_name;
        std::uint64_t turns_ended_before = 0;
        // The plan was proposed and nothing runs until the person approves it. The next turn
        // is the one a Run starts, so turns_ended_before is reset to the current count.
        bool awaiting_approval = false;
        // The turn in flight is a Run of the approved plan (see SendPlanTurn).
        bool plan_approved = false;
        // The queue task this research runs as ("" when none, as in tests that set it directly).
        std::string task_id;
        // Stop arrived while the turn was open: whatever that turn ends with is the end of
        // the research (a cancel the agent did not honor in time must not wait for approval).
        bool stopped = false;
    };
    std::optional<ResearchTask> research_;
    bool research_finish_posted_ = false;
    std::size_t attach_count_ = 0;
    // The plan on show was proposed and the person has not approved it (set when it starts to
    // wait, cleared by an approving Run); a cancelled wait keeps it so Retry cannot start a run.
    bool plan_unapproved_ = false;
    std::size_t view_swap_count_ = 0;
    bool view_swap_pending_ = false;
    std::optional<std::uint64_t> view_swap_pending_generation_;
    std::optional<std::uint64_t> fallback_timer_generation_;
    std::optional<std::uint64_t> uncreated_fallback_generation_;
    // The tab views in the content slot (one, or two in split view), as last attached. Every
    // replacement advances the generation so delayed work cannot act on a newer attachment.
    std::vector<AttachedView> attached_views_;
    std::uint64_t attached_view_generation_ = 0;
    std::optional<ViewSwapClock::time_point> view_swap_time_for_test_;
    bool attached_view_for_test_ = false;
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
    bool address_focus_ring_suppressed_ = false;
    AddressFocusRepaint address_focus_repaint_ = AddressFocusRepaint::kNone;
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
    // In-memory until the persisted path is set in the constructor (headless and smoke shapes
    // never touch bookmarks.json).
    BookmarkStore bookmarks_;
    // Virtual ("phantom") favorite tabs: live Tabs kept outside spaces_, one per favorite that has
    // been activated, keyed like their session records. Their TabIds are fresh each run.
    std::map<FavoriteTabKey, Tab> virtual_tabs_;
    FavoriteTabRegistry favorite_tabs_;
    std::optional<FavoriteTabKey> displayed_favorite_;
    // The last failed bookmark mutation (or the load problem) and how to run it again. A null retry
    // means the status cannot be retried, only dismissed.
    struct FailedBookmarkMutation {
        std::string message;
        std::function<BookmarkResult()> retry;
    };
    std::optional<FailedBookmarkMutation> failed_bookmark_mutation_;
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
    bool show_internal_page_ = false;
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
    std::optional<AgentContextChip> context_chip_;
    // Agent work runs one task at a time from this queue (see src/agent/agent_task_queue.h).
    agent::AgentTaskQueue agent_queue_{
        [this](const agent::AgentTask& task) { StartQueuedTask(task); }};
    // The task whose prompt the session accepted; its turn end is reported with this id.
    std::string in_flight_id_;
    // The in-flight task is a chat turn (reported by the window); research reports itself. Kept
    // beside the id because "Clear finished" can drop the task from the list.
    bool in_flight_is_chat_ = false;
    // A task the queue started while a turn ran outside it (plan continuation): it is sent when
    // that turn ends.
    std::optional<agent::AgentTask> deferred_task_;
    std::optional<bool> agent_busy_for_test_;
    bool turn_finish_posted_ = false;
    bool queue_waiting_ = false;
    std::string waiting_id_;   // the task OnWaiting(…, true) was last sent for
    std::string start_error_;  // why the last StartQueuedTask failed
    // Transcript items before the live task's prompt, so older plans are not its progress.
    std::size_t task_items_base_ = 0;
    std::size_t reported_done_ = 0, reported_total_ = 0;
    bool queue_fixture_ = false;
    std::vector<PendingConfirmation> pending_confirmations_;
    std::chrono::seconds confirmation_clock_offset_{0};
    bool confirmation_timer_armed_ = false;
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
    std::optional<std::filesystem::path> export_path_for_test_;
    // Bumped to open the Collect sheet (the page opens it when the number changes).
    std::uint64_t collect_sheet_seq_ = 0;
    // Turns the session had ended when the in-flight collect turn was sent.
    std::uint64_t collect_turns_before_ = 0;
    // Dev capture only: the report whose Export menu the collection fixture opens on load.
    std::string report_fixture_menu_id_;
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
