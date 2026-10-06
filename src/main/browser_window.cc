#include "browser_window.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "app_resources.h"
#include "browser_import.h"
#include "cef_address_parser.h"
#include "design_tokens.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_build.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_frame.h"
#include "include/cef_image.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "session_store.h"
#include "window_agent_host.h"

namespace island {
namespace {

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

std::string CssColor(ArgbColor color) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string css = "#";
    for (int shift = 20; shift >= 0; shift -= 4) {
        css += kHex[(color.argb >> static_cast<unsigned>(shift)) & 0xFU];
    }
    return css;
}

std::string TrimmedCopy(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

bool IsWebUrl(std::string_view url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

// The stdio MCP bridge ships next to the browser binary; agents without MCP
// over HTTP launch it. Empty when it is not there.
std::string McpBridgePath() {
    std::error_code error;
    const std::filesystem::path binary = CurrentRuntimeBinaryPath();
#if defined(_WIN32)
    const std::filesystem::path bridge = binary.parent_path() / "island_mcp_bridge.exe";
#else
    const std::filesystem::path bridge = binary.parent_path() / "island_mcp_bridge";
#endif
    return std::filesystem::is_regular_file(bridge, error) ? bridge.string() : std::string();
}

void RunAgentTask(std::function<void()> task) {
    if (task) {
        task();
    }
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
    : initial_url_(std::move(initial_url)),
      persist_session_(persist_session),
      address_validator_(&ParseAndValidate) {
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
    keymap_ = Keymap::WithOverrides(prefs_.keybindings);
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

BrowserWindow::~BrowserWindow() = default;

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
            tab.SetPinned(tab_state.pinned);
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
            space_state.tabs.push_back(
                TabState{tab.id(), tab.navigation_state().snapshot().url, tab.pinned()});
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
        case BrowserCommand::kNewTab:
            AppendTabToActiveSpace({});
            return;
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
        case BrowserCommand::kTogglePinTab: {
            const Space& space = active_space();
            if (space.has_active_tab()) {
                const std::size_t index = space.active_tab_index();
                static_cast<void>(SetActiveSpaceTabPinned(index, !space.tabs()[index].pinned()));
            }
            return;
        }
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

void BrowserWindow::AppendTabToActiveSpace(std::string startup_url) {
    Space& space = active_space();
    DetachActiveTabObservers();
    Tab tab{NextTabId()};
    tab.SetStartupUrl(std::move(startup_url));
    space.AppendTab(std::move(tab));
    Tab* appended = active_tab();
    if (chrome_ != nullptr && appended != nullptr) {
        // Same creation path as OnWindowCreated's first view: the space's
        // request context and the tab's startup URL (the fixed local data
        // startup page unless session restore or an agent set one).
        CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
            this, CefString(StartupUrlForTab(*appended)), CefBrowserSettings(), nullptr,
            space.request_context(), this);
        appended->SetBrowserView(browser_view);
    }
    AttachActiveTabObservers();
}

bool BrowserWindow::OpenNewTab(std::string_view text) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    std::string url;
    if (!text.empty()) {
        const std::optional<std::string> resolved =
            ResolveAgentNavigation(text, address_validator_);
        if (!resolved.has_value()) {
            return false;
        }
        url = *resolved;
    }
    AppendTabToActiveSpace(std::move(url));
    return true;
}

bool BrowserWindow::NavigateTab(std::optional<std::size_t> index, std::string_view text) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    Space& space = active_space();
    if (index.has_value() && *index >= space.tab_count()) {
        return false;
    }
    if (!index.has_value() && !space.has_active_tab()) {
        return false;
    }
    const std::optional<std::string> url = ResolveAgentNavigation(text, address_validator_);
    if (!url.has_value()) {
        return false;
    }
    Tab& tab = space.tabs()[index.value_or(space.active_tab_index())];
    if (index.value_or(space.active_tab_index()) == space.active_tab_index()) {
        // The active tab goes through the address model so the rail shows the
        // committed URL exactly as for typed navigation.
        address_bar_model_.SetEditText(*url);
        static_cast<void>(address_bar_model_.Submit(ValidatedAddress{.url = *url}));
        if (chrome_ != nullptr) {
            chrome_->OnAddressChanged(address_bar_model_.snapshot());
        }
    }
    if (tab.browser() != nullptr && tab.browser()->GetMainFrame() != nullptr) {
        tab.browser()->GetMainFrame()->LoadURL(*url);
    } else {
        tab.SetStartupUrl(*url);
    }
    return true;
}

CefRefPtr<CefBrowser> BrowserWindow::BrowserForTab(std::optional<std::size_t> index) const {
    if (closing_) {
        return nullptr;
    }
    const Space& space = active_space();
    if (index.has_value()) {
        return *index < space.tab_count() ? space.tabs()[*index].browser() : nullptr;
    }
    const Tab* tab = active_tab();
    return tab != nullptr ? tab->browser() : nullptr;
}

bool BrowserWindow::CreateSpace(std::string_view name) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    ExecuteCommand(BrowserCommand::kNewSpace);
    if (!name.empty()) {
        static_cast<void>(RenameSpace(active_space_index_, std::string(name)));
    }
    return true;
}

bool BrowserWindow::SetActiveSpaceTabPinned(std::size_t index, bool pinned) {
    CEF_REQUIRE_UI_THREAD();
    Space& space = active_space();
    if (closing_ || index >= space.tab_count()) {
        return false;
    }
    if (!space.SetTabPinned(space.tabs()[index].id(), pinned)) {
        return false;
    }
    UpdateChromeCollections();
    return true;
}

std::string BrowserWindow::ResolvedAgentCommand() const {
    const char* const configured = std::getenv("ISLAND_AGENT_COMMAND");
    if (configured != nullptr && *configured != '\0') {
        return configured;
    }
    if (!prefs_.agent_command.empty()) {
        return prefs_.agent_command;
    }
    return std::string(kDefaultAgentCommand);
}

agent::AgentSessionConfig BrowserWindow::AgentConfig(std::string command) const {
    agent::AgentSessionConfig config;
    config.command = command.empty() ? ResolvedAgentCommand() : std::move(command);
    config.cwd = UserBaseHome();
    const agent::AgentEndpoint* endpoint =
        agent_host_ != nullptr ? agent_host_->endpoint() : nullptr;
    if (endpoint != nullptr && endpoint->running()) {
        config.mcp_servers.push_back({.name = "island-browser",
                                      .url = endpoint->url(),
                                      .bearer_token = endpoint->token(),
                                      .bridge_command = McpBridgePath()});
    }
    return config;
}

void BrowserWindow::EnsureAgentSession() {
    if (agent_session_ != nullptr) {
        return;
    }
    // Process output is hopped onto the UI thread; the session drops tasks
    // that arrive after it was destroyed.
    agent_session_ = std::make_unique<agent::AgentSession>(
        [](std::function<void()> task) {
            CefPostTask(TID_UI,
                        CefCreateClosureTask(base::BindOnce(&RunAgentTask, std::move(task))));
        },
        [this] { ScheduleLocalPagesRender(); });
    agent_session_->Configure(AgentConfig({}));
}

bool BrowserWindow::agent_panel_open() const noexcept {
    return chrome_ != nullptr && chrome_->agent_panel_open();
}

void BrowserWindow::ToggleAgentPanel() { SetAgentPanelOpen(!agent_panel_open()); }

void BrowserWindow::SetAgentPanelOpen(bool open) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || chrome_ == nullptr) {
        return;
    }
    if (open && agent_panel_ == nullptr) {
        EnsureAgentSession();
        agent_panel_ = std::make_unique<LocalPage>(LocalPageKind::kAgent, *this);
        chrome_->SetAgentPanelView(agent_panel_->view());
    }
    chrome_->SetAgentPanelOpen(open);
    ApplySidebarState();
    PublishChromeSnapshot();
    if (prefs_.agent_panel_open != open) {
        prefs_.agent_panel_open = open;
        SavePrefs();
    }
    if (open) {
        ScheduleLocalPagesRender();
        agent_panel_->Focus();
    } else {
        FocusBrowserView();
    }
}

