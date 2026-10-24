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

void IslandApp::ShowCommandPaletteWithQueryForDev(std::string_view query) {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowCommandPaletteWithQueryForDev(query);
    }
}

void IslandApp::BeginSpaceRenaming() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->BeginSpaceRenaming();
    }
}

void IslandApp::SetActiveSpaceIcon(const std::string& icon_id) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(
            browser_window_->SetSpaceIcon(browser_window_->active_space_index(), icon_id));
    }
}

void IslandApp::SetSpaceIconForDev(std::size_t index, const std::string& icon_id) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->SetSpaceIcon(index, icon_id));
    }
}

std::string IslandApp::ActiveSpaceIconId() const {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ == nullptr) {
        return {};
    }
    const Space* space = browser_window_->FindSpace(browser_window_->active_space_id());
    return space != nullptr ? space->icon_id() : std::string();
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

std::size_t IslandApp::SleepInactiveTabsNow() {
    CEF_REQUIRE_UI_THREAD();

    return browser_window_ != nullptr ? browser_window_->SleepInactiveTabsNow() : 0;
}

bool IslandApp::OpenPeek(const std::string& address) {
    CEF_REQUIRE_UI_THREAD();

    return browser_window_ != nullptr && browser_window_->OpenPeek(address);
}

std::size_t IslandApp::SleepingTabCount() const {
    return browser_window_ != nullptr ? browser_window_->sleeping_tab_count() : 0;
}

std::string IslandApp::DevStateReport() const {
    if (browser_window_ == nullptr) {
        return "no window";
    }
    std::size_t tabs = 0;
    for (const Space& space : browser_window_->spaces()) {
        tabs += space.tab_count();
    }
    return "spaces=" + std::to_string(browser_window_->space_count()) +
           " tabs=" + std::to_string(tabs) +
           " sleeping=" + std::to_string(browser_window_->sleeping_tab_count()) +
           " peek=" + (browser_window_->peek_open() ? "open" : "closed") +
           " attaches=" + std::to_string(browser_window_->AttachCountForTest()) +
           " browser_size_changes=" +
           std::to_string(browser_window_->BrowserViewSizeChangeCountForDev()) +
           " chrome_relayouts=" + std::to_string(browser_window_->ChromeRelayoutCountForDev());
}

void IslandApp::FindInPage(const std::string& query) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->FindInPage(query);
    }
}

void IslandApp::StartResearch(const std::string& question) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        static_cast<void>(browser_window_->StartResearch(question));
    }
}

void IslandApp::OpenPlanFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->OpenPlanFixtureTab(/*include_child_tabs=*/true);
    }
}

void IslandApp::ShowConfirmationFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowConfirmationFixtureForDev();
    }
}

void IslandApp::ShowQueueFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowQueueFixtureForDev();
    }
}

void IslandApp::ShowQuoteFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowQuoteFixtureForDev();
    }
}

void IslandApp::ShowCollectFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowCollectFixtureForDev();
    }
}

void IslandApp::ShowThreadFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowThreadFixtureForDev();
    }
}

void IslandApp::ShowCollectionReportFixture() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->ShowCollectionReportFixtureForDev();
    }
}

void IslandApp::OpenAgentPagesForDev(int count, int gap_ms) {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->OpenAgentPagesInQuickSuccessionForDev(count, gap_ms);
    }
}

void IslandApp::OpenNewestReport() {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr && !browser_window_->reports().reports.empty()) {
        static_cast<void>(browser_window_->OpenReportTab(browser_window_->reports().reports[0].id));
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

void IslandApp::OpenBookmarksPage() {
    CEF_REQUIRE_UI_THREAD();
    if (browser_window_ != nullptr) {
        browser_window_->OpenBookmarksPage();
    }
}

void IslandApp::OpenSettingsSection(const std::string& section) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_window_ != nullptr) {
        browser_window_->OpenSettingsSection(section);
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
