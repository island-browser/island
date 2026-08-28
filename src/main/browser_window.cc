#include "browser_window.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

#include "app_resources.h"
#include "cef_address_parser.h"
#include "design_tokens.h"
#include "include/base/cef_build.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
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
constexpr int kChromeWindowWidth = 1440;
constexpr int kChromeWindowHeight = 900;
constexpr int kMinimumWindowWidth = 800;
constexpr int kMinimumWindowHeight = 560;
}  // namespace

CefRefPtr<BrowserWindow> BrowserWindow::Create(std::string initial_url) {
    CEF_REQUIRE_UI_THREAD();

    CefRefPtr<BrowserWindow> browser_window(new BrowserWindow(std::move(initial_url)));
    CefWindow::CreateTopLevelWindow(browser_window);
    return browser_window;
}

BrowserWindow::BrowserWindow(std::string initial_url) : initial_url_(std::move(initial_url)) {
    spaces_.emplace_back(SpaceId{1}, "Default", ArgbColor{0xFF5B8DEF});
    active_space().AppendTab(Tab{TabId{1}});
    chrome_snapshot_.rail_bounds = {
        .x = 0,
        .y = 0,
        .width = ChromeTokens::ForTheme(ChromeTheme::kLight).rail_width_dip,
        .height = kChromeWindowHeight};
    chrome_snapshot_.content_bounds = {
        .x = chrome_snapshot_.rail_bounds.width,
        .y = 0,
        .width = kChromeWindowWidth - chrome_snapshot_.rail_bounds.width,
        .height = kChromeWindowHeight,
    };
    Tab* tab = active_tab();
    if (tab != nullptr) {
        tab->navigation_state().SetObserver(this);
    }
}

void BrowserWindow::ExecuteCommand(BrowserCommand command) {
    CEF_REQUIRE_UI_THREAD();

    Tab* tab = active_tab();
    if (closing_ || tab == nullptr || tab->browser() == nullptr) {
        return;
    }

    switch (command) {
        case BrowserCommand::kBack:
            tab->browser()->GoBack();
            return;
        case BrowserCommand::kForward:
            tab->browser()->GoForward();
            return;
        case BrowserCommand::kReload:
            tab->browser()->Reload();
            return;
    }
}

void BrowserWindow::SetNavigationObserver(NavigationObserver* observer) {
    CEF_REQUIRE_UI_THREAD();

    navigation_observer_ = observer == this ? nullptr : observer;
    if (navigation_observer_ != nullptr && !closing_) {
        const Tab* tab = active_tab();
        if (tab != nullptr) {
            navigation_observer_->OnNavigationChanged(tab->navigation_state().snapshot());
        }
    }
}

void BrowserWindow::SetChromeObserver(ChromeObserver* observer) {
    CEF_REQUIRE_UI_THREAD();

    chrome_observer_ = observer;
    if (chrome_observer_ != nullptr && !closing_) {
        chrome_observer_->OnChromeChanged(chrome_snapshot_);
    }
}

const NavigationSnapshot& BrowserWindow::navigation_snapshot() const noexcept {
    const Tab* tab = active_tab();
    static const NavigationSnapshot kEmpty;
    return tab != nullptr ? tab->navigation_state().snapshot() : kEmpty;
}

const ChromeSnapshot& BrowserWindow::chrome_snapshot() const noexcept { return chrome_snapshot_; }

ChromeViewTreeNode BrowserWindow::chrome_view_tree_snapshot() const {
    return chrome_ == nullptr ? ChromeViewTreeNode{} : chrome_->view_tree_snapshot();
}

void BrowserWindow::RequestClose() {
    CEF_REQUIRE_UI_THREAD();

    if (window_ != nullptr) {
        window_->Close();
    }
}

void BrowserWindow::ExecuteBrowserCommand(BrowserCommand command) { ExecuteCommand(command); }

void BrowserWindow::BeginAddressEditing() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    address_bar_model_.Focus();
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    chrome_snapshot_.focus_target = FocusTarget::kAddress;
    PublishChromeSnapshot();
}

void BrowserWindow::CancelAddressEditing() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    address_bar_model_.Escape();
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
}

void BrowserWindow::SubmitAddressDraft(std::string_view draft) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    address_bar_model_.SetEditText(std::string(draft));
    const std::optional<std::string> url = address_bar_model_.Submit(ParseAndValidate(draft));
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    Tab* tab = active_tab();
    if (!url.has_value() || tab == nullptr || tab->browser() == nullptr) {
        return;
    }

    CefRefPtr<CefFrame> main_frame = tab->browser()->GetMainFrame();
    if (main_frame != nullptr) {
        main_frame->LoadURL(*url);
    }
}

