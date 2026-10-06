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
