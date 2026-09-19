#include "browser_window.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "session_store.h"

namespace island {
namespace {

constexpr int kVirtualKeyLeft = 0x25;
constexpr int kVirtualKeyRight = 0x27;
constexpr int kVirtualKeyF2 = 0x71;
constexpr int kVirtualKeyF5 = 0x74;
constexpr int kVirtualKeyEscape = 0x1B;
constexpr int kChromeWindowWidth = 1440;
constexpr int kChromeWindowHeight = 900;
constexpr int kMinimumWindowWidth = 800;
constexpr int kMinimumWindowHeight = 560;

constexpr std::string_view kDefaultSpaceName = "Default";
constexpr ArgbColor kDefaultSpaceColor{0xFF5B8DEF};
// Fixed palette for spaces created through New Space, cycled in creation order
// so the switcher's color marks stay distinguishable. The default space keeps
// the palette's first entry.
constexpr std::array<ArgbColor, 5> kSpaceColorPalette{
    ArgbColor{0xFF5B8DEF}, ArgbColor{0xFF34A853}, ArgbColor{0xFFFBBC05},
    ArgbColor{0xFFEA4335}, ArgbColor{0xFF9334E6},
};

// The per-user home root the import detection probes under (mirrors the
// session/prefs path conventions: profile-relative on Windows, home-relative
// elsewhere).
std::filesystem::path UserBaseHome() {
#if defined(_WIN32)
    const char* const profile = std::getenv("USERPROFILE");
    return profile != nullptr ? std::filesystem::path(profile) : std::filesystem::path(".");
#else
    const char* const home = std::getenv("HOME");
    return home != nullptr && *home != '\0' ? std::filesystem::path(home)
                                            : std::filesystem::path(".");
#endif
}

// The edge sliver is a bare fill panel; it only needs a preferred size so the
// overlay controller does not collapse it to nothing before SetBounds applies.
class HoverSliverDelegate final : public CefPanelDelegate {
  public:
    CefSize GetPreferredSize(CefRefPtr<CefView>) override {
        return CefSize(kHoverSliverWidthDip, kChromeWindowHeight);
    }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return CefSize(kHoverSliverWidthDip, 0); }

  private:
    IMPLEMENT_REFCOUNTING(HoverSliverDelegate);
};

}  // namespace

CefRefPtr<BrowserWindow> BrowserWindow::Create(std::string initial_url, bool persist_session) {
    CEF_REQUIRE_UI_THREAD();

    CefRefPtr<BrowserWindow> browser_window(
        new BrowserWindow(std::move(initial_url), persist_session));
    CefWindow::CreateTopLevelWindow(browser_window);
    return browser_window;
}

CefRefPtr<BrowserWindow> BrowserWindow::CreateHeadlessForTest(std::string initial_url) {
    CEF_REQUIRE_UI_THREAD();

    return CefRefPtr<BrowserWindow>(
        new BrowserWindow(std::move(initial_url), /*persist_session=*/false));
}

