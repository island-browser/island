#include "island_app.h"

#include <string>

#include "browser_window.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "lifecycle_config.h"

namespace island {

IslandApp::IslandApp(StartupOptions startup_options) : startup_options_(startup_options) {}

IslandApp::~IslandApp() = default;

void IslandApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                              CefRefPtr<CefCommandLine> command_line) {
    // An empty process type is the browser process; helpers inherit what they
    // need from it.
    if (!process_type.empty()) {
        return;
    }
    for (const std::string_view name : kBrowserProcessSwitches) {
        command_line->AppendSwitch(std::string(name));
    }
}

CefRefPtr<CefBrowserProcessHandler> IslandApp::GetBrowserProcessHandler() { return this; }

void IslandApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ == nullptr) {
        // The smoke run starts from its fixed page and never touches the
        // session file, so it stays deterministic on any machine.
        browser_window_ =
            BrowserWindow::Create(std::string(startup_options_.initial_url()),
                                  /*persist_session=*/!startup_options_.is_smoke_test());
        browser_window_->SetNavigationObserver(navigation_observer_);
        browser_window_->SetChromeObserver(chrome_observer_);
    }
}

void IslandApp::ExecuteCommand(BrowserCommand command) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ExecuteCommand(command);
    }
}

void IslandApp::RunKeyAction(KeyAction action) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->RunKeyAction(action);
    }
}

bool IslandApp::CanStopLoading() const {
    CEF_REQUIRE_UI_THREAD();

    return browser_window_ != nullptr && browser_window_->CanStopLoading();
}

bool IslandApp::CanStopLoadingWithEscape() const {
    CEF_REQUIRE_UI_THREAD();

    return browser_window_ != nullptr && browser_window_->CanStopLoadingWithEscape();
}

void IslandApp::ShowWelcomeFlow() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ShowWelcomeFlow();
    }
}

void IslandApp::SetThemePreference(ThemePreference preference) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->SetThemePreference(preference));
    }
}

void IslandApp::BeginAddressEditing() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->BeginAddressEditing();
    }
}

void IslandApp::ShowSearchPalette() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ShowSearchPalette();
    }
}

void IslandApp::ShowCommandPalette() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ShowCommandPalette();
    }
}

void IslandApp::BeginSpaceRenaming() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->BeginSpaceRenaming();
    }
}

void IslandApp::MoveSpaceLeft() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->MoveActiveSpace(-1));
    }
}

void IslandApp::MoveSpaceRight() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->MoveActiveSpace(1));
    }
}

void IslandApp::ToggleSidebar() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ToggleSidebar();
    }
}

void IslandApp::SetSidebarExpanded(bool expanded) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->SetSidebarExpanded(expanded);
    }
}

void IslandApp::NavigateActiveTab(const std::string& address) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        // Submitting needs an edit in progress, exactly as when the user focuses the field.
        browser_window_->BeginAddressEditing();
        browser_window_->SubmitAddressDraft(address);
    }
}

void IslandApp::ApplyAppearanceForDev(const std::string& key, const std::string& value) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ApplyAppearanceForDev(key, value);
    }
}

void IslandApp::ToggleAgentPanel() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ToggleAgentPanel();
    }
}

void IslandApp::ToggleSettings() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ToggleSettings();
    }
}

void IslandApp::ToggleTabOverview() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->ToggleTabOverview();
    }
}

void IslandApp::RequestClose() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->RequestClose();
    }
}

void IslandApp::SelectActiveSpaceTabIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->SelectActiveSpaceTabIndex(index));
    }
}

void IslandApp::SetNavigationObserver(NavigationObserver* observer) {
    CEF_REQUIRE_UI_THREAD();

    navigation_observer_ = observer;
    if (browser_window_ != nullptr) {
        browser_window_->SetNavigationObserver(observer);
    }
}

void IslandApp::SetChromeObserver(ChromeObserver* observer) {
    CEF_REQUIRE_UI_THREAD();

    chrome_observer_ = observer;
    if (browser_window_ != nullptr) {
        browser_window_->SetChromeObserver(observer);
    }
}

}  // namespace island