std::string BrowserWindow::ActiveTabAgentContext() const {
    const Tab* tab = active_tab();
    if (tab == nullptr) {
        return {};
    }
    const NavigationSnapshot& nav = tab->navigation_state().snapshot();
    if (!IsWebUrl(nav.url)) {
        return {};
    }
    return "The user is looking at the tab \"" + nav.page_title + "\" (" + nav.url +
           ") in the Island browser. Use the island-browser tools (page_read, page_snapshot, "
           "page_click, page_type, browser_*) to see and act on the browser.";
}

std::string BrowserWindow::AgentPanelStateJson() const {
    json::Value theme = ThemeJson();
    json::Value context = json::Value::Null();
    if (const Tab* tab = active_tab(); tab != nullptr) {
        const NavigationSnapshot& nav = tab->navigation_state().snapshot();
        if (IsWebUrl(nav.url)) {
            context = json::Value::MakeObject()
                          .Set("title", json::Value::String(nav.page_title))
                          .Set("url", json::Value::String(nav.url));
        }
    }
    const agent::AgentEndpoint* endpoint =
        agent_host_ != nullptr ? agent_host_->endpoint() : nullptr;
    json::Value state =
        json::Value::MakeObject()
            .Set("theme", std::move(theme))
            .Set("context", std::move(context))
            .Set("endpoint",
                 json::Value::String(endpoint != nullptr && endpoint->running() ? endpoint->url()
                                                                                : std::string()));
    if (agent_session_ != nullptr) {
        std::optional<json::Value> session = json::Parse(agent_session_->StateJson());
        if (session.has_value()) {
            state.Set("session", std::move(*session));
        }
    }
    return json::Serialize(state);
}

void BrowserWindow::ScheduleLocalPagesRender() {
    if ((agent_panel_ == nullptr && !internal_page_.has_value()) || agent_panel_render_scheduled_ ||
        closing_) {
        return;
    }
    agent_panel_render_scheduled_ = true;
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(&BrowserWindow::FlushLocalPagesRender,
                                                           CefRefPtr<BrowserWindow>(this))),
                       30);
}

void BrowserWindow::FlushLocalPagesRender() {
    agent_panel_render_scheduled_ = false;
    if (closing_) {
        return;
    }
    if (agent_panel_ != nullptr) {
        agent_panel_->Render(AgentPanelStateJson());
    }
    if (internal_page_ == LocalPageKind::kSettings && settings_page_ != nullptr) {
        settings_page_->Render(SettingsStateJson());
        settings_message_.clear();
    } else if (internal_page_ == LocalPageKind::kTabOverview && overview_page_ != nullptr) {
        overview_page_->Render(TabOverviewStateJson());
    }
}

void BrowserWindow::OnLocalPageMessage(LocalPageKind kind, const json::Value& message) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    switch (kind) {
        case LocalPageKind::kAgent:
            HandleAgentPageMessage(message);
            return;
        case LocalPageKind::kSettings:
            HandleSettingsMessage(message);
            return;
        case LocalPageKind::kTabOverview:
            HandleOverviewMessage(message);
            return;
    }
}

