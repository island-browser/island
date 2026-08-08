#ifndef ISLAND_MAIN_ISLAND_APP_H_
#define ISLAND_MAIN_ISLAND_APP_H_

#include "browser_command.h"
#include "include/cef_app.h"
#include "startup_options.h"

namespace island {

class BrowserWindow;
class NavigationObserver;

class IslandApp final : public CefApp, public CefBrowserProcessHandler {
  public:
    explicit IslandApp(StartupOptions startup_options);

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
    void OnContextInitialized() override;

    void ExecuteCommand(BrowserCommand command);
    void RequestClose();
    void SetNavigationObserver(NavigationObserver* observer);

  private:
    ~IslandApp() override;

    const StartupOptions startup_options_;
    CefRefPtr<BrowserWindow> browser_window_;

    IMPLEMENT_REFCOUNTING(IslandApp);
    DISALLOW_COPY_AND_ASSIGN(IslandApp);
};

}  // namespace island

#endif  // ISLAND_MAIN_ISLAND_APP_H_