BrowserWindow::BrowserWindow(std::string initial_url, bool persist_session)
    : initial_url_(std::move(initial_url)), persist_session_(persist_session) {
    if (!persist_session_ || !RestoreSession()) {
        // CreateDefaultSpace already appends the space's single starting tab.
        spaces_.push_back(CreateDefaultSpace());
    }
    // Preferences and bookmarks share the session file's persistence rules:
    // headless and smoke shapes keep fresh-install defaults and never read or
    // write the files, so tests and smoke runs stay deterministic.
    if (persist_session_) {
        prefs_ = PrefsStore::Load(PrefsStore::DefaultPrefsFilePath()).state;
        bookmarks_ = BookmarkStore::Load(BookmarkStore::DefaultBookmarksFilePath()).state;
    }
    chrome_snapshot_.rail_bounds = {.x = 0,
                                    .y = 0,
                                    .width = sidebar_state_.RailWidthDip(
                                        ChromeTokens::ForTheme(ChromeTheme::kLight).rail_width_dip),
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

Space BrowserWindow::CreateDefaultSpace() {
    Space space{SpaceId{NextSpaceId()}, std::string(kDefaultSpaceName), kDefaultSpaceColor};
    space.AppendTab(Tab{NextTabId()});
    return space;
}

bool BrowserWindow::RestoreSession() {
    const LoadResult loaded = SessionStore::Load(SessionStore::DefaultSessionFilePath());
    if (loaded.error != SessionError::kNone || loaded.state.spaces.empty()) {
        return false;
    }

    // Re-reserve persisted identities first so newly allocated ids can never
    // collide with a restored one in this process lifetime.
    std::uint64_t max_tab_id = 0;
    std::uint64_t max_space_id = 0;
    for (const SpaceState& space_state : loaded.state.spaces) {
        max_space_id = std::max(max_space_id, space_state.id.value);
        for (const TabState& tab_state : space_state.tabs) {
            max_tab_id = std::max(max_tab_id, tab_state.id.value);
        }
    }
    ReserveTabIdsUpTo(max_tab_id);
    ReserveSpaceIdsUpTo(max_space_id);

    for (const SpaceState& space_state : loaded.state.spaces) {
        if (space_state.tabs.empty()) {
            continue;
        }
        Space space{SpaceId{space_state.id.value}, space_state.name,
                    ArgbColor{space_state.color_argb}};
        for (const TabState& tab_state : space_state.tabs) {
            Tab tab{TabId{tab_state.id.value}};
            // A restored tab navigates through the same validation path as
            // manual entry: a persisted URL that no longer passes the
            // allow-list falls back to the fixed startup page instead of
            // force-loading.
            const ValidatedAddress address = ParseAndValidate(tab_state.url);
            if (address.is_valid()) {
                tab.SetStartupUrl(address.url);
            }
            space.AppendTab(std::move(tab));
        }
        if (space_state.active_tab_id.has_value()) {
            static_cast<void>(space.SelectTab(*space_state.active_tab_id));
        }
        if (space_state.split.has_value()) {
            static_cast<void>(space.SetSplit(
                SplitPairing{space_state.split->first_tab_id, space_state.split->second_tab_id}));
        }
        spaces_.push_back(std::move(space));
    }
    if (spaces_.empty()) {
        return false;
    }

    if (loaded.state.active_space_id.has_value()) {
        for (std::size_t index = 0; index < spaces_.size(); ++index) {
            if (spaces_[index].id() == *loaded.state.active_space_id) {
                active_space_index_ = index;
                break;
            }
        }
    }
    return true;
}

void BrowserWindow::SaveSession() const {
    SessionState state;
    state.active_space_id = active_space().id();
    for (const Space& space : spaces_) {
        SpaceState space_state;
        space_state.id = space.id();
        space_state.name = space.name();
        space_state.color_argb = space.color().argb;
        for (const Tab& tab : space.tabs()) {
            space_state.tabs.push_back(TabState{tab.id(), tab.navigation_state().snapshot().url});
        }
        space_state.active_tab_id =
            space.has_active_tab() ? std::optional<TabId>(space.active_tab_id()) : std::nullopt;
        if (space.split().has_value()) {
            space_state.split = SplitState{space.split()->first, space.split()->second};
        }
        state.spaces.push_back(std::move(space_state));
    }
    // Spaces currently create their request contexts without a cache path, so
    // there is no storage directory to persist yet.
    static_cast<void>(SessionStore::Save(SessionStore::DefaultSessionFilePath(), state));
}

std::string BrowserWindow::StartupUrlForTab(const Tab& tab) const {
    return tab.startup_url().empty() ? initial_url_ : tab.startup_url();
}

void BrowserWindow::ExecuteCommand(BrowserCommand command) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    switch (command) {
        case BrowserCommand::kBack:
        case BrowserCommand::kForward:
        case BrowserCommand::kReload: {
            Tab* tab = active_tab();
            if (tab == nullptr || tab->browser() == nullptr) {
                return;
            }
            if (command == BrowserCommand::kBack) {
                tab->browser()->GoBack();
            } else if (command == BrowserCommand::kForward) {
                tab->browser()->GoForward();
            } else {
                tab->browser()->Reload();
            }
            return;
        }
        case BrowserCommand::kNewTab: {
            Space& space = active_space();
            DetachActiveTabObservers();
            space.AppendTab(Tab{NextTabId()});
            Tab* appended = active_tab();
            if (chrome_ != nullptr && appended != nullptr) {
                // Same creation path as OnWindowCreated's first view: the
                // space's request context and the tab's startup URL (the fixed
                // local data startup page unless session restore set one).
                CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
                    this, CefString(StartupUrlForTab(*appended)), CefBrowserSettings(), nullptr,
                    space.request_context(), this);
                appended->SetBrowserView(browser_view);
            }
            AttachActiveTabObservers();
            return;
        }
        case BrowserCommand::kCloseTab: {
            const Space& space = active_space();
            if (!space.has_active_tab()) {
                return;
            }
            static_cast<void>(CloseActiveSpaceTabIndex(space.active_tab_index()));
            return;
        }
        case BrowserCommand::kNextTab: {
            Space& space = active_space();
            if (space.tab_count() < 2) {
                return;
            }
            DetachActiveTabObservers();
            space.SelectNextTab();
            BreakSplitIfSelectionLeftPair(space, space.active_tab_id());
            AttachActiveTabObservers();
            return;
        }
        case BrowserCommand::kPreviousTab: {
            Space& space = active_space();
            if (space.tab_count() < 2) {
                return;
            }
            DetachActiveTabObservers();
            space.SelectPreviousTab();
            BreakSplitIfSelectionLeftPair(space, space.active_tab_id());
            AttachActiveTabObservers();
            return;
        }
        case BrowserCommand::kToggleSplit: {
            Space& space = active_space();
            if (space.split().has_value()) {
                static_cast<void>(UnsplitActiveSpace());
                return;
            }
            if (space.tab_count() < 2) {
                return;
            }
            // The "split with the adjacent tab" affordance: the right neighbor,
            // or the left one when the active tab is the last in the space.
            const std::size_t active_index = space.active_tab_index();
            const std::size_t other_index =
                active_index + 1 < space.tab_count() ? active_index + 1 : active_index - 1;
            static_cast<void>(SplitTabs(space.active_tab_id(), space.tabs()[other_index].id()));
            return;
        }
        case BrowserCommand::kMoveDividerLeft:
            static_cast<void>(SetSplitRatio(split_ratio_ - BrowserChrome::SplitRatioStep()));
            return;
        case BrowserCommand::kMoveDividerRight:
            static_cast<void>(SetSplitRatio(split_ratio_ + BrowserChrome::SplitRatioStep()));
            return;
        case BrowserCommand::kNewSpace: {
            DetachActiveTabObservers();
            const std::size_t created_index = spaces_.size();
            // A monotonic counter, not the vector size, so names stay unique
            // after spaces are removed and recreated.
            Space& created = spaces_.emplace_back(
                SpaceId{NextSpaceId()}, "Space " + std::to_string(++created_space_count_),
                kSpaceColorPalette[created_index % kSpaceColorPalette.size()]);
            created.AppendTab(Tab{NextTabId()});
            active_space_index_ = created_index;
            AttachActiveTabObservers();
            return;
        }
        case BrowserCommand::kCloseSpace: {
            // RemoveSpaceAtIndex returns the removed space so its browsers are
            // closed after the model mutation, exactly like the tab-close path.
            Space removed = RemoveSpaceAtIndex(active_space_index_);
            AttachActiveTabObservers();
            CloseSpaceBrowsers(removed);
            return;
        }
    }
}