json::Value BrowserWindow::ThemeJson() const {
    const ChromeTokens tokens = ResolvedTokens(chrome_snapshot_.theme);
    return json::Value::MakeObject()
        .Set("bg", json::Value::String(CssColor(tokens.background)))
        .Set("surface", json::Value::String(CssColor(tokens.surface)))
        .Set("surface_2", json::Value::String(CssColor(tokens.surface_secondary)))
        .Set("text", json::Value::String(CssColor(tokens.text)))
        .Set("text_2", json::Value::String(CssColor(tokens.text_secondary)))
        .Set("border", json::Value::String(CssColor(tokens.border)))
        .Set("accent", json::Value::String(CssColor(tokens.accent)))
        .Set("dark", json::Value::Bool(chrome_snapshot_.theme == ChromeTheme::kDark));
}

LocalPage* BrowserWindow::InternalPageFor(LocalPageKind kind) const {
    switch (kind) {
        case LocalPageKind::kSettings:
            return settings_page_.get();
        case LocalPageKind::kTabOverview:
            return overview_page_.get();
        case LocalPageKind::kAgent:
            return nullptr;
    }
    return nullptr;
}

void BrowserWindow::ToggleTabOverview() { ToggleInternalPage(LocalPageKind::kTabOverview); }

void BrowserWindow::ToggleSettings() { ToggleInternalPage(LocalPageKind::kSettings); }

void BrowserWindow::ToggleInternalPage(LocalPageKind kind) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || chrome_ == nullptr || kind == LocalPageKind::kAgent) {
        return;
    }
    if (internal_page_ == kind) {
        HideInternalPage();
        return;
    }
    std::unique_ptr<LocalPage>& page =
        kind == LocalPageKind::kSettings ? settings_page_ : overview_page_;
    if (page == nullptr) {
        page = std::make_unique<LocalPage>(kind, *this);
    }
    internal_page_ = kind;
    chrome_->AttachBrowserView(page->view());
    FlushLocalPagesRender();
    page->Focus();
}

void BrowserWindow::HideInternalPage() {
    if (!internal_page_.has_value()) {
        return;
    }
    internal_page_.reset();
    AttachActiveTabBrowserView();
    FocusBrowserView();
}

std::string BrowserWindow::SettingsStateJson() const {
#if defined(__APPLE__)
    const std::string platform = "mac";
#elif defined(_WIN32)
    const std::string platform = "windows";
#else
    const std::string platform = "linux";
#endif
    using json::Value;
    const char* const env_command = std::getenv("ISLAND_AGENT_COMMAND");
    Value agent =
        Value::MakeObject()
            .Set("command", Value::String(prefs_.agent_command))
            .Set("default_command", Value::String(std::string(kDefaultAgentCommand)))
            .Set("env_override", Value::Bool(env_command != nullptr && *env_command != '\0'));

    Value endpoint = Value::MakeObject().Set("running", Value::Bool(false));
    const agent::AgentEndpoint* live = agent_host_ != nullptr ? agent_host_->endpoint() : nullptr;
    if (live != nullptr && live->running()) {
        // The snippet MCP clients paste: the HTTP transport with the bearer
        // token for this launch.
        const std::string config =
            "{\n  \"mcpServers\": {\n    \"island\": {\n      \"type\": \"http\",\n"
            "      \"url\": \"" +
            live->url() +
            "\",\n      \"headers\": {\n"
            "        \"Authorization\": \"Bearer " +
            live->token() + "\"\n      }\n    }\n  }\n}";
        endpoint.Set("running", Value::Bool(true))
            .Set("url", Value::String(live->url()))
            .Set("config_json", Value::String(config))
            .Set("bridge_path", Value::String(McpBridgePath()));
    }

    Value shortcuts = Value::MakeArray();
    std::vector<KeyAction> conflicted;
    for (const auto& [first, second] : keymap_.Conflicts()) {
        conflicted.push_back(first);
        conflicted.push_back(second);
    }
    for (const KeyActionInfo& info : KeyActions()) {
        const std::optional<KeyBinding> binding = keymap_.Binding(info.action);
        const std::optional<KeyBinding> fallback = ParseKeyBinding(info.default_binding);
        shortcuts.Push(
            Value::MakeObject()
                .Set("id", Value::String(std::string(info.id)))
                .Set("label", Value::String(std::string(info.label)))
                .Set("binding", Value::String(binding ? FormatKeyBinding(*binding) : ""))
                .Set("default_binding", Value::String(fallback ? FormatKeyBinding(*fallback) : ""))
                .Set("conflict", Value::Bool(std::find(conflicted.begin(), conflicted.end(),
                                                       info.action) != conflicted.end())));
    }

    Value sources = Value::MakeArray();
    for (const ImportSourceInfo& source : DetectInstalledSources(UserBaseHome())) {
        sources.Push(Value::MakeObject()
                         .Set("id", Value::String(std::string(ImportSourceId(source.id))))
                         .Set("name", Value::String(source.display_name))
                         .Set("available", Value::Bool(source.available))
                         .Set("description", Value::String(ImportSourceDescription(source)))
                         .Set("action",
                              Value::String(source.id == ImportSource::kArc ? "Import spaces"
                                                                            : "Import bookmarks")));
    }

    Value about_rows = Value::MakeArray();
    auto row = [&about_rows](std::string label, std::string value) {
        about_rows.Push(Value::MakeArray()
                            .Push(Value::String(std::move(label)))
                            .Push(Value::String(std::move(value))));
    };
    row("Version", "0.4.0");
    row("Preferences", PrefsStore::DefaultPrefsFilePath().string());
    row("Session", SessionStore::DefaultSessionFilePath().string());
    row("Agent endpoint file", agent::DefaultDiscoveryFilePath().string());

    const std::string_view theme_pref = prefs_.theme == ThemePreference::kLight  ? "light"
                                        : prefs_.theme == ThemePreference::kDark ? "dark"
                                                                                 : "system";
    Value settings = Value::MakeObject()
                         .Set("theme_pref", Value::String(std::string(theme_pref)))
                         .Set("platform", Value::String(platform))
                         .Set("agent", std::move(agent))
                         .Set("endpoint", std::move(endpoint))
                         .Set("shortcuts", std::move(shortcuts))
                         .Set("import", Value::MakeObject().Set("sources", std::move(sources)))
                         .Set("about", Value::MakeObject().Set("rows", std::move(about_rows)))
                         .Set("message", Value::String(settings_message_));
    return json::Serialize(
        Value::MakeObject().Set("theme", ThemeJson()).Set("settings", std::move(settings)));
}