void BrowserWindow::FocusBrowserView() {
    CEF_REQUIRE_UI_THREAD();
    const Tab* tab = active_tab();
    if (!closing_ && tab != nullptr && tab->browser_view() != nullptr) {
        tab->browser_view()->RequestFocus();
    }
}

void BrowserWindow::OnNavigationChanged(const NavigationSnapshot& snapshot) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    address_bar_model_.UpdateCommittedUrl(snapshot.url);
    if (chrome_ != nullptr) {
        chrome_->OnNavigationChanged(snapshot);
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    chrome_snapshot_.back_enabled = snapshot.can_go_back;
    chrome_snapshot_.forward_enabled = snapshot.can_go_forward;
    chrome_snapshot_.active_page_title =
        snapshot.page_title.empty() ? "Island" : snapshot.page_title;
    PublishChromeSnapshot();
    if (navigation_observer_ != nullptr) {
        navigation_observer_->OnNavigationChanged(snapshot);
    }
}

CefRefPtr<CefDisplayHandler> BrowserWindow::GetDisplayHandler() { return this; }

CefRefPtr<CefLifeSpanHandler> BrowserWindow::GetLifeSpanHandler() { return this; }

CefRefPtr<CefLoadHandler> BrowserWindow::GetLoadHandler() { return this; }

void BrowserWindow::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain()) {
        return;
    }

    owner->navigation_state().SetAddress(url.ToString());
}

void BrowserWindow::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr) {
        return;
    }

    owner->navigation_state().OnTitleChange(title.ToString());
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
    if (closing_) {
        return;
    }
    browser_was_created_ = true;
    Tab* owner = FindTabByBrowser(browser);
    if (owner != nullptr) {
        owner->SetBrowser(browser);
    }
}

bool BrowserWindow::DoClose(CefRefPtr<CefBrowser>) {
    CEF_REQUIRE_UI_THREAD();
    return false;
}

void BrowserWindow::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    closing_ = true;
    DetachChromeAndObservers();
    Tab* owner = FindTabByBrowser(browser);
    if (owner != nullptr) {
        owner->SetBrowser(nullptr);
        owner->SetBrowserView(nullptr);
    }
    CloseNavigationAndQuitMessageLoop();
}

void BrowserWindow::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool, bool can_go_back,
                                         bool can_go_forward) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr) {
        return;
    }

    owner->navigation_state().OnLoadingStateChange(can_go_back, can_go_forward);
}

void BrowserWindow::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                TransitionType) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain()) {
        return;
    }

    owner->navigation_state().OnLoadStart(frame->GetURL().ToString());
}

void BrowserWindow::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              int http_status_code) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain()) {
        return;
    }

    owner->navigation_state().OnLoadEnd(http_status_code);
}

void BrowserWindow::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                ErrorCode error_code, const CefString&, const CefString&) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain() || error_code == ERR_ABORTED) {
        return;
    }

    owner->navigation_state().OnLoadError(static_cast<int>(error_code));
}

void BrowserWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
    CEF_REQUIRE_UI_THREAD();

    window_ = window;
    const IconResources icon_resources = ResolveCurrentProcessIconResources(std::nullopt);
    std::error_code error;
    const std::filesystem::path icon_resource_root =
        std::filesystem::absolute(icon_resources.root, error);
    if (!icon_resources.manifest_present || error ||
        !std::filesystem::is_regular_file(icon_resource_root / "manifest.json", error)) {
        closing_ = true;
        window_->Close();
        return;
    }

    const ChromeTheme theme =
        ClassifyChromeTheme(window_->GetThemeColor(CEF_ColorPrimaryBackground));
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        this, CefString(initial_url_), CefBrowserSettings(), nullptr,
        active_space().request_context(), this);
    Tab* tab = active_tab();
    if (tab != nullptr) {
        tab->SetBrowserView(browser_view);
    }
    window_->SetToFillLayout();
    chrome_ = std::make_unique<BrowserChrome>(*this, browser_view, ChromeTokens::ForTheme(theme),
                                              icon_resource_root);
    chrome_->OnNavigationChanged(tab != nullptr ? tab->navigation_state().snapshot()
                                                 : NavigationSnapshot{});
    chrome_->OnAddressChanged(address_bar_model_.snapshot());
    window_->AddChildView(chrome_->root());
    window_->SetTitle(CefString("Island"));
    window_->CenterWindow(CefSize(kChromeWindowWidth, kChromeWindowHeight));
    ApplyTheme(window_, theme, true);
    window_->Show();
    window_->Activate();
    browser_view->RequestFocus();

    window_->SetAccelerator(kBackAccelerator, kVirtualKeyLeft, false, false, true, true);
    window_->SetAccelerator(kForwardAccelerator, kVirtualKeyRight, false, false, true, true);
    window_->SetAccelerator(kReloadAccelerator, kVirtualKeyF5, false, false, false, true);
    window_->SetAccelerator(kReloadWithControlAccelerator, 'R', false, true, false, true);