bool BrowserWindow::SelectActiveSpaceTabIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    Space& space = active_space();
    if (index >= space.tab_count()) {
        return false;
    }
    if (index == space.active_tab_index()) {
        return true;
    }
    DetachActiveTabObservers();
    space.SelectTabIndex(index);
    BreakSplitIfSelectionLeftPair(space, space.active_tab_id());
    AttachActiveTabObservers();
    return true;
}

bool BrowserWindow::SelectSpaceIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || index >= spaces_.size()) {
        return false;
    }
    if (index == active_space_index_) {
        return true;
    }
    DetachActiveTabObservers();
    active_space_index_ = index;
    AttachActiveTabObservers();
    return true;
}

bool BrowserWindow::CloseActiveSpaceTabIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    Space& space = active_space();
    if (index >= space.tab_count()) {
        return false;
    }

    if (space.tab_count() == 1) {
        // Encoded fallback: closing the last tab of a space closes that space,
        // which may in turn recreate a fresh default space when it was the
        // last remaining one. The model is mutated before any CEF teardown.
        Space removed = RemoveSpaceAtIndex(active_space_index_);
        AttachActiveTabObservers();
        CloseSpaceBrowsers(removed);
        return true;
    }

    DetachActiveTabObservers();
    const std::optional<Tab> removed = active_space().RemoveTab(space.tabs()[index].id());
    AttachActiveTabObservers();
    if (removed.has_value() && removed->browser() != nullptr) {
        removed->browser()->GetHost()->CloseBrowser(true);
    }
    return true;
}

bool BrowserWindow::RenameSpace(std::size_t space_index, std::string name) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_index >= spaces_.size() || name.empty()) {
        return false;
    }
    spaces_[space_index].Rename(std::move(name));
    UpdateChromeCollections();
    return true;
}

bool BrowserWindow::MoveSpace(std::size_t from_index, std::size_t to_index) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || from_index >= spaces_.size() || to_index >= spaces_.size() ||
        from_index == to_index) {
        return false;
    }
    Space moved = std::move(spaces_[from_index]);
    spaces_.erase(spaces_.begin() + static_cast<std::ptrdiff_t>(from_index));
    spaces_.insert(spaces_.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(moved));
    // The active selection follows its space, not the position.
    if (active_space_index_ == from_index) {
        active_space_index_ = to_index;
    } else if (from_index < active_space_index_ && to_index >= active_space_index_) {
        --active_space_index_;
    } else if (from_index > active_space_index_ && to_index <= active_space_index_) {
        ++active_space_index_;
    }
    UpdateChromeCollections();
    return true;
}

const Space* BrowserWindow::FindSpace(SpaceId id) const noexcept {
    for (const Space& space : spaces_) {
        if (space.id() == id) {
            return &space;
        }
    }
    return nullptr;
}

bool BrowserWindow::SplitTabs(TabId first, TabId second) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || first == second) {
        return false;
    }
    Space& space = active_space();
    const std::optional<std::size_t> first_index = space.IndexOfTab(first);
    const std::optional<std::size_t> second_index = space.IndexOfTab(second);
    // Split view is scoped to the active space: a tab that lives in another
    // space (or nowhere) is rejected rather than silently pulled in.
    if (!first_index.has_value() || !second_index.has_value()) {
        return false;
    }
    DetachActiveTabObservers();
    static_cast<void>(space.SetSplit(SplitPairing{first, second}));
    AttachActiveTabObservers();
    return true;
}

bool BrowserWindow::UnsplitActiveSpace() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    Space& space = active_space();
    if (!space.split().has_value()) {
        return false;
    }
    space.ClearSplit();
    AttachActiveTabObservers();
    return true;
}