namespace {

// A tab's favicon as a PNG data URL for the overview page, or "".
std::string FaviconDataUrl(const CefRefPtr<CefImage>& favicon) {
    if (favicon == nullptr || favicon->IsEmpty()) {
        return {};
    }
    int width = 0;
    int height = 0;
    CefRefPtr<CefBinaryValue> png = favicon->GetAsPNG(1.0F, true, width, height);
    if (png == nullptr || png->GetSize() == 0) {
        return {};
    }
    std::string bytes(png->GetSize(), '\0');
    png->GetData(bytes.data(), bytes.size(), 0);
    return "data:image/png;base64," + CefBase64Encode(bytes.data(), bytes.size()).ToString();
}

}  // namespace

std::string BrowserWindow::TabOverviewStateJson() const {
    using json::Value;
    Value spaces = Value::MakeArray();
    for (std::size_t space_index = 0; space_index < spaces_.size(); ++space_index) {
        const Space& space = spaces_[space_index];
        Value tabs = Value::MakeArray();
        for (std::size_t tab_index = 0; tab_index < space.tab_count(); ++tab_index) {
            const Tab& tab = space.tabs()[tab_index];
            const NavigationSnapshot& nav = tab.navigation_state().snapshot();
            tabs.Push(
                Value::MakeObject()
                    .Set("index", Value::Int(static_cast<std::int64_t>(tab_index)))
                    .Set("title", Value::String(nav.page_title.empty() ? std::string("New Tab")
                                                                       : nav.page_title))
                    .Set("url", Value::String(nav.url.empty() ? tab.startup_url() : nav.url))
                    .Set("active", Value::Bool(space.has_active_tab() &&
                                               space.active_tab_index() == tab_index))
                    .Set("pinned", Value::Bool(tab.pinned()))
                    .Set("favicon", Value::String(FaviconDataUrl(tab.favicon()))));
        }
        spaces.Push(Value::MakeObject()
                        .Set("index", Value::Int(static_cast<std::int64_t>(space_index)))
                        .Set("name", Value::String(space.name()))
                        .Set("color", Value::String(CssColor(space.color())))
                        .Set("active", Value::Bool(space_index == active_space_index_))
                        .Set("tabs", std::move(tabs)));
    }
    return json::Serialize(
        Value::MakeObject()
            .Set("theme", ThemeJson())
            .Set("overview", Value::MakeObject().Set("spaces", std::move(spaces))));
}

void BrowserWindow::HandleSettingsMessage(const json::Value& message) {
    const std::string_view type = message.StringOr("type", "");
    if (type == "ready") {
        FlushLocalPagesRender();
        return;
    }
    if (type == "close") {
        HideInternalPage();
        return;
    }
    if (type == "set_theme") {
        ThemePreference preference = ThemePreference::kSystem;
        if (ThemePreferenceFromString(message.StringOr("value", ""), preference)) {
            static_cast<void>(SetThemePreference(preference));
        }
    } else if (type == "set_agent_command") {
        prefs_.agent_command = TrimmedCopy(message.StringOr("command", ""));
        SavePrefs();
        if (agent_session_ != nullptr) {
            agent_session_->Configure(AgentConfig({}));
        }
    } else if (type == "open_agent") {
        SetAgentPanelOpen(true);
    } else if (type == "set_binding" || type == "reset_binding") {
        const std::optional<KeyAction> action = KeyActionFromId(message.StringOr("action", ""));
        if (!action.has_value()) {
            return;
        }
        if (type == "reset_binding") {
            static_cast<void>(ResetKeyBinding(*action));
        } else {
            const std::string_view text = message.StringOr("binding", "");
            if (text.empty()) {
                static_cast<void>(SetKeyBinding(*action, std::nullopt));
            } else if (const std::optional<KeyBinding> binding = ParseKeyBinding(text)) {
                static_cast<void>(SetKeyBinding(*action, binding));
            } else {
                settings_message_ = "That shortcut needs Ctrl/Cmd or Alt.";
            }
        }
    } else if (type == "reset_shortcuts") {
        ResetKeymap();
        settings_message_ = "Shortcuts reset to defaults.";
    } else if (type == "import") {
        settings_message_ = ImportFromBrowser(message.StringOr("source", ""));
    }
    ScheduleLocalPagesRender();
}

