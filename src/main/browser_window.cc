#include "browser_window.h"

#include <utility>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace island {
namespace {

constexpr int kVirtualKeyLeft = 0x25;
constexpr int kVirtualKeyRight = 0x27;
constexpr int kVirtualKeyF5 = 0x74;
constexpr int kWindowWidth = 1024;
constexpr int kWindowHeight = 768;

}  // namespace

CefRefPtr<BrowserWindow> BrowserWindow::Create(std::string initial_url) {
    CEF_REQUIRE_UI_THREAD();

    CefRefPtr<BrowserWindow> browser_window(new BrowserWindow(std::move(initial_url)));
    CefWindow::CreateTopLevelWindow(browser_window);
    return browser_window;
}

BrowserWindow::BrowserWindow(std::string initial_url) : initial_url_(std::move(initial_url)) {}

void BrowserWindow::ExecuteCommand(BrowserCommand command) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_ == nullptr) {
        return;
    }

    switch (command) {
        case BrowserCommand::kBack:
            browser_->GoBack();
            return;
        case BrowserCommand::kForward:
            browser_->GoForward();
            return;
        case BrowserCommand::kReload:
            browser_->Reload();
            return;
    }
}

void BrowserWindow::SetNavigationObserver(NavigationObserver* observer) {
    CEF_REQUIRE_UI_THREAD();
    navigation_state_.SetObserver(observer);
}

const NavigationSnapshot& BrowserWindow::navigation_snapshot() const noexcept {
    return navigation_state_.snapshot();
}

void BrowserWindow::RequestClose() {
    CEF_REQUIRE_UI_THREAD();

    if (window_ != nullptr) {
        window_->Close();
    }
}

CefRefPtr<CefDisplayHandler> BrowserWindow::GetDisplayHandler() { return this; }

CefRefPtr<CefLifeSpanHandler> BrowserWindow::GetLifeSpanHandler() { return this; }

CefRefPtr<CefLoadHandler> BrowserWindow::GetLoadHandler() { return this; }

void BrowserWindow::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser) || !frame->IsMain()) {
        return;
    }

    navigation_state_.SetAddress(url.ToString());
}

void BrowserWindow::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser)) {
        return;
    }

    navigation_state_.OnTitleChange(title.ToString());
    UpdateWindowTitle();
}

bool BrowserWindow::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&,
                                  const CefString&, WindowOpenDisposition, bool,
                                  const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                                  CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) {
    CEF_REQUIRE_UI_THREAD();
    return true;
}

void BrowserWindow::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_was_created_ = true;
    browser_ = browser;
}

bool BrowserWindow::DoClose(CefRefPtr<CefBrowser>) {
    CEF_REQUIRE_UI_THREAD();
    return false;
}

void BrowserWindow::OnBeforeClose(CefRefPtr<CefBrowser>) {
    CEF_REQUIRE_UI_THREAD();

    browser_ = nullptr;
    CloseNavigationAndQuitMessageLoop();
}

void BrowserWindow::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool, bool can_go_back,
                                         bool can_go_forward) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser)) {
        return;
    }

    navigation_state_.OnLoadingStateChange(can_go_back, can_go_forward);
}

void BrowserWindow::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                TransitionType) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser) || !frame->IsMain()) {
        return;
    }

    navigation_state_.OnLoadStart(frame->GetURL().ToString());
}

void BrowserWindow::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              int http_status_code) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser) || !frame->IsMain()) {
        return;
    }

    navigation_state_.OnLoadEnd(http_status_code);
}

void BrowserWindow::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                ErrorCode error_code, const CefString&, const CefString&) {
    CEF_REQUIRE_UI_THREAD();

    if (!IsMainBrowser(browser) || !frame->IsMain() || error_code == ERR_ABORTED) {
        return;
    }

    navigation_state_.OnLoadError(static_cast<int>(error_code));
}

void BrowserWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
    CEF_REQUIRE_UI_THREAD();

    window_ = window;
    browser_view_ = CefBrowserView::CreateBrowserView(this, CefString(initial_url_),
                                                      CefBrowserSettings(), nullptr, nullptr, this);
    window_->SetToFillLayout();
    window_->AddChildView(browser_view_);
    window_->SetTitle(CefString("Island"));
    window_->CenterWindow(CefSize(kWindowWidth, kWindowHeight));
    window_->Show();
    window_->Activate();
    browser_view_->RequestFocus();

    window_->SetAccelerator(kBackAccelerator, kVirtualKeyLeft, false, false, true, true);
    window_->SetAccelerator(kForwardAccelerator, kVirtualKeyRight, false, false, true, true);
    window_->SetAccelerator(kReloadAccelerator, kVirtualKeyF5, false, false, false, true);
    window_->SetAccelerator(kReloadWithControlAccelerator, 'R', false, true, false, true);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    window_ = nullptr;
    browser_view_ = nullptr;

    if (!browser_was_created_) {
        CloseNavigationAndQuitMessageLoop();
    }
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    if (browser_ == nullptr) {
        return true;
    }

    return browser_->GetHost()->TryCloseBrowser();
}

bool BrowserWindow::OnAccelerator(CefRefPtr<CefWindow>, int command_id) {
    CEF_REQUIRE_UI_THREAD();

    switch (command_id) {
        case kBackAccelerator:
            ExecuteCommand(BrowserCommand::kBack);
            return true;
        case kForwardAccelerator:
            ExecuteCommand(BrowserCommand::kForward);
            return true;
        case kReloadAccelerator:
        case kReloadWithControlAccelerator:
            ExecuteCommand(BrowserCommand::kReload);
            return true;
        default:
            return false;
    }
}

void BrowserWindow::OnBrowserCreated(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_was_created_ = true;
    browser_ = browser;
}

void BrowserWindow::OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    if (IsMainBrowser(browser)) {
        browser_ = nullptr;
        browser_view_ = nullptr;
    }
}

BrowserWindow::ChromeToolbarType BrowserWindow::GetChromeToolbarType(CefRefPtr<CefBrowserView>) {
    return CEF_CTT_NONE;
}

bool BrowserWindow::IsMainBrowser(CefRefPtr<CefBrowser> browser) const {
    return browser_ != nullptr && browser_->IsSame(browser);
}

void BrowserWindow::UpdateWindowTitle() {
    if (window_ != nullptr) {
        window_->SetTitle(CefString(navigation_state_.snapshot().display_title));
    }
}

void BrowserWindow::CloseNavigationAndQuitMessageLoop() {
    navigation_state_.Close();

    if (message_loop_quit_) {
        return;
    }

    message_loop_quit_ = true;
    CefQuitMessageLoop();
}

}  // namespace island