bool BrowserWindow::SetSplitRatio(double ratio) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    split_ratio_ =
        std::clamp(ratio, BrowserChrome::SplitRatioMin(), BrowserChrome::SplitRatioMax());
    if (chrome_ != nullptr) {
        chrome_->SetSplitRatio(split_ratio_);
    }
    return true;
}

void BrowserWindow::BreakSplitIfSelectionLeftPair(Space& space, TabId selected) {
    if (space.split().has_value() && space.split()->first != selected &&
        space.split()->second != selected) {
        space.ClearSplit();
    }
}

void BrowserWindow::SelectTab(std::size_t index) {
    static_cast<void>(SelectActiveSpaceTabIndex(index));
}

void BrowserWindow::CloseTab(std::size_t index) {
    static_cast<void>(CloseActiveSpaceTabIndex(index));
}

void BrowserWindow::SelectSpace(std::size_t index) { static_cast<void>(SelectSpaceIndex(index)); }

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
    if (snapshot.page_title != projected_page_title_) {
        // Only the active tab publishes here, so a changed title means the tab
        // strip's active entry text changed; skip the rebuild otherwise.
        UpdateChromeCollections();
    }
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

    Tab* owner = FindTabByBrowser(browser);
    if (owner != nullptr) {
        owner->SetBrowser(nullptr);
        owner->SetBrowserView(nullptr);
    }
    if (!closing_) {
        // Tab teardown, not window teardown: the command path already removed
        // the tab from the model before closing its browser, so the window and
        // its remaining tabs stay alive. A browser that closes itself out from
        // under a still-registered tab leaves that tab inert with no browser.
        return;
    }
    DetachChromeAndObservers();
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

    const ChromeTheme theme = ResolvedChromeTheme();
    const Tab* startup_tab = active_tab();
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        this, CefString(startup_tab != nullptr ? StartupUrlForTab(*startup_tab) : initial_url_),
        CefBrowserSettings(), nullptr, active_space().request_context(), this);
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
    UpdateChromeCollections();
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
    // palette opens while web content holds focus. Cmd/Ctrl+Shift+K keeps the
    // search palette: the Phase 3 design fixes plain Cmd/Ctrl+K on the command
    // palette.
    window_->SetAccelerator(kOpenPaletteAccelerator, 'K', false, true, false, true);
    window_->SetAccelerator(kOpenSearchPaletteAccelerator, 'K', true, true, false, true);
    window_->SetAccelerator(kToggleSidebarAccelerator, 'B', false, true, false, true);
    window_->SetAccelerator(kRenameSpaceAccelerator, kVirtualKeyF2, false, false, false, true);
    // Phase 3 tab commands (Cmd/Ctrl+T, Cmd/Ctrl+W, Cmd/Ctrl+Shift+[/],
    // Cmd/Ctrl+1..9). On macOS these never dispatch — the NSMenu key
    // equivalents in main_mac.mm own them — but registration stays uniform so
    // every platform declares its bindings in one place.
    window_->SetAccelerator(kNewTabAccelerator, 'T', false, true, false, true);
    window_->SetAccelerator(kCloseTabAccelerator, 'W', false, true, false, true);
    window_->SetAccelerator(kNextTabAccelerator, ']', true, true, false, true);
    window_->SetAccelerator(kPreviousTabAccelerator, '[', true, true, false, true);
    for (int position = 0; position < 9; ++position) {
        window_->SetAccelerator(kSelectTab1Accelerator + position, '1' + position, false, true,
                                false, true);
    }
    // U6 split view: Cmd/Ctrl+Shift+S toggles the pair; the divider nudges via
    // Shift+Cmd/Ctrl+Left/Right on platforms where SetAccelerator dispatches.
    window_->SetAccelerator(kToggleSplitAccelerator, 'S', true, false, false, true);
    window_->SetAccelerator(kMoveDividerLeftAccelerator, kVirtualKeyLeft, true, false, false, true);
    window_->SetAccelerator(kMoveDividerRightAccelerator, kVirtualKeyRight, true, false, false,
                            true);
    window_->SetAccelerator(kWelcomeDismissAccelerator, kVirtualKeyEscape, false, false, false,
                            true);

    CreateHoverSliver();
    ApplySidebarState();
    // Fresh install: offer the import/appearance welcome once, without
    // blocking browsing (Escape dismisses). The smoke run skips it so the
    // smoke page stays the only surface.
    if (persist_session_ && !prefs_.onboarding_completed) {
        ShowWelcomeFlow();
    }
    // The seam only observes; every chrome mutation it triggers is posted onto the
    // CEF UI thread. It holds a raw BrowserWindow pointer, never a CefRefPtr, so it
    // cannot create a refcount cycle, and OnWindowDestroyed uninstalls it.
    // CefWindowHandle is a pointer on macOS and Windows but an unsigned long
    // X11 XID on Linux, so a plain conversion to the seam's opaque void* is
    // ill-formed there; reinterpret_cast is well-defined for both shapes.
    hover_seam_ = InstallSidebarHoverSeam(
        reinterpret_cast<void*>(window_->GetWindowHandle()),
        [](void* context, int x_dip) {
            static_cast<BrowserWindow*>(context)->OnSidebarHoverPointer(x_dip);
        },
        this);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    closing_ = true;
    DetachChromeAndObservers();
    window_ = nullptr;

    // The window is gone, so this is always the end: with multiple tabs the
    // last browser may already have closed through a tab command, so the old
    // "no browser was ever created" guard would strand the message loop.
    CloseNavigationAndQuitMessageLoop();
}

void BrowserWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect& new_bounds) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    chrome_snapshot_.rail_bounds = {
        .x = 0,
        .y = 0,
        // The hidden rail reports 0 width so observers never see a snapshot
        // claiming a 286 DIP rail while the sidebar is collapsed.
        .width = sidebar_state_.RailWidthDip(
            std::min(ChromeTokens::ForTheme(ChromeTheme::kLight).rail_width_dip, new_bounds.width)),
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
    if (command_palette_ != nullptr) {
        command_palette_->UpdateBounds();
    }
    if (space_rename_overlay_ != nullptr) {
        space_rename_overlay_->UpdateBounds();
    }
    ApplySidebarState();
    PublishChromeSnapshot();
}

void BrowserWindow::OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_) {
        // With a forced preference this re-asserts the same theme; with
        // kSystem it re-classifies, so the chrome follows the OS.
        ApplyTheme(window, ResolvedChromeTheme(), false);
    }
}

CefSize BrowserWindow::GetMinimumSize(CefRefPtr<CefView>) {
    return CefSize(kMinimumWindowWidth, kMinimumWindowHeight);
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    const Tab* active = active_tab();
    if (!closing_) {
        // The window close begins here (CanClose is re-entered as each browser
        // accepts the close), so every later OnBeforeClose is teardown, never a
        // tab close. Inactive tabs keep detached browsers alive, so they are
        // force-closed now; the attached active browser closes with the
        // window's view tree through TryCloseBrowser below. The startup pages
        // carry no unload handlers, so the cancel path TryCloseBrowser allows
        // for cannot arise in practice.
        closing_ = true;
        // Clean quit: persist the session before any browser tears down, so
        // the last-committed URLs are still in the navigation snapshots. The
        // smoke run never touches the session file.
        if (persist_session_) {
            SaveSession();
        }
        for (Space& space : spaces_) {
            for (Tab& tab : space.tabs()) {
                if (tab.browser() == nullptr ||
                    (active != nullptr && tab.browser()->IsSame(active->browser()))) {
                    continue;
                }
                tab.browser()->GetHost()->CloseBrowser(true);
            }
        }
    }
    if (active == nullptr || active->browser() == nullptr) {
        return true;
    }
    return active->browser()->GetHost()->TryCloseBrowser();
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
            ShowCommandPalette();
            return true;
        case kOpenSearchPaletteAccelerator:
            ShowSearchPalette();
            return true;
        case kToggleSidebarAccelerator:
            ToggleSidebar();
            return true;
        case kRenameSpaceAccelerator:
            BeginSpaceRenaming();
            return true;
        case kNewTabAccelerator:
            ExecuteCommand(BrowserCommand::kNewTab);
            return true;
        case kCloseTabAccelerator:
            ExecuteCommand(BrowserCommand::kCloseTab);
            return true;
        case kNextTabAccelerator:
            ExecuteCommand(BrowserCommand::kNextTab);
            return true;
        case kPreviousTabAccelerator:
            ExecuteCommand(BrowserCommand::kPreviousTab);
            return true;
        case kToggleSplitAccelerator:
            ExecuteCommand(BrowserCommand::kToggleSplit);
            return true;
        case kMoveDividerLeftAccelerator:
            ExecuteCommand(BrowserCommand::kMoveDividerLeft);
            return true;
        case kMoveDividerRightAccelerator:
            ExecuteCommand(BrowserCommand::kMoveDividerRight);
            return true;
        case kWelcomeDismissAccelerator: {
            const bool consumed = welcome_ != nullptr && welcome_->visible();
            if (consumed) {
                // Escape on the welcome flow means "just start browsing".
                OnWelcomeCompleted({});
            }
            return consumed;
        }
        default:
            if (command_id >= kSelectTab1Accelerator && command_id <= kSelectTab9Accelerator) {
                static_cast<void>(SelectActiveSpaceTabIndex(
                    static_cast<std::size_t>(command_id - kSelectTab1Accelerator)));
                return true;
            }
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

void BrowserWindow::DetachActiveTabObservers() {
    CEF_REQUIRE_UI_THREAD();
    Tab* tab = active_tab();
    if (tab != nullptr) {
        tab->navigation_state().SetObserver(nullptr);
    }
}

void BrowserWindow::AttachActiveTabObservers() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    Tab* tab = active_tab();
    if (tab == nullptr) {
        UpdateChromeCollections();
        return;
    }
    // SetObserver pushes the tab's current snapshot through OnNavigationChanged,
    // which re-projects the address model, chrome, and external observers — the
    // same path the CEF load callbacks use.
    tab->navigation_state().SetObserver(this);
    UpdateChromeCollections();
    UpdateWindowTitle();
    AttachActiveTabBrowserView();
    FocusBrowserView();
}