void BrowserWindow::HandleOverviewMessage(const json::Value& message) {
    const std::string_view type = message.StringOr("type", "");
    const auto index = [&message](std::string_view key) {
        const std::int64_t value = message.IntOr(key, -1);
        return value < 0 ? std::numeric_limits<std::size_t>::max()
                         : static_cast<std::size_t>(value);
    };
    if (type == "ready") {
        FlushLocalPagesRender();
        return;
    }
    if (type == "close") {
        HideInternalPage();
        return;
    }
    if (type == "activate") {
        static_cast<void>(ActivateTabAt(index("space"), index("tab")));
        return;
    }
    if (type == "new_tab") {
        if (SelectSpaceIndex(index("space"))) {
            HideInternalPage();
            ExecuteCommand(BrowserCommand::kNewTab);
            if (chrome_ != nullptr) {
                chrome_->BeginAddressEditing();
            }
        }
        return;
    }
    if (type == "close_tab") {
        static_cast<void>(CloseTabAt(index("space"), index("tab")));
    } else if (type == "pin") {
        static_cast<void>(
            SetTabPinnedAt(index("space"), index("tab"), message.BoolOr("pinned", true)));
    } else if (type == "reorder") {
        static_cast<void>(MoveTabWithinSpace(index("space"), index("from"), index("to")));
    } else if (type == "move_tab") {
        static_cast<void>(MoveTabToSpace(index("from_space"), index("tab"), index("to_space")));
    }
    ScheduleLocalPagesRender();
}

bool BrowserWindow::ActivateTabAt(std::size_t space, std::size_t tab) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space >= spaces_.size() || tab >= spaces_[space].tab_count()) {
        return false;
    }
    if (!SelectSpaceIndex(space) || !SelectActiveSpaceTabIndex(tab)) {
        return false;
    }
    HideInternalPage();
    return true;
}

bool BrowserWindow::CloseTabAt(std::size_t space_index, std::size_t tab) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_index >= spaces_.size() || tab >= spaces_[space_index].tab_count()) {
        return false;
    }
    keep_internal_page_ = true;
    bool closed = true;
    if (space_index == active_space_index_) {
        closed = CloseActiveSpaceTabIndex(tab);
    } else if (spaces_[space_index].tab_count() == 1) {
        // The space's last tab: the space goes with it, like in the rail.
        Space removed = RemoveSpaceAtIndex(space_index);
        AttachActiveTabObservers();
        CloseSpaceBrowsers(removed);
    } else {
        Space& space = spaces_[space_index];
        std::optional<Tab> removed = space.RemoveTab(space.tabs()[tab].id());
        if (removed.has_value() && removed->browser() != nullptr) {
            removed->browser()->GetHost()->CloseBrowser(true);
        }
        UpdateChromeCollections();
    }
    keep_internal_page_ = false;
    return closed;
}

bool BrowserWindow::SetTabPinnedAt(std::size_t space_index, std::size_t tab, bool pinned) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_index >= spaces_.size() || tab >= spaces_[space_index].tab_count()) {
        return false;
    }
    Space& space = spaces_[space_index];
    if (!space.SetTabPinned(space.tabs()[tab].id(), pinned)) {
        return false;
    }
    UpdateChromeCollections();
    return true;
}

bool BrowserWindow::MoveTabWithinSpace(std::size_t space_index, std::size_t from, std::size_t to) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_index >= spaces_.size()) {
        return false;
    }
    Space& space = spaces_[space_index];
    if (from >= space.tab_count() || to >= space.tab_count()) {
        return false;
    }
    const std::size_t pinned = space.pinned_count();
    const bool is_pinned = space.tabs()[from].pinned();
    const std::size_t lowest = is_pinned ? 0 : pinned;
    const std::size_t highest = is_pinned ? pinned - 1 : space.tab_count() - 1;
    if (!space.MoveTab(from, std::clamp(to, lowest, highest))) {
        return false;
    }
    UpdateChromeCollections();
    return true;
}

bool BrowserWindow::MoveTabToSpace(std::size_t from_space, std::size_t tab, std::size_t to_space) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || from_space >= spaces_.size() || to_space >= spaces_.size() ||
        from_space == to_space || tab >= spaces_[from_space].tab_count()) {
        return false;
    }
    const Tab& source = spaces_[from_space].tabs()[tab];
    const std::string url = source.navigation_state().snapshot().url.empty()
                                ? source.startup_url()
                                : source.navigation_state().snapshot().url;
    const bool pinned = source.pinned();
    const SpaceId target_id = spaces_[to_space].id();

    // Open the copy first (the target space keeps its identity across the
    // close, which may remove the source space and shift indices).
    Tab moved{NextTabId()};
    const ValidatedAddress address = address_validator_(url);
    if (address.is_valid()) {
        moved.SetStartupUrl(address.url);
    }
    moved.SetPinned(pinned);
    for (std::size_t index = 0; index < spaces_.size(); ++index) {
        if (spaces_[index].id() != target_id) {
            continue;
        }
        Space& target = spaces_[index];
        const TabId moved_id = moved.id();
        const std::optional<TabId> previously_active =
            target.has_active_tab() ? std::optional<TabId>(target.active_tab_id()) : std::nullopt;
        target.AppendTab(std::move(moved));
        // Keep the target space's selection; the moved tab joins its group.
        if (previously_active.has_value()) {
            static_cast<void>(target.SelectTab(*previously_active));
        }
        if (pinned) {
            static_cast<void>(target.SetTabPinned(moved_id, false));
            static_cast<void>(target.SetTabPinned(moved_id, true));
        }
        break;
    }
    return CloseTabAt(from_space, tab);
}