#if defined(OS_WIN) || defined(OS_LINUX)
    window_->SetAccelerator(kFocusAddressAccelerator, 'L', false, true, false, true);
#endif
    // ctrl_pressed maps to Cmd on macOS and Ctrl elsewhere; high_priority so the
    // palette opens while web content holds focus.
    window_->SetAccelerator(kOpenPaletteAccelerator, 'K', false, true, false, true);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    closing_ = true;
    DetachChromeAndObservers();
    window_ = nullptr;

    if (!browser_was_created_) {
        CloseNavigationAndQuitMessageLoop();
    }
}

void BrowserWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect& new_bounds) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    chrome_snapshot_.rail_bounds = {
        .x = 0,
        .y = 0,
        .width =
            std::min(ChromeTokens::ForTheme(ChromeTheme::kLight).rail_width_dip, new_bounds.width),
        .height = new_bounds.height,
    };
    chrome_snapshot_.content_bounds = {
        .x = chrome_snapshot_.rail_bounds.width,
        .y = 0,
        .width = std::max(0, new_bounds.width - chrome_snapshot_.rail_bounds.width),
        .height = new_bounds.height,
    };
    if (search_palette_ != nullptr) {
        search_palette_->UpdateBounds();
    }
    PublishChromeSnapshot();
}

void BrowserWindow::OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_) {
        ApplyTheme(window, ClassifyChromeTheme(window->GetThemeColor(CEF_ColorPrimaryBackground)),
                   false);
    }
}

CefSize BrowserWindow::GetMinimumSize(CefRefPtr<CefView>) {
    return CefSize(kMinimumWindowWidth, kMinimumWindowHeight);
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    const Tab* tab = active_tab();
    if (tab == nullptr || tab->browser() == nullptr) {
        return true;
    }

    return tab->browser()->GetHost()->TryCloseBrowser();
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
        case kFocusAddressAccelerator:
            if (chrome_ != nullptr) {
                chrome_->BeginAddressEditing();
            } else {
                BeginAddressEditing();
            }
            return true;
        case kOpenPaletteAccelerator:
            ShowSearchPalette();
            return true;
        default:
            return false;
    }
}

void BrowserWindow::OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                                     CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    browser_was_created_ = true;
    Tab* owner = FindTabByBrowserView(browser_view);
    if (owner != nullptr) {
        owner->SetBrowser(browser);
    }
}

void BrowserWindow::OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (owner != nullptr) {
        DetachChromeAndObservers();
        owner->SetBrowser(nullptr);
        owner->SetBrowserView(nullptr);
    }
}

BrowserWindow::ChromeToolbarType BrowserWindow::GetChromeToolbarType(CefRefPtr<CefBrowserView>) {
    return CEF_CTT_NONE;
}

void BrowserWindow::OnFocus(CefRefPtr<CefView> view) {
    CEF_REQUIRE_UI_THREAD();
    const Tab* tab = active_tab();
    if (!closing_ && tab != nullptr && tab->browser_view() != nullptr && view != nullptr &&
        view->IsSame(tab->browser_view())) {
        CancelAddressEditing();
    }
}

Tab* BrowserWindow::FindTabByBrowser(CefRefPtr<CefBrowser> browser) noexcept {
    if (browser == nullptr) {
        return nullptr;
    }
    for (Space& space : spaces_) {
        for (Tab& tab : space.tabs()) {
            if (tab.browser() != nullptr && tab.browser()->IsSame(browser)) {
                return &tab;
            }
        }
    }
    return nullptr;
}

const Tab* BrowserWindow::FindTabByBrowser(CefRefPtr<CefBrowser> browser) const noexcept {
    if (browser == nullptr) {
        return nullptr;
    }
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            if (tab.browser() != nullptr && tab.browser()->IsSame(browser)) {
                return &tab;
            }
        }
    }
    return nullptr;
}

