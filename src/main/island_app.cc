#include "island_app.h"

#include <string>

#include "browser_window.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace island {

IslandApp::IslandApp(StartupOptions startup_options) : startup_options_(startup_options) {}

IslandApp::~IslandApp() = default;

CefRefPtr<CefBrowserProcessHandler> IslandApp::GetBrowserProcessHandler() { return this; }

void IslandApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ == nullptr) {
        browser_window_ = BrowserWindow::Create(std::string(startup_options_.initial_url()));
    }
}

void IslandApp::ExecuteCommand(BrowserCommand command) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ExecuteCommand(command);
    }
}

void IslandApp::RequestClose() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->RequestClose();
    }
}

void IslandApp::SetNavigationObserver(NavigationObserver* observer) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->SetNavigationObserver(observer);
    }
}

}  // namespace island