std::size_t BrowserWindow::AddImportedSpaces(const std::vector<ImportedSpace>& imported,
                                             std::size_t* tab_count) {
    CEF_REQUIRE_UI_THREAD();
    std::size_t added = 0;
    for (const ImportedSpace& source : imported) {
        Space space{SpaceId{NextSpaceId()}, source.name,
                    source.color_argb.has_value()
                        ? ArgbColor{*source.color_argb}
                        : kSpaceColorPalette[spaces_.size() % kSpaceColorPalette.size()]};
        for (const BookmarkItem& item : source.pinned_tabs) {
            // Imported URLs pass the same allow-list as typed addresses.
            const ValidatedAddress address = address_validator_(item.url);
            if (!address.is_valid()) {
                continue;
            }
            Tab tab{NextTabId()};
            tab.SetStartupUrl(address.url);
            tab.SetPinned(true);
            space.AppendTab(std::move(tab));
        }
        if (space.tab_count() == 0) {
            continue;  // a space needs at least one tab
        }
        static_cast<void>(space.SelectTabIndex(0));
        if (tab_count != nullptr) {
            *tab_count += space.tab_count();
        }
        spaces_.push_back(std::move(space));
        ++added;
    }
    if (added > 0) {
        UpdateChromeCollections();
    }
    return added;
}

std::string BrowserWindow::ImportFromBrowser(std::string_view source_id) {
    CEF_REQUIRE_UI_THREAD();
    const std::optional<ImportSource> source = ImportSourceFromId(source_id);
    if (!source.has_value() || closing_) {
        return "Unknown import source.";
    }
    if (*source == ImportSource::kArc) {
        const std::optional<std::vector<ImportedSpace>> spaces = ImportArcSpaces(UserBaseHome());
        if (!spaces.has_value()) {
            return "Could not read Arc's sidebar file.";
        }
        std::size_t tabs = 0;
        const std::size_t added = AddImportedSpaces(*spaces, &tabs);
        if (added == 0) {
            return "Arc had no pinned tabs to bring over.";
        }
        return "Imported " + std::to_string(added) + (added == 1 ? " space" : " spaces") +
               " with " + std::to_string(tabs) + " pinned tabs from Arc.";
    }
    const std::optional<std::vector<BookmarkItem>> items =
        ImportFromSource(*source, UserBaseHome());
    std::string name(source_id);
    for (const ImportSourceInfo& info : DetectInstalledSources(UserBaseHome())) {
        if (info.id == *source) {
            name = info.display_name;
        }
    }
    if (!items.has_value()) {
        return "Could not read " + name + " bookmarks.";
    }
    const std::size_t added =
        BookmarkStore::MergeFolder(bookmarks_, "Imported from " + name, *items);
    if (persist_session_ && added > 0) {
        static_cast<void>(
            BookmarkStore::Save(BookmarkStore::DefaultBookmarksFilePath(), bookmarks_));
    }
    return added == 0 ? "No new bookmarks in " + name + "."
                      : "Imported " + std::to_string(added) + " bookmarks from " + name +
                            ". Find them in the command palette.";
}

bool BrowserWindow::ResetKeyBinding(KeyAction action) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    keymap_.ResetToDefault(action);
    prefs_.keybindings = keymap_.Overrides();
    SavePrefs();
    ApplyKeymap();
    return true;
}

void BrowserWindow::HandleAgentPageMessage(const json::Value& message) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const std::string_view type = message.StringOr("type", "");
    if (type == "ready") {
        FlushLocalPagesRender();
        return;
    }
    if (type == "open_url") {
        static_cast<void>(OpenNewTab(message.StringOr("url", "")));
        return;
    }
    if (type == "close_panel") {
        SetAgentPanelOpen(false);
        return;
    }
    EnsureAgentSession();
    if (type == "send") {
        const std::string context =
            message.BoolOr("include_context", true) ? ActiveTabAgentContext() : std::string();
        static_cast<void>(agent_session_->Send(std::string(message.StringOr("text", "")), context));
    } else if (type == "cancel") {
        agent_session_->Cancel();
    } else if (type == "permission") {
        const json::Value* option = message.FindMember("option_id");
        agent_session_->ResolvePermission(message.IntOr("request_id", -1),
                                          option != nullptr && option->IsString()
                                              ? std::optional<std::string>(option->string_val)
                                              : std::nullopt);
    } else if (type == "new_chat") {
        agent_session_->NewChat();
    } else if (type == "start") {
        const std::string command = TrimmedCopy(message.StringOr("command", ""));
        if (command != prefs_.agent_command) {
            prefs_.agent_command = command;
            SavePrefs();
        }
        static_cast<void>(agent_session_->Start(AgentConfig(command)));
    }
    ScheduleLocalPagesRender();
}

void BrowserWindow::CloseAgentPanelAndSession() {
    internal_page_.reset();
    for (std::unique_ptr<LocalPage>* page : {&agent_panel_, &settings_page_, &overview_page_}) {
        if (*page != nullptr) {
            (*page)->Close();
            page->reset();
        }
    }
    agent_session_.reset();
}

void BrowserWindow::SetTabPinned(std::size_t index, bool pinned) {
    static_cast<void>(SetActiveSpaceTabPinned(index, pinned));
}

ChromeTokens BrowserWindow::ResolvedTokens(ChromeTheme theme) const {
    return ChromeTokens::ForTheme(theme).TintedForSpace(active_space().color(), theme);
}