void BrowserWindow::AttachActiveTabBrowserView() {
    CEF_REQUIRE_UI_THREAD();
    if (chrome_ == nullptr) {
        return;
    }
    Space& space = active_space();
    if (space.split().has_value()) {
        Tab* first = space.FindTab(space.split()->first);
        Tab* second = space.FindTab(space.split()->second);
        // A restored split partner has no view until first shown; both panes
        // are built on the same path as kNewTab before the split attaches.
        if (first != nullptr && second != nullptr && EnsureTabBrowserView(*first) &&
            EnsureTabBrowserView(*second)) {
            chrome_->AttachSplitBrowserViews(first->browser_view(), second->browser_view());
            return;
        }
    }
    Tab* tab = active_tab();
    if (tab == nullptr || !EnsureTabBrowserView(*tab)) {
        return;
    }
    chrome_->AttachBrowserView(tab->browser_view());
}

bool BrowserWindow::EnsureTabBrowserView(Tab& tab) {
    if (tab.browser_view() != nullptr) {
        return true;
    }
    if (chrome_ == nullptr) {
        return false;
    }
    // Only valid for tabs of the active space: the view carries the space's
    // request context and the tab's validated startup URL, matching kNewTab.
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        this, CefString(StartupUrlForTab(tab)), CefBrowserSettings(), nullptr,
        active_space().request_context(), this);
    tab.SetBrowserView(browser_view);
    return true;
}

void BrowserWindow::UpdateChromeCollections() {
    CEF_REQUIRE_UI_THREAD();
    if (chrome_ == nullptr || closing_) {
        return;
    }
    const Tab* active = active_tab();
    std::vector<TabStripEntrySnapshot> tabs;
    tabs.reserve(active_space().tab_count());
    for (const Tab& tab : active_space().tabs()) {
        const std::string& page_title = tab.navigation_state().snapshot().page_title;
        tabs.push_back({.title = page_title.empty() ? "Island" : page_title,
                        .active = active != nullptr && tab.id() == active->id()});
    }
    std::vector<SpaceSwitcherEntrySnapshot> spaces;
    spaces.reserve(spaces_.size());
    for (std::size_t index = 0; index < spaces_.size(); ++index) {
        spaces.push_back({.color = spaces_[index].color(),
                          .name = spaces_[index].name(),
                          .active = index == active_space_index_});
    }
    if (active != nullptr) {
        projected_page_title_ = active->navigation_state().snapshot().page_title;
    }
    chrome_->SetTabStripEntries(tabs);
    chrome_->SetSpaceSwitcherEntries(spaces);
}

void BrowserWindow::CloseSpaceBrowsers(Space& space) {
    CEF_REQUIRE_UI_THREAD();
    for (Tab& tab : space.tabs()) {
        if (tab.browser() != nullptr) {
            tab.browser()->GetHost()->CloseBrowser(true);
        }
    }
}

Space BrowserWindow::RemoveSpaceAtIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    DetachActiveTabObservers();
    Space removed = std::move(spaces_[index]);
    spaces_.erase(spaces_.begin() + static_cast<std::ptrdiff_t>(index));
    if (spaces_.empty()) {
        // Encoded fallback: closing the last remaining space recreates a single
        // fresh default space instead of leaving the window with zero.
        spaces_.push_back(CreateDefaultSpace());
        active_space_index_ = 0;
    } else if (index == active_space_index_) {
        // Encoded fallback: the previous neighbor becomes active; at the front
        // the next space (now at index 0) takes over.
        active_space_index_ = index > 0 ? index - 1 : 0;
    } else if (index < active_space_index_) {
        --active_space_index_;
    }
    return removed;
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
    if (command_palette_ != nullptr) {
        command_palette_->ApplyTheme(tokens);
    }
    if (space_rename_overlay_ != nullptr) {
        space_rename_overlay_->ApplyTheme(tokens);
    }
    chrome_snapshot_.theme = theme;
    ApplySidebarState();
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
    UninstallHoverSeam();
    if (hover_sliver_overlay_ != nullptr) {
        if (hover_sliver_overlay_->IsValid()) {
            hover_sliver_overlay_->SetVisible(false);
            hover_sliver_overlay_->Destroy();
        }
        hover_sliver_overlay_ = nullptr;
    }
    hover_sliver_ = nullptr;
    if (search_palette_ != nullptr) {
        search_palette_->Detach();
        search_palette_.reset();
    }
    if (command_palette_ != nullptr) {
        command_palette_->Detach();
        command_palette_.reset();
    }
    if (space_rename_overlay_ != nullptr) {
        space_rename_overlay_->Detach();
        space_rename_overlay_.reset();
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
        // Lazily created on the first Cmd/Ctrl+Shift+K, then only shown and hidden.
        search_palette_ = std::make_unique<SearchPalette>(
            *this, window_, ChromeTokens::ForTheme(ResolvedChromeTheme()));
    }
    search_palette_->Show();
}

