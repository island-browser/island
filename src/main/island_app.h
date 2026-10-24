#ifndef ISLAND_MAIN_ISLAND_APP_H_
#define ISLAND_MAIN_ISLAND_APP_H_

#include <string>

#include "browser_command.h"
#include "chrome_snapshot.h"
#include "include/cef_app.h"
#include "keymap.h"
#include "prefs_store.h"
#include "startup_options.h"

namespace island {

class BrowserWindow;
class NavigationObserver;

class IslandApp final : public CefApp, public CefBrowserProcessHandler {
  public:
    explicit IslandApp(StartupOptions startup_options);

    // Appends kBrowserProcessSwitches to the browser process's command line.
    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
    void OnContextInitialized() override;

    void ExecuteCommand(BrowserCommand command);
    // Keyboard shortcut dispatch for the macOS menu (the same path the
    // accelerators take on other platforms).
    void RunKeyAction(KeyAction action);
    [[nodiscard]] bool CanStopLoading() const;
    [[nodiscard]] bool CanStopLoadingWithEscape() const;
    void BeginAddressEditing();
    void ShowSearchPalette();
    void ShowCommandPalette();
    void ToggleSidebar();
    // Developer-capture entry points (--island-dev-actions): set the persistent sidebar state
    // and load an address in the active tab through the address bar's own submit path.
    void SetSidebarExpanded(bool expanded);
    void NavigateActiveTab(const std::string& address);
    // Developer-capture entry points for tab sleep and peek: probe-and-sleep every eligible
    // inactive tab (returns the number of probes started), open an address in the peek card,
    // and report counts for the dev log.
    std::size_t SleepInactiveTabsNow();
    bool OpenPeek(const std::string& address);
    // Shows the favorite at `index` of the global strip (or of the active space's list) the way a
    // click on its sidebar row does (`favorite:global:<i>`, `favorite:space:<i>`).
    bool ActivateFavoriteForDev(bool global, std::size_t index);
    // Moves keyboard focus to the i-th global strip icon (`focus:global_favorite:<i>`).
    bool FocusGlobalFavoriteForDev(std::size_t index);
    [[nodiscard]] std::size_t SleepingTabCount() const;
    // One-line state for the dev log: tab, sleeping-tab and peek state.
    [[nodiscard]] std::string DevStateReport() const;
    // Opens the find bar on the active tab and searches for `query` (dev capture).
    void FindInPage(const std::string& query);
    // `research:<question>` starts a Research task as the start page does;
    // `open_report` opens the newest report in a new tab.
    void StartResearch(const std::string& question);
    void OpenNewestReport();
    // Developer capture only: a Plan tab showing the sample plan waiting for approval.
    void OpenPlanFixture();
    // Developer capture only: a sample agent confirmation card (`agent_fixture:confirm`).
    void ShowConfirmationFixture();
    // Developer capture only: one running and two queued agent tasks (`agent_fixture:queue`).
    void ShowQueueFixture();
    // Developer capture only: the panel with a page chip and a quote chip (`agent_fixture:quote`).
    void ShowQuoteFixture();
    // Developer capture only: the Collect sheet over the panel (`agent_fixture:collect`).
    void ShowCollectFixture();
    // Developer capture only: a sample conversation with tool rows, a Markdown reply and a queued
    // message (`agent_fixture:thread`).
    void ShowThreadFixture();
    // Developer capture only: a collection report with its Export menu open
    // (`report_fixture:collection`).
    void ShowCollectionReportFixture();
    // Developer capture only: `count` agent pages opened back to back during the plan fixture.
    void OpenAgentPagesForDev(int count, int gap_ms);
    void ApplyAppearanceForDev(const std::string& key, const std::string& value);
    void ToggleAgentPanel();
    void ToggleSettings();
    void OpenSettingsSection(const std::string& section);
    void ToggleTabOverview();
    void BeginSpaceRenaming();
    void SetActiveSpaceIcon(const std::string& icon_id);
    void SetSpaceIconForDev(std::size_t index, const std::string& icon_id);
    [[nodiscard]] std::string ActiveSpaceIconId() const;
    // Space reorder menu entries; a move that would leave the space list is a
    // no-op.
    void MoveSpaceLeft();
    void MoveSpaceRight();
    // Welcome flow and appearance entries for the macOS menu.
    void ShowWelcomeFlow();
    void SetThemePreference(ThemePreference preference);
    void RequestClose();
    // Direct-index tab switch (Cmd/Ctrl+1..9) for the macOS menu; an
    // out-of-range index is a no-op.
    void SelectActiveSpaceTabIndex(std::size_t index);
    void SetNavigationObserver(NavigationObserver* observer);
    void SetChromeObserver(ChromeObserver* observer);

  private:
    ~IslandApp() override;

    const StartupOptions startup_options_;
    CefRefPtr<BrowserWindow> browser_window_;
    NavigationObserver* navigation_observer_ = nullptr;
    ChromeObserver* chrome_observer_ = nullptr;

    IMPLEMENT_REFCOUNTING(IslandApp);
    DISALLOW_COPY_AND_ASSIGN(IslandApp);
};

}  // namespace island

#endif  // ISLAND_MAIN_ISLAND_APP_H_