void BrowserWindow::ShutdownAgentHost() {
    if (agent_host_ != nullptr) {
        agent_host_->Shutdown();
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
    if (internal_page_.has_value()) {
        if (LocalPage* page = InternalPageFor(*internal_page_); page != nullptr) {
            page->Focus();
            return;
        }
    }
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
    ScheduleLocalPagesRender();
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

namespace {

class FaviconDownload final : public CefDownloadImageCallback {
  public:
    FaviconDownload(CefRefPtr<BrowserWindow> window, TabId tab)
        : window_(std::move(window)), tab_(tab) {}

    void OnDownloadImageFinished(const CefString&, int, CefRefPtr<CefImage> image) override {
        if (image != nullptr && !image->IsEmpty()) {
            window_->OnFaviconDownloaded(tab_, image);
        }
    }

  private:
    CefRefPtr<BrowserWindow> window_;
    TabId tab_;

    IMPLEMENT_REFCOUNTING(FaviconDownload);
};

}  // namespace

void BrowserWindow::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                       const std::vector<CefString>& icon_urls) {
    CEF_REQUIRE_UI_THREAD();
    const Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || icon_urls.empty()) {
        return;
    }
    // 16 DIP at up to 2x; the page's first declared icon wins.
    browser->GetHost()->DownloadImage(icon_urls.front(), /*is_favicon=*/true,
                                      /*max_image_size=*/32, /*bypass_cache=*/false,
                                      new FaviconDownload(this, owner->id()));
}

void BrowserWindow::OnFaviconDownloaded(TabId tab_id, CefRefPtr<CefImage> image) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    for (Space& space : spaces_) {
        if (Tab* tab = space.FindTab(tab_id); tab != nullptr) {
            tab->SetFavicon(image);
            if (&space == &active_space()) {
                UpdateChromeCollections();
            }
            return;
        }
    }
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

    if (agent_host_ != nullptr) {
        agent_host_->OnBrowserClosed(browser);
    }

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
    chrome_ = std::make_unique<BrowserChrome>(*this, browser_view, ResolvedTokens(theme),
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

    ApplyKeymap();

    CreateHoverSliver();
    ApplySidebarState();
    // Fresh install: offer the import/appearance welcome once, without
    // blocking browsing (Escape dismisses). The smoke run skips it so the
    // smoke page stays the only surface.
    if (persist_session_ && !prefs_.onboarding_completed) {
        ShowWelcomeFlow();
    }
    // AI-agent tools over MCP on 127.0.0.1. The smoke run stays offline and
    // deterministic, so only persisted windows serve them.
    if (persist_session_) {
        agent_host_ = std::make_unique<WindowAgentHost>(*this);
        static_cast<void>(agent_host_->StartEndpoint(agent::DefaultDiscoveryFilePath()));
        if (prefs_.agent_panel_open) {
            SetAgentPanelOpen(true);
        }
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
    ShutdownAgentHost();
    CloseAgentPanelAndSession();
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

    window_width_dip_ = new_bounds.width;
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
        // No agent tool call may touch the model once teardown starts, and the
        // panel's browser closes alongside the tab browsers.
        ShutdownAgentHost();
        CloseAgentPanelAndSession();
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

    if (command_id >= kKeymapAcceleratorBase &&
        command_id < kKeymapAcceleratorBase + static_cast<int>(kKeyActionCount)) {
        RunKeyAction(static_cast<KeyAction>(command_id - kKeymapAcceleratorBase));
        return true;
    }
    switch (command_id) {
        case kReloadAccelerator:
            ExecuteCommand(BrowserCommand::kReload);
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

void BrowserWindow::ApplyKeymap() {
    CEF_REQUIRE_UI_THREAD();
    if (window_ == nullptr || closing_) {
        return;
    }
    window_->RemoveAllAccelerators();
    // ctrl_pressed maps to Cmd on macOS and Ctrl elsewhere; high_priority so
    // shortcuts work while web content holds focus. On macOS these never
    // dispatch (the NSMenu key equivalents in main_mac.mm own the keys), but
    // registration stays uniform so every platform declares its bindings here.
    for (const KeyActionInfo& info : KeyActions()) {
        const std::optional<KeyBinding> binding = keymap_.Binding(info.action);
        if (!binding.has_value()) {
            continue;
        }
        window_->SetAccelerator(kKeymapAcceleratorBase + static_cast<int>(info.action),
                                binding->key_code, binding->shift, binding->primary, binding->alt,
                                true);
    }
    // Fixed bindings: F5 reloads, Cmd/Ctrl+1..9 select tabs, and Escape
    // dismisses the welcome flow while it is visible.
    window_->SetAccelerator(kReloadAccelerator, kVirtualKeyF5, false, false, false, true);
    for (int position = 0; position < 9; ++position) {
        window_->SetAccelerator(kSelectTab1Accelerator + position, '1' + position, false, true,
                                false, true);
    }
    window_->SetAccelerator(kWelcomeDismissAccelerator, kVirtualKeyEscape, false, false, false,
                            true);
}

void BrowserWindow::RunKeyAction(KeyAction action) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    switch (action) {
        case KeyAction::kBack:
            ExecuteCommand(BrowserCommand::kBack);
            return;
        case KeyAction::kForward:
            ExecuteCommand(BrowserCommand::kForward);
            return;
        case KeyAction::kReload:
            ExecuteCommand(BrowserCommand::kReload);
            return;
        case KeyAction::kFocusAddress:
            if (chrome_ != nullptr) {
                chrome_->BeginAddressEditing();
            } else {
                BeginAddressEditing();
            }
            return;
        case KeyAction::kCommandPalette:
            ShowCommandPalette();
            return;
        case KeyAction::kSearchPalette:
            ShowSearchPalette();
            return;
        case KeyAction::kToggleSidebar:
            ToggleSidebar();
            return;
        case KeyAction::kNewTab:
            ExecuteCommand(BrowserCommand::kNewTab);
            return;
        case KeyAction::kCloseTab:
            ExecuteCommand(BrowserCommand::kCloseTab);
            return;
        case KeyAction::kNextTab:
            ExecuteCommand(BrowserCommand::kNextTab);
            return;
        case KeyAction::kPreviousTab:
            ExecuteCommand(BrowserCommand::kPreviousTab);
            return;
        case KeyAction::kNewSpace:
            ExecuteCommand(BrowserCommand::kNewSpace);
            return;
        case KeyAction::kRenameSpace:
            BeginSpaceRenaming();
            return;
        case KeyAction::kToggleSplit:
            ExecuteCommand(BrowserCommand::kToggleSplit);
            return;
        case KeyAction::kMoveDividerLeft:
            ExecuteCommand(BrowserCommand::kMoveDividerLeft);
            return;
        case KeyAction::kMoveDividerRight:
            ExecuteCommand(BrowserCommand::kMoveDividerRight);
            return;
        case KeyAction::kTogglePinTab:
            ExecuteCommand(BrowserCommand::kTogglePinTab);
            return;
        case KeyAction::kToggleAgentPanel:
            ToggleAgentPanel();
            return;
        case KeyAction::kTabOverview:
            ToggleInternalPage(LocalPageKind::kTabOverview);
            return;
        case KeyAction::kSettings:
            ToggleInternalPage(LocalPageKind::kSettings);
            return;
    }
}

bool BrowserWindow::SetKeyBinding(KeyAction action, std::optional<KeyBinding> binding) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    keymap_.SetBinding(action, binding);
    prefs_.keybindings = keymap_.Overrides();
    SavePrefs();
    ApplyKeymap();
    return true;
}

void BrowserWindow::ResetKeymap() {
    CEF_REQUIRE_UI_THREAD();
    keymap_ = Keymap::Defaults();
    prefs_.keybindings.clear();
    SavePrefs();
    ApplyKeymap();
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
    if (internal_page_.has_value()) {
        // An overview action mutating tabs keeps the overview on screen; any
        // other selection change returns to the page content.
        LocalPage* page = InternalPageFor(*internal_page_);
        if (keep_internal_page_ && page != nullptr) {
            chrome_->AttachBrowserView(page->view());
            return;
        }
        internal_page_.reset();
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
                        .active = active != nullptr && tab.id() == active->id(),
                        .pinned = tab.pinned(),
                        .favicon = tab.favicon()});
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
    ScheduleLocalPagesRender();
    // Arc-style space theming: a switch to a differently colored space
    // re-tints the whole chrome.
    if (window_ != nullptr && tinted_space_color_ != active_space().color().argb) {
        ApplyTheme(window_, ResolvedChromeTheme(), false);
    }
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
    const ChromeTokens tokens = ResolvedTokens(theme);
    tinted_space_color_ = active_space().color().argb;
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
    ScheduleLocalPagesRender();
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
        search_palette_ =
            std::make_unique<SearchPalette>(*this, window_, ResolvedTokens(ResolvedChromeTheme()));
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
            *this, window_, ResolvedTokens(ResolvedChromeTheme()), &ParseAndValidate);
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
        welcome_->ApplyTheme(ResolvedTokens(ResolvedChromeTheme()));
    }
}