void BrowserWindow::ShowCommandPalette() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    if (command_palette_ == nullptr) {
        // Lazily created on the first Cmd/Ctrl+K, then only shown and hidden.
        command_palette_ = std::make_unique<CommandPaletteView>(
            *this, window_, ChromeTokens::ForTheme(ResolvedChromeTheme()), &ParseAndValidate);
    }
    // Read-only snapshots taken at open time: the open tabs of the active space
    // (title/URL for matching, TabId for routing) and the whole space list.
    std::vector<PaletteTabEntry> tabs;
    tabs.reserve(active_space().tab_count());
    for (const Tab& tab : active_space().tabs()) {
        const NavigationSnapshot& nav = tab.navigation_state().snapshot();
        tabs.push_back({.id = tab.id(),
                        .title = nav.page_title.empty() ? "Island" : nav.page_title,
                        .url = nav.url});
    }
    std::vector<PaletteSpaceEntry> spaces;
    spaces.reserve(spaces_.size());
    for (const Space& space : spaces_) {
        spaces.push_back({.id = space.id(), .name = space.name()});
    }
    std::vector<PaletteBookmarkEntry> bookmarks;
    for (const BookmarkFolder& folder : bookmarks_.folders) {
        for (const BookmarkItem& item : folder.items) {
            bookmarks.push_back({.title = item.title, .url = item.url});
        }
    }
    command_palette_->Show(std::move(tabs), std::move(spaces), std::move(bookmarks));
}

void BrowserWindow::OnCommandPaletteSubmitted(const PaletteSelection& selection,
                                              std::string_view url_query) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    switch (selection.kind) {
        case PaletteEntryKind::kTab:
            ActivatePaletteTab(selection.tab.id);
            break;
        case PaletteEntryKind::kSpace:
            ActivatePaletteSpace(selection.space.id);
            break;
        case PaletteEntryKind::kUrl:
            SubmitPaletteUrl(url_query, selection.address);
            break;
        case PaletteEntryKind::kBookmark:
            OpenBookmark(selection.bookmark);
            break;
    }
    FocusBrowserView();
}

void BrowserWindow::OnCommandPaletteDismissed() {
    CEF_REQUIRE_UI_THREAD();
    // Escape restores focus to the invocation point, which is the browser view
    // by default.
    FocusBrowserView();
}

ChromeTheme BrowserWindow::ResolvedChromeTheme() const {
    switch (prefs_.theme) {
        case ThemePreference::kLight:
            return ChromeTheme::kLight;
        case ThemePreference::kDark:
            return ChromeTheme::kDark;
        case ThemePreference::kSystem:
            break;
    }
    return window_ != nullptr
               ? ClassifyChromeTheme(window_->GetThemeColor(CEF_ColorPrimaryBackground))
               : ChromeTheme::kLight;
}

void BrowserWindow::SavePrefs() const {
    // Headless and smoke shapes keep fresh-install defaults and never write
    // the prefs file, mirroring the session file rule.
    if (!persist_session_) {
        return;
    }
    static_cast<void>(PrefsStore::Save(PrefsStore::DefaultPrefsFilePath(), prefs_));
}

bool BrowserWindow::SetThemePreference(ThemePreference preference) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    prefs_.theme = preference;
    SavePrefs();
    if (window_ != nullptr) {
        ApplyTheme(window_, ResolvedChromeTheme(), true);
    }
    return true;
}

void BrowserWindow::OnWelcomeThemeChanged(ThemePreference preference) {
    CEF_REQUIRE_UI_THREAD();
    static_cast<void>(SetThemePreference(preference));
    if (welcome_ != nullptr) {
        welcome_->ApplyTheme(ChromeTokens::ForTheme(ResolvedChromeTheme()));
    }
}

void BrowserWindow::OnWelcomeCompleted(const std::vector<ImportSource>& sources) {
    CEF_REQUIRE_UI_THREAD();
    std::size_t imported = 0;
    for (const ImportSource source : sources) {
        const std::optional<std::vector<BookmarkItem>> items =
            ImportFromSource(source, UserBaseHome());
        if (!items.has_value()) {
            continue;
        }
        imported += BookmarkStore::MergeFolder(bookmarks_, "Imported", *items);
    }
    if (imported > 0) {
        static_cast<void>(
            BookmarkStore::Save(BookmarkStore::DefaultBookmarksFilePath(), bookmarks_));
    }
    prefs_.onboarding_completed = true;
    SavePrefs();
    if (welcome_ != nullptr) {
        welcome_->Hide();
    }
    FocusBrowserView();
}

void BrowserWindow::ShowWelcomeFlow() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    if (welcome_ == nullptr) {
        welcome_ = std::make_unique<WelcomeFlow>(
            *this, window_, ChromeTokens::ForTheme(ResolvedChromeTheme()), prefs_.theme,
            DetectInstalledSources(UserBaseHome()));
    }
    welcome_->Show();
}

void BrowserWindow::OpenBookmark(const PaletteBookmarkEntry& bookmark) {
    CEF_REQUIRE_UI_THREAD();
    // The same validation seam the address bar uses: a stored URL the current
    // allow-list rejects is a no-op, never a force-load.
    const ValidatedAddress address = ParseAndValidate(bookmark.url);
    if (!address.is_valid()) {
        return;
    }
    SubmitPaletteUrl(bookmark.url, address);
}

