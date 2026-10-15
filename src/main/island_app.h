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
    [[nodiscard]] std::size_t SleepingTabCount() const;
    // One-line state for the dev log: tab, sleeping-tab and peek state.
    [[nodiscard]] std::string DevStateReport() const;
    void ToggleAgentPanel();
    void ToggleSettings();
    void ToggleTabOverview();
    void BeginSpaceRenaming();
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