void BrowserWindow::OnWelcomeCompleted(const std::vector<ImportSource>& sources) {
    CEF_REQUIRE_UI_THREAD();
    std::size_t imported = 0;
    for (const ImportSource source : sources) {
        if (source == ImportSource::kArc) {
            // Arc's spaces come over as spaces, not bookmarks.
            static_cast<void>(ImportFromBrowser(ImportSourceId(source)));
            continue;
        }
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
        welcome_ =
            std::make_unique<WelcomeFlow>(*this, window_, ResolvedTokens(ResolvedChromeTheme()),
                                          prefs_.theme, DetectInstalledSources(UserBaseHome()));
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
            *this, window_, ResolvedTokens(ResolvedChromeTheme()), active_space().name());
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
    hover_sliver_->SetBackgroundColor(ResolvedTokens(chrome_snapshot_.theme).accent.argb);
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
        hover_sliver_->SetBackgroundColor(ResolvedTokens(chrome_snapshot_.theme).accent.argb);
    }
    // Keep the published snapshot's rail/content split in step with the reveal
    // state so observers never read a stale 286 DIP rail.
    const int window_width = window_width_dip_;
    const int rail_width = sidebar_state_.RailWidthDip(
        std::min(ChromeTokens::ForTheme(chrome_snapshot_.theme).rail_width_dip, window_width));
    // The open agent panel takes its column out of the published content
    // width exactly like BrowserChrome::LayoutForBounds does.
    const int panel_width =
        agent_panel_open()
            ? std::clamp(window_width - rail_width - BrowserChrome::MinimumBrowserContentWidthDip(),
                         0, BrowserChrome::AgentPanelWidthDip())
            : 0;
    chrome_snapshot_.rail_bounds.width = rail_width;
    chrome_snapshot_.content_bounds.x = rail_width;
    chrome_snapshot_.content_bounds.width = std::max(0, window_width - rail_width - panel_width);
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