void BrowserWindow::ActivatePaletteTab(TabId id) {
    CEF_REQUIRE_UI_THREAD();
    for (std::size_t space_index = 0; space_index < spaces_.size(); ++space_index) {
        const Space& space = spaces_[space_index];
        for (std::size_t tab_index = 0; tab_index < space.tab_count(); ++tab_index) {
            if (space.tabs()[tab_index].id() != id) {
                continue;
            }
            if (SelectSpaceIndex(space_index)) {
                static_cast<void>(SelectActiveSpaceTabIndex(tab_index));
            }
            return;
        }
    }
}

void BrowserWindow::ActivatePaletteSpace(SpaceId id) {
    CEF_REQUIRE_UI_THREAD();
    for (std::size_t index = 0; index < spaces_.size(); ++index) {
        if (spaces_[index].id() == id) {
            static_cast<void>(SelectSpaceIndex(index));
            return;
        }
    }
}

void BrowserWindow::SubmitPaletteUrl(std::string_view query, const ValidatedAddress& address) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // The exact address-submission path the rail's address control uses: the
    // palette already routed the query through the same ParseAndValidate seam,
    // so the model consumes one ValidatedAddress and there is no second URL
    // parser anywhere in the palette.
    address_bar_model_.SetEditText(std::string(query));
    const std::optional<std::string> url = address_bar_model_.Submit(address);
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

void BrowserWindow::BeginSpaceRenaming() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    if (space_rename_overlay_ == nullptr) {
        space_rename_overlay_ = std::make_unique<SpaceRenameOverlay>(
            *this, window_, ChromeTokens::ForTheme(ResolvedChromeTheme()), active_space().name());
    }
    space_rename_overlay_->Show();
}

void BrowserWindow::OnSpaceRenameCommitted(std::string name) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    static_cast<void>(RenameSpace(active_space_index_, std::move(name)));
    FocusBrowserView();
}

void BrowserWindow::OnSpaceRenameCancelled() {
    CEF_REQUIRE_UI_THREAD();
    FocusBrowserView();
}

bool BrowserWindow::MoveActiveSpace(int delta) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || delta == 0) {
        return false;
    }
    const std::ptrdiff_t target =
        static_cast<std::ptrdiff_t>(active_space_index_) + static_cast<std::ptrdiff_t>(delta);
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(spaces_.size())) {
        return false;
    }
    return MoveSpace(active_space_index_, static_cast<std::size_t>(target));
}

void BrowserWindow::ToggleSidebar() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    sidebar_state_.Toggle();
    ApplySidebarState();
}

void BrowserWindow::OnSidebarHoverPointer(int x_dip) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || chrome_ == nullptr) {
        return;
    }
    const bool was_revealed = sidebar_state_.revealed();
    sidebar_state_.OnPointerMoved(x_dip,
                                  ChromeTokens::ForTheme(chrome_snapshot_.theme).rail_width_dip);
    if (sidebar_state_.revealed() != was_revealed) {
        ApplySidebarState();
    }
}

void BrowserWindow::CreateHoverSliver() {
    CEF_REQUIRE_UI_THREAD();
    if (window_ == nullptr || hover_sliver_ != nullptr) {
        return;
    }
    hover_sliver_ = CefPanel::CreatePanel(new HoverSliverDelegate());
    hover_sliver_->SetID(static_cast<int>(ChromeViewId::kHoverSliver));
    hover_sliver_->SetBackgroundColor(ChromeTokens::ForTheme(chrome_snapshot_.theme).accent.argb);
    // can_activate is false: a 2-DIP edge marker must never take keyboard focus.
    hover_sliver_overlay_ =
        window_->AddOverlayView(hover_sliver_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/false);
}

void BrowserWindow::ApplySidebarState() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const bool revealed = sidebar_state_.revealed();
    if (chrome_ != nullptr) {
        chrome_->SetSidebarRevealed(revealed);
    }
    if (hover_sliver_overlay_ != nullptr && window_ != nullptr) {
        const CefSize window_size = window_->GetSize();
        hover_sliver_->SetVisible(sidebar_state_.sliver_visible());
        hover_sliver_overlay_->SetVisible(sidebar_state_.sliver_visible());
        hover_sliver_overlay_->SetBounds(CefRect(0, 0, kHoverSliverWidthDip, window_size.height));
    }
    if (hover_sliver_ != nullptr) {
        hover_sliver_->SetBackgroundColor(
            ChromeTokens::ForTheme(chrome_snapshot_.theme).accent.argb);
    }
    // Keep the published snapshot's rail/content split in step with the reveal
    // state so observers never read a stale 286 DIP rail.
    const int window_width =
        chrome_snapshot_.rail_bounds.width + chrome_snapshot_.content_bounds.width;
    const int rail_width = sidebar_state_.RailWidthDip(
        std::min(ChromeTokens::ForTheme(chrome_snapshot_.theme).rail_width_dip, window_width));
    chrome_snapshot_.rail_bounds.width = rail_width;
    chrome_snapshot_.content_bounds.x = rail_width;
    chrome_snapshot_.content_bounds.width = std::max(0, window_width - rail_width);
}

void BrowserWindow::UninstallHoverSeam() {
    if (hover_seam_ != nullptr) {
        RemoveSidebarHoverSeam(hover_seam_);
        hover_seam_ = nullptr;
    }
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