Tab* BrowserWindow::FindTabByBrowserView(CefRefPtr<CefBrowserView> browser_view) noexcept {
    if (browser_view == nullptr) {
        return nullptr;
    }
    for (Space& space : spaces_) {
        for (Tab& tab : space.tabs()) {
            if (tab.browser_view() != nullptr && tab.browser_view()->IsSame(browser_view)) {
                return &tab;
            }
        }
    }
    return nullptr;
}

Tab* BrowserWindow::active_tab() noexcept {
    return active_space().FindTab(active_space().active_tab_id());
}

const Tab* BrowserWindow::active_tab() const noexcept {
    return active_space().FindTab(active_space().active_tab_id());
}

void BrowserWindow::ApplyTheme(CefRefPtr<CefWindow> window, ChromeTheme theme, bool notify_views) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
    window->SetThemeColor(CEF_ColorPrimaryBackground, tokens.background.argb);
    window->SetThemeColor(CEF_ColorPrimaryForeground, tokens.text.argb);
    window->SetThemeColor(CEF_ColorSecondaryForeground, tokens.text_secondary.argb);
    window->SetThemeColor(CEF_ColorAccent, tokens.accent.argb);
    if (notify_views) {
        window->ThemeChanged();
    }
    if (chrome_ != nullptr) {
        chrome_->ApplyTheme(tokens);
    }
    if (search_palette_ != nullptr) {
        search_palette_->ApplyTheme(tokens);
    }
    chrome_snapshot_.theme = theme;
    PublishChromeSnapshot();
}

void BrowserWindow::PublishChromeSnapshot() {
    if (chrome_observer_ != nullptr && !closing_) {
        chrome_observer_->OnChromeChanged(chrome_snapshot_);
    }
}

void BrowserWindow::DetachChromeAndObservers() {
    navigation_observer_ = nullptr;
    chrome_observer_ = nullptr;
    if (search_palette_ != nullptr) {
        search_palette_->Detach();
        search_palette_.reset();
    }
    Tab* tab = active_tab();
    if (tab != nullptr) {
        tab->navigation_state().SetObserver(nullptr);
    }
    if (chrome_ != nullptr) {
        CefRefPtr<CefPanel> root = chrome_->root();
        chrome_->Detach();
        if (window_ != nullptr && root != nullptr) {
            window_->RemoveChildView(root);
        }
        chrome_.reset();
    }
}

void BrowserWindow::UpdateWindowTitle() {
    const Tab* tab = active_tab();
    if (window_ != nullptr && tab != nullptr) {
        window_->SetTitle(CefString(tab->navigation_state().snapshot().display_title));
    }
}

CefRefPtr<CefBrowser> BrowserWindow::ActiveBrowser() {
    CEF_REQUIRE_UI_THREAD();
    const Tab* tab = active_tab();
    return closing_ || tab == nullptr ? nullptr : tab->browser();
}

void BrowserWindow::OnSearchPaletteSubmitted(const SearchSubmission& submission) {
    CEF_REQUIRE_UI_THREAD();
    SubmitSearchQuery(submission.query, submission.provider);
}

void BrowserWindow::SubmitSearchQuery(std::string_view query, SearchProviderId provider) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    CefRefPtr<CefBrowser> browser = ActiveBrowser();
    const SearchDispatchDecision decision =
        DecideSearchDispatch(query, provider, browser != nullptr);
    if (decision.dispatch != SearchDispatch::kNavigate) {
        return;
    }

    CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    if (main_frame != nullptr) {
        main_frame->LoadURL(decision.url);
    }
    if (search_palette_ != nullptr) {
        search_palette_->Hide();
    }
    FocusBrowserView();
}

void BrowserWindow::OnSearchPaletteDismissed() {
    CEF_REQUIRE_UI_THREAD();
    // Escape restores focus to the invocation point, which is the browser view
    // by default.
    FocusBrowserView();
}

void BrowserWindow::ShowSearchPalette() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    if (search_palette_ == nullptr) {
        // Lazily created on the first Cmd/Ctrl+K, then only shown and hidden.
        search_palette_ = std::make_unique<SearchPalette>(
            *this, window_,
            ChromeTokens::ForTheme(
                ClassifyChromeTheme(window_->GetThemeColor(CEF_ColorPrimaryBackground))));
    }
    search_palette_->Show();
}

void BrowserWindow::CloseNavigationAndQuitMessageLoop() {
    Tab* tab = active_tab();
    if (tab != nullptr) {
        tab->navigation_state().Close();
    }

    if (message_loop_quit_) {
        return;
    }

    message_loop_quit_ = true;
    CefQuitMessageLoop();
}

}  // namespace island
