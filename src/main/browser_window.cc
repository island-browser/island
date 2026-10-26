#include "browser_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "agent_context.h"
#include "app_resources.h"
#include "browser_import.h"
#include "cef_address_parser.h"
#include "cef_update_fetcher.h"
#include "clipboard.h"
#include "collect_prompt.h"
#include "content_blocker_policy.h"
#include "design_tokens.h"
#include "download_path.h"
#include "favicon_policy.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_build.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_frame.h"
#include "include/cef_image.h"
#include "include/cef_menu_model.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "island_version.h"
#include "native_handle.h"
#include "page_card.h"
#include "runtime_style.h"
#include "search_provider.h"
#include "session_store.h"
#include "start_tiles.h"
#include "startup_options.h"
#include "window_agent_host.h"
#include "zoom_policy.h"

namespace island {
namespace {
constexpr int kVirtualKeyF5 = 0x74;
constexpr int kVirtualKeyEscape = 0x1B;
constexpr int kChromeWindowWidth = 1440;
constexpr int kChromeWindowHeight = 900;
constexpr int kMinimumWindowWidth = 800;
constexpr int kMinimumWindowHeight = 560;

// The frameless Arc-style window (traffic lights over the sidebar, no title bar) is macOS-only
// for now: the views toolkit's frameless + draggable-region mode is the supported combination
// there, while Windows/Linux keep the platform frame.
constexpr bool FramelessWindowSupported() {
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

constexpr std::string_view kDefaultSpaceName = "Default";
constexpr ArgbColor kDefaultSpaceColor{0xFF0090FF};
// Fixed palette for spaces created through New Space, cycled in creation order
// so the switcher's color marks stay distinguishable. The default space keeps
// the palette's first entry.
constexpr std::array<ArgbColor, 5> kSpaceColorPalette{
    ArgbColor{0xFF0090FF}, ArgbColor{0xFF30A46C}, ArgbColor{0xFFFFB224},
    ArgbColor{0xFFE5484D}, ArgbColor{0xFF8E4EC6},
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
    static constexpr char kHex[] = "0123456789abcdef";
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

// Tab sleep: how often the idle sweep runs, how long a page has to answer the pre-sleep probe,
// and the prefix of the probe's reply (a console message carrying a per-probe random token).
constexpr int kSleepTickMs = 30 * 1000;
constexpr int kSleepProbeTimeoutMs = 500;
constexpr std::string_view kSleepProbePrefix = "\x01island-sleep:";

std::string RandomHexToken() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<std::uint64_t>(device()) << 32U) ^ device());
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    for (int index = 0; index < 24; ++index) {
        token += kHex[engine() & 0xFU];
    }
    return token;
}

// One script answers all three questions the sleep decision needs: where the page is scrolled,
// whether an input holds text the user typed (or a contenteditable has focus with content), and
// whether any media element is audibly playing. The reply is a console message tagged with the
// probe's token: "<prefix><token>:<scrollX>,<scrollY>,<dirty 0|1>,<playing 0|1>".
std::string SleepProbeScript(const std::string& token) {
    return R"JS((function () {
  var dirty = 0, playing = 0;
  try {
    var fields = document.querySelectorAll('input,textarea');
    for (var i = 0; i < fields.length && !dirty; i++) {
      var f = fields[i], t = (f.type || '').toLowerCase();
      if (t === 'hidden' || t === 'submit' || t === 'button' || t === 'reset' || t === 'image') continue;
      if (t === 'checkbox' || t === 'radio') { if (f.checked !== f.defaultChecked) dirty = 1; }
      else if (f.value !== f.defaultValue) dirty = 1;
    }
    var a = document.activeElement;
    if (!dirty && a && a.isContentEditable && a.textContent && a.textContent.length > 0) dirty = 1;
    var media = document.querySelectorAll('audio,video');
    for (var j = 0; j < media.length && !playing; j++) {
      var m = media[j];
      if (!m.paused && !m.ended && !m.muted && m.volume > 0) playing = 1;
    }
  } catch (e) {}
  console.log('\u0001island-sleep:)JS" +
           token + R"JS(:' + [window.scrollX, window.scrollY, dirty, playing].join(','));
})();)JS";
}

// Replays a woken page's scroll offset; pages often grow after load, so retry briefly.
std::string ScrollRestoreScript(double x, double y) {
    return "(function(x,y){var n=0;function "
           "t(){window.scrollTo(x,y);if(++n<4)setTimeout(t,250);}t();})(" +
           std::to_string(x) + "," + std::to_string(y) + ");";
}

// Closed tabs kept for Reopen Closed Tab; the oldest drops first.
constexpr std::size_t kClosedTabLimit = 20;

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

// The updater's view of this process: the running version, the release
// target, and where (and whether) the install can be replaced.
update::Updater::Config DefaultUpdaterConfig() {
    update::Updater::Config config;
    config.current_version = ISLAND_VERSION_STRING;
    config.target = std::string(update::CurrentTarget());
    if (const std::optional<update::Platform> platform = update::CurrentPlatform()) {
        config.install = update::DetectInstallLocation(*platform, CurrentRuntimeBinaryPath());
    }
    return config;
}

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::uint64_t UnixNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

std::string UrlHost(std::string_view url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return {};
    }
    const std::size_t begin = scheme + 3;
    const std::size_t end = url.find_first_of("/?#", begin);
    return std::string(url.substr(begin, end - begin));
}

void RunAgentTask(std::function<void()> task) {
    if (task) {
        task();
    }
}

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

namespace {

// The inline status for a library that did not load cleanly (see the constructor).
std::string BookmarkLoadMessage(BookmarkError error) {
    switch (error) {
        case BookmarkError::kParseError:
        case BookmarkError::kSchemaError:
            return "bookmarks.json could not be read. The library starts empty, and the original "
                   "file is kept as bookmarks.json.invalid.bak on the next save.";
        case BookmarkError::kFileWriteError:
            return "bookmarks.json was upgraded but could not be saved. The library is read-only "
                   "for this run.";
        default:
            return "bookmarks.json could not be read. The library is read-only for this run, so "
                   "the file is not overwritten.";
    }
}

}  // namespace

BrowserWindow::BrowserWindow(std::string initial_url, bool persist_session)
    : initial_url_(std::move(initial_url)),
      persist_session_(persist_session),
      address_validator_(&ParseAndValidate) {
    // Preferences and bookmarks share the session file's persistence rules:
    // headless and smoke shapes keep fresh-install defaults and never read or
    // write the files, so tests and smoke runs stay deterministic. The library
    // loads before any space exists: its favorite lists own space IDs, which must
    // be reserved before a fresh or discarded session allocates new ones.
    if (persist_session_) {
        prefs_ = PrefsStore::Load(PrefsStore::DefaultPrefsFilePath()).state;
        bookmarks_ = BookmarkStore(BookmarkStore::DefaultBookmarksFilePath());
        const BookmarkError load_error = bookmarks_.Load();
        // A missing file is the normal first run. Anything else is shown, and a read-only library
        // says why it cannot be saved this run.
        if (load_error != BookmarkError::kNone &&
            (load_error != BookmarkError::kFileReadError || bookmarks_.read_only())) {
            failed_bookmark_mutation_ = FailedBookmarkMutation{
                .message = BookmarkLoadMessage(load_error), .retry = nullptr};
        }
    }
    ReserveSpaceIdsUpTo(bookmarks_.MaxSpaceKey());
    if (!persist_session_ || !RestoreSession()) {
        // CreateDefaultSpace already appends the space's single starting tab.
        spaces_.push_back(CreateDefaultSpace());
    }
    if (persist_session_) {
        HistoryRecorder::Options history_options;
        history_options.segment_path =
            PrefsStore::DefaultPrefsFilePath().parent_path() / "history" / "index.seg";
        history_ = std::make_unique<HistoryRecorder>(std::move(history_options));
        reports_path_ = ReportStore::DefaultReportsFilePath();
        reports_ = ReportStore::Load(reports_path_).state;
    }
    keymap_ = Keymap::WithOverrides(prefs_.keybindings);
    sleep_policy_.SetMinutes(prefs_.tab_sleep_minutes);
    // The sidebar starts the way the last session left it (the headless and smoke shapes
    // keep the fresh-install default: expanded).
    sidebar_state_.SetExpanded(!prefs_.sidebar_collapsed);
    content_blocker_ = new CefContentBlocker(PrefsStore::DefaultPrefsFilePath().parent_path(),
                                             [this] { ScheduleLocalPagesRender(); });
    ApplyContentBlockerPolicy();
    ConfigureUpdater(DefaultUpdaterConfig(), std::make_unique<CefUpdateFetcher>());
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
    return RestoreSessionState(loaded.state);
}

bool BrowserWindow::RestoreSessionStateForTest(const SessionState& state) {
    spaces_.clear();
    active_space_index_ = 0;
    return RestoreSessionState(state);
}

bool BrowserWindow::IsSessionInternalTabUrl(std::string_view url) const {
    if ((!plan_page_url_.empty() || persist_session_) && IsPlanPageUrl(url)) {
        return true;
    }
    return (!report_page_url_.empty() || persist_session_) && ReportIdForUrl(url).has_value();
}

bool BrowserWindow::RestoreSessionState(const SessionState& state) {
    // Virtual-tab records belong to the library, not to a space, so they restore even when the
    // session has no spaces. The display itself is never restored.
    virtual_tabs_.clear();
    displayed_favorite_.reset();
    favorite_tabs_ = FavoriteTabsFromSession(state.favorite_tabs);
    static_cast<void>(ReconcileFavoriteTabs());
    if (state.spaces.empty()) {
        return false;
    }

    // Re-reserve persisted identities first so newly allocated ids can never
    // collide with a restored one in this process lifetime.
    std::uint64_t max_tab_id = 0;
    std::uint64_t max_space_id = 0;
    for (const SpaceState& space_state : state.spaces) {
        max_space_id = std::max(max_space_id, space_state.id.value);
        for (const TabState& tab_state : space_state.tabs) {
            max_tab_id = std::max(max_tab_id, tab_state.id.value);
        }
    }
    ReserveTabIdsUpTo(max_tab_id);
    ReserveSpaceIdsUpTo(max_space_id);

    for (const SpaceState& space_state : state.spaces) {
        if (space_state.tabs.empty()) {
            continue;
        }
        Space space{SpaceId{space_state.id.value}, space_state.name,
                    ArgbColor{space_state.color_argb}, space_state.icon_id};
        std::set<TabId> restored_ids;
        for (const TabState& tab_state : space_state.tabs) {
            if (!IsSessionInternalTabUrl(tab_state.url)) {
                restored_ids.insert(tab_state.id);
            }
        }
        // A group parent whose saved page is a `data:` document or no longer validates is a
        // Plan/Report page from an older build that IsSessionInternalTabUrl does not recognize
        // (the page text changed). It is not restored pinned with its children: a `data:` parent
        // is dropped, any other keeps its place unpinned, and its children become ordinary tabs.
        std::set<TabId> stale_parents;
        for (const TabState& child : space_state.tabs) {
            if (!child.group_parent.has_value() || !restored_ids.contains(*child.group_parent)) {
                continue;
            }
            for (const TabState& parent : space_state.tabs) {
                if (parent.id != *child.group_parent) {
                    continue;
                }
                const ValidatedAddress parent_address = address_validator_
                                                            ? address_validator_(parent.url)
                                                            : ParseAndValidate(parent.url);
                const bool is_data = parent.url.starts_with("data:");
                if (is_data || !parent_address.is_valid()) {
                    stale_parents.insert(parent.id);
                }
                if (is_data) {
                    restored_ids.erase(parent.id);
                }
            }
        }
        for (const TabState& tab_state : space_state.tabs) {
            if (!restored_ids.contains(tab_state.id)) {
                continue;
            }
            Tab tab{TabId{tab_state.id.value}};
            // A restored tab navigates through the same validation path as
            // manual entry: a persisted URL that no longer passes the
            // allow-list falls back to the fixed startup page instead of
            // force-loading.
            const ValidatedAddress address = address_validator_ ? address_validator_(tab_state.url)
                                                                : ParseAndValidate(tab_state.url);
            if (address.is_valid()) {
                tab.SetStartupUrl(address.url);
            }
            tab.SetPinned(tab_state.pinned && !stale_parents.contains(tab_state.id));
            tab.SetOpenedByAgent(tab_state.opened_by_agent);
            if (tab_state.group_parent.has_value() &&
                restored_ids.contains(*tab_state.group_parent) &&
                !stale_parents.contains(*tab_state.group_parent)) {
                tab.SetGroupParent(tab_state.group_parent);
                tab.SetGroupLabel(tab_state.group_label);
            }
            space.AppendTab(std::move(tab));
        }
        if (space.tabs().empty()) {
            continue;
        }
        space.NormalizeGroups();
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

    if (state.active_space_id.has_value()) {
        for (std::size_t index = 0; index < spaces_.size(); ++index) {
            if (spaces_[index].id() == *state.active_space_id) {
                active_space_index_ = index;
                break;
            }
        }
    }
    return true;
}

SessionState BrowserWindow::BuildSessionState() const {
    SessionState state;
    for (const Space& space : spaces_) {
        SpaceState space_state;
        space_state.id = space.id();
        space_state.name = space.name();
        space_state.color_argb = space.color().argb;
        space_state.icon_id = space.icon_id();
        std::set<TabId> saved_ids;
        for (const Tab& tab : space.tabs()) {
            const std::string url = EffectiveUrl(tab);
            if (!IsSessionInternalTabUrl(url)) {
                saved_ids.insert(tab.id());
            }
        }
        for (const Tab& tab : space.tabs()) {
            if (!saved_ids.contains(tab.id())) {
                continue;
            }
            // A reader view is saved as the page it was made from, never as its data: URL.
            const std::optional<TabId> group_parent =
                tab.group_parent().has_value() && saved_ids.contains(*tab.group_parent())
                    ? tab.group_parent()
                    : std::nullopt;
            space_state.tabs.push_back(
                TabState{tab.id(), EffectiveUrl(tab), tab.pinned(), group_parent,
                         group_parent.has_value() ? tab.group_label() : "", tab.opened_by_agent()});
        }
        if (space_state.tabs.empty()) {
            continue;
        }
        space_state.active_tab_id =
            space.has_active_tab() && saved_ids.contains(space.active_tab_id())
                ? std::optional<TabId>(space.active_tab_id())
                : std::nullopt;
        if (space.split().has_value() && saved_ids.contains(space.split()->first) &&
            saved_ids.contains(space.split()->second)) {
            space_state.split = SplitState{space.split()->first, space.split()->second};
        }
        state.spaces.push_back(std::move(space_state));
        if (space.id() == active_space().id()) {
            state.active_space_id = space.id();
        }
    }
    if (!state.active_space_id.has_value() && !state.spaces.empty()) {
        state.active_space_id = state.spaces.front().id;
    }
    // A live virtual tab's last URL is where it is now; its record is written from that.
    FavoriteTabRegistry records = favorite_tabs_;
    for (const auto& [key, tab] : virtual_tabs_) {
        records.SetLastUrl(key, EffectiveUrl(tab));
    }
    state.favorite_tabs = ToSessionFavoriteTabs(records);
    return state;
}

void BrowserWindow::SaveSession() const {
    // Spaces currently create their request contexts without a cache path, so
    // there is no storage directory to persist yet.
    static_cast<void>(
        SessionStore::Save(SessionStore::DefaultSessionFilePath(), BuildSessionState()));
}

std::string BrowserWindow::StartupUrlForTab(const Tab& tab) const {
    if (!tab.startup_url().empty()) {
        return tab.startup_url();
    }
    // An empty window URL means the production start page (the smoke run and
    // tests pass their own fixed document).
    return initial_url_.empty() ? StartPageUrl() : initial_url_;
}

const std::string& BrowserWindow::StartPageUrl() const {
    if (start_page_url_.empty()) {
        start_page_url_ = LocalPage::PageUrl(LocalPageKind::kStart);
    }
    return start_page_url_;
}

ValidatedAddress BrowserWindow::ResolveAddressInput(std::string_view text) const {
    const AddressInput input = ClassifyAddressInput(text);
    if (input.kind == AddressInputKind::kSearch) {
        return ValidatedAddress{.url = BuildSearchUrl(search_engine(), input.text),
                                .error = std::nullopt};
    }
    return address_validator_ ? address_validator_(input.text) : ParseAndValidate(input.text);
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
            Tab* tab = DisplayedTab();
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
            if (displayed_favorite_.has_value()) {
                // The shown tab is the favorite's virtual tab; the ordinary tab stays open.
                CloseFavoriteTab();
                return;
            }
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
        case BrowserCommand::kSelectLastTab: {
            const std::size_t count = active_space().tab_count();
            if (count > 0) {
                static_cast<void>(SelectActiveSpaceTabIndex(count - 1));
            }
            return;
        }
        case BrowserCommand::kReopenClosedTab:
            static_cast<void>(ReopenClosedTab());
            return;
        case BrowserCommand::kZoomIn:
        case BrowserCommand::kZoomOut:
        case BrowserCommand::kZoomReset: {
            Tab* tab = DisplayedTab();
            if (tab == nullptr || tab->browser() == nullptr) {
                return;
            }
            CefRefPtr<CefBrowserHost> host = tab->browser()->GetHost();
            if (command == BrowserCommand::kZoomReset) {
                host->SetZoomLevel(0.0);
            } else {
                const int steps = command == BrowserCommand::kZoomIn ? 1 : -1;
                host->SetZoomLevel(SteppedZoomLevel(host->GetZoomLevel(), steps));
            }
            return;
        }
        case BrowserCommand::kReloadIgnoringCache: {
            Tab* tab = DisplayedTab();
            if (tab != nullptr && tab->browser() != nullptr) {
                tab->browser()->ReloadIgnoreCache();
            }
            return;
        }
        case BrowserCommand::kStopLoading: {
            // Escape closes the cheatsheet first, then a peek, and only then stops loading.
            // The palettes do not block this command, only the Escape route (see below).
            switch (ResolveStopLoading(cheatsheet_open(), peek_open() && !OverlayShown(),
                                       CanStopLoading())) {
                case StopLoadingTarget::kCloseCheatsheet:
                    CloseCheatsheet(/*refocus_page=*/true);
                    return;
                case StopLoadingTarget::kClosePeek:
                    ClosePeek();
                    return;
                case StopLoadingTarget::kStopLoad: {
                    Tab* tab = DisplayedTab();
                    if (tab != nullptr && tab->browser() != nullptr) {
                        tab->browser()->StopLoad();
                    }
                    return;
                }
                case StopLoadingTarget::kNone:
                    return;
            }
            return;
        }
        case BrowserCommand::kNextSpace:
        case BrowserCommand::kPreviousSpace: {
            const std::size_t count = spaces_.size();
            if (count < 2) {
                return;
            }
            const std::size_t next = command == BrowserCommand::kNextSpace
                                         ? (active_space_index_ + 1) % count
                                         : (active_space_index_ + count - 1) % count;
            static_cast<void>(SelectSpaceIndex(next));
            return;
        }
        case BrowserCommand::kFindInPage:
            FindInPage();
            return;
        case BrowserCommand::kFindNext:
            StepFind(/*forward=*/true);
            return;
        case BrowserCommand::kFindPrevious:
            StepFind(/*forward=*/false);
            return;
        case BrowserCommand::kToggleReaderMode:
            ToggleReaderMode();
            return;
        case BrowserCommand::kShowCheatsheet:
            ToggleCheatsheet();
            return;
        case BrowserCommand::kCopyTabUrl: {
            const Tab* tab = DisplayedTab();
            const std::string url = tab != nullptr ? WebUrlOf(*tab) : std::string();
            if (!url.empty()) {
                static_cast<void>(clipboard_writer_(url));
            }
            return;
        }
        case BrowserCommand::kNewSpace: {
            DetachActiveTabObservers();
            const std::size_t created_index = spaces_.size();
            // A monotonic counter, not the vector size, so names stay unique
            // after spaces are removed and recreated.
            Space& created = AppendSpace("Space " + std::to_string(++created_space_count_));
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

namespace {

// Island's Light or Dark theme is also the pages' color mode; System leaves it to the OS.
cef_color_variant_t WebColorSchemeFor(ThemePreference preference) {
    switch (preference) {
        case ThemePreference::kLight:
            return CEF_COLOR_VARIANT_LIGHT;
        case ThemePreference::kDark:
            return CEF_COLOR_VARIANT_DARK;
        case ThemePreference::kSystem:
            break;
    }
    return CEF_COLOR_VARIANT_SYSTEM;
}

}  // namespace

void BrowserWindow::ApplyWebColorScheme() {
    const cef_color_variant_t scheme = WebColorSchemeFor(prefs_.theme);
    for (Space& space : spaces_) {
        space.SetWebColorScheme(scheme);
    }
}

Space& BrowserWindow::AppendSpace(std::string name) {
    Space& created =
        spaces_.emplace_back(SpaceId{NextSpaceId()}, std::move(name),
                             kSpaceColorPalette[spaces_.size() % kSpaceColorPalette.size()]);
    created.SetWebColorScheme(WebColorSchemeFor(prefs_.theme));
    return created;
}

void BrowserWindow::AppendTabToActiveSpace(std::string startup_url, bool opened_by_agent) {
    Space& space = active_space();
    context_chip_.reset();  // the new tab is a new page context
    DetachActiveTabObservers();
    Tab tab{NextTabId()};
    tab.SetStartupUrl(std::move(startup_url));
    tab.SetOpenedByAgent(opened_by_agent);
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

std::optional<std::string> BrowserWindow::ResolveNewTabTarget(std::string_view text) const {
    if (text.empty()) {
        return std::string();
    }
    return ResolveAgentNavigation(text, address_validator_);
}

bool BrowserWindow::OpenNewTab(std::string_view text) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    std::optional<std::string> url = ResolveNewTabTarget(text);
    if (!url.has_value()) {
        return false;
    }
    AppendTabToActiveSpace(std::move(*url));
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
    if (IsPlanTabIndex(index)) {
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
        tab.ClearSavedScroll();
    }
    return true;
}

std::optional<std::string> BrowserWindow::ResolveNavigationTarget(std::string_view text) const {
    return ResolveAgentNavigation(text, address_validator_);
}

bool BrowserWindow::IsPlanTabIndex(std::optional<std::size_t> index) const {
    const Space& space = active_space();
    if (!index.has_value() && !space.has_active_tab()) {
        return false;
    }
    const std::size_t at = index.value_or(space.active_tab_index());
    if (at >= space.tab_count()) {
        return false;
    }
    const Tab& tab = space.tabs()[at];
    if (plan_tab_.has_value() && tab.id() == *plan_tab_) {
        return true;
    }
    return !plan_page_url_.empty() && tab.startup_url() == plan_page_url_;
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

bool BrowserWindow::CreateSpace(std::string_view name, bool opened_by_agent) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    ExecuteCommand(BrowserCommand::kNewSpace);
    if (opened_by_agent) {
        // kNewSpace makes the new space active and gives it its start tab.
        if (Tab* start = active_tab(); start != nullptr) {
            start->SetOpenedByAgent(true);
        }
    }
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

bool BrowserWindow::ResearchOwnsAgent() const {
    // A plan that waits for approval runs no turn, so the agent is in ordinary chat again: its
    // calls address the window the person sees, as the sidebar's context does.
    return research_.has_value() && !research_->awaiting_approval && plan_tab_.has_value() &&
           TabLocation(*plan_tab_).has_value();
}

std::size_t BrowserWindow::AgentSpaceIndex() const {
    if (ResearchOwnsAgent()) {
        return TabLocation(*plan_tab_)->first;
    }
    return active_space_index_;
}

BrowserWindow::AgentTabLookup BrowserWindow::ResolveAgentTab(
    std::optional<std::size_t> index) const {
    if (!ResearchOwnsAgent()) {
        // Ordinary chat: the agent works with the window the person sees. Indices and calls
        // without an index refer to the active space and its active tab.
        const Space& space = active_space();
        if (index.has_value()) {
            if (*index >= space.tab_count()) {
                return {.error = AgentTabError::kNoSuchTab};
            }
            return {.tab = AgentTabRef{.space = active_space_index_, .tab = *index}};
        }
        if (!space.has_active_tab()) {
            return {.error = AgentTabError::kNoAgentPage};
        }
        return {.tab = AgentTabRef{.space = active_space_index_, .tab = space.active_tab_index()}};
    }
    // During research the agent works in the Plan's space and on its own most recent page.
    const std::size_t space_index = AgentSpaceIndex();
    if (index.has_value()) {
        if (*index >= spaces_[space_index].tab_count()) {
            return {.error = AgentTabError::kNoSuchTab};
        }
        return {.tab = AgentTabRef{.space = space_index, .tab = *index}, .research = true};
    }
    if (!agent_tab_id_.has_value()) {
        return {.error = AgentTabError::kNoAgentPage};
    }
    const std::optional<std::pair<std::size_t, std::size_t>> location = TabLocation(*agent_tab_id_);
    if (!location.has_value() || location->first != space_index) {
        return {.error = AgentTabError::kAgentPageClosed};
    }
    return {.tab = AgentTabRef{.space = location->first, .tab = location->second},
            .research = true};
}

bool BrowserWindow::IsAgentPlanTab(std::optional<std::size_t> index) const {
    const AgentTabLookup lookup = ResolveAgentTab(index);
    if (!lookup.tab.has_value()) {
        return false;
    }
    const Tab& tab = spaces_[lookup.tab->space].tabs()[lookup.tab->tab];
    if (plan_tab_.has_value() && tab.id() == *plan_tab_) {
        return true;
    }
    return !plan_page_url_.empty() && tab.startup_url() == plan_page_url_;
}

CefRefPtr<CefBrowser> BrowserWindow::AgentBrowser(AgentTabRef ref) const {
    if (closing_) {
        return nullptr;
    }
    return spaces_[ref.space].tabs()[ref.tab].browser();
}

bool BrowserWindow::NavigateAgentTab(std::optional<std::size_t> index, std::string_view text) {
    CEF_REQUIRE_UI_THREAD();
    const AgentTabLookup lookup = ResolveAgentTab(index);
    if (closing_ || !lookup.tab.has_value() || IsAgentPlanTab(lookup.tab->tab)) {
        return false;
    }
    const AgentTabRef ref = *lookup.tab;
    if (ref.space == active_space_index_) {
        return NavigateTab(ref.tab, text);
    }
    // The page is in a space the person is not viewing: change its address without switching.
    const std::optional<std::string> url = ResolveAgentNavigation(text, address_validator_);
    if (!url.has_value()) {
        return false;
    }
    Tab& tab = spaces_[ref.space].tabs()[ref.tab];
    if (tab.browser() != nullptr && tab.browser()->GetMainFrame() != nullptr) {
        tab.browser()->GetMainFrame()->LoadURL(*url);
    } else {
        tab.SetStartupUrl(*url);
        tab.ClearSavedScroll();
    }
    return true;
}

bool BrowserWindow::ActivateAgentTab(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    const AgentTabLookup lookup = ResolveAgentTab(index);
    if (closing_ || !lookup.tab.has_value()) {
        return false;
    }
    SelectTabWithOneAttach(lookup.tab->space, lookup.tab->tab);
    return true;
}

bool BrowserWindow::CloseAgentTab(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    const AgentTabLookup lookup = ResolveAgentTab(index);
    if (closing_ || !lookup.tab.has_value() || lookup.tab->space != active_space_index_) {
        return false;
    }
    return CloseActiveSpaceTabIndex(lookup.tab->tab);
}

bool BrowserWindow::SetAgentTabPinned(std::size_t index, bool pinned) {
    CEF_REQUIRE_UI_THREAD();
    const AgentTabLookup lookup = ResolveAgentTab(index);
    if (closing_ || !lookup.tab.has_value()) {
        return false;
    }
    Space& space = spaces_[lookup.tab->space];
    if (!space.SetTabPinned(space.tabs()[lookup.tab->tab].id(), pinned)) {
        return false;
    }
    UpdateChromeCollections();
    return true;
}

agent::ResolvedAgentCommand BrowserWindow::ResolvedAgent() const {
    const char* const env_command = std::getenv("ISLAND_AGENT_COMMAND");
    return agent::ResolveAgentCommand(prefs_.agent_provider, prefs_.agent_command,
                                      env_command != nullptr ? env_command : "");
}

agent::AgentSessionConfig BrowserWindow::AgentConfig() const {
    const agent::ResolvedAgentCommand resolved = ResolvedAgent();
    agent::AgentSessionConfig config;
    config.command = resolved.command;
    config.provider_id = resolved.provider_id;
    if (const agent::AgentProvider* provider = agent::FindAgentProvider(resolved.provider_id)) {
        config.provider_name = std::string(provider->name);
    }
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

const std::vector<agent::AgentProviderStatus>& BrowserWindow::ProviderStatuses() const {
    // Availability is a PATH search (a few stat calls per provider); the
    // pages re-render on every streamed chunk, so it is refreshed at most
    // every few seconds, or when the custom command changes.
    const auto now = std::chrono::steady_clock::now();
    const bool stale = !provider_statuses_.has_value() ||
                       now - provider_statuses_at_ > std::chrono::seconds(5) ||
                       provider_statuses_->back().command != TrimmedCopy(prefs_.agent_command);
    if (stale) {
        provider_statuses_ =
            agent_search_dirs_for_test_.has_value()
                ? agent::DetectAgentProviders(prefs_.agent_command, *agent_search_dirs_for_test_)
                : agent::DetectAgentProviders(prefs_.agent_command);
        provider_statuses_at_ = now;
    }
    return *provider_statuses_;
}

json::Value BrowserWindow::AgentProvidersStateJson() const {
    using json::Value;
    const agent::ResolvedAgentCommand resolved = ResolvedAgent();
    const bool env_override = resolved.source == agent::AgentCommandSource::kEnvironment;
    return Value::MakeObject()
        .Set("provider", Value::String(resolved.provider_id))
        .Set("providers", agent::AgentProvidersJson(ProviderStatuses()))
        .Set("command", Value::String(prefs_.agent_command))
        .Set("default_command", Value::String(std::string(agent::DefaultAgentProvider().command)))
        .Set("env_override", Value::Bool(env_override))
        .Set("env_command", Value::String(env_override ? resolved.command : std::string()))
        .Set("effective_command", Value::String(resolved.command));
}

bool BrowserWindow::SetAgentProvider(std::string_view id) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || agent::FindAgentProvider(id) == nullptr) {
        return false;
    }
    if (prefs_.agent_provider == id) {
        return true;
    }
    prefs_.agent_provider = std::string(id);
    SavePrefs();
    if (agent_session_ != nullptr) {
        agent::AgentSessionConfig config = AgentConfig();
        if (config.command == agent_session_->config().command) {
            // ISLAND_AGENT_COMMAND pins the command: nothing to restart.
            agent_session_->Configure(std::move(config));
        } else {
            // The old agent's turn end never arrives.
            AbandonInFlightTurn([this, &config] {
                static_cast<void>(agent_session_->SwitchAgent(std::move(config)));
            });
        }
    }
    RefreshAgentEntry();
    ScheduleLocalPagesRender();
    return true;
}

void BrowserWindow::OpenSettingsSection(std::string section) {
    ShowSettingsSection(std::move(section));
}

void BrowserWindow::ShowSettingsSection(std::string section) {
    settings_focus_.Request(std::move(section));
    if (internal_page_ == LocalPageKind::kSettings) {
        ScheduleLocalPagesRender();
    } else {
        ToggleInternalPage(LocalPageKind::kSettings);
    }
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
        [this] { OnAgentSessionChanged(); });
    agent_session_->Configure(AgentConfig());
}

bool BrowserWindow::agent_panel_open() const noexcept {
    return chrome_ != nullptr && chrome_->agent_panel_open();
}

void BrowserWindow::ToggleAgentPanel() { SetAgentPanelOpen(!agent_panel_open()); }

void BrowserWindow::SetAgentPanelOpen(bool open) {
    CEF_REQUIRE_UI_THREAD();
    if (!open) {
        // A chip left unsent when the panel closes is stale by the time the panel opens again.
        context_chip_.reset();
    }
    if (closing_ || chrome_ == nullptr) {
        return;
    }
    if (open) {
        provider_statuses_.reset();
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
    const Tab* tab = DisplayedTab();
    if (tab == nullptr) {
        return {};
    }
    const NavigationSnapshot& nav = tab->navigation_state().snapshot();
    if (!IsWebUrl(nav.url)) {
        return {};
    }
    return BuildAgentContext(nav.url, nav.page_title, {});
}

BrowserWindow::AgentContextChip BrowserWindow::ActiveTabChip() const {
    const Tab* tab = DisplayedTab();
    if (tab == nullptr) {
        return {};
    }
    const NavigationSnapshot& nav = tab->navigation_state().snapshot();
    if (!IsWebUrl(nav.url)) {
        return {};
    }
    return {.title = nav.page_title, .url = nav.url, .selection = {}};
}

void BrowserWindow::ShowAgentContextChip(AgentContextChip chip) {
    // The chip holds what a send would use, clipped: never a whole page or selection.
    chip.title = std::string(ClipUtf8(chip.title, kAgentTitleLimit));
    chip.selection = std::string(ClipUtf8(chip.selection, kAgentSelectionLimit));
    context_chip_ = std::move(chip);
    SetAgentPanelOpen(true);
    ScheduleLocalPagesRender();
}

bool BrowserWindow::AskAgent(std::string prompt, std::string context) {
    if (closing_ || TrimmedCopy(prompt).empty()) {
        return false;
    }
    EnsureAgentSession();
    SetAgentPanelOpen(true);
    const agent::AgentTask task =
        agent_queue_.Enqueue(agent::TaskKind::kAsk, std::move(prompt), std::move(context),
                             static_cast<std::int64_t>(UnixNowMs()));
    ScheduleLocalPagesRender();
    return task.state != agent::TaskState::kFailed;
}

bool BrowserWindow::CollectFromTabs(std::string_view what,
                                    const std::vector<CollectChoice>& choices) {
    CEF_REQUIRE_UI_THREAD();
    const std::string asked = std::string(ClipUtf8(TrimmedCopy(what), kMaxCollectWhatBytes));
    if (closing_ || asked.empty()) {
        return false;
    }
    // Web pages only: a Report, the start page or a settings page has nothing to collect. The
    // sheet can be open while tabs close or the space changes, so a choice must still match its
    // URL; the index is only a hint for finding it.
    const std::vector<Tab>& space_tabs = active_space().tabs();
    const auto has_url = [&space_tabs](std::size_t index, std::string_view url) {
        const NavigationSnapshot& nav = space_tabs[index].navigation_state().snapshot();
        return nav.url == url && IsWebUrl(nav.url);
    };
    std::vector<CollectTab> tabs;
    std::vector<std::size_t> taken;
    for (const CollectChoice& choice : choices) {
        if (tabs.size() >= kMaxCollectTabs) {
            break;
        }
        std::optional<std::size_t> found;
        if (choice.index < space_tabs.size() && has_url(choice.index, choice.url)) {
            found = choice.index;
        } else {
            for (std::size_t index = 0; index < space_tabs.size() && !found; ++index) {
                if (has_url(index, choice.url)) {
                    found = index;
                }
            }
        }
        if (!found.has_value() || std::find(taken.begin(), taken.end(), *found) != taken.end()) {
            continue;
        }
        taken.push_back(*found);
        const NavigationSnapshot& nav = space_tabs[*found].navigation_state().snapshot();
        tabs.push_back({.index = *found, .title = nav.page_title, .url = nav.url});
    }
    if (tabs.empty()) {
        return false;
    }
    EnsureAgentSession();
    SetAgentPanelOpen(true);
    const agent::AgentTask task =
        agent_queue_.Enqueue(agent::TaskKind::kCollect, asked, BuildCollectPrompt(asked, tabs),
                             static_cast<std::int64_t>(UnixNowMs()));
    ScheduleLocalPagesRender();
    return task.state != agent::TaskState::kFailed;
}

json::Value BrowserWindow::CollectSheetStateJson() const {
    json::Value tabs = json::Value::MakeArray();
    const std::vector<Tab>& space_tabs = spaces_[active_space_index_].tabs();
    for (std::size_t index = 0; index < space_tabs.size(); ++index) {
        const Tab& tab = space_tabs[index];
        const NavigationSnapshot& nav = tab.navigation_state().snapshot();
        if (!IsWebUrl(nav.url)) {
            continue;
        }
        tabs.Push(json::Value::MakeObject()
                      .Set("index", json::Value::Int(static_cast<std::int64_t>(index)))
                      .Set("title", json::Value::String(nav.page_title))
                      .Set("url", json::Value::String(nav.url))
                      .Set("pinned", json::Value::Bool(tab.pinned())));
    }
    return json::Value::MakeObject()
        .Set("seq", json::Value::Int(static_cast<std::int64_t>(collect_sheet_seq_)))
        .Set("tabs", std::move(tabs));
}

bool BrowserWindow::RunAgentMenuCommand(int offset, const AgentContextChip& page,
                                        std::string_view link) {
    // Selections come from web pages only: a Report or the start page is not a page to quote.
    switch (offset) {
        case kAgentMenuAsk:
            if (!IsWebUrl(page.url)) {
                return false;
            }
            ShowAgentContextChip(page);
            return true;
        case kAgentMenuExplain:
            if (!IsWebUrl(page.url)) {
                return false;
            }
            static_cast<void>(AskAgent("Explain this in plain words.",
                                       BuildAgentContext(page.url, page.title, page.selection)));
            return true;
        case kAgentMenuSummarizeLink:
            if (!IsWebUrl(link)) {
                return false;
            }
            // The linked page opens in a peek card for the person; the agent reads it in its own
            // background tab.
            if (window_ != nullptr) {
                DeferPeek(std::string(link));
            }
            static_cast<void>(AskAgent("Summarize this page.", BuildLinkAgentContext(link)));
            return true;
        default:
            return false;
    }
}

void BrowserWindow::RunPaletteCommand(std::string_view id) {
    if (id == "open_bookmarks" || id == "import_bookmarks") {
        // The Bookmarks page holds the import menu; the palette does not open it for the person.
        OpenBookmarksPage();
    } else if (id == "add_bookmark") {
        static_cast<void>(AddCurrentPageBookmark());
    } else if (id == "toggle_global_favorite") {
        static_cast<void>(ToggleGlobalFavoriteForCurrentPage());
    } else if (id == "toggle_space_favorite") {
        static_cast<void>(ToggleSpaceFavoriteForCurrentPage());
    } else if (id == "move_global_favorite_left") {
        static_cast<void>(MoveCurrentPageFavorite(std::nullopt, -1));
    } else if (id == "move_global_favorite_right") {
        static_cast<void>(MoveCurrentPageFavorite(std::nullopt, 1));
    } else if (id == "move_space_favorite_up" || id == "move_space_favorite_down") {
        if (!spaces_.empty()) {
            const FavoriteScope scope{active_space().id().value};
            static_cast<void>(
                MoveCurrentPageFavorite(scope, id == "move_space_favorite_up" ? -1 : 1));
        }
    } else if (id == "new_bookmark_folder") {
        // A direct creation keeps this palette action simple; folder editing
        // remains available in the Bookmarks page.
        static_cast<void>(
            RunBookmarkMutation([this] { return bookmarks_.CreateFolder("New folder"); }));
    } else if (id == "sleep_inactive_tabs") {
        static_cast<void>(SleepInactiveTabsNow());
    } else if (id == "agent_tasks") {
        // The Tasks strip sits on top of the panel.
        SetAgentPanelOpen(true);
    } else if (id == "collect_from_tabs") {
        // The Collect sheet lives in the panel; without an agent the panel explains the setup.
        EnsureAgentSession();
        SetAgentPanelOpen(true);
        if (!ResolvedAgentCommand().empty()) {
            ++collect_sheet_seq_;
            ScheduleLocalPagesRender();
        }
    } else if (id == "summarize_page" || id == "ask_about_page") {
        const AgentContextChip page = ActiveTabChip();
        if (page.url.empty() || ResolvedAgentCommand().empty()) {
            // Nothing to ask about, or no agent yet: the panel explains how to set one up.
            SetAgentPanelOpen(true);
        } else if (id == "summarize_page") {
            static_cast<void>(
                AskAgent("Summarize this page.", BuildAgentContext(page.url, page.title, {})));
        } else {
            ShowAgentContextChip(page);
        }
    }
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
    json::Value context_chip = json::Value::Null();
    if (context_chip_.has_value()) {
        context_chip = json::Value::MakeObject()
                           .Set("title", json::Value::String(context_chip_->title))
                           .Set("url", json::Value::String(context_chip_->url))
                           .Set("selection", json::Value::String(context_chip_->selection));
    }
    const agent::AgentEndpoint* endpoint =
        agent_host_ != nullptr ? agent_host_->endpoint() : nullptr;
    json::Value state =
        json::Value::MakeObject()
            .Set("theme", std::move(theme))
            .Set("context", std::move(context))
            .Set("context_chip", std::move(context_chip))
            .Set("endpoint",
                 json::Value::String(endpoint != nullptr && endpoint->running() ? endpoint->url()
                                                                                : std::string()))
            .Set("confirmations", ConfirmationsStateJson())
            .Set("tasks", TasksStateJson())
            .Set("collect", CollectSheetStateJson());
    json::Value providers = AgentProvidersStateJson();
    for (auto& [key, value] : providers.object_val) {
        if (key == "provider" || key == "providers" || key == "env_override" ||
            key == "env_command") {
            state.Set(key, std::move(value));
        }
    }
    if (agent_session_ != nullptr) {
        std::optional<json::Value> session = json::Parse(agent_session_->StateJson());
        if (session.has_value()) {
            state.Set("session", std::move(*session));
        }
    }
    return json::Serialize(state);
}

void BrowserWindow::ScheduleLocalPagesRender() {
    RefreshAgentButtonLabel();
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
        // A focus request waits until the page has loaded; a render queued before that
        // would otherwise replace it with a state that has no focus.
        if (settings_page_->loaded()) {
            settings_focus_.Clear();
        }
    } else if (internal_page_ == LocalPageKind::kTabOverview && overview_page_ != nullptr) {
        overview_page_->Render(TabOverviewStateJson());
    } else if (internal_page_ == LocalPageKind::kBookmarks && bookmarks_page_ != nullptr) {
        bookmarks_page_->Render(BookmarksStateJson());
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
        case LocalPageKind::kStart:
            HandleStartPageMessage(message);
            return;
        case LocalPageKind::kSettings:
            HandleSettingsMessage(message);
            return;
        case LocalPageKind::kTabOverview:
            HandleOverviewMessage(message);
            return;
        case LocalPageKind::kBookmarks:
            HandleBookmarksMessage(message);
            return;
        case LocalPageKind::kReport:
            // Report pages live in tabs and reach HandleReportPageMessage through
            // the tab's console handler, which knows which report the URL names.
            return;
        case LocalPageKind::kPlan:
            // Plan pages live in tabs too and reach HandlePlanPageMessage the same way.
            return;
    }
}

void BrowserWindow::OnLocalPageBrowserCreated(LocalPageKind) {
    CEF_REQUIRE_UI_THREAD();
    if (view_swap_pending_) {
        SchedulePendingViewSwap();
    }
}

json::Value BrowserWindow::ThemeJson() const {
    const ChromeTokens tokens = ResolvedTokens(chrome_snapshot_.theme);
    json::Value branch = json::Value::MakeArray();
    for (const ArgbColor color : tokens.branch) {
        branch.Push(json::Value::String(CssColor(color)));
    }
    return json::Value::MakeObject()
        .Set("bg", json::Value::String(CssColor(tokens.background)))
        .Set("surface", json::Value::String(CssColor(tokens.surface)))
        .Set("surface_2", json::Value::String(CssColor(tokens.surface_secondary)))
        .Set("fill", json::Value::String(CssColor(tokens.fill)))
        .Set("text", json::Value::String(CssColor(tokens.text)))
        .Set("text_2", json::Value::String(CssColor(tokens.text_secondary)))
        .Set("text_3", json::Value::String(CssColor(tokens.text_tertiary)))
        .Set("border", json::Value::String(CssColor(tokens.border)))
        .Set("border_2", json::Value::String(CssColor(tokens.border_strong)))
        .Set("accent", json::Value::String(CssColor(tokens.accent)))
        .Set("selection", json::Value::String(CssColor(tokens.selection)))
        .Set("accent_soft", json::Value::String(CssColor(tokens.accent_soft)))
        .Set("accent_text", json::Value::String(CssColor(tokens.accent_text)))
        .Set("warn_bg", json::Value::String(CssColor(tokens.warning_bg)))
        .Set("warn_border", json::Value::String(CssColor(tokens.warning_border)))
        .Set("warn_text", json::Value::String(CssColor(tokens.warning_text)))
        .Set("success", json::Value::String(CssColor(tokens.success)))
        .Set("danger", json::Value::String(CssColor(tokens.danger)))
        .Set("segment_selected", json::Value::String(CssColor(tokens.segment_selected)))
        .Set("branch", std::move(branch))
        .Set("canvas", json::Value::String(CssColor(tokens.canvas)))
        .Set("font", json::Value::String(CssFontStack(tokens)))
        .Set("look", json::Value::String(std::string(LookName(tokens.look))))
        .Set("dark", json::Value::Bool(chrome_snapshot_.theme == ChromeTheme::kDark));
}

LocalPage* BrowserWindow::InternalPageFor(LocalPageKind kind) const {
    switch (kind) {
        case LocalPageKind::kSettings:
            return settings_page_.get();
        case LocalPageKind::kTabOverview:
            return overview_page_.get();
        case LocalPageKind::kBookmarks:
            return bookmarks_page_.get();
        case LocalPageKind::kAgent:
        case LocalPageKind::kStart:
        case LocalPageKind::kReport:
        case LocalPageKind::kPlan:
            return nullptr;
    }
    return nullptr;
}

void BrowserWindow::ToggleTabOverview() { ToggleInternalPage(LocalPageKind::kTabOverview); }

void BrowserWindow::ToggleSettings() { ToggleInternalPage(LocalPageKind::kSettings); }

void BrowserWindow::OpenBookmarksPage() {
    CEF_REQUIRE_UI_THREAD();
    if (internal_page_ == LocalPageKind::kBookmarks) {
        if (bookmarks_page_ != nullptr) {
            bookmarks_page_->Focus();
        }
        return;
    }
    bookmarks_page_status_.reset();
    ToggleInternalPage(LocalPageKind::kBookmarks);
}

void BrowserWindow::OpenBlockerSettings() { ShowSettingsSection("privacy"); }

void BrowserWindow::ToggleInternalPage(LocalPageKind kind) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || chrome_ == nullptr || kind == LocalPageKind::kAgent ||
        kind == LocalPageKind::kStart || kind == LocalPageKind::kReport ||
        kind == LocalPageKind::kPlan) {
        return;
    }
    if (internal_page_ == kind) {
        HideInternalPage();
        return;
    }
    std::unique_ptr<LocalPage>* page = nullptr;
    if (kind == LocalPageKind::kSettings) {
        page = &settings_page_;
    } else if (kind == LocalPageKind::kTabOverview) {
        page = &overview_page_;
    } else if (kind == LocalPageKind::kBookmarks) {
        page = &bookmarks_page_;
    } else {
        return;
    }
    if (*page == nullptr) {
        *page = std::make_unique<LocalPage>(kind, *this);
    }
    if (kind == LocalPageKind::kSettings) {
        provider_statuses_.reset();  // re-detect agents installed meanwhile
    }
    // Settings and All tabs replace the page the find bar was searching.
    CloseFindBar(/*refocus_page=*/false);
    internal_page_ = kind;
    show_internal_page_ = true;
    UpdateAddressBarForCurrentPage();
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    RequestViewSwap();
    FlushLocalPagesRender();
}

void BrowserWindow::HideInternalPage() {
    if (!internal_page_.has_value()) {
        return;
    }
    if (internal_page_ == LocalPageKind::kBookmarks) {
        bookmarks_page_status_.reset();
    }
    internal_page_.reset();
    show_internal_page_ = false;
    shortcut_conflict_.reset();
    UpdateAddressBarForCurrentPage();
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    RequestViewSwap();
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
    Value agent = AgentProvidersStateJson();

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
    row("Version", ISLAND_VERSION_STRING);
    row("Preferences", PrefsStore::DefaultPrefsFilePath().string());
    row("Session", SessionStore::DefaultSessionFilePath().string());
    row("Reports", ReportStore::DefaultReportsFilePath().string());
    row("Agent endpoint file", agent::DefaultDiscoveryFilePath().string());

    const std::string_view theme_pref = prefs_.theme == ThemePreference::kLight  ? "light"
                                        : prefs_.theme == ThemePreference::kDark ? "dark"
                                                                                 : "system";
    Value engines = Value::MakeArray();
    for (const SearchEngine& engine : SearchEngines()) {
        engines.Push(Value::MakeObject()
                         .Set("id", Value::String(std::string(engine.key)))
                         .Set("name", Value::String(std::string(engine.display_name))));
    }
    const ReaderSettings reader =
        ReaderSettings::Normalized(prefs_.reader_size, prefs_.reader_width, prefs_.reader_theme);
    const AppearanceSettings& look = prefs_.appearance;
    Value appearance_spaces = Value::MakeArray();
    for (std::size_t index = 0; index < spaces_.size(); ++index) {
        appearance_spaces.Push(Value::MakeObject()
                                   .Set("index", Value::Int(static_cast<std::int64_t>(index)))
                                   .Set("name", Value::String(spaces_[index].name()))
                                   .Set("color", Value::String(CssColor(spaces_[index].color())))
                                   .Set("active", Value::Bool(index == active_space_index_)));
    }
    Value appearance =
        Value::MakeObject()
            .Set("look", Value::String(std::string(LookName(MatchingLook(look)))))
            // The preset the knobs were last picked from; "custom" hides which one, so
            // Settings uses this to decide whether space colors apply.
            .Set("base_look", Value::String(std::string(LookName(look.look))))
            .Set("sidebar_tint", Value::Int(look.sidebar_tint_percent))
            .Set("sidebar_side",
                 Value::String(look.sidebar_side == SidebarSide::kRight ? "right" : "left"))
            .Set("card_radius", Value::Int(look.card_radius_dip))
            .Set("density",
                 Value::String(look.density == Density::kCompact ? "compact" : "comfortable"))
            .Set("font", Value::String(look.font_family.empty() ? "geist" : look.font_family))
            .Set("card_shadow", Value::Bool(look.card_shadow))
            .Set("full_bleed", Value::Bool(look.full_bleed))
            .Set("frameless", Value::Bool(FramelessWindowSupported()))
            .Set("spaces", std::move(appearance_spaces));
    Value settings =
        Value::MakeObject()
            .Set("theme_pref", Value::String(std::string(theme_pref)))
            .Set("version", Value::String(ISLAND_VERSION_STRING))
            .Set("appearance", std::move(appearance))
            .Set("search", Value::MakeObject()
                               .Set("engine", Value::String(std::string(search_engine().key)))
                               .Set("engines", std::move(engines)))
            .Set("history",
                 Value::MakeObject().Set("remember", Value::Bool(prefs_.remember_history)))
            .Set("tabs",
                 Value::MakeObject().Set("sleep_minutes", Value::Int(prefs_.tab_sleep_minutes)))
            .Set("reader", Value::MakeObject()
                               .Set("size", Value::Int(reader.size))
                               .Set("width", Value::String(reader.width))
                               .Set("theme", Value::String(reader.theme)))
            .Set("platform", Value::String(platform))
            .Set("agent", std::move(agent))
            .Set("trusted_sites", TrustedSitesJson())
            .Set("endpoint", std::move(endpoint))
            .Set("shortcuts", std::move(shortcuts))
            .Set("reports", ReportsListJson())
            .Set("import", Value::MakeObject().Set("sources", std::move(sources)))
            .Set("about", Value::MakeObject().Set("rows", std::move(about_rows)))
            .Set("privacy", ContentBlockerStateJson())
            .Set("update", UpdateStateJson())
            .Set("message", Value::String(settings_message_))
            .Set("focus", Value::String(settings_focus_.section()))
            .Set("focus_seq", Value::Int(static_cast<std::int64_t>(settings_focus_.sequence())));
    if (shortcut_conflict_.has_value()) {
        settings.Set(
            "shortcut_conflict",
            Value::MakeObject()
                .Set("action",
                     Value::String(std::string(KeyActionInfoFor(shortcut_conflict_->action).id)))
                .Set("binding", Value::String(FormatKeyBinding(shortcut_conflict_->binding)))
                .Set("conflict", Value::String(std::string(
                                     KeyActionInfoFor(shortcut_conflict_->holder).label))));
    }
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

namespace {

// Island's own pages are base64 data: documents; the overview neither shows nor
// searches that blob.
std::string OverviewUrl(const std::string& url) {
    return url.rfind("data:", 0) == 0 ? std::string() : url;
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
                    .Set("url",
                         Value::String(OverviewUrl(nav.url.empty() ? tab.startup_url() : nav.url)))
                    .Set("active", Value::Bool(space.has_active_tab() &&
                                               space.active_tab_index() == tab_index))
                    .Set("pinned", Value::Bool(tab.pinned()))
                    .Set(
                        "blocked",
                        Value::Int(static_cast<std::int64_t>(std::min<std::uint64_t>(
                            tab.blocked_requests(),
                            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))))
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

std::string BrowserWindow::BookmarksStateJson() const {
    using json::Value;
    Value folders = Value::MakeArray();
    Value bookmarks = Value::MakeArray();
    const BookmarkState& state = bookmarks_.state();
    for (const BookmarkFolder& folder : state.folders) {
        folders.Push(
            Value::MakeObject()
                .Set("id", Value::Int(static_cast<std::int64_t>(folder.id)))
                .Set("name", Value::String(folder.name))
                .Set("count", Value::Int(static_cast<std::int64_t>(folder.bookmark_ids.size()))));
    }
    for (const Bookmark& bookmark : state.bookmarks) {
        const BookmarkFolder* folder = nullptr;
        for (const BookmarkFolder& candidate : state.folders) {
            if (std::find(candidate.bookmark_ids.begin(), candidate.bookmark_ids.end(),
                          bookmark.id) != candidate.bookmark_ids.end()) {
                folder = &candidate;
                break;
            }
        }
        Value spaces = Value::MakeArray();
        for (const auto& [space_id, ids] : state.space_favorite_ids) {
            if (std::find(ids.begin(), ids.end(), bookmark.id) == ids.end()) {
                continue;
            }
            const Space* space = FindSpace(SpaceId{space_id});
            if (space == nullptr) {
                continue;
            }
            spaces.Push(Value::MakeObject()
                            .Set("id", Value::Int(static_cast<std::int64_t>(space_id)))
                            .Set("name", Value::String(space->name())));
        }
        const bool global =
            std::find(state.global_favorite_ids.begin(), state.global_favorite_ids.end(),
                      bookmark.id) != state.global_favorite_ids.end();
        bookmarks.Push(Value::MakeObject()
                           .Set("id", Value::Int(static_cast<std::int64_t>(bookmark.id)))
                           .Set("title", Value::String(bookmark.title))
                           .Set("url", Value::String(bookmark.url))
                           .Set("host", Value::String(UrlHost(bookmark.url)))
                           .Set("folder_id", Value::Int(static_cast<std::int64_t>(
                                                 folder != nullptr ? folder->id : 0)))
                           .Set("folder_path", Value::String(folder != nullptr ? folder->name : ""))
                           .Set("global", Value::Bool(global))
                           .Set("spaces", std::move(spaces)));
    }
    Value sources = Value::MakeArray();
    for (const ImportSourceInfo& source : DetectInstalledSources(UserBaseHome())) {
        if (source.available && source.id != ImportSource::kArc) {
            sources.Push(Value::MakeObject()
                             .Set("id", Value::String(std::string(ImportSourceId(source.id))))
                             .Set("label", Value::String(source.display_name)));
        }
    }
    Value result = Value::MakeObject()
                       .Set("theme", ThemeJson())
                       .Set("folders", std::move(folders))
                       .Set("bookmarks", std::move(bookmarks))
                       .Set("selected_folder",
                            selected_bookmark_folder_.has_value() &&
                                    bookmarks_.FindFolder(*selected_bookmark_folder_) != nullptr
                                ? Value::Int(static_cast<std::int64_t>(*selected_bookmark_folder_))
                                : Value::String("all"))
                       .Set("import_sources", std::move(sources));
    if (!spaces_.empty()) {
        result.Set("current_space",
                   Value::MakeObject()
                       .Set("id", Value::Int(static_cast<std::int64_t>(active_space().id().value)))
                       .Set("name", Value::String(active_space().name())));
    }
    if (failed_bookmark_mutation_.has_value()) {
        result.Set(
            "status",
            Value::MakeObject()
                .Set("kind", Value::String("error"))
                .Set("text", Value::String(failed_bookmark_mutation_->message))
                .Set("retry", Value::Bool(static_cast<bool>(failed_bookmark_mutation_->retry))));
    } else {
        result.Set("status", Value::Null());
    }
    if (bookmarks_page_status_.has_value()) {
        result.Set("page_status", Value::MakeObject()
                                      .Set("kind", Value::String(bookmarks_page_status_->kind))
                                      .Set("text", Value::String(bookmarks_page_status_->message)));
    } else {
        result.Set("page_status", Value::Null());
    }
    return json::Serialize(result);
}

void BrowserWindow::HandleBookmarksMessage(const json::Value& message) {
    CEF_REQUIRE_UI_THREAD();
    const std::string_view type = message.StringOr("type", "");
    const auto valid_id = [&message](std::string_view key) -> std::optional<std::uint64_t> {
        const json::Value* value = message.FindMember(key);
        if (value == nullptr || !value->IsInt() || value->int_val <= 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value->int_val);
    };
    const auto valid_text = [&message](std::string_view key, std::size_t max,
                                       bool nonempty) -> std::optional<std::string> {
        const json::Value* value = message.FindMember(key);
        if (value == nullptr || !value->IsString() || value->string_val.size() > max ||
            (nonempty && value->string_val.empty())) {
            return std::nullopt;
        }
        return value->string_val;
    };
    const auto reject = [&type] {
        fprintf(stderr, "island: rejected Bookmarks message: %.*s\n", static_cast<int>(type.size()),
                type.data());
    };
    const auto finish = [this](BookmarkResult result) {
        if (!result.ok()) {
            ScheduleLocalPagesRender();
        }
    };
    if (type == "ready") {
        ScheduleLocalPagesRender();
        return;
    }
    // Page feedback describes the preceding interaction only. Durable save failures live in
    // failed_bookmark_mutation_ instead, so their Retry action remains visible.
    bookmarks_page_status_.reset();
    if (type == "select-folder") {
        const json::Value* value = message.FindMember("id");
        if (value != nullptr && value->IsString() && value->string_val == "all") {
            selected_bookmark_folder_.reset();
            ScheduleLocalPagesRender();
            return;
        }
        const std::optional<std::uint64_t> id = valid_id("id");
        if (!id || bookmarks_.FindFolder(*id) == nullptr) {
            reject();
            return;
        }
        selected_bookmark_folder_ = *id;
        ScheduleLocalPagesRender();
        return;
    }
    if (type == "retry") {
        finish(RetryLastBookmarkMutation());
        return;
    }
    if (type == "folder:create") {
        const auto name = valid_text("name", 120, true);
        if (!name) {
            reject();
            return;
        }
        finish(RunBookmarkMutation([this, name = *name] { return bookmarks_.CreateFolder(name); }));
        return;
    }
    if (type == "folder:rename") {
        const auto id = valid_id("id");
        const auto name = valid_text("name", 120, true);
        if (!id || !name || bookmarks_.FindFolder(*id) == nullptr) {
            reject();
            return;
        }
        finish(RunBookmarkMutation(
            [this, id = *id, name = *name] { return bookmarks_.RenameFolder(id, name); }));
        return;
    }
    if (type == "folder:delete") {
        const auto id = valid_id("id");
        const auto mode = valid_text("mode", 6, true);
        if (!id || !mode || (*mode != "move" && *mode != "delete") ||
            bookmarks_.FindFolder(*id) == nullptr) {
            reject();
            return;
        }
        const BookmarkResult result = RunBookmarkMutation([this, id = *id, mode = *mode] {
            return bookmarks_.DeleteFolderWithContents(id, mode == "move");
        });
        finish(result);
        if (result.ok() && selected_bookmark_folder_ == id) {
            selected_bookmark_folder_.reset();
        }
        return;
    }
    if (type == "import") {
        const auto source_id = valid_text("source_id", 32, true);
        const std::optional<ImportSource> source =
            source_id ? ImportSourceFromId(*source_id) : std::nullopt;
        bool available = false;
        if (source.has_value()) {
            for (const ImportSourceInfo& info : DetectInstalledSources(UserBaseHome())) {
                if (info.id == *source && info.available) {
                    available = true;
                    break;
                }
            }
        }
        if (!source_id || !source || !available) {
            reject();
            return;
        }
        const BookmarkImportResult result = ImportBookmarksFromBrowser(*source_id);
        if (!result.durable_failure) {
            bookmarks_page_status_ =
                BookmarksPageStatus{.kind = result.ok ? "ok" : "error", .message = result.text};
        }
        ScheduleLocalPagesRender();
        return;
    }
    const auto bookmark_id = valid_id("id");
    if (!bookmark_id || bookmarks_.FindBookmark(*bookmark_id) == nullptr) {
        reject();
        return;
    }
    if (type == "bookmarks:edit") {
        const auto title = valid_text("title", 500, false);
        const auto url = valid_text("url", 4000, true);
        if (!title || !url) {
            reject();
            return;
        }
        finish(RunBookmarkMutation([this, id = *bookmark_id, title = *title, url = *url] {
            return bookmarks_.EditBookmark(id, title, url);
        }));
    } else if (type == "bookmarks:delete") {
        finish(RunBookmarkMutation(
            [this, id = *bookmark_id] { return bookmarks_.DeleteBookmark(id); }));
    } else if (type == "bookmarks:move") {
        const auto folder_id = valid_id("folder_id");
        if (!folder_id || bookmarks_.FindFolder(*folder_id) == nullptr) {
            reject();
            return;
        }
        finish(RunBookmarkMutation([this, id = *bookmark_id, folder = *folder_id] {
            return bookmarks_.MoveBookmark(id, folder);
        }));
    } else if (type == "favorite:toggle-global" || type == "favorite:toggle-space") {
        if (type == "favorite:toggle-space" && spaces_.empty()) {
            reject();
            return;
        }
        const FavoriteScope scope = type == "favorite:toggle-global"
                                        ? FavoriteScope{}
                                        : FavoriteScope{active_space().id().value};
        finish(ToggleFavorite(scope, *bookmark_id));
    } else if (type == "bookmarks:open" || type == "bookmarks:open-new-tab") {
        const Bookmark* bookmark = bookmarks_.FindBookmark(*bookmark_id);
        const ValidatedAddress address = address_validator_ ? address_validator_(bookmark->url)
                                                            : ParseAndValidate(bookmark->url);
        if (!address.is_valid()) {
            bookmarks_page_status_ = BookmarksPageStatus{
                .kind = "error", .message = "This bookmark has an unsupported address."};
            ScheduleLocalPagesRender();
            return;
        }
        if (type == "bookmarks:open-new-tab") ExecuteCommand(BrowserCommand::kNewTab);
        HideInternalPage();
        SubmitPaletteUrl(bookmark->url, address);
    } else {
        reject();
    }
}

bool BrowserWindow::SetSpaceColorFromHex(std::size_t space_index, std::string_view hex) {
    if (space_index >= spaces_.size() || hex.size() != 7 || hex.front() != '#') {
        return false;
    }
    std::uint32_t rgb = 0;
    for (const char ch : hex.substr(1)) {
        rgb <<= 4;
        if (ch >= '0' && ch <= '9') {
            rgb |= static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            rgb |= static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            rgb |= static_cast<std::uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    spaces_[space_index].SetColor({.argb = 0xFF000000U | rgb});
    return true;
}

void BrowserWindow::ApplyAppearance() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    ApplyTheme(window_, ResolvedChromeTheme(), true);
    UpdateChromeCollections();
    ApplySidebarState();
    if (chrome_ != nullptr) {
        // The new sidebar side / inset need one more pass over the live bounds.
        chrome_->ApplyTheme(ResolvedTokens(chrome_snapshot_.theme));
    }
}

void BrowserWindow::ApplyAppearanceForDev(std::string_view key, std::string_view value) {
    using json::Value;
    Value message = Value::MakeObject();
    if (key == "look") {
        const std::optional<LookPreset> look = ParseLook(value);
        if (!look.has_value()) {
            return;
        }
        message.Set("type", Value::String("set_look"))
            .Set("look", Value::String(std::string(LookName(*look))));
        HandleSettingsMessage(message);
        return;
    } else if (key == "space_color") {
        const std::size_t equals = value.find('=');
        if (equals == std::string_view::npos) {
            return;
        }
        message.Set("type", Value::String("set_space_color"))
            .Set("index", Value::Int(std::strtoll(std::string(value.substr(0, equals)).c_str(),
                                                  nullptr, 10)))
            .Set("color", Value::String(std::string(value.substr(equals + 1))));
    } else if (key == "reset") {
        message.Set("type", Value::String("reset_appearance"));
    } else {
        message.Set("type", Value::String("set_appearance"))
            .Set("key", Value::String(std::string(key)));
        if (value == "true" || value == "false") {
            message.Set("value", Value::Bool(value == "true"));
        } else if (!value.empty() && std::all_of(value.begin(), value.end(),
                                                 [](char ch) { return ch >= '0' && ch <= '9'; })) {
            message.Set("value", Value::Int(std::strtoll(std::string(value).c_str(), nullptr, 10)));
        } else {
            message.Set("value", Value::String(std::string(value)));
        }
    }
    HandleAppearanceMessage(message);
}

void BrowserWindow::HandleAppearanceMessage(const json::Value& message) {
    const std::string_view type = message.StringOr("type", "");
    if (type == "reset_appearance") {
        prefs_.appearance = AppearanceSettings{};
        SavePrefs();
        ApplyAppearance();
        return;
    }
    if (type == "set_space_color") {
        const std::int64_t index = message.IntOr("index", -1);
        if (index >= 0 &&
            SetSpaceColorFromHex(static_cast<std::size_t>(index), message.StringOr("color", ""))) {
            SaveSession();
            ApplyAppearance();
        }
        return;
    }
    const std::string_view key = message.StringOr("key", "");
    AppearanceSettings next = prefs_.appearance;
    if (key == "sidebar_tint") {
        next.sidebar_tint_percent =
            static_cast<int>(message.IntOr("value", next.sidebar_tint_percent));
    } else if (key == "sidebar_side") {
        next.sidebar_side =
            message.StringOr("value", "") == "right" ? SidebarSide::kRight : SidebarSide::kLeft;
    } else if (key == "card_radius") {
        next.card_radius_dip = static_cast<int>(message.IntOr("value", next.card_radius_dip));
    } else if (key == "density") {
        next.density =
            message.StringOr("value", "") == "compact" ? Density::kCompact : Density::kComfortable;
    } else if (key == "font") {
        next.font_family = std::string(message.StringOr("value", ""));
    } else if (key == "card_shadow") {
        next.card_shadow = message.BoolOr("value", next.card_shadow);
    } else if (key == "full_bleed") {
        next.full_bleed = message.BoolOr("value", next.full_bleed);
    } else {
        return;
    }
    next = NormalizeAppearance(next);
    if (next == prefs_.appearance) {
        return;
    }
    prefs_.appearance = next;
    SavePrefs();
    ApplyAppearance();
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
    if (HandleUpdateMessage(type, message)) {
        ScheduleLocalPagesRender();
        return;
    }
    if (type == "set_theme") {
        ThemePreference preference = ThemePreference::kSystem;
        if (ThemePreferenceFromString(message.StringOr("value", ""), preference)) {
            static_cast<void>(SetThemePreference(preference));
        }
    } else if (type == "set_look") {
        if (const std::optional<LookPreset> look = ParseLook(message.StringOr("look", ""))) {
            prefs_.appearance = ApplyLookPreset(*look);
            SavePrefs();
            ApplyAppearance();
        }
    } else if (type == "set_appearance" || type == "set_space_color" ||
               type == "reset_appearance") {
        HandleAppearanceMessage(message);
    } else if (type == "set_search_engine") {
        const std::string_view id = message.StringOr("id", "");
        if (IsSearchEngineKey(id)) {
            prefs_.search_engine = std::string(id);
            SavePrefs();
            RenderStartPages();
        }
    } else if (type == "set_remember_history") {
        const json::Value* remember = message.FindMember("value");
        if (remember != nullptr && remember->IsBool()) {
            prefs_.remember_history = remember->bool_val;
            SavePrefs();
        }
    } else if (type == "clear_history") {
        if (history_ != nullptr) {
            history_->ClearAsync([window = CefRefPtr<BrowserWindow>(this)](bool cleared) {
                CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                                        [](CefRefPtr<BrowserWindow> browser_window, bool ok) {
                                            browser_window->settings_message_ =
                                                ok ? "Browsing history cleared."
                                                   : "Browsing history could not be cleared.";
                                            if (ok) {
                                                browser_window->start_tiles_.clear();
                                                browser_window->RenderStartPages();
                                            }
                                            browser_window->ScheduleLocalPagesRender();
                                        },
                                        window, cleared)));
            });
        } else {
            settings_message_ = "Browsing history cleared.";
        }
    } else if (type == "set_tab_sleep") {
        static_cast<void>(SetTabSleepMinutes(static_cast<int>(message.IntOr("minutes", -1))));
    } else if (type == "set_reader_settings") {
        const ReaderSettings settings =
            ReaderSettings::Normalized(static_cast<int>(message.IntOr("size", prefs_.reader_size)),
                                       message.StringOr("width", prefs_.reader_width),
                                       message.StringOr("theme", prefs_.reader_theme));
        if (settings.size != prefs_.reader_size || settings.width != prefs_.reader_width ||
            settings.theme != prefs_.reader_theme) {
            prefs_.reader_size = settings.size;
            prefs_.reader_width = settings.width;
            prefs_.reader_theme = settings.theme;
            SavePrefs();
        }
    } else if (type == "remove_trusted_site") {
        RemoveTrustedSite(message.StringOr("host", ""));
    } else if (type == "set_agent_provider") {
        static_cast<void>(SetAgentProvider(message.StringOr("id", "")));
    } else if (type == "set_agent_command") {
        // The custom provider's command; it takes effect on the next start.
        prefs_.agent_command = TrimmedCopy(message.StringOr("command", ""));
        SavePrefs();
        if (agent_session_ != nullptr) {
            agent_session_->Configure(AgentConfig());
        }
    } else if (type == "open_agent_docs") {
        // Only a registry URL opens; the page cannot supply one.
        if (const agent::AgentProvider* provider =
                agent::FindAgentProvider(message.StringOr("id", ""))) {
            static_cast<void>(OpenNewTab(std::string(provider->docs_url)));
        }
    } else if (type == "open_agent") {
        SetAgentPanelOpen(true);
    } else if (type == "set_content_blocker_pref") {
        const json::Value* value = message.FindMember("value");
        if (value == nullptr || !value->IsBool()) {
            return;
        }
        const std::string_view key = message.StringOr("key", "");
        if (key == "ads") {
            prefs_.block_ads = value->bool_val;
        } else if (key == "trackers") {
            prefs_.block_trackers = value->bool_val;
        } else {
            return;
        }
        SavePrefs();
        ApplyContentBlockerPolicy();
    } else if (type == "add_content_blocker_site") {
        const std::string site = NormalizeAllowlistSite(message.StringOr("site", ""));
        if (!site.empty() && std::find(prefs_.content_blocker_allowlist.begin(),
                                       prefs_.content_blocker_allowlist.end(),
                                       site) == prefs_.content_blocker_allowlist.end()) {
            prefs_.content_blocker_allowlist.push_back(site);
            std::sort(prefs_.content_blocker_allowlist.begin(),
                      prefs_.content_blocker_allowlist.end());
            SavePrefs();
            ApplyContentBlockerPolicy();
        }
    } else if (type == "remove_content_blocker_site") {
        const std::string site = NormalizeAllowlistSite(message.StringOr("site", ""));
        std::erase(prefs_.content_blocker_allowlist, site);
        SavePrefs();
        ApplyContentBlockerPolicy();
    } else if (type == "update_content_blocker") {
        if (content_blocker_ != nullptr) {
            content_blocker_->UpdateNow();
        }
    } else if (type == "open_report") {
        static_cast<void>(OpenReportTab(message.StringOr("id", "")));
        return;
    } else if (type == "delete_report") {
        static_cast<void>(DeleteReport(message.StringOr("id", "")));
    } else if (type == "record_shortcut" || type == "replace_shortcut") {
        // Recording binds a free chord at once; a taken chord waits for
        // Replace, which unbinds the holder and moves the chord over.
        shortcut_conflict_.reset();
        const std::optional<KeyAction> action = KeyActionFromId(message.StringOr("action", ""));
        const std::optional<KeyBinding> binding = ParseKeyBinding(message.StringOr("binding", ""));
        if (!action.has_value()) {
            return;
        }
        if (!binding.has_value()) {
            settings_message_ = "That shortcut needs Ctrl/Cmd or Alt.";
        } else if (const std::optional<KeyAction> holder = keymap_.ConflictFor(*action, *binding)) {
            if (type == "record_shortcut") {
                shortcut_conflict_ = ShortcutConflict{*action, *binding, *holder};
            } else {
                keymap_.SetBinding(*holder, std::nullopt);
                static_cast<void>(SetKeyBinding(*action, binding));
            }
        } else {
            static_cast<void>(SetKeyBinding(*action, binding));
        }
    } else if (type == "cancel_shortcut") {
        shortcut_conflict_.reset();
    } else if (type == "set_binding" || type == "reset_binding") {
        shortcut_conflict_.reset();
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
        shortcut_conflict_.reset();
        ResetKeymap();
        settings_message_ = "Shortcuts reset to defaults.";
    } else if (type == "import") {
        settings_message_ = ImportFromBrowser(message.StringOr("source", ""));
    }
    ScheduleLocalPagesRender();
}

void BrowserWindow::ConfigureUpdater(update::Updater::Config config,
                                     std::unique_ptr<update::UpdateFetcher> fetcher) {
    updater_ = std::make_unique<update::Updater>(std::move(config), std::move(fetcher),
                                                 [this] { OnUpdaterChanged(); });
}

bool BrowserWindow::CheckForUpdates() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || updater_ == nullptr) {
        return false;
    }
    if (update::UpdatesDisabledByEnvironment()) {
        settings_message_ = "Updates are turned off by ISLAND_DISABLE_UPDATES.";
        ScheduleLocalPagesRender();
        return false;
    }
    if (!updater_->Check(prefs_.include_prereleases)) {
        return false;
    }
    prefs_.last_update_check = UnixNow();
    SavePrefs();
    return true;
}

void BrowserWindow::RunStartupUpdateCheck() {
    if (closing_ || updater_ == nullptr || update::UpdatesDisabledByEnvironment()) {
        return;
    }
    // A relaunch after an update: report the outcome, drop the backup.
    static_cast<void>(updater_->FinalizePreviousInstall());
    if (update::AutoCheckDue(prefs_.auto_check_updates, prefs_.last_update_check, UnixNow())) {
        static_cast<void>(CheckForUpdates());
    }
}

void BrowserWindow::RestartToUpdate() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || updater_ == nullptr) {
        return;
    }
    const std::optional<std::filesystem::path> script =
        updater_->PrepareInstall(update::CurrentProcessId());
    if (!script.has_value()) {
        return;  // the updater reports why
    }
    if (!update::LaunchDetachedScript(updater_->config().install.platform, *script)) {
        settings_message_ = "Couldn't start the updater.";
        ScheduleLocalPagesRender();
        return;
    }
    // The script waits for this process to exit, then swaps and relaunches.
    RequestClose();
}

void BrowserWindow::OnUpdaterChanged() {
    if (closing_ || updater_ == nullptr) {
        return;
    }
    const update::UpdateSnapshot& snapshot = updater_->snapshot();
    std::string notice;
    if (snapshot.status == update::UpdateStatus::kAvailable) {
        notice = "Island " + snapshot.latest_version + " is available";
    } else if (snapshot.status == update::UpdateStatus::kReady) {
        notice = "Island " + snapshot.latest_version + " is ready. Restart to update";
    }
    if (chrome_ != nullptr) {
        chrome_->SetUpdateNotice(notice);
    }
    ScheduleLocalPagesRender();
}

json::Value BrowserWindow::UpdateStateJson() const {
    json::Value state = updater_ != nullptr ? updater_->StateJson() : json::Value::MakeObject();
    state.Set("auto_check", json::Value::Bool(prefs_.auto_check_updates))
        .Set("include_prereleases", json::Value::Bool(prefs_.include_prereleases))
        .Set("last_check", json::Value::Int(prefs_.last_update_check))
        .Set("disabled_by_env", json::Value::Bool(update::UpdatesDisabledByEnvironment()));
    return state;
}

bool BrowserWindow::HandleUpdateMessage(std::string_view type, const json::Value& message) {
    if (type == "check_updates") {
        static_cast<void>(CheckForUpdates());
    } else if (type == "install_update") {
        if (updater_ != nullptr && !update::UpdatesDisabledByEnvironment()) {
            static_cast<void>(updater_->StartDownload());
        }
    } else if (type == "restart_to_update") {
        RestartToUpdate();
    } else if (type == "open_release_notes") {
        // Only the updater's own release URL, never one from the page.
        const std::string url = updater_ != nullptr ? updater_->snapshot().release_url : "";
        if (!url.empty() && OpenNewTab(url)) {
            HideInternalPage();
        }
    } else if (type == "set_update_pref") {
        const std::string_view key = message.StringOr("key", "");
        const json::Value* value = message.FindMember("value");
        if (value == nullptr || !value->IsBool()) {
            return true;
        }
        if (key == "auto_check") {
            prefs_.auto_check_updates = value->bool_val;
        } else if (key == "include_prereleases") {
            prefs_.include_prereleases = value->bool_val;
        } else {
            return true;
        }
        SavePrefs();
    } else {
        return false;
    }
    return true;
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
    SelectTabWithOneAttach(space, tab);
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
        const TabId closed_id = spaces_[space_index].tabs()[tab].id();
        if (!ConfirmPlanTabClose(closed_id)) {
            keep_internal_page_ = false;
            return false;
        }
        // The space's last tab: the space goes with it, like in the rail.
        RememberClosedTab(spaces_[space_index].tabs()[tab]);
        ForgetPlanTabMapping(closed_id);
        Space removed = RemoveSpaceAtIndex(space_index);
        AttachActiveTabObservers();
        CloseSpaceBrowsers(removed);
    } else {
        Space& space = spaces_[space_index];
        const TabId closed_id = space.tabs()[tab].id();
        if (!ConfirmPlanTabClose(closed_id)) {
            keep_internal_page_ = false;
            return false;
        }
        RememberClosedTab(space.tabs()[tab]);
        std::optional<Tab> removed = space.RemoveTab(closed_id);
        ForgetPlanTabMapping(closed_id);
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
    // The mark is not carried over: moving a tab is the person's act, so the tab is theirs now
    // and the agent must ask before it navigates or closes it.
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
    const BookmarkImportResult result = ImportBookmarksFromBrowser(source_id);
    if (result.durable_failure) {
        return {};
    }
    return result.text;
}

BrowserWindow::BookmarkImportResult BrowserWindow::ImportBookmarksFromBrowser(
    std::string_view source_id) {
    CEF_REQUIRE_UI_THREAD();
    const std::optional<ImportSource> source = ImportSourceFromId(source_id);
    if (!source.has_value() || *source == ImportSource::kArc || closing_) {
        return {.ok = false, .text = "Unknown import source."};
    }
    std::string name(source_id);
    for (const ImportSourceInfo& info : DetectInstalledSources(UserBaseHome())) {
        if (info.id == *source) {
            name = info.display_name;
            break;
        }
    }
    const std::optional<std::vector<BookmarkItem>> items =
        ImportFromSource(*source, UserBaseHome());
    if (!items.has_value()) {
        return {.ok = false, .text = "Could not read " + name + " bookmarks."};
    }
    const BookmarkResult merged =
        RunBookmarkMutation([this, folder = "Imported from " + name, items = *items] {
            return bookmarks_.Import(folder, items);
        });
    if (!merged.ok()) {
        return {.ok = false, .durable_failure = true, .text = merged.message};
    }
    const std::size_t added = merged.added;
    return {.ok = true,
            .text = added == 0 ? "No new bookmarks in " + name + "."
                               : "Imported " + std::to_string(added) + " bookmarks from " + name +
                                     ". Find them in the command palette."};
}

bool BrowserWindow::ReopenClosedTab() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || closed_tab_urls_.empty()) {
        return false;
    }
    std::string url = std::move(closed_tab_urls_.back());
    closed_tab_urls_.pop_back();
    AppendTabToActiveSpace(std::move(url));
    return true;
}

bool BrowserWindow::CanStopLoading() const {
    const Tab* tab = active_tab();
    return !closing_ && tab != nullptr && tab->browser() != nullptr && tab->browser()->IsLoading();
}

bool BrowserWindow::CanStopLoadingWithEscape() const {
    // An open cheatsheet owns Escape (it closes the sheet). An open peek owns it next (it
    // closes the card) unless a palette or dialog sits above it, and only then does Escape
    // stop a loading page.
    return ResolveStopLoading(cheatsheet_open(), peek_open() && !OverlayShown(),
                              CanStopLoading() && !OverlayShown()) != StopLoadingTarget::kNone;
}

bool BrowserWindow::OverlayShown() const noexcept {
    return (welcome_ != nullptr && welcome_->visible()) ||
           (search_palette_ != nullptr && search_palette_->visible()) ||
           (command_palette_ != nullptr && command_palette_->visible()) ||
           (space_rename_overlay_ != nullptr && space_rename_overlay_->visible()) ||
           internal_page_.has_value() ||
           address_bar_model_.snapshot().mode == AddressBarMode::kEditing;
}

std::string BrowserWindow::WebUrlOf(const Tab& tab) const {
    // While a tab shows its reader view the page it came from is the one to copy or reopen.
    const std::string committed = EffectiveUrl(tab);
    const std::string& url = committed.empty() ? tab.startup_url() : committed;
    return IsWebUrl(url) ? url : std::string();
}

void BrowserWindow::RememberClosedTab(const Tab& tab) {
    // Non-web pages reopen at the startup page rather than a stored URL.
    closed_tab_urls_.push_back(WebUrlOf(tab));
    if (closed_tab_urls_.size() > kClosedTabLimit) {
        closed_tab_urls_.erase(closed_tab_urls_.begin());
    }
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
    if (type == "open_settings") {
        ShowSettingsSection("agent");
        return;
    }
    if (type == "confirm") {
        HandleConfirmMessage(message);
        return;
    }
    EnsureAgentSession();
    if (type == "send") {
        SubmitAgentPrompt(std::string(message.StringOr("text", "")),
                          message.BoolOr("include_context", true));
    } else if (type == "clear_context") {
        context_chip_.reset();
    } else if (type == "collect") {
        std::vector<CollectChoice> choices;
        if (const json::Value* list = message.FindMember("tabs");
            list != nullptr && list->IsArray()) {
            for (const json::Value& item : list->array_val) {
                const json::Value* index = item.IsObject() ? item.FindMember("index") : nullptr;
                if (index != nullptr && index->IsInt() && index->int_val >= 0) {
                    choices.push_back({.index = static_cast<std::size_t>(index->int_val),
                                       .url = std::string(item.StringOr("url", ""))});
                }
            }
        }
        static_cast<void>(CollectFromTabs(message.StringOr("what", ""), choices));
    } else if (type == "cancel" || type == "task_stop") {
        // Cards carry no origin, so Stop withdraws every open card; a stopped turn's action
        // must not run later.
        StopAgentTask();
    } else if (type == "task_move" || type == "task_remove" || type == "tasks_clear_finished") {
        HandleTaskMessage(type, message);
    } else if (type == "permission") {
        const json::Value* option = message.FindMember("option_id");
        agent_session_->ResolvePermission(message.IntOr("request_id", -1),
                                          option != nullptr && option->IsString()
                                              ? std::optional<std::string>(option->string_val)
                                              : std::nullopt);
        RefreshAddressIndicators();
    } else if (type == "new_chat") {
        // The new conversation drops the live turn without an end event.
        AbandonInFlightTurn([this] { agent_session_->NewChat(); });
        CancelPendingConfirmations();
        context_chip_.reset();
    } else if (type == "set_provider") {
        static_cast<void>(SetAgentProvider(message.StringOr("id", "")));
    } else if (type == "start") {
        // (Re)starts the selected agent. A command that differs from what the
        // selected provider runs becomes the custom provider's command.
        const std::string command = TrimmedCopy(message.StringOr("command", ""));
        if (!command.empty() && command != ResolvedAgentCommand()) {
            prefs_.agent_provider = std::string(agent::kCustomAgentProviderId);
            prefs_.agent_command = command;
            SavePrefs();
        }
        static_cast<void>(agent_session_->Start(AgentConfig()));
    }
    ScheduleLocalPagesRender();
}

bool BrowserWindow::SubmitAgentPrompt(std::string text, bool include_context) {
    if (closing_ || TrimmedCopy(text).empty()) {
        return false;
    }
    EnsureAgentSession();
    std::string context;
    if (include_context) {
        context = context_chip_.has_value()
                      ? BuildAgentContext(context_chip_->url, context_chip_->title,
                                          context_chip_->selection)
                      : ActiveTabAgentContext();
    }
    // A question sent with a chip is an ask about that chip; the chip is for one question.
    const agent::TaskKind kind = include_context && context_chip_.has_value()
                                     ? agent::TaskKind::kAsk
                                     : agent::TaskKind::kChat;
    context_chip_.reset();
    const agent::AgentTask task = agent_queue_.Enqueue(kind, std::move(text), std::move(context),
                                                       static_cast<std::int64_t>(UnixNowMs()));
    ScheduleLocalPagesRender();
    return task.state != agent::TaskState::kFailed;
}

void BrowserWindow::StartQueuedTask(const agent::AgentTask& task) {
    in_flight_id_.clear();
    queue_waiting_ = false;
    waiting_id_.clear();
    reported_done_ = 0;
    reported_total_ = 0;
    start_error_.clear();
    bool started = false;
    if (closing_) {
        start_error_ = "The window is closing.";
    } else {
        EnsureAgentSession();
        // A grant ("allow for this task") never outlives the task that earned it.
        if (agent_host_ != nullptr) {
            agent_host_->toolbox().ResetTaskGrants();
        }
        if (!queue_fixture_ && AgentTurnOpen()) {
            // A turn runs outside the queue (plan continuation): the task is live and sent when
            // that turn ends.
            deferred_task_ = task;
            in_flight_id_ = task.id;
            in_flight_is_chat_ = false;
            return;
        }
        task_items_base_ = agent_session_->transcript().items().size();
        if (queue_fixture_) {
            started = true;  // dev capture: the task is live, nothing is sent
        } else if (task.kind == agent::TaskKind::kResearch) {
            started = StartResearchTask(task);
        } else if (task.kind == agent::TaskKind::kCollect) {
            // The collect prompt (the context) names the tabs; the page context does not apply.
            collect_turns_before_ = TurnsEnded();
            started = SendToAgent(task.context);
            if (!started) {
                start_error_ = agent_session_->error().empty() ? "The agent could not be started."
                                                               : agent_session_->error();
            }
        } else {
            started = SendToAgent(task.prompt, task.context);
            if (!started) {
                start_error_ = agent_session_->error().empty() ? "The agent could not be started."
                                                               : agent_session_->error();
            }
        }
    }
    if (!started) {
        // Reported after the failed start, so the queue moves on to the next task.
        agent_queue_.OnTurnFinished(task.id, false);
        return;
    }
    in_flight_id_ = task.id;
    in_flight_is_chat_ = task.kind != agent::TaskKind::kResearch;
    SyncQueueWaiting();
}

bool BrowserWindow::AgentTurnOpen() const {
    if (agent_busy_for_test_.has_value()) {
        return *agent_busy_for_test_;
    }
    return !agent_send_for_test_ && agent_session_ != nullptr && agent_session_->busy();
}

bool BrowserWindow::OutsideResearchOwnsTurn() const {
    return research_.has_value() && !research_->awaiting_approval && research_->task_id.empty();
}

void BrowserWindow::PostStartDeferredTask() {
    if (!deferred_task_.has_value()) {
        return;
    }
    if (chrome_ == nullptr) {
        StartDeferredTask();
        return;
    }
    // The session is inside its own event handler: send from a posted task.
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::StartDeferredTask,
                                                            CefRefPtr<BrowserWindow>(this))));
}

void BrowserWindow::StartDeferredTask() {
    if (!deferred_task_.has_value() || closing_ || AgentTurnOpen()) {
        return;
    }
    const agent::AgentTask task = std::move(*deferred_task_);
    deferred_task_.reset();
    StartQueuedTask(task);
}

std::optional<agent::TaskKind> BrowserWindow::TaskKindOf(std::string_view id) const {
    for (const agent::AgentTask& task : agent_queue_.tasks()) {
        if (task.id == id) {
            return task.kind;
        }
    }
    return std::nullopt;
}

void BrowserWindow::FinishInFlightTurn(bool ok) {
    if (in_flight_id_.empty()) {
        return;
    }
    // Cleared first: the queue starts the next task from inside this call.
    const std::string id = std::move(in_flight_id_);
    in_flight_id_.clear();
    if (ok) {
        // Before the queue moves on: the next task's prompt would replace this turn's answer.
        FinishCollectTurn(id);
    }
    agent_queue_.OnTurnFinished(id, ok);
    ScheduleLocalPagesRender();
}

void BrowserWindow::CheckInFlightTurn() {
    if (deferred_task_.has_value()) {
        // An outside research owns the turn that just ended: it is classified first, and
        // CompleteResearch starts the deferred task afterwards.
        if (closing_ || AgentTurnOpen() || (agent_send_for_test_ && !agent_busy_for_test_) ||
            OutsideResearchOwnsTurn()) {
            return;
        }
        PostStartDeferredTask();
        return;
    }
    // Research reports its own end (CompleteResearch); the test seam never touches the session.
    if (in_flight_id_.empty() || turn_finish_posted_ ||
        (agent_send_for_test_ && !agent_busy_for_test_) || closing_ || agent_session_ == nullptr ||
        AgentTurnOpen() || !in_flight_is_chat_) {
        return;
    }
    if (chrome_ == nullptr) {
        FinishPostedTurn();
        return;
    }
    // The session is inside its own event handler here, and the next task sends from the queue:
    // finish from a posted task.
    turn_finish_posted_ = true;
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::FinishPostedTurn,
                                                            CefRefPtr<BrowserWindow>(this))));
}

void BrowserWindow::FinishPostedTurn() {
    turn_finish_posted_ = false;
    if (in_flight_id_.empty() || closing_ || agent_session_ == nullptr || AgentTurnOpen() ||
        !in_flight_is_chat_) {
        return;
    }
    // A dead agent reports no turn end; the failed state is the end.
    FinishInFlightTurn(agent_session_->state() != agent::AcpState::kFailed);
}

void BrowserWindow::ReportQueueProgress() {
    // A deferred task has not reached the agent: the progress on screen is the outside turn's.
    if (in_flight_id_.empty() || agent_session_ == nullptr || deferred_task_.has_value()) {
        return;
    }
    const std::vector<agent::TranscriptItem>& items = agent_session_->transcript().items();
    for (std::size_t index = items.size(); index > task_items_base_; --index) {
        const agent::TranscriptItem& item = items[index - 1];
        if (item.kind != agent::TranscriptItem::Kind::kPlan) {
            continue;
        }
        const std::size_t total = item.plan.size();
        const std::size_t done = static_cast<std::size_t>(
            std::count_if(item.plan.begin(), item.plan.end(),
                          [](const agent::AcpPlanEntry& e) { return e.status == "completed"; }));
        if (done != reported_done_ || total != reported_total_) {
            reported_done_ = done;
            reported_total_ = total;
            agent_queue_.OnProgress(in_flight_id_, done, total);
        }
        return;
    }
}

void BrowserWindow::SyncQueueWaiting() {
    // Waiting is a boolean for the queue: it ends only when the last card closes and the
    // research no longer waits for approval.
    const bool want =
        !pending_confirmations_.empty() || (research_.has_value() && research_->awaiting_approval);
    if (want == queue_waiting_) {
        return;
    }
    if (want) {
        if (in_flight_id_.empty() || deferred_task_.has_value()) {
            return;  // an external agent's card, or the outside turn's: no running task waits
        }
        waiting_id_ = in_flight_id_;
        queue_waiting_ = true;
        agent_queue_.OnWaiting(waiting_id_, true);
    } else {
        queue_waiting_ = false;
        const std::string id = std::move(waiting_id_);
        waiting_id_.clear();
        agent_queue_.OnWaiting(id, false);
    }
    ScheduleLocalPagesRender();
}

void BrowserWindow::StopAgentTask() {
    if (agent_queue_.current() == nullptr) {
        // Nothing of ours is live (an external agent's cards may be): just withdraw the cards.
        CancelPendingConfirmations();
        if (agent_session_ != nullptr) {
            agent_session_->Cancel();
        }
        return;
    }
    // A deferred task never reached the agent: the outside turn is not the person's target.
    const bool deferred = deferred_task_.has_value();
    deferred_task_.reset();
    if (!deferred && agent_session_ != nullptr) {
        agent_session_->Cancel();
    }
    const bool turn_open = !deferred && !in_flight_id_.empty() && AgentTurnOpen();
    EndLiveResearch(turn_open);
    // The queue stops first: withdrawing a card can lift "waiting", and a task that no longer
    // is live ignores it.
    queue_waiting_ = false;
    waiting_id_.clear();
    if (turn_open) {
        agent_queue_.StopCurrent();  // the next task starts at the stopped turn's end
    } else {
        in_flight_id_.clear();
        agent_queue_.StopCurrentAndAdvanceNow();
    }
    CancelPendingConfirmations();
    ScheduleLocalPagesRender();
}

void BrowserWindow::AbandonInFlightTurn(const std::function<void()>& reset_session) {
    // Cleared before the session is replaced: its events must not report the old turn.
    in_flight_id_.clear();
    deferred_task_.reset();
    queue_waiting_ = false;
    waiting_id_.clear();
    EndLiveResearch(/*turn_open=*/false);
    if (reset_session) {
        reset_session();
    }
    agent_queue_.StopCurrentAndAdvanceNow();
    ScheduleLocalPagesRender();
}

void BrowserWindow::EndLiveResearch(bool turn_open) {
    const agent::AgentTask* live = agent_queue_.current();
    if (!research_.has_value() || live == nullptr || research_->task_id != live->id) {
        return;
    }
    if (turn_open) {
        research_->stopped = true;
        return;
    }
    research_.reset();
    research_finish_posted_ = false;
    SetResearchStatus("cancelled", "Research cancelled.");
    RenderPlanPages();
}

void BrowserWindow::HandleTaskMessage(std::string_view type, const json::Value& message) {
    const std::string_view id = message.StringOr("id", "");
    if (type == "task_move") {
        if (message.StringOr("dir", "") == "up") {
            agent_queue_.MoveUp(id);
        } else if (message.StringOr("dir", "") == "down") {
            agent_queue_.MoveDown(id);
        }
    } else if (type == "task_remove") {
        agent_queue_.Remove(id);
    } else if (type == "tasks_clear_finished") {
        agent_queue_.ClearFinished();
    }
    ScheduleLocalPagesRender();
}

json::Value BrowserWindow::TasksStateJson() const {
    json::Value list = json::Value::MakeArray();
    std::size_t queued_position = 0;
    for (const agent::AgentTask& task : agent_queue_.tasks()) {
        const char* state = "queued";
        switch (task.state) {
            case agent::TaskState::kQueued:
                state = "queued";
                break;
            case agent::TaskState::kRunning:
                state = "running";
                break;
            case agent::TaskState::kWaitingForYou:
                state = "waiting";
                break;
            case agent::TaskState::kDone:
                state = "done";
                break;
            case agent::TaskState::kFailed:
                state = "failed";
                break;
            case agent::TaskState::kCancelled:
                state = "cancelled";
                break;
        }
        const char* kind = "chat";
        switch (task.kind) {
            case agent::TaskKind::kChat:
                kind = "chat";
                break;
            case agent::TaskKind::kResearch:
                kind = "research";
                break;
            case agent::TaskKind::kCollect:
                kind = "collect";
                break;
            case agent::TaskKind::kAsk:
                kind = "ask";
                break;
        }
        json::Value entry =
            json::Value::MakeObject()
                .Set("id", json::Value::String(task.id))
                .Set("kind", json::Value::String(kind))
                .Set("title", json::Value::String(task.title))
                .Set("state", json::Value::String(state))
                .Set("done", json::Value::Int(static_cast<std::int64_t>(task.done)))
                .Set("total", json::Value::Int(static_cast<std::int64_t>(task.total)));
        if (task.state == agent::TaskState::kQueued) {
            ++queued_position;
            entry.Set("position", json::Value::Int(static_cast<std::int64_t>(queued_position)));
            if (task.kind == agent::TaskKind::kChat || task.kind == agent::TaskKind::kAsk) {
                entry.Set("prompt", json::Value::String(task.prompt));
            }
        }
        list.Push(std::move(entry));
    }
    return list;
}

void BrowserWindow::RefreshAgentButtonLabel() {
    if (chrome_ != nullptr && !closing_) {
        chrome_->SetAgentButtonLabel(AgentButtonLabel(agent_queue_));
        RefreshAgentEntry();
    }
}

void BrowserWindow::ShowQueueFixtureForDev() {
    queue_fixture_ = true;
    EnsureAgentSession();
    for (const char* prompt : {"Summarize the open tabs about the espresso machine",
                               "Find the cheapest flight from Berlin to Lisbon in May",
                               "Draft a reply to the landlord about the lease"}) {
        agent_queue_.Enqueue(agent::TaskKind::kChat, prompt, {},
                             static_cast<std::int64_t>(UnixNowMs()));
    }
    agent_queue_.OnProgress("t1", 3, 7);
    ScheduleLocalPagesRender();
}

void BrowserWindow::ShowCollectFixtureForDev() {
    EnsureAgentSession();
    SetAgentPanelOpen(true);
    ++collect_sheet_seq_;
    ScheduleLocalPagesRender();
}

void BrowserWindow::ShowThreadFixtureForDev() {
    // queue_fixture_ keeps the tasks live without sending them: the first is running, the second
    // waits in the queue and shows as a queued message under the conversation.
    queue_fixture_ = true;
    EnsureAgentSession();
    SetAgentPanelOpen(true);
    const auto now = static_cast<std::int64_t>(UnixNowMs());
    // The transcript goes first: each session change re-checks the in-flight turn, and a turn that
    // was enqueued before its transcript looks finished.
    agent_session_->AddDevUserPrompt("Compare the espresso grinders on my list");
    using agent::AcpEvent;
    const auto tool = [](std::string id, std::string title, std::string status) {
        AcpEvent event;
        event.type = AcpEvent::Type::kToolCall;
        event.tool_call_id = std::move(id);
        event.title = std::move(title);
        event.kind = "read";
        event.status = std::move(status);
        return event;
    };
    agent_session_->ApplyDevEvent(
        tool("t1", "mcp__island-browser__page_read_text example.org", "completed"));
    agent_session_->ApplyDevEvent(
        tool("t2", "mcp__island-browser__page_snapshot shop.example.com", "completed"));
    agent_session_->ApplyDevEvent(
        tool("t3", "mcp__island-browser__page_click Place order", "failed"));
    AcpEvent reply;
    reply.type = AcpEvent::Type::kAgentMessageChunk;
    reply.text =
        "**Short answer:** the Hario Mini fits a small kitchen best.\n\n"
        "What to weigh:\n\n"
        "- Footprint: under 20 cm tall\n"
        "- Grind: set `medium-fine` for espresso\n\n"
        "The shop page did not accept the order, so nothing was bought.";
    agent_session_->ApplyDevEvent(reply);
    agent_queue_.Enqueue(agent::TaskKind::kChat, "Compare the espresso grinders on my list", {},
                         now);
    agent_queue_.Enqueue(agent::TaskKind::kChat, "Check the price on the second grinder", {}, now);
    ScheduleLocalPagesRender();
}

void BrowserWindow::ShowQuoteFixtureForDev() {
    ShowAgentContextChip(
        {.title = "Example Domain",
         .url = "https://example.com/",
         .selection = "This domain is for use in illustrative examples in documents. You may use "
                      "this domain in literature without prior coordination or asking for "
                      "permission."});
}

std::string BrowserWindow::StartPageStateJson() const {
    using json::Value;
    Value tiles = Value::MakeArray();
    for (const StartTile& tile : start_tiles_) {
        tiles.Push(Value::MakeObject()
                       .Set("title", Value::String(tile.title))
                       .Set("url", Value::String(tile.url))
                       .Set("host", Value::String(tile.host)));
    }
    return json::Serialize(
        Value::MakeObject()
            .Set("theme", ThemeJson())
            .Set("start",
                 Value::MakeObject()
                     .Set("mode", Value::String(std::string(start_mode())))
                     .Set("engine", Value::String(std::string(search_engine().display_name)))
                     .Set("tiles", std::move(tiles))
                     .Set("research",
                          Value::MakeObject()
                              .Set("seq",
                                   Value::Int(static_cast<std::int64_t>(research_status_.seq)))
                              .Set("state", Value::String(research_status_.state))
                              .Set("message", Value::String(research_status_.message))
                              .Set("question", Value::String(research_status_.question))
                              .Set("report_id", Value::String(research_status_.report_id))
                              .Set("report_title", Value::String(research_status_.report_title)))));
}

void BrowserWindow::RenderStartPages() {
    if (closing_) {
        return;
    }
    std::string script;
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            const CefRefPtr<CefBrowser> browser = tab.browser();
            const CefRefPtr<CefFrame> frame =
                browser != nullptr ? browser->GetMainFrame() : nullptr;
            if (frame == nullptr) {
                continue;
            }
            if (const std::optional<std::string> report_id =
                    ReportIdForUrl(frame->GetURL().ToString())) {
                RenderReportFrame(frame, *report_id);
                continue;
            }
            // The start page and the reader document both take the theme from this state.
            const bool shows_start = frame->GetURL().ToString() == StartPageUrl();
            if (!shows_start && !ReaderActiveFor(tab)) {
                continue;
            }
            if (script.empty()) {
                script = LocalPage::RenderScript(StartPageStateJson());
            }
            frame->ExecuteJavaScript(script, shows_start ? StartPageUrl() : std::string(), 0);
        }
    }
}

bool BrowserWindow::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t,
                                     const CefString& message, const CefString&, int) {
    CEF_REQUIRE_UI_THREAD();
    // Tabs show arbitrary web pages, so this bridge is closed to everything but
    // Island's own start and report pages: the message must carry the bridge
    // prefix, come from a tab, and the tab's main frame must be exactly the
    // start page document (or a report page document with a stored report id). Anything else logs
    // as ordinary console output. (An ask_agent message from a site would otherwise be a
    // prompt-injection channel.)
    const std::string text = message.ToString();
    if (!closing_ && text.rfind(kSleepProbePrefix, 0) == 0) {
        // The pre-sleep probe's reply: accepted only with the right token from the probed
        // browser, and never routed to the start-page bridge below.
        return HandleSleepProbeMessage(browser, text);
    }
    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || text.rfind(LocalPage::kMessagePrefix, 0) != 0 || owner == nullptr) {
        return false;
    }
    const CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (frame == nullptr) {
        return false;
    }
    const std::string frame_url = frame->GetURL().ToString();
    const bool is_start_page = frame_url == StartPageUrl();
    // The Plan tab is a page of its own: its messages edit the research plan.
    const bool is_plan_page = !is_start_page && IsPlanPageUrl(frame_url);
    // The reader document is the other page this bridge serves: the tab must be showing exactly
    // the data: URL Island built for it (and the message must carry that document's token).
    const auto reader = reader_sessions_.find(owner->id());
    const bool is_reader_page = reader != reader_sessions_.end() &&
                                frame_url.size() == reader->second.url_size &&
                                std::hash<std::string_view>{}(frame_url) == reader->second.url_hash;
    // A report page speaks for the one report its URL names.
    const std::optional<std::string> report_id =
        is_start_page || is_reader_page ? std::nullopt : ReportIdForUrl(frame_url);
    if (!is_start_page && !is_reader_page && !is_plan_page && !report_id) {
        return false;
    }
    const std::optional<json::Value> decoded = LocalPage::DecodeMessage(text);
    if (!decoded) {
        return false;
    }
    if (is_reader_page) {
        HandleReaderMessage(*owner, *decoded);
    } else if (report_id) {
        HandleReportPageMessage(*decoded, *report_id, browser);
    } else if (is_plan_page) {
        HandlePlanPageMessage(*decoded, browser);
    } else {
        HandleStartPageMessage(*decoded, browser);
    }
    return true;
}

void BrowserWindow::HandleStartPageMessage(const json::Value& message,
                                           CefRefPtr<CefBrowser> source) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // The last-used mode is remembered when the user switches and, as a
    // backstop, whenever they submit in a mode.
    const auto remember_mode = [this](std::string_view mode) {
        const std::string normalized(NormalizeStartMode(mode));
        if (prefs_.start_mode != normalized) {
            prefs_.start_mode = normalized;
            SavePrefs();
        }
    };
    const std::string_view type = message.StringOr("type", "");
    if (type == "set_start_mode") {
        remember_mode(message.StringOr("mode", ""));
    } else if (type == "search") {
        const std::string_view query = message.StringOr("query", "");
        if (!IsSubmittableQuery(query)) {
            return;
        }
        remember_mode(kStartModeSearch);
        // The tab that hosts the page navigates; tests have no browser and
        // fall back to the active tab.
        const CefRefPtr<CefBrowser> target =
            source != nullptr ? source : BrowserForTab(std::nullopt);
        const CefRefPtr<CefFrame> frame = target != nullptr ? target->GetMainFrame() : nullptr;
        if (frame != nullptr) {
            frame->LoadURL(BuildSearchUrl(search_engine(), query));
        }
    } else if (type == "ask_agent") {
        const std::string_view prompt = message.StringOr("prompt", "");
        if (!IsSubmittableQuery(prompt)) {
            return;
        }
        remember_mode(kStartModeAgent);
        // The same path as typing in the panel: open it, then send the turn.
        SetAgentPanelOpen(true);
        const bool sent = SubmitAgentPrompt(std::string(prompt), /*include_context=*/true);
        // Tell the page whether the agent took the task, so a busy agent never
        // silently swallows what the user typed.
        const CefRefPtr<CefFrame> frame = source != nullptr ? source->GetMainFrame() : nullptr;
        if (frame != nullptr) {
            frame->ExecuteJavaScript(
                std::string("window.islandAgentResult&&window.islandAgentResult(") +
                    (sent ? "true" : "false") + ")",
                StartPageUrl(), 0);
        }
    } else if (type == "research") {
        const std::string_view question = message.StringOr("question", "");
        if (!IsSubmittableQuery(question)) {
            return;
        }
        remember_mode(kStartModeResearch);
        const ResearchStartResult result = StartResearch(question);
        // A refusal that leaves the running state alone (busy) is a one-line note;
        // everything else is already in the research status the pages render.
        const CefRefPtr<CefFrame> frame = source != nullptr ? source->GetMainFrame() : nullptr;
        if (result.code == ResearchStart::kBusy && frame != nullptr) {
            frame->ExecuteJavaScript("window.islandNote&&window.islandNote(" +
                                         json::Serialize(json::Value::String(result.message)) + ")",
                                     StartPageUrl(), 0);
        }
    } else if (type == "cancel_research") {
        CancelResearch();
    } else if (type == "ready") {
        RefreshStartTiles();
    } else if (type == "open_tile") {
        // Only http and https pages open from a tile: the page's message is not trusted to name
        // any other scheme.
        const std::string_view url = message.StringOr("url", "");
        if (!IsWebTileUrl(url)) {
            return;
        }
        const CefRefPtr<CefBrowser> target =
            source != nullptr ? source : BrowserForTab(std::nullopt);
        const CefRefPtr<CefFrame> frame = target != nullptr ? target->GetMainFrame() : nullptr;
        if (frame != nullptr) {
            frame->LoadURL(std::string(url));
        }
    } else if (type == "open_settings") {
        ShowSettingsSection("agent");
    } else if (type == "open_report") {
        if (!research_status_.report_id.empty()) {
            static_cast<void>(OpenReportTab(research_status_.report_id));
        }
    }
}

void BrowserWindow::CloseAgentPanelAndSession() {
    internal_page_.reset();
    for (std::unique_ptr<LocalPage>* page :
         {&agent_panel_, &settings_page_, &overview_page_, &bookmarks_page_}) {
        if (*page != nullptr) {
            (*page)->Close();
            page->reset();
        }
    }
    research_.reset();
    CancelPendingConfirmations();
    agent_tab_id_.reset();
    agent_session_.reset();
    // Teardown: the session is gone, so its turn end never comes. closing_ fails the rest.
    AbandonInFlightTurn();
}

void BrowserWindow::SetTabPinned(std::size_t index, bool pinned) {
    static_cast<void>(SetActiveSpaceTabPinned(index, pinned));
}

void BrowserWindow::ActivateFavoriteFromSidebar(FavoriteScope scope, std::uint64_t bookmark_id) {
    CEF_REQUIRE_UI_THREAD();
    static_cast<void>(ActivateFavorite(scope, bookmark_id));
}

void BrowserWindow::OpenMoreGlobalFavorites() {
    CEF_REQUIRE_UI_THREAD();
    // The Bookmarks page is a later task; until then the "+N" control only logs.
    LOG(INFO) << "Show more global favorites: the Bookmarks page is not available yet";
}

void BrowserWindow::AddCurrentPageToGlobalFavorites() {
    CEF_REQUIRE_UI_THREAD();
    // The page on screen, the same one the palette's commands act on.
    static_cast<void>(RunBookmarkMutation([this]() -> BookmarkResult {
        const std::optional<BookmarkablePage> page = CurrentBookmarkablePage();
        if (!page.has_value()) {
            return BookmarkResult{.error = BookmarkError::kInvalidInput,
                                  .message = "Only web pages can be favorites."};
        }
        return bookmarks_.AddFavoriteByUrl(std::nullopt, page->title, page->url);
    }));
}

void BrowserWindow::RetryFailedBookmarkMutation() {
    CEF_REQUIRE_UI_THREAD();
    static_cast<void>(RetryLastBookmarkMutation());
}

void BrowserWindow::DismissFailedBookmarkStatus() {
    CEF_REQUIRE_UI_THREAD();
    DismissBookmarkStatus();
}

ChromeTokens BrowserWindow::ResolvedTokens(ChromeTheme theme) const {
    ChromeTokens tokens = ChromeTokens::ForLook(prefs_.appearance.look, theme)
                              .Customized(active_space().color(), theme, prefs_.appearance);
    if (FramelessWindowSupported()) {
        tokens.titlebar_band_dip = BrowserChrome::TrafficLightBandDip();
    }
    return tokens;
}

void BrowserWindow::ShutdownAgentHost() {
    // Every open card is answered "cancel" first, so a tool call waiting on one finishes while
    // the endpoint can still reply.
    CancelPendingConfirmations();
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
        ReturnToOrdinaryTab();
        return true;
    }
    DetachActiveTabObservers();
    space.SelectTabIndex(index);
    BreakSplitIfSelectionLeftPair(space, space.active_tab_id());
    AttachActiveTabObservers();
    return true;
}

void BrowserWindow::SelectTabWithOneAttach(std::size_t space, std::size_t tab) {
    Space& target = spaces_[space];
    if (space == active_space_index_ && target.has_active_tab() &&
        target.active_tab_index() == tab) {
        ReturnToOrdinaryTab();
        return;
    }
    // A chip describes the tab it was made from; another tab's question starts clean.
    context_chip_.reset();
    DetachActiveTabObservers();
    active_space_index_ = space;
    target.SelectTabIndex(tab);
    BreakSplitIfSelectionLeftPair(target, target.active_tab_id());
    AttachActiveTabObservers();
}

bool BrowserWindow::SelectSpaceIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || index >= spaces_.size()) {
        return false;
    }
    if (index == active_space_index_) {
        // Choosing the space already shown brings back its ordinary tab, as choosing a tab does.
        ReturnToOrdinaryTab();
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

    const TabId closed_id = space.tabs()[index].id();
    if (!ConfirmPlanTabClose(closed_id)) {
        return false;
    }

    RememberClosedTab(space.tabs()[index]);
    if (space.tab_count() == 1) {
        // Encoded fallback: closing the last tab of a space closes that space,
        // which may in turn recreate a fresh default space when it was the
        // last remaining one. The model is mutated before any CEF teardown.
        ForgetPlanTabMapping(closed_id);
        Space removed = RemoveSpaceAtIndex(active_space_index_);
        AttachActiveTabObservers();
        CloseSpaceBrowsers(removed);
        return true;
    }

    DetachActiveTabObservers();
    const std::optional<Tab> removed = active_space().RemoveTab(closed_id);
    ForgetPlanTabMapping(closed_id);
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

bool BrowserWindow::SetSpaceIcon(std::size_t space_index, std::string icon_id) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_index >= spaces_.size()) {
        return false;
    }
    spaces_[space_index].SetIconId(std::move(icon_id));
    UpdateChromeCollections();
    SaveSession();
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

    // The address field only exists in the full sidebar; from the icon rail the sidebar
    // opens for the length of the edit.
    BeginTransientSidebarExpand();
    address_bar_model_.Focus();
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    chrome_snapshot_.focus_target = FocusTarget::kAddress;
    PublishChromeSnapshot();
}

void BrowserWindow::ApplyAddressFocusColors(CefRefPtr<CefWindow> window,
                                            const ChromeTokens& tokens) const {
    window->SetThemeColor(
        CEF_ColorTextfieldOutline,
        BrowserChrome::TextfieldOutlineColor(tokens, address_focus_ring_suppressed_).argb);
    // CEF installs its own two-DIP FocusRing inside every textfield, drawn in
    // CEF_ColorFocusableBorderFocused and clamped to a minimum contrast against
    // CEF_ColorWindowBackground (a ring darker than that clamp is lifted to a grey 94,94,94). While
    // the address field is focused (every look), paint that ring as the plate fill so Island's
    // outer accent ring is the sole focus indicator. Restore the accent as soon as focus leaves the
    // address field so palette textfields keep their normal focus treatment.
    window->SetThemeColor(
        CEF_ColorWindowBackground,
        BrowserChrome::TextfieldFocusRingBackdrop(tokens, address_focus_ring_suppressed_).argb);
    window->SetThemeColor(
        CEF_ColorFocusableBorderFocused,
        BrowserChrome::TextfieldFocusRingColor(tokens, address_focus_ring_suppressed_).argb);
    if (OverlayFocusRingShown()) {
        // A shown overlay (welcome or palette) owns the one neutral ring: no accent anywhere.
        window->SetThemeColor(
            CEF_ColorWindowBackground,
            BrowserChrome::TextfieldFocusRingBackdrop(tokens, /*address_focused=*/true).argb);
        window->SetThemeColor(CEF_ColorFocusableBorderFocused, tokens.border_strong.argb);
    }
}

void BrowserWindow::SetAddressFocusRingSuppressed(bool suppressed) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || address_focus_ring_suppressed_ == suppressed) {
        return;
    }
    address_focus_ring_suppressed_ = suppressed;
    // All looks paint the same single address ring (the helpers in BrowserChrome), so every look
    // takes the color-only repaint below.
    const ChromeTokens tokens = ResolvedTokens(ResolvedChromeTheme());
    // A full ApplyTheme here re-themed every view, page and start page on each focus change.
    // Only the three colors change; ThemeChanged then repaints the views that read them.
    address_focus_repaint_ = AddressFocusRepaint::kColorsOnly;
    if (window_ != nullptr) {
        ApplyAddressFocusColors(window_, tokens);
        window_->ThemeChanged();
    }
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
    ScheduleTransientSidebarCollapse();
}

void BrowserWindow::SubmitAddressDraft(std::string_view draft) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }

    address_bar_model_.SetEditText(std::string(draft));
    const std::optional<std::string> url = address_bar_model_.Submit(ResolveAddressInput(draft));
    if (chrome_ != nullptr) {
        chrome_->OnAddressChanged(address_bar_model_.snapshot());
    }
    Tab* tab = DisplayedTab();
    if (!url.has_value() || tab == nullptr || tab->browser() == nullptr) {
        // An invalid address keeps the field (and its message) on screen.
        return;
    }

    CefRefPtr<CefFrame> main_frame = tab->browser()->GetMainFrame();
    if (main_frame != nullptr) {
        main_frame->LoadURL(*url);
    }
    EndTransientSidebarExpand();
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

    UpdateAddressBarForNavigation(snapshot);
    UpdateReaderButton();
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

void BrowserWindow::UpdateAddressBarForNavigation(const NavigationSnapshot& snapshot) {
    if (internal_page_.has_value()) {
        address_bar_model_.UpdateInternalPage(std::string(LocalPage::DisplayUrl(*internal_page_)));
        return;
    }

    const Tab* shown_tab = DisplayedTab();
    const std::string url = shown_tab != nullptr && ReaderActiveFor(*shown_tab)
                                ? EffectiveUrl(*shown_tab)
                                : snapshot.url;
    if (!start_page_url_.empty() && url == start_page_url_) {
        address_bar_model_.UpdateInternalPage(
            std::string(LocalPage::DisplayUrl(LocalPageKind::kStart)));
    } else if (!plan_page_url_.empty() && url == plan_page_url_) {
        address_bar_model_.UpdateInternalPage(
            std::string(LocalPage::DisplayUrl(LocalPageKind::kPlan)));
    } else if (!report_page_url_.empty() && url.size() > report_page_url_.size() + 1 &&
               url.compare(0, report_page_url_.size(), report_page_url_) == 0 &&
               url[report_page_url_.size()] == '#') {
        address_bar_model_.UpdateInternalPage(
            std::string(LocalPage::DisplayUrl(LocalPageKind::kReport)) +
            url.substr(report_page_url_.size()));
    } else {
        address_bar_model_.UpdateCommittedUrl(url);
    }
}

void BrowserWindow::UpdateAddressBarForCurrentPage() {
    UpdateAddressBarForNavigation(navigation_snapshot());
}

CefRefPtr<CefDisplayHandler> BrowserWindow::GetDisplayHandler() { return this; }

CefRefPtr<CefDownloadHandler> BrowserWindow::GetDownloadHandler() { return this; }

CefRefPtr<CefLifeSpanHandler> BrowserWindow::GetLifeSpanHandler() { return this; }

CefRefPtr<CefLoadHandler> BrowserWindow::GetLoadHandler() { return this; }

CefRefPtr<CefRequestHandler> BrowserWindow::GetRequestHandler() { return this; }

CefRefPtr<CefResourceRequestHandler> BrowserWindow::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool, bool,
    const CefString&, bool&) {
    if (browser == nullptr || request == nullptr || content_blocker_ == nullptr ||
        ShouldBypassContentBlocker(request->GetURL().ToString(), request->GetResourceType())) {
        return nullptr;
    }
    return this;
}

CefResourceRequestHandler::ReturnValue BrowserWindow::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>) {
    if (browser == nullptr || request == nullptr || content_blocker_ == nullptr) {
        return RV_CONTINUE;
    }
    const std::string url = request->GetURL().ToString();
    const cef_resource_type_t resource_type = request->GetResourceType();
    if (ShouldBypassContentBlocker(url, resource_type)) {
        return RV_CONTINUE;
    }
    const CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    const std::string source_url = main_frame != nullptr ? main_frame->GetURL().ToString() : "";
    if (source_url.empty()) {
        return RV_CONTINUE;
    }
    const ContentBlockerResult result =
        content_blocker_->Check(url, source_url, ContentBlockerRequestType(resource_type));
    if (!result.blocked || result.exception) {
        return RV_CONTINUE;
    }
    const int identifier = browser->GetIdentifier();
    CefPostTask(TID_UI,
                CefCreateClosureTask(base::BindOnce(&BrowserWindow::RecordBlockedRequest,
                                                    CefRefPtr<BrowserWindow>(this), identifier)));
    return RV_CANCEL;
}

void BrowserWindow::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain()) {
        return;
    }
    // The icon belongs to the page that declared it: leaving the origin (or the web
    // altogether) restores the globe until the new page declares its own.
    const std::string new_url = url.ToString();
    if (ShouldClearFaviconOnNavigation(owner->navigation_state().snapshot().url, new_url) &&
        (owner->favicon() != nullptr || !owner->pending_favicon_url().empty())) {
        owner->ClearFavicon();
        UpdateChromeCollections();
    }
    owner->navigation_state().SetAddress(new_url);
}

void BrowserWindow::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr) {
        return;
    }

    const std::string page_title = title.ToString();
    owner->navigation_state().OnTitleChange(page_title);
    if (history_ != nullptr && prefs_.remember_history) {
        static_cast<void>(
            history_->UpdateTitle(owner->navigation_state().snapshot().url, page_title));
    }
    UpdateWindowTitle();
}

namespace {

class FaviconDownload final : public CefDownloadImageCallback {
  public:
    FaviconDownload(CefRefPtr<BrowserWindow> window, TabId tab)
        : window_(std::move(window)), tab_(tab) {}

    void OnDownloadImageFinished(const CefString& image_url, int,
                                 CefRefPtr<CefImage> image) override {
        window_->OnFaviconDownloaded(tab_, image_url.ToString(), image);
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
    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || icon_urls.empty()) {
        return;
    }
    // Local pages (data:, file:, about:) never fetch; the page's first declared icon wins.
    const std::string icon_url = icon_urls.front().ToString();
    if (!ShouldFetchFavicon(owner->navigation_state().snapshot().url, icon_url)) {
        return;
    }
    if (owner->pending_favicon_url() == icon_url) {
        return;
    }
    owner->BeginFaviconRequest(icon_url);
    // Never blocks: the callback runs later on the UI thread and failures are ignored.
    browser->GetHost()->DownloadImage(icon_url, /*is_favicon=*/true,
                                      /*max_image_size=*/kFaviconDownloadDip,
                                      /*bypass_cache=*/false,
                                      new FaviconDownload(this, owner->id()));
}

void BrowserWindow::OnFaviconDownloaded(TabId tab_id, const std::string& icon_url,
                                        CefRefPtr<CefImage> image) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // Virtual favorite tabs are not in any space, so the lookup goes through FindTabById.
    Tab* tab = FindTabById(tab_id);
    if (tab == nullptr) {
        return;
    }
    if (tab->CompleteFaviconRequest(icon_url, std::move(image)) &&
        FaviconAffectsCollections(tab_id)) {
        UpdateChromeCollections();
    }
}

bool BrowserWindow::FaviconAffectsCollections(TabId id) const noexcept {
    for (const auto& [key, tab] : virtual_tabs_) {
        if (tab.id() == id) {
            return !key.scope.has_value() || *key.scope == active_space().id().value;
        }
    }
    return active_space().FindTab(id) != nullptr;
}

void BrowserWindow::CompleteFaviconDownloadForTest(TabId tab, const std::string& icon_url,
                                                   CefRefPtr<CefImage> image) {
    if (Tab* owner = FindTabById(tab); owner != nullptr) {
        owner->BeginFaviconRequest(icon_url);
    }
    OnFaviconDownloaded(tab, icon_url, std::move(image));
}

bool BrowserWindow::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
                                  const CefString& target_url, const CefString&,
                                  cef_window_open_disposition_t target_disposition,
                                  bool user_gesture, const CefPopupFeatures&, CefWindowInfo&,
                                  CefRefPtr<CefClient>&, CefBrowserSettings&,
                                  CefRefPtr<CefDictionaryValue>&, bool*) {
    CEF_REQUIRE_UI_THREAD();
    // Popups and new windows stay rejected (Island is single-window). A user-initiated new-tab
    // open (Cmd/Ctrl+click, middle-click, target=_blank) becomes a peek instead.
    if ((target_disposition == CEF_WOD_NEW_FOREGROUND_TAB ||
         target_disposition == CEF_WOD_NEW_BACKGROUND_TAB) &&
        user_gesture && IsPeekLinkSource(browser)) {
        DeferPeek(target_url.ToString());
    }
    return true;
}

CefRefPtr<CefContextMenuHandler> BrowserWindow::GetContextMenuHandler() { return this; }

bool BrowserWindow::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
                                     const CefString& target_url,
                                     cef_window_open_disposition_t target_disposition, bool) {
    CEF_REQUIRE_UI_THREAD();
    if ((target_disposition == CEF_WOD_NEW_FOREGROUND_TAB ||
         target_disposition == CEF_WOD_NEW_BACKGROUND_TAB) &&
        IsPeekLinkSource(browser)) {
        DeferPeek(target_url.ToString());
        return true;
    }
    return false;
}

void BrowserWindow::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
                                        CefRefPtr<CefContextMenuParams> params,
                                        CefRefPtr<CefMenuModel> model) {
    CEF_REQUIRE_UI_THREAD();
    if (params == nullptr || model == nullptr) {
        return;
    }
    const bool has_link = IsWebUrl(params->GetLinkUrl().ToString());
    if (has_link) {
        model->AddItem(MENU_ID_USER_FIRST, "Open in peek");
    }
    const Tab* tab = FindTabByBrowser(browser);
    const bool on_web_page = tab != nullptr && IsWebUrl(tab->navigation_state().snapshot().url);
    const bool has_selection =
        on_web_page && !TrimmedCopy(params->GetSelectionText().ToString()).empty();
    const std::vector<AgentMenuItem> items =
        AgentContextMenuItems(!ResolvedAgentCommand().empty(), has_selection, has_link);
    if (!items.empty()) {
        if (model->GetCount() > 0) {
            model->AddSeparator();
        }
        for (const AgentMenuItem& item : items) {
            model->AddItem(MENU_ID_USER_FIRST + item.offset, std::string(item.label));
        }
    }
}

bool BrowserWindow::OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
                                         CefRefPtr<CefContextMenuParams> params, int command_id,
                                         EventFlags) {
    CEF_REQUIRE_UI_THREAD();
    if (params == nullptr || !IsPeekLinkSource(browser)) {
        return false;
    }
    if (command_id == MENU_ID_USER_FIRST) {
        DeferPeek(params->GetLinkUrl().ToString());
        return true;
    }
    const Tab* tab = FindTabByBrowser(browser);
    if (tab == nullptr) {
        return false;
    }
    const NavigationSnapshot& nav = tab->navigation_state().snapshot();
    return RunAgentMenuCommand(command_id - MENU_ID_USER_FIRST,
                               {.title = nav.page_title,
                                .url = nav.url,
                                .selection = params->GetSelectionText().ToString()},
                               params->GetLinkUrl().ToString());
}

bool BrowserWindow::IsPeekLinkSource(CefRefPtr<CefBrowser> browser) const {
    return !closing_ && window_ != nullptr && browser != nullptr &&
           FindTabByBrowser(browser) != nullptr;
}

void BrowserWindow::DeferPeek(const std::string& url) {
    // The popup and menu callbacks run inside CEF's own dispatch: open the card on the next
    // UI task instead of building views there.
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                            [](CefRefPtr<BrowserWindow> window, std::string target) {
                                static_cast<void>(window->OpenPeek(target));
                            },
                            CefRefPtr<BrowserWindow>(this), url)));
}

PeekOverlay& BrowserWindow::EnsurePeek() {
    if (peek_ == nullptr) {
        peek_ =
            std::make_unique<PeekOverlay>(*this, window_, ResolvedTokens(ResolvedChromeTheme()));
    }
    return *peek_;
}

bool BrowserWindow::OpenPeek(std::string_view url) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr || chrome_ == nullptr) {
        return false;
    }
    const ValidatedAddress address = address_validator_(url);
    if (!address.is_valid() || !IsWebUrl(address.url)) {
        return false;
    }
    PeekOverlay& peek = EnsurePeek();
    peek.Open(address.url, active_space().request_context(), chrome_snapshot_.content_bounds);
    return peek.open();
}

void BrowserWindow::ClosePeek() {
    CEF_REQUIRE_UI_THREAD();
    if (peek_ != nullptr) {
        peek_->Close();
    }
}

void BrowserWindow::OnPeekOpenAsTab(std::string url) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // The card's page is reopened as a real tab of the active space; the card's browser (and its
    // history and scroll state) is not transferred.
    AppendTabToActiveSpace(std::move(url));
}

void BrowserWindow::OnPeekClosed() {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_) {
        FocusBrowserView();
    }
}

void BrowserWindow::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    browser_was_created_ = true;
    SchedulePageCardRefresh();
    Tab* owner = FindTabByBrowser(browser);
    if (owner == nullptr) {
        owner = FindTabByBrowserView(CefBrowserView::GetForBrowser(browser));
    }
    if (owner != nullptr) {
        owner->SetBrowser(browser);
        owner->MarkBrowserCreated(ViewSwapNow());
        SchedulePendingViewSwap();
    }
}

bool BrowserWindow::CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) {
    CEF_REQUIRE_UI_THREAD();
    return true;
}

// Alloy style cancels a download that nobody handles, so Island saves it
// itself: straight into the Downloads folder, no dialog, never overwriting an
// existing file (CEF does not de-duplicate names). With no known home
// directory CEF's own default (suggested name in its temp directory) applies.
bool BrowserWindow::OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback> callback) {
    CEF_REQUIRE_UI_THREAD();
    const std::filesystem::path directory = DefaultDownloadsDirectory();
    if (directory.empty()) {
        callback->Continue(CefString(), false);
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path target = ResolveDownloadPath(directory, suggested_name.ToString());
    // native() is UTF-8 on POSIX and UTF-16 on Windows; CefString takes both.
    callback->Continue(CefString(target.native()), false);
    return true;
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

    if (browser != nullptr && sleep_closing_browser_ids_.erase(browser->GetIdentifier()) > 0) {
        // A tab put to sleep: its model entry already dropped the browser and the window stays
        // up, even if this close lands after shutdown began.
        return;
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

    // The blocked-request badge counts the current page, not the tab's history.
    owner->ResetBlockedRequests();
    if (active_tab() == owner) {
        RefreshAddressIndicators();
    }
    owner->navigation_state().OnLoadStart(frame->GetURL().ToString());
    if (find_tab_.has_value() && *find_tab_ == owner->id()) {
        // The old counts describe a document that is gone.
        find_model_.ClearResults();
        RefreshFindCount(false);
    }
}

void BrowserWindow::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                              int http_status_code) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (closing_ || owner == nullptr || !frame->IsMain()) {
        return;
    }

    owner->navigation_state().OnLoadEnd(http_status_code);
    if (history_ != nullptr && prefs_.remember_history) {
        const NavigationSnapshot& navigation = owner->navigation_state().snapshot();
        // Island has no private-mode model yet. Keep the explicit parameter so
        // a future private tab cannot accidentally inherit history capture.
        static_cast<void>(history_->RecordNavigation(frame->GetURL().ToString(),
                                                     navigation.page_title, UnixNowMs(),
                                                     /*private_mode=*/false));
    }
    RestoreSavedScroll(*owner, frame);
    if (frame->GetURL().ToString() == StartPageUrl()) {
        frame->ExecuteJavaScript(LocalPage::RenderScript(StartPageStateJson()), StartPageUrl(), 0);
    } else if (const std::optional<std::string> report_id =
                   ReportIdForUrl(frame->GetURL().ToString())) {
        RenderReportFrame(frame, *report_id);
    } else if (IsPlanPageUrl(frame->GetURL().ToString())) {
        RenderPlanFrame(frame);
    }
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

    icon_resource_root_ = icon_resource_root;
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
    if (tab != nullptr) {
        // BrowserChrome's constructor attached the startup view directly; register it with the
        // same gate bookkeeping used by later swaps.
        SetAttachedViews({AttachedView{.view = browser_view, .tab = tab->id()}});
    }
    chrome_->OnNavigationChanged(tab != nullptr ? tab->navigation_state().snapshot()
                                                : NavigationSnapshot{});
    chrome_->OnAddressChanged(address_bar_model_.snapshot());
    // Refreshes that ran before the chrome existed updated the cache without delivering it.
    DeliverAddressIndicators();
    UpdateChromeCollections();
    RefreshAgentEntry();
    window_->AddChildView(chrome_->root());
    chrome_->SetCornerPatchSink([this](const std::vector<CornerPatch>& patches) {
        corner_patches_ = patches;
        if (!closing_ && chrome_ != nullptr) {
            ApplyCornerPatches(NativeContentHandles(), corner_patches_,
                               ResolvedTokens(chrome_snapshot_.theme).canvas);
        }
    });
    window_->SetTitle(CefString("Island"));
    window_->CenterWindow(CefSize(kChromeWindowWidth, kChromeWindowHeight));
    ApplyTheme(window_, theme, true);
    window_->Show();
    window_->Activate();
    browser_view->RequestFocus();

    ApplyKeymap();

    ApplySidebarState();
    if (persist_session_) {
        ScheduleSleepTick();
    }
    // Fresh install: offer the import/appearance welcome once, without
    // blocking browsing (Escape dismisses). The smoke run skips it so the
    // smoke page stays the only surface.
    if (persist_session_ && !prefs_.onboarding_completed) {
        ShowWelcomeFlow();
    }
    // AI-agent tools over MCP on 127.0.0.1. The smoke run stays offline and
    // deterministic, so only persisted windows serve them.
    if (persist_session_) {
        if (content_blocker_ != nullptr) {
            content_blocker_->Start();
        }
        agent_host_ = std::make_unique<WindowAgentHost>(*this);
        static_cast<void>(agent_host_->StartEndpoint(agent::DefaultDiscoveryFilePath()));
        if (prefs_.agent_panel_open) {
            SetAgentPanelOpen(true);
        }
        // Updates: deferred so startup stays offline and fast; the smoke run
        // and ISLAND_DISABLE_UPDATES=1 never check.
        if (!update::UpdatesDisabledByEnvironment()) {
            CefPostDelayedTask(
                TID_UI,
                CefCreateClosureTask(base::BindOnce(&BrowserWindow::RunStartupUpdateCheck,
                                                    CefRefPtr<BrowserWindow>(this))),
                update::kStartupCheckDelayMs);
        }
    }
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow>) {
    CEF_REQUIRE_UI_THREAD();

    closing_ = true;
    ShutdownAgentHost();
    CloseAgentPanelAndSession();
    if (updater_ != nullptr) {
        updater_->Cancel();
    }
    if (content_blocker_ != nullptr) {
        content_blocker_->Stop();
    }
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
    window_height_dip_ = new_bounds.height;
    chrome_snapshot_.rail_bounds = {
        .x = 0,
        .y = 0,
        // The collapsed icon rail reports its 52 DIP so observers never see a snapshot
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
    ScheduleAnchoredOverlayBounds();
    if (welcome_ != nullptr && welcome_->visible()) {
        welcome_->UpdateBounds();
    }
    ApplySidebarState();
    if (peek_open()) {
        peek_->UpdateBounds(chrome_snapshot_.content_bounds);
    }
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

bool BrowserWindow::IsFrameless(CefRefPtr<CefWindow>) { return FramelessWindowSupported(); }

bool BrowserWindow::WithStandardWindowButtons(CefRefPtr<CefWindow>) {
    return FramelessWindowSupported();
}

bool BrowserWindow::GetTitlebarHeight(CefRefPtr<CefWindow>, float* titlebar_height) {
    if (!FramelessWindowSupported()) {
        return false;
    }
    // The traffic lights sit halfway down this height: centered on the sidebar's top strip.
    *titlebar_height = static_cast<float>(BrowserChrome::TrafficLightBandDip());
    return true;
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
        if (updater_ != nullptr) {
            updater_->Cancel();
        }
        // Clean quit: persist the session before any browser tears down, so
        // the last-committed URLs are still in the navigation snapshots. The
        // smoke run never touches the session file.
        if (persist_session_) {
            SaveSession();
        }
        if (history_ != nullptr) {
            history_->BeginShutdown();
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
        // Virtual favorite tabs are not in a space, so they close here too.
        for (const auto& entry : virtual_tabs_) {
            if (entry.second.browser() != nullptr) {
                entry.second.browser()->GetHost()->CloseBrowser(true);
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
        case kEscapeAccelerator:
            if (welcome_ != nullptr && welcome_->visible()) {
                // Escape on the welcome flow means "just start browsing".
                OnWelcomeCompleted({});
                return true;
            }
            // Escape stops a loading page only while no overlay owns the key;
            // otherwise it passes through to the palette, Settings, or field.
            if (CanStopLoadingWithEscape()) {
                ExecuteCommand(BrowserCommand::kStopLoading);
                return true;
            }
            return false;
        default:
            if (command_id >= kSelectTab1Accelerator && command_id <= kSelectTab9Accelerator) {
                if (command_id == kSelectTab9Accelerator) {
                    // Ctrl/Cmd+9 selects the last tab whatever the tab count.
                    ExecuteCommand(BrowserCommand::kSelectLastTab);
                    return true;
                }
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
                                binding->key_code, binding->shift,
                                binding->primary || binding->control, binding->alt, true);
    }
    // Fixed bindings: F5 reloads, Cmd/Ctrl+1..9 select tabs (9 is the last
    // tab), and Escape dismisses the welcome flow or stops loading.
    window_->SetAccelerator(kReloadAccelerator, kVirtualKeyF5, false, false, false, true);
    for (int position = 0; position < 9; ++position) {
        window_->SetAccelerator(kSelectTab1Accelerator + position, '1' + position, false, true,
                                false, true);
    }
    window_->SetAccelerator(kEscapeAccelerator, kVirtualKeyEscape, false, false, false, true);
    RefreshAgentEntry();
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
        case KeyAction::kReopenClosedTab:
            ExecuteCommand(BrowserCommand::kReopenClosedTab);
            return;
        case KeyAction::kZoomIn:
        case KeyAction::kZoomInAlternate:
            ExecuteCommand(BrowserCommand::kZoomIn);
            return;
        case KeyAction::kZoomOut:
            ExecuteCommand(BrowserCommand::kZoomOut);
            return;
        case KeyAction::kZoomReset:
            ExecuteCommand(BrowserCommand::kZoomReset);
            return;
        case KeyAction::kReloadIgnoringCache:
            ExecuteCommand(BrowserCommand::kReloadIgnoringCache);
            return;
        case KeyAction::kStopLoading:
            ExecuteCommand(BrowserCommand::kStopLoading);
            return;
        case KeyAction::kNextSpace:
            ExecuteCommand(BrowserCommand::kNextSpace);
            return;
        case KeyAction::kPreviousSpace:
            ExecuteCommand(BrowserCommand::kPreviousSpace);
            return;
        case KeyAction::kCopyTabUrl:
            ExecuteCommand(BrowserCommand::kCopyTabUrl);
            return;
        case KeyAction::kFindInPage:
            ExecuteCommand(BrowserCommand::kFindInPage);
            return;
        case KeyAction::kFindNext:
            ExecuteCommand(BrowserCommand::kFindNext);
            return;
        case KeyAction::kFindPrevious:
            ExecuteCommand(BrowserCommand::kFindPrevious);
            return;
        case KeyAction::kReaderMode:
            ExecuteCommand(BrowserCommand::kToggleReaderMode);
            return;
        case KeyAction::kShowCheatsheet:
            ExecuteCommand(BrowserCommand::kShowCheatsheet);
            return;
        case KeyAction::kNextTabAlternate:
            ExecuteCommand(BrowserCommand::kNextTab);
            return;
        case KeyAction::kPreviousTabAlternate:
            ExecuteCommand(BrowserCommand::kPreviousTab);
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
    Tab* owner = FindTabByBrowserView(browser_view);
    if (closing_ || owner == nullptr) {
        // The close paths walk the model's browsers, which this one is not part of: its tab was
        // closed or put to sleep while it was being created, or shutdown began. Left open it
        // would keep the process alive, so it closes here.
        if (browser != nullptr) {
            browser->GetHost()->CloseBrowser(true);
        }
        SchedulePendingViewSwap();
        return;
    }
    browser_was_created_ = true;
    owner->SetBrowser(browser);
    owner->MarkBrowserCreated(ViewSwapNow());
    SchedulePendingViewSwap();
}

void BrowserWindow::OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    Tab* owner = FindTabByBrowser(browser);
    if (owner != nullptr) {
        DetachChromeAndObservers();
        owner->SetBrowser(nullptr);
        owner->SetBrowserView(nullptr);
    }
    // A creation that failed leaves the attached tab without a view: the waiting swap proceeds.
    SchedulePendingViewSwap();
}

BrowserWindow::ChromeToolbarType BrowserWindow::GetChromeToolbarType(CefRefPtr<CefBrowserView>) {
    return CEF_CTT_NONE;
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() { return kIslandRuntimeStyle; }

cef_runtime_style_t BrowserWindow::GetWindowRuntimeStyle() { return kIslandRuntimeStyle; }

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
    for (auto& [key, tab] : virtual_tabs_) {
        if (tab.browser() != nullptr && tab.browser()->IsSame(browser)) {
            return &tab;
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
    for (const auto& [key, tab] : virtual_tabs_) {
        if (tab.browser() != nullptr && tab.browser()->IsSame(browser)) {
            return &tab;
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
    for (auto& [key, tab] : virtual_tabs_) {
        if (tab.browser_view() != nullptr && tab.browser_view()->IsSame(browser_view)) {
            return &tab;
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
    // Any selection change also ends a displayed favorite, so the slot returns to the selected
    // ordinary tab (the favorite keeps its virtual tab). Both candidate tabs stop reporting first.
    ClearShownTabObservers();
    displayed_favorite_.reset();
    // Leaving a tab starts its idle clock.
    TouchActiveTabs();
}

void BrowserWindow::ClearShownTabObservers() {
    if (Tab* shown = DisplayedTab(); shown != nullptr) {
        shown->navigation_state().SetObserver(nullptr);
    }
    if (Tab* ordinary = active_tab(); ordinary != nullptr) {
        ordinary->navigation_state().SetObserver(nullptr);
    }
}

Tab* BrowserWindow::DisplayedTab() noexcept {
    if (displayed_favorite_.has_value()) {
        if (Tab* virtual_tab = FindVirtualTab(*displayed_favorite_); virtual_tab != nullptr) {
            return virtual_tab;
        }
    }
    return active_tab();
}

const Tab* BrowserWindow::DisplayedTab() const noexcept {
    if (displayed_favorite_.has_value()) {
        const auto found = virtual_tabs_.find(*displayed_favorite_);
        if (found != virtual_tabs_.end()) {
            return &found->second;
        }
    }
    return active_tab();
}

std::optional<TabId> BrowserWindow::shown_tab_id() const noexcept {
    const Tab* shown = DisplayedTab();
    if (shown == nullptr) {
        return std::nullopt;
    }
    return shown->id();
}

void BrowserWindow::AttachActiveTabObservers() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    ++attach_count_;
    CancelStaleConfirmations();
    // A peek floats over one page; changing the selection dismisses it.
    ClosePeek();
    TouchActiveTabs();
    Tab* tab = DisplayedTab();
    if (find_tab_.has_value() && (tab == nullptr || tab->id() != *find_tab_)) {
        // The find bar belongs to the tab it searched; a different (or no) active tab ends it.
        CloseFindBar(/*refocus_page=*/false);
    }
    RefreshAddressIndicators();
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
    show_internal_page_ = keep_internal_page_ && internal_page_.has_value();
    RequestViewSwap();
}

void BrowserWindow::RequestViewSwap() {
    if (AttachedBrowserStillCreating()) {
        if (!view_swap_pending_ || view_swap_pending_generation_ != attached_view_generation_) {
            view_swap_pending_ = true;
            view_swap_pending_generation_ = attached_view_generation_;
            fallback_timer_generation_.reset();
            uncreated_fallback_generation_.reset();
            SchedulePendingViewSwap();
        }
        return;
    }
    view_swap_pending_ = false;
    view_swap_pending_generation_.reset();
    fallback_timer_generation_.reset();
    uncreated_fallback_generation_.reset();
    ++view_swap_count_;
    AttachActiveTabBrowserView();
    FocusAttachedContent();
}

void BrowserWindow::FocusAttachedContent() {
    if (show_internal_page_ && internal_page_.has_value()) {
        if (LocalPage* page = InternalPageFor(*internal_page_)) {
            page->Focus();
            return;
        }
    }
    FocusBrowserView();
}

BrowserWindow::ViewSwapClock::time_point BrowserWindow::ViewSwapNow() const {
    return view_swap_time_for_test_.value_or(ViewSwapClock::now());
}

Tab* BrowserWindow::TabForAttachedView(const AttachedView& attached) noexcept {
    if (!attached.tab.has_value()) {
        return nullptr;
    }
    Tab* tab = FindTabById(*attached.tab);
    if (tab == nullptr) {
        return nullptr;
    }
    if (attached.view == nullptr) {
        return attached_view_for_test_ ? tab : nullptr;
    }
    if (tab->browser_view() == nullptr || !tab->browser_view()->IsSame(attached.view)) {
        return nullptr;
    }
    return tab;
}

bool BrowserWindow::AttachedViewWillRemain(const AttachedView& attached) const {
    if (show_internal_page_ && internal_page_.has_value()) {
        if (attached.local_page_for_test.has_value()) {
            return attached.local_page_for_test == internal_page_;
        }
        return attached.local_page != nullptr &&
               attached.local_page == InternalPageFor(*internal_page_);
    }
    if (!attached.tab.has_value()) {
        return false;
    }
    const Space& space = active_space();
    if (space.split().has_value()) {
        // A pane that changes slot is removed and re-added by the attach, so it leaves.
        return *attached.tab ==
               (attached.second_slot ? space.split()->second : space.split()->first);
    }
    return *attached.tab == space.active_tab_id();
}

bool BrowserWindow::AttachedViewIsLive(const AttachedView& attached) {
    if (attached.local_page_for_test.has_value()) {
        return true;
    }
    if (attached.local_page != nullptr) {
        return attached.local_page == settings_page_.get() ||
               attached.local_page == overview_page_.get() ||
               attached.local_page == bookmarks_page_.get();
    }
    // A closed tab or a tab whose view was replaced no longer owns this attachment.
    return TabForAttachedView(attached) != nullptr;
}

bool BrowserWindow::AttachedViewBrowserWasCreated(const AttachedView& attached) {
    if (attached.local_page_for_test.has_value()) {
        return attached.browser_created_at_for_test.has_value();
    }
    if (attached.local_page != nullptr) {
        return attached.local_page->browser_was_created();
    }
    Tab* tab = attached.tab.has_value() ? FindTabById(*attached.tab) : nullptr;
    return tab != nullptr && tab->browser_was_created();
}

std::optional<BrowserWindow::ViewSwapClock::duration> BrowserWindow::AttachedViewAgeAt(
    const AttachedView& attached, ViewSwapClock::time_point now) {
    if (attached.local_page_for_test.has_value()) {
        if (!attached.browser_created_at_for_test.has_value() ||
            now < *attached.browser_created_at_for_test) {
            return std::nullopt;
        }
        return now - *attached.browser_created_at_for_test;
    }
    if (attached.local_page != nullptr) {
        return attached.local_page->BrowserViewAgeAt(now);
    }
    Tab* tab = attached.tab.has_value() ? FindTabById(*attached.tab) : nullptr;
    return tab != nullptr ? tab->BrowserViewAgeAt(now) : std::nullopt;
}

bool BrowserWindow::AttachedBrowserStillCreating() {
    if (chrome_ == nullptr && !attached_view_for_test_) {
        return false;
    }
    const bool allow_uncreated = uncreated_fallback_generation_.has_value() &&
                                 *uncreated_fallback_generation_ == attached_view_generation_;
    const ViewSwapClock::time_point now = ViewSwapNow();
    for (const AttachedView& attached : attached_views_) {
        // Only a view that leaves the hierarchy needs a settled browser (a remaining one is not
        // removed, so a split that drops one pane waits for that pane alone).
        if (!AttachedViewIsLive(attached) || AttachedViewWillRemain(attached)) {
            continue;
        }
        if (!AttachedViewBrowserWasCreated(attached)) {
            if (!allow_uncreated) {
                return true;
            }
            continue;
        }
        const std::optional<ViewSwapClock::duration> age = AttachedViewAgeAt(attached, now);
        if (!age.has_value() || *age < kMinimumAttachedViewAge) {
            return true;
        }
    }
    return false;
}

void BrowserWindow::SetAttachedViews(std::vector<AttachedView> attached) {
    attached_views_ = std::move(attached);
    ++attached_view_generation_;
    fallback_timer_generation_.reset();
    uncreated_fallback_generation_.reset();
}

void BrowserWindow::RunUncreatedViewFallback(std::uint64_t generation) {
    CEF_REQUIRE_UI_THREAD();
    if (!view_swap_pending_ || view_swap_pending_generation_ != generation ||
        attached_view_generation_ != generation) {
        return;
    }
    // This generation has waited the cap. It may ignore only views whose browser still does not
    // exist; a browser created just before the cap must still reach the 100 ms minimum age.
    uncreated_fallback_generation_ = generation;
    RunPendingViewSwap(generation);
}

void BrowserWindow::SchedulePendingViewSwap() {
    if (!view_swap_pending_ || !view_swap_pending_generation_.has_value() ||
        *view_swap_pending_generation_ != attached_view_generation_) {
        return;
    }
    const std::uint64_t generation = attached_view_generation_;
    const ViewSwapClock::time_point now = ViewSwapNow();
    ViewSwapClock::duration longest_age_wait = ViewSwapClock::duration::zero();
    bool has_uncreated_browser = false;
    for (const AttachedView& attached : attached_views_) {
        if (!AttachedViewIsLive(attached) || AttachedViewWillRemain(attached)) {
            continue;
        }
        const std::optional<ViewSwapClock::duration> age = AttachedViewAgeAt(attached, now);
        if (!age.has_value()) {
            has_uncreated_browser = true;
            continue;
        }
        if (*age < kMinimumAttachedViewAge) {
            longest_age_wait = std::max(longest_age_wait, kMinimumAttachedViewAge - *age);
        }
    }
    if (has_uncreated_browser && fallback_timer_generation_ != generation) {
        fallback_timer_generation_ = generation;
        if (!attached_view_for_test_) {
            CefPostDelayedTask(
                TID_UI,
                CefCreateClosureTask(base::BindOnce(&BrowserWindow::RunUncreatedViewFallback,
                                                    CefRefPtr<BrowserWindow>(this), generation)),
                kUncreatedViewFallbackDelay.count());
        }
    }
    if (longest_age_wait > ViewSwapClock::duration::zero()) {
        if (!attached_view_for_test_) {
            const auto delay = std::chrono::ceil<std::chrono::milliseconds>(longest_age_wait);
            CefPostDelayedTask(
                TID_UI,
                CefCreateClosureTask(base::BindOnce(&BrowserWindow::RunPendingViewSwap,
                                                    CefRefPtr<BrowserWindow>(this), generation)),
                std::max<std::int64_t>(1, delay.count()));
        }
        return;
    }
    if ((!has_uncreated_browser || uncreated_fallback_generation_ == generation) &&
        !attached_view_for_test_) {
        // Not from inside the CEF callback that reported the browser: removing its view there
        // re-enters CEF while it is still setting that browser up.
        CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::RunPendingViewSwap,
                                                                CefRefPtr<BrowserWindow>(this),
                                                                generation)));
    }
}

void BrowserWindow::RunPendingViewSwap(std::uint64_t generation) {
    CEF_REQUIRE_UI_THREAD();
    if (!view_swap_pending_ || closing_ || view_swap_pending_generation_ != generation ||
        attached_view_generation_ != generation) {
        return;
    }
    if (AttachedBrowserStillCreating()) {
        SchedulePendingViewSwap();
        return;
    }
    view_swap_pending_ = false;
    view_swap_pending_generation_.reset();
    fallback_timer_generation_.reset();
    uncreated_fallback_generation_.reset();
    ++view_swap_count_;
    AttachActiveTabBrowserView();
    FocusAttachedContent();
}

void BrowserWindow::SetViewSwapTimeForTest(std::chrono::milliseconds since_epoch) {
    view_swap_time_for_test_ = ViewSwapClock::time_point(since_epoch);
}

void BrowserWindow::AdvanceViewSwapTimeForTest(std::chrono::milliseconds elapsed) {
    if (!view_swap_time_for_test_.has_value()) {
        view_swap_time_for_test_ = ViewSwapClock::now();
    }
    *view_swap_time_for_test_ += elapsed;
}

void BrowserWindow::SetAttachedViewForSwapTest(TabId tab_id) {
    Tab* tab = FindTabById(tab_id);
    if (tab == nullptr) {
        return;
    }
    tab->SetBrowserView(nullptr);
    attached_view_for_test_ = true;
    SetAttachedViews({AttachedView{.view = nullptr, .tab = tab_id}});
}

void BrowserWindow::SetAttachedViewsForSwapTest(std::vector<TabId> tabs) {
    std::vector<AttachedView> attached;
    for (const TabId tab_id : tabs) {
        Tab* tab = FindTabById(tab_id);
        if (tab == nullptr) {
            continue;
        }
        tab->SetBrowserView(nullptr);
        attached.push_back(
            AttachedView{.view = nullptr, .tab = tab_id, .second_slot = !attached.empty()});
    }
    attached_view_for_test_ = true;
    SetAttachedViews(std::move(attached));
}

void BrowserWindow::SetAttachedInternalPageForSwapTest(LocalPageKind kind) {
    attached_view_for_test_ = true;
    internal_page_ = kind;
    show_internal_page_ = true;
    SetAttachedViews(
        {AttachedView{.local_page_for_test = kind, .browser_created_at_for_test = ViewSwapNow()}});
}

void BrowserWindow::ToggleInternalPageForSwapTest(LocalPageKind kind) {
    if (internal_page_ == kind) {
        HideInternalPage();
        return;
    }
    internal_page_ = kind;
    show_internal_page_ = true;
    RequestViewSwap();
}

void BrowserWindow::BrowserCreatedForViewSwapTest(TabId tab_id) {
    Tab* tab = FindTabById(tab_id);
    if (tab == nullptr) {
        return;
    }
    tab->MarkBrowserCreated(ViewSwapNow());
    SchedulePendingViewSwap();
}

void BrowserWindow::RecheckPendingViewSwapForTest(std::uint64_t generation) {
    RunPendingViewSwap(generation);
}

void BrowserWindow::RunUncreatedViewFallbackForTest(std::uint64_t generation) {
    RunUncreatedViewFallback(generation);
}

std::size_t BrowserWindow::BrowserViewSizeChangeCountForDev() const noexcept {
    return chrome_ != nullptr ? chrome_->browser_view_size_change_count() : 0;
}

std::size_t BrowserWindow::ChromeRelayoutCountForDev() const noexcept {
    return chrome_ != nullptr ? chrome_->relayout_count() : 0;
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
        if (show_internal_page_ && page != nullptr) {
            chrome_->AttachBrowserView(page->view());
            SetAttachedViews({AttachedView{.view = page->view(), .local_page = page}});
            ApplyPageViewVisibility();
            return;
        }
        internal_page_.reset();
        show_internal_page_ = false;
        shortcut_conflict_.reset();
    }
    if (displayed_favorite_.has_value()) {
        // Single-pane display only: the favorite takes the whole slot. A split pair stays in its
        // space and is shown again once the favorite closes or another tab is selected.
        Tab* virtual_tab = FindVirtualTab(*displayed_favorite_);
        if (virtual_tab != nullptr && EnsureTabBrowserView(*virtual_tab)) {
            chrome_->AttachBrowserView(virtual_tab->browser_view());
            SetAttachedViews(
                {AttachedView{.view = virtual_tab->browser_view(), .tab = virtual_tab->id()}});
            ApplyPageViewVisibility();
            return;
        }
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
            SetAttachedViews(
                {AttachedView{.view = first->browser_view(), .tab = first->id()},
                 AttachedView{
                     .view = second->browser_view(), .tab = second->id(), .second_slot = true}});
            ApplyPageViewVisibility();
            return;
        }
    }
    Tab* tab = active_tab();
    if (tab == nullptr || !EnsureTabBrowserView(*tab)) {
        return;
    }
    chrome_->AttachBrowserView(tab->browser_view());
    SetAttachedViews({AttachedView{.view = tab->browser_view(), .tab = tab->id()}});
    ApplyPageViewVisibility();
}

bool BrowserWindow::EnsureTabBrowserView(Tab& tab) {
    if (tab.browser_view() != nullptr) {
        return true;
    }
    if (chrome_ == nullptr) {
        return false;
    }
    // Waking a sleeping tab: its startup URL is the page it slept on, and the saved scroll is
    // replayed from OnLoadEnd.
    const bool waking = tab.sleeping();
    tab.SetSleeping(false);
    // Only valid for tabs of the active space: the view carries the space's
    // request context and the tab's validated startup URL, matching kNewTab.
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        this, CefString(StartupUrlForTab(tab)), CefBrowserSettings(), nullptr,
        active_space().request_context(), this);
    tab.SetBrowserView(browser_view);
    if (waking) {
        sleep_policy_.Touch(tab.id(), TabSleepPolicy::Clock::now());
        UpdateChromeCollections();
    }
    return true;
}

void BrowserWindow::UpdateChromeCollections() {
    CEF_REQUIRE_UI_THREAD();
    CancelStaleConfirmations();
    if (chrome_ == nullptr || closing_) {
        return;
    }
    const Tab* active = active_tab();
    std::vector<SpaceSwitcherEntrySnapshot> spaces;
    spaces.reserve(spaces_.size());
    for (std::size_t index = 0; index < spaces_.size(); ++index) {
        spaces.push_back({.color = spaces_[index].color(),
                          .name = spaces_[index].name(),
                          .icon_id = spaces_[index].icon_id(),
                          .active = index == active_space_index_});
    }
    if (active != nullptr) {
        projected_page_title_ = active->navigation_state().snapshot().page_title;
    }
    chrome_->SetSidebarCollection(BuildSidebarCollection());
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

BookmarkResult BrowserWindow::AddFavoriteByUrl(FavoriteScope scope, std::string_view title,
                                               std::string_view url) {
    CEF_REQUIRE_UI_THREAD();
    return RunBookmarkMutation([this, scope, title = std::string(title), url = std::string(url)] {
        return bookmarks_.AddFavoriteByUrl(scope, title, url);
    });
}

BookmarkResult BrowserWindow::RemoveFavorite(FavoriteScope scope, std::uint64_t bookmark_id) {
    CEF_REQUIRE_UI_THREAD();
    return RunBookmarkMutation(
        [this, scope, bookmark_id] { return bookmarks_.RemoveFavorite(scope, bookmark_id); });
}

BookmarkResult BrowserWindow::ToggleGlobalFavoriteForCurrentPage() {
    CEF_REQUIRE_UI_THREAD();
    return ToggleFavoriteForCurrentPage(std::nullopt);
}

BookmarkResult BrowserWindow::ToggleSpaceFavoriteForCurrentPage() {
    CEF_REQUIRE_UI_THREAD();
    if (spaces_.empty()) {
        return BookmarkResult{.error = BookmarkError::kInvalidInput,
                              .message = "No current space."};
    }
    return ToggleFavoriteForCurrentPage(FavoriteScope{active_space().id().value});
}

const std::vector<std::uint64_t>* BrowserWindow::FavoriteIdsFor(FavoriteScope scope) const {
    const BookmarkState& state = bookmarks_.state();
    if (!scope.has_value()) {
        return &state.global_favorite_ids;
    }
    const auto found = state.space_favorite_ids.find(*scope);
    return found == state.space_favorite_ids.end() ? nullptr : &found->second;
}

bool BrowserWindow::IsFavorite(FavoriteScope scope, std::uint64_t bookmark_id) const {
    const std::vector<std::uint64_t>* favorite_ids = FavoriteIdsFor(scope);
    return favorite_ids != nullptr && std::find(favorite_ids->begin(), favorite_ids->end(),
                                                bookmark_id) != favorite_ids->end();
}

BookmarkResult BrowserWindow::ToggleFavorite(FavoriteScope scope, std::uint64_t bookmark_id) {
    CEF_REQUIRE_UI_THREAD();
    return RunBookmarkMutation(
        [this, scope, bookmark_id] { return ToggleFavoriteInLibrary(scope, bookmark_id); });
}

BookmarkResult BrowserWindow::ToggleFavoriteInLibrary(FavoriteScope scope,
                                                      std::uint64_t bookmark_id) {
    if (IsFavorite(scope, bookmark_id)) {
        return bookmarks_.RemoveFavorite(scope, bookmark_id);
    }
    return bookmarks_.AddFavorite(scope, bookmark_id);
}

std::optional<BrowserWindow::BookmarkablePage> BrowserWindow::CurrentBookmarkablePage() const {
    // A local page (Bookmarks, Settings, All tabs) fills the slot the person sees, so it is the
    // page on screen and it cannot be bookmarked.
    if (internal_page_.has_value() && show_internal_page_) {
        return std::nullopt;
    }
    // The displayed favorite's virtual tab is the page on screen; the ordinary tab is hidden
    // behind it. A page that has not loaded yet names its startup address.
    const Tab* tab = DisplayedTab();
    if (tab == nullptr) {
        return std::nullopt;
    }
    const NavigationSnapshot& nav = tab->navigation_state().snapshot();
    const std::string& url = nav.url.empty() ? tab->startup_url() : nav.url;
    if (!IsWebUrl(url)) {
        return std::nullopt;
    }
    const std::string host = UrlHost(url);
    std::string title = !nav.page_title.empty() ? nav.page_title : (!host.empty() ? host : url);
    return BookmarkablePage{.url = url, .title = std::move(title)};
}

BookmarkResult BrowserWindow::ToggleFavoriteForCurrentPage(FavoriteScope scope) {
    CEF_REQUIRE_UI_THREAD();
    // Every outcome, including "not a web page", goes through the mutation seam so the status
    // shows it. The page is read inside the mutation, after any earlier change to the slot.
    return RunBookmarkMutation([this, scope]() -> BookmarkResult {
        const std::optional<BookmarkablePage> page = CurrentBookmarkablePage();
        if (!page.has_value()) {
            return BookmarkResult{.error = BookmarkError::kInvalidInput,
                                  .message = "Only web pages can be favorites."};
        }
        const std::optional<std::uint64_t> id = bookmarks_.FindBookmarkByUrl(page->url);
        if (id.has_value()) {
            return ToggleFavoriteInLibrary(scope, *id);
        }
        return bookmarks_.AddFavoriteByUrl(scope, page->title, page->url);
    });
}

BookmarkResult BrowserWindow::AddCurrentPageBookmark() {
    CEF_REQUIRE_UI_THREAD();
    return RunBookmarkMutation([this]() -> BookmarkResult {
        const std::optional<BookmarkablePage> page = CurrentBookmarkablePage();
        if (!page.has_value()) {
            return BookmarkResult{.error = BookmarkError::kInvalidInput,
                                  .message = "Only web pages can be bookmarked."};
        }
        return bookmarks_.AddOrFindByUrl(page->title, page->url);
    });
}

BookmarkResult BrowserWindow::MoveCurrentPageFavorite(FavoriteScope scope, int step) {
    CEF_REQUIRE_UI_THREAD();
    return RunBookmarkMutation([this, scope, step]() -> BookmarkResult {
        const std::optional<BookmarkablePage> page = CurrentBookmarkablePage();
        const std::optional<std::uint64_t> id =
            page.has_value() ? bookmarks_.FindBookmarkByUrl(page->url) : std::nullopt;
        const std::vector<std::uint64_t>* favorite_ids = FavoriteIdsFor(scope);
        const auto found = !id.has_value() || favorite_ids == nullptr
                               ? std::vector<std::uint64_t>::const_iterator{}
                               : std::find(favorite_ids->begin(), favorite_ids->end(), *id);
        if (!id.has_value() || favorite_ids == nullptr || found == favorite_ids->end()) {
            return BookmarkResult{.error = BookmarkError::kNotFound,
                                  .message = "This page is not in that favorites list."};
        }
        const auto position = static_cast<std::size_t>(found - favorite_ids->begin());
        const bool at_end = step < 0 ? position == 0 : position + 1 >= favorite_ids->size();
        if (at_end) {
            return BookmarkResult{.error = BookmarkError::kInvalidInput,
                                  .message = "This favorite is already at that end."};
        }
        const std::size_t target = step < 0 ? position - 1 : position + 1;
        return bookmarks_.ReorderFavorite(scope, *id, target);
    });
}

BookmarkCommandState BrowserWindow::CurrentBookmarkCommandState() const {
    BookmarkCommandState state{.space_name = spaces_.empty() ? "" : active_space().name()};
    const std::optional<BookmarkablePage> page = CurrentBookmarkablePage();
    if (!page.has_value()) {
        return state;
    }
    state.page_bookmarkable = true;
    const std::optional<std::uint64_t> id = bookmarks_.FindBookmarkByUrl(page->url);
    if (!id.has_value()) {
        return state;
    }
    const auto position_in = [&](FavoriteScope scope, std::size_t& count) {
        const std::vector<std::uint64_t>* favorite_ids = FavoriteIdsFor(scope);
        if (favorite_ids == nullptr) {
            return BookmarkCommandState::kNoFavoritePosition;
        }
        count = favorite_ids->size();
        const auto found = std::find(favorite_ids->begin(), favorite_ids->end(), *id);
        return found == favorite_ids->end()
                   ? BookmarkCommandState::kNoFavoritePosition
                   : static_cast<std::size_t>(found - favorite_ids->begin());
    };
    state.global_position = position_in(std::nullopt, state.global_count);
    if (!spaces_.empty()) {
        state.space_position =
            position_in(FavoriteScope{active_space().id().value}, state.space_count);
    }
    return state;
}

std::vector<PaletteBookmarkEntry> BrowserWindow::PaletteBookmarkEntries() const {
    const BookmarkState& state = bookmarks_.state();
    std::vector<PaletteBookmarkEntry> entries;
    for (const FlatBookmark& entry : bookmarks_.Flattened()) {
        const std::optional<std::uint64_t> id = bookmarks_.FindBookmarkByUrl(entry.url);
        // A favorite in several lists is one row, labelled with each list it is in: "Global" or
        // a space's name. A list of a space that no longer exists is not presented.
        std::vector<std::string> scopes;
        if (id.has_value()) {
            if (IsFavorite(std::nullopt, *id)) {
                scopes.emplace_back("Global");
            }
            for (const auto& [space_id, favorite_ids] : state.space_favorite_ids) {
                const Space* space = FindSpace(SpaceId{space_id});
                if (space != nullptr && IsFavorite(FavoriteScope{space_id}, *id)) {
                    scopes.push_back(space->name());
                }
            }
        }
        std::string scope_label;
        for (const std::string& scope : scopes) {
            scope_label += (scope_label.empty() ? "" : ", ") + scope;
        }
        entries.push_back({.id = id.value_or(0),
                           .title = entry.title,
                           .url = entry.url,
                           .folder_path = entry.folder_name,
                           .scope_label = std::move(scope_label)});
    }
    return entries;
}

BookmarkResult BrowserWindow::RetryLastBookmarkMutation() {
    CEF_REQUIRE_UI_THREAD();
    if (!failed_bookmark_mutation_.has_value() || failed_bookmark_mutation_->retry == nullptr) {
        return BookmarkResult{.error = BookmarkError::kInvalidInput,
                              .message = "Nothing to retry."};
    }
    return RunBookmarkMutation(failed_bookmark_mutation_->retry);
}

void BrowserWindow::DismissBookmarkStatus() {
    CEF_REQUIRE_UI_THREAD();
    failed_bookmark_mutation_.reset();
    UpdateChromeCollections();
}

void BrowserWindow::ReplaceBookmarkLibraryForTest(BookmarkStore library) {
    bookmarks_ = std::move(library);
    ReserveSpaceIdsUpTo(bookmarks_.MaxSpaceKey());
}

namespace {

// Only a failure to read or write the library can clear up on a second attempt. Invalid input or
// a missing item fails the same way every time, so its status is shown without Retry.
bool IsRetryableBookmarkError(BookmarkError error) {
    return error == BookmarkError::kFileReadError || error == BookmarkError::kFileWriteError;
}

}  // namespace

BookmarkResult BrowserWindow::RunBookmarkMutation(std::function<BookmarkResult()> mutation) {
    CEF_REQUIRE_UI_THREAD();
    BookmarkResult result = mutation();
    if (result.ok()) {
        failed_bookmark_mutation_.reset();
    } else {
        FailedBookmarkMutation failed{.message = result.message, .retry = nullptr};
        if (IsRetryableBookmarkError(result.error)) {
            failed.retry = std::move(mutation);
        }
        failed_bookmark_mutation_ = std::move(failed);
    }
    if (ReconcileFavoriteTabs()) {
        // The displayed virtual tab went away: the slot shows the selected ordinary tab.
        AttachActiveTabObservers();
    }
    UpdateChromeCollections();
    return result;
}

bool BrowserWindow::ReconcileFavoriteTabs() {
    std::vector<FavoriteTabKey> gone;
    for (const auto& entry : favorite_tabs_.records()) {
        if (!FavoriteListContains(entry.first)) {
            gone.push_back(entry.first);
        }
    }
    bool displayed_gone = false;
    for (const FavoriteTabKey& key : gone) {
        if (displayed_favorite_ == key) {
            displayed_favorite_.reset();
            displayed_gone = true;
        }
        DropVirtualTab(key);
        favorite_tabs_.Dispose(key);
    }
    return displayed_gone;
}

bool BrowserWindow::FavoriteListContains(const FavoriteTabKey& key) const {
    const BookmarkState& state = bookmarks_.state();
    const std::vector<std::uint64_t>* list = &state.global_favorite_ids;
    if (key.scope.has_value()) {
        const auto found = state.space_favorite_ids.find(*key.scope);
        if (found == state.space_favorite_ids.end()) {
            return false;
        }
        list = &found->second;
    }
    return std::find(list->begin(), list->end(), key.bookmark_id) != list->end();
}

void BrowserWindow::DropVirtualTab(const FavoriteTabKey& key) {
    CEF_REQUIRE_UI_THREAD();
    const auto found = virtual_tabs_.find(key);
    if (found == virtual_tabs_.end()) {
        return;
    }
    const CefRefPtr<CefBrowser> browser = found->second.browser();
    virtual_tabs_.erase(found);
    if (browser != nullptr) {
        browser->GetHost()->CloseBrowser(true);
    }
}

Tab* BrowserWindow::FindVirtualTab(const FavoriteTabKey& key) noexcept {
    const auto found = virtual_tabs_.find(key);
    return found == virtual_tabs_.end() ? nullptr : &found->second;
}

void BrowserWindow::ReturnToOrdinaryTab() {
    if (!displayed_favorite_.has_value()) {
        return;
    }
    ClearShownTabObservers();
    displayed_favorite_.reset();
    AttachActiveTabObservers();
}

bool BrowserWindow::ActivateFavorite(FavoriteScope scope, std::uint64_t bookmark_id) {
    CEF_REQUIRE_UI_THREAD();
    const FavoriteTabKey key{.scope = scope, .bookmark_id = bookmark_id};
    const Bookmark* bookmark = bookmarks_.FindBookmark(bookmark_id);
    if (closing_ || bookmark == nullptr || !FavoriteListContains(key)) {
        return false;
    }
    // The sidebar shows the active space's list only. A list of another space (or of a space that
    // is gone) stays in the library and cannot open from here.
    if (scope.has_value() && *scope != active_space().id().value) {
        return false;
    }
    const FavoriteTabRecord& record = favorite_tabs_.Ensure(key, bookmark->url);
    if (!virtual_tabs_.contains(key)) {
        Tab tab{NextTabId()};
        const ValidatedAddress address = address_validator_ ? address_validator_(record.last_url)
                                                            : ParseAndValidate(record.last_url);
        if (address.is_valid()) {
            tab.SetStartupUrl(address.url);
        }
        virtual_tabs_.emplace(key, std::move(tab));
    }
    // Only the displayed favorite changes: the ordinary tab list and its selection stay as they
    // were, so no Detach here (a Detach would end the display we are about to start).
    ClearShownTabObservers();
    displayed_favorite_ = key;
    AttachActiveTabObservers();
    return true;
}

void BrowserWindow::CloseFavoriteTab() {
    CEF_REQUIRE_UI_THREAD();
    if (!displayed_favorite_.has_value()) {
        return;
    }
    const FavoriteTabKey key = *displayed_favorite_;
    ClearShownTabObservers();
    displayed_favorite_.reset();
    DropVirtualTab(key);
    favorite_tabs_.ResetToFavorite(key);
    AttachActiveTabObservers();
}

SidebarCollectionSnapshot BrowserWindow::BuildSidebarCollection() const {
    SidebarCollectionSnapshot snapshot;
    const BookmarkState& state = bookmarks_.state();
    const auto favorites = [this](FavoriteScope scope, const std::vector<std::uint64_t>& ids) {
        std::vector<FavoriteEntrySnapshot> entries;
        for (const std::uint64_t bookmark_id : ids) {
            const Bookmark* bookmark = bookmarks_.FindBookmark(bookmark_id);
            if (bookmark == nullptr) {
                continue;
            }
            const FavoriteTabKey key{.scope = scope, .bookmark_id = bookmark_id};
            FavoriteEntrySnapshot entry{.scope = scope,
                                        .bookmark_id = bookmark_id,
                                        .title = bookmark->title,
                                        .url = bookmark->url,
                                        .displayed = displayed_favorite_ == key};
            const auto virtual_tab = virtual_tabs_.find(key);
            if (virtual_tab != virtual_tabs_.end()) {
                entry.favicon = virtual_tab->second.favicon();
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    };
    snapshot.global_favorites = favorites(std::nullopt, state.global_favorite_ids);
    const Space& space = active_space();
    const auto space_list = state.space_favorite_ids.find(space.id().value);
    if (space_list != state.space_favorite_ids.end()) {
        snapshot.space_favorites = favorites(FavoriteScope{space.id().value}, space_list->second);
    }

    const Tab* active = active_tab();
    snapshot.tabs.reserve(space.tab_count());
    for (const Tab& tab : space.tabs()) {
        snapshot.tabs.push_back({.title = tab.DisplayTitle(),
                                 .active = active != nullptr && tab.id() == active->id(),
                                 .pinned = tab.pinned(),
                                 .favicon = tab.favicon(),
                                 .sleeping = tab.sleeping(),
                                 .group_parent = tab.group_parent(),
                                 .group_label = tab.group_label()});
    }
    if (const std::optional<BookmarkablePage> page = CurrentBookmarkablePage(); page.has_value()) {
        // The cheap http(s) check comes first, so internal pages never reach the validator. The
        // injected validator (the same seam the other address paths use) keeps headless tests off
        // the CEF URL API, which they have not initialized.
        snapshot.add_current_page_available =
            IsWebTileUrl(page->url) &&
            (address_validator_ ? address_validator_(page->url) : ParseAndValidate(page->url))
                .is_valid();
    }
    if (failed_bookmark_mutation_.has_value()) {
        snapshot.bookmark_status =
            BookmarkStatusSnapshot{.text = failed_bookmark_mutation_->message,
                                   .retryable = failed_bookmark_mutation_->retry != nullptr};
    }
    return snapshot;
}

Space BrowserWindow::RemoveSpaceAtIndex(std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (plan_tab_.has_value()) {
        const std::optional<std::pair<std::size_t, std::size_t>> plan = TabLocation(*plan_tab_);
        if (plan.has_value() && plan->first == index) {
            // The research's hub goes with its space, so the research stops with it.
            const TabId plan_id = *plan_tab_;
            CancelResearch();
            ForgetPlanTabMapping(plan_id);
        }
    }
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
    // A real deletion (never a reorder) ends the space's favorite lists and virtual tabs. The
    // list is removed only when it exists, so a space without favorites writes nothing.
    const FavoriteScope removed_scope = FavoriteScope{removed.id().value};
    for (const FavoriteTabKey& key : favorite_tabs_.DisposeScope(removed_scope)) {
        DropVirtualTab(key);
    }
    if (bookmarks_.state().space_favorite_ids.contains(removed.id().value)) {
        static_cast<void>(RunBookmarkMutation([this, space_key = removed.id().value] {
            return bookmarks_.RemoveSpaceFavorites(space_key);
        }));
    }
    return removed;
}

void BrowserWindow::ApplyTheme(CefRefPtr<CefWindow> window, ChromeTheme theme, bool notify_views) {
    const ChromeTokens tokens = ResolvedTokens(theme);
    tinted_space_color_ = active_space().color().argb;
    window->SetThemeColor(CEF_ColorPrimaryBackground, tokens.canvas.argb);
    window->SetThemeColor(CEF_ColorPrimaryForeground, tokens.text.argb);
    window->SetThemeColor(CEF_ColorSecondaryForeground, tokens.text_secondary.argb);
    window->SetThemeColor(CEF_ColorAccent, tokens.accent.argb);
    // Textfields (the address field and both palette query fields) read their colors from the
    // theme: the typed value in --text, the placeholder in --text-2, a selection in the accent
    // with the page background as its text, and the field's own outline as the hairline.
    // Native's address field is flush with its fill plate. CEF textfields otherwise inherit the
    // generic surface here, leaving a visible rounded inner step inside the address focus ring.
    const ArgbColor textfield_background =
        tokens.look == LookPreset::kNative ? tokens.fill : tokens.surface;
    window->SetThemeColor(CEF_ColorTextfieldBackground, textfield_background.argb);
    window->SetThemeColor(CEF_ColorTextfieldBackgroundDisabled, textfield_background.argb);
    window->SetThemeColor(CEF_ColorTextfieldForeground, tokens.text.argb);
    window->SetThemeColor(CEF_ColorTextfieldForegroundDisabled, tokens.text.argb);
    window->SetThemeColor(CEF_ColorTextfieldForegroundPlaceholder, tokens.text_secondary.argb);
    window->SetThemeColor(CEF_ColorTextfieldSelectionBackground, tokens.accent.argb);
    window->SetThemeColor(CEF_ColorTextfieldSelectionForeground, tokens.background.argb);
    window->SetThemeColor(CEF_ColorTextfieldHover, tokens.surface_secondary.argb);
    ApplyAddressFocusColors(window, tokens);
    window->SetThemeColor(CEF_ColorFocusableBorderUnfocused, tokens.border.argb);
    ApplyWebColorScheme();
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
    if (cheatsheet_ != nullptr) {
        cheatsheet_->ApplyTheme(tokens);
    }
    if (find_overlay_ != nullptr) {
        find_overlay_->ApplyTheme(tokens);
    }
    if (welcome_ != nullptr) {
        welcome_->ApplyTheme(tokens);
    }
    if (peek_ != nullptr) {
        peek_->ApplyTheme(tokens);
    }
    chrome_snapshot_.theme = theme;
    ApplySidebarState();
    PublishChromeSnapshot();
    ScheduleLocalPagesRender();
    RenderStartPages();
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
    if (command_palette_ != nullptr) {
        command_palette_->Detach();
        command_palette_.reset();
    }
    if (space_rename_overlay_ != nullptr) {
        space_rename_overlay_->Detach();
        space_rename_overlay_.reset();
    }
    if (peek_ != nullptr) {
        peek_->Detach();
        peek_.reset();
    }
    if (cheatsheet_ != nullptr) {
        cheatsheet_->Detach();
        cheatsheet_.reset();
    }
    if (find_overlay_ != nullptr) {
        find_overlay_->Detach();
        find_overlay_.reset();
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
    const Tab* tab = DisplayedTab();
    return closing_ || tab == nullptr ? nullptr : tab->browser();
}

Tab* BrowserWindow::FindTabById(TabId id) noexcept {
    for (Space& space : spaces_) {
        if (Tab* tab = space.FindTab(id)) {
            return tab;
        }
    }
    for (auto& [key, tab] : virtual_tabs_) {
        if (tab.id() == id) {
            return &tab;
        }
    }
    return nullptr;
}

std::vector<TabSleepInput> BrowserWindow::CollectSleepInputs() const {
    std::vector<TabSleepInput> inputs;
    for (std::size_t space_index = 0; space_index < spaces_.size(); ++space_index) {
        const Space& space = spaces_[space_index];
        const bool space_shown = space_index == active_space_index_;
        for (const Tab& tab : space.tabs()) {
            TabSleepInput input;
            input.id = tab.id();
            input.active =
                space_shown && space.has_active_tab() && space.active_tab_id() == tab.id();
            input.pinned = tab.pinned();
            input.split_partner = space.split().has_value() && (space.split()->first == tab.id() ||
                                                                space.split()->second == tab.id());
            input.sleeping = tab.sleeping();
            input.has_browser = tab.browser() != nullptr;
            input.web_page = !WebUrlOf(tab).empty();
            inputs.push_back(input);
        }
    }
    return inputs;
}

void BrowserWindow::TouchActiveTabs() {
    if (spaces_.empty() || active_space_index_ >= spaces_.size()) {
        return;
    }
    const TabSleepPolicy::Clock::time_point now = TabSleepPolicy::Clock::now();
    const Space& space = active_space();
    if (space.has_active_tab()) {
        sleep_policy_.Touch(space.active_tab_id(), now);
    }
    if (space.split().has_value()) {
        sleep_policy_.Touch(space.split()->first, now);
        sleep_policy_.Touch(space.split()->second, now);
    }
}

bool BrowserWindow::SetTabSleepMinutes(int minutes) {
    if (!IsValidTabSleepMinutes(minutes)) {
        return false;
    }
    prefs_.tab_sleep_minutes = minutes;
    sleep_policy_.SetMinutes(minutes);
    SavePrefs();
    return true;
}

std::size_t BrowserWindow::sleeping_tab_count() const {
    std::size_t count = 0;
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            count += tab.sleeping() ? 1U : 0U;
        }
    }
    return count;
}

void BrowserWindow::ScheduleSleepTick() {
    if (sleep_tick_scheduled_ || closing_) {
        return;
    }
    sleep_tick_scheduled_ = true;
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(&BrowserWindow::RunSleepTick,
                                                           CefRefPtr<BrowserWindow>(this))),
                       kSleepTickMs);
}

void BrowserWindow::RunSleepTick() {
    CEF_REQUIRE_UI_THREAD();
    sleep_tick_scheduled_ = false;
    if (closing_) {
        return;
    }
    const TabSleepPolicy::Clock::time_point now = TabSleepPolicy::Clock::now();
    TouchActiveTabs();
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            sleep_policy_.Observe(tab.id(), now);
        }
    }
    for (const TabId id : sleep_policy_.Expired(CollectSleepInputs(), now)) {
        if (Tab* tab = FindTabById(id)) {
            static_cast<void>(BeginSleepProbe(*tab));
        }
    }
    ScheduleSleepTick();
}

std::size_t BrowserWindow::SleepInactiveTabsNow() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return 0;
    }
    std::size_t started = 0;
    for (const TabId id : sleep_policy_.AllEligible(CollectSleepInputs())) {
        Tab* tab = FindTabById(id);
        if (tab != nullptr && BeginSleepProbe(*tab)) {
            ++started;
        }
    }
    return started;
}

bool BrowserWindow::BeginSleepProbe(Tab& tab) {
    const CefRefPtr<CefBrowser> browser = tab.browser();
    const CefRefPtr<CefFrame> frame = browser != nullptr ? browser->GetMainFrame() : nullptr;
    if (frame == nullptr) {
        return false;
    }
    for (const auto& [token, probe] : sleep_probes_) {
        if (probe.tab == tab.id()) {
            return false;  // already asking
        }
    }
    const std::string token = RandomHexToken();
    sleep_probes_[token] = {.tab = tab.id(), .browser_id = browser->GetIdentifier()};
    frame->ExecuteJavaScript(SleepProbeScript(token), frame->GetURL(), 0);
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(&BrowserWindow::ExpireSleepProbe,
                                                           CefRefPtr<BrowserWindow>(this), token)),
                       kSleepProbeTimeoutMs);
    return true;
}

void BrowserWindow::ExpireSleepProbe(const std::string& token) {
    // A page that did not answer in time is busy or hostile: it stays awake this round.
    sleep_probes_.erase(token);
}

bool BrowserWindow::HandleSleepProbeMessage(CefRefPtr<CefBrowser> browser,
                                            const std::string& text) {
    CEF_REQUIRE_UI_THREAD();
    const std::size_t token_end = text.find(':', kSleepProbePrefix.size());
    if (token_end == std::string::npos || browser == nullptr) {
        return true;
    }
    const std::string token =
        text.substr(kSleepProbePrefix.size(), token_end - kSleepProbePrefix.size());
    const auto found = sleep_probes_.find(token);
    if (found == sleep_probes_.end() || found->second.browser_id != browser->GetIdentifier()) {
        return true;  // unknown token or a different browser: ignore
    }
    const SleepProbe probe = found->second;
    sleep_probes_.erase(found);

    double fields[4] = {0.0, 0.0, 0.0, 0.0};
    const char* cursor = text.c_str() + token_end + 1;
    for (double& field : fields) {
        char* end = nullptr;
        field = std::strtod(cursor, &end);
        if (end == cursor) {
            return true;
        }
        cursor = *end == ',' ? end + 1 : end;
    }
    if (fields[2] != 0.0 || fields[3] != 0.0) {
        return true;  // unsaved input or playing media: skip this round
    }
    // The tab may have been selected, split or closed while the probe was in flight.
    for (const TabSleepInput& input : CollectSleepInputs()) {
        if (input.id == probe.tab) {
            if (sleep_policy_.Eligible(input)) {
                static_cast<void>(SleepTab(probe.tab, fields[0], fields[1]));
            }
            break;
        }
    }
    return true;
}

bool BrowserWindow::SleepTab(TabId id, double scroll_x, double scroll_y) {
    CEF_REQUIRE_UI_THREAD();
    Tab* tab = FindTabById(id);
    if (closing_ || tab == nullptr || tab->sleeping()) {
        return false;
    }
    const Space& active = active_space();
    if (active.has_active_tab() && active.active_tab_id() == id) {
        return false;  // the page on screen never sleeps
    }
    const std::string url = WebUrlOf(*tab);
    if (url.empty()) {
        return false;
    }
    // The model keeps the committed page as the tab's startup URL (the same URL session save
    // writes) and the scroll offset; title, favicon and navigation snapshot stay as they are.
    tab->SetStartupUrl(url);
    tab->SetSavedScroll(scroll_x, scroll_y);
    tab->SetSleeping(true);
    // Order matters: OnBrowserDestroyed tears the chrome down for any browser it can still map
    // to a tab, so the tab forgets the browser and view before the close is requested.
    const CefRefPtr<CefBrowser> browser = tab->browser();
    tab->SetBrowser(nullptr);
    tab->SetBrowserView(nullptr);
    if (browser != nullptr) {
        sleep_closing_browser_ids_.insert(browser->GetIdentifier());
        browser->GetHost()->CloseBrowser(true);
    }
    UpdateChromeCollections();
    return true;
}

void BrowserWindow::RestoreSavedScroll(Tab& tab, CefRefPtr<CefFrame> frame) {
    if (!tab.has_saved_scroll() || tab.sleeping() || frame == nullptr) {
        return;
    }
    frame->ExecuteJavaScript(ScrollRestoreScript(tab.saved_scroll_x(), tab.saved_scroll_y()),
                             frame->GetURL(), 0);
    tab.ClearSavedScroll();
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
    CloseCheatsheet(/*refocus_page=*/false);
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
    CloseCheatsheet(/*refocus_page=*/false);
    space_icon_picker_open_ = false;
    if (command_palette_ == nullptr) {
        // Lazily created on the first Cmd/Ctrl+K, then only shown and hidden.
        command_palette_ = std::make_unique<CommandPaletteView>(
            *this, window_, ResolvedTokens(ResolvedChromeTheme()), &ParseAndValidate,
            icon_resource_root_);
    }
    // Read-only snapshots taken at open time: the open tabs of the active space
    // (title/URL for matching, TabId for routing) and the whole space list.
    std::vector<PaletteTabEntry> tabs;
    std::vector<PaletteTabVisual> tab_visuals;
    tabs.reserve(active_space().tab_count());
    tab_visuals.reserve(active_space().tab_count());
    for (const Tab& tab : active_space().tabs()) {
        const NavigationSnapshot& nav = tab.navigation_state().snapshot();
        tabs.push_back({.id = tab.id(),
                        .title = nav.page_title.empty() ? "Island" : nav.page_title,
                        .url = nav.url});
        tab_visuals.push_back({.id = tab.id(), .favicon = tab.favicon()});
    }
    std::vector<PaletteSpaceEntry> spaces;
    spaces.reserve(spaces_.size());
    for (const Space& space : spaces_) {
        spaces.push_back({.id = space.id(), .name = space.name()});
    }
    std::vector<PaletteBookmarkEntry> bookmarks = PaletteBookmarkEntries();
    std::vector<PaletteReportEntry> reports;
    reports.reserve(reports_.reports.size());
    for (const ResearchReport& report : reports_.reports) {
        reports.push_back({.id = report.id,
                           .title = report.title,
                           .question = report.question,
                           .kind = report.kind});
    }
    std::vector<PaletteCommandEntry> commands = PaletteCommands(CurrentBookmarkCommandState());
    for (PaletteCommandEntry& command : commands) {
        const std::optional<KeyAction> action = KeyActionFromId(command.shortcut_action_id);
        const std::optional<KeyBinding> binding =
            action.has_value() ? keymap_.Binding(*action) : std::nullopt;
        if (binding.has_value()) {
#if defined(__APPLE__)
            command.shortcut = FormatKeyGlyphs(*binding, /*mac=*/true);
#else
            command.shortcut = FormatKeyGlyphs(*binding, /*mac=*/false);
#endif
        }
    }
    command_palette_->Show(std::move(tabs), std::move(spaces), std::move(bookmarks),
                           std::move(commands), std::move(reports), std::move(tab_visuals));
}

void BrowserWindow::ShowCommandPaletteWithQueryForDev(std::string_view query) {
    CEF_REQUIRE_UI_THREAD();
    ShowCommandPalette();
    if (command_palette_ != nullptr) {
        command_palette_->SetQueryForDev(query);
    }
}

std::vector<PaletteCommandEntry> BrowserWindow::PaletteCommands(
    const BookmarkCommandState& bookmarks) {
    const std::string space = bookmarks.space_name.empty() ? "current-space" : bookmarks.space_name;
    std::vector<PaletteCommandEntry> commands;
    commands.push_back({.id = "open_bookmarks",
                        .title = "Open Bookmarks",
                        .icon = PaletteRowIcon::kBookmark,
                        .bookmark_group = true});
    // The page-changing bookmark rows exist only when the page on screen can be bookmarked.
    if (bookmarks.page_bookmarkable) {
        commands.push_back({.id = "add_bookmark",
                            .title = "Add bookmark",
                            .icon = PaletteRowIcon::kBookmark,
                            .bookmark_group = true});
        commands.push_back({.id = "toggle_global_favorite",
                            .title = bookmarks.global_favorite() ? "Remove global favorite"
                                                                 : "Add global favorite",
                            .icon = PaletteRowIcon::kBookmark,
                            .bookmark_group = true});
        commands.push_back(
            {.id = "toggle_space_favorite",
             .title = (bookmarks.space_favorite() ? "Remove " : "Add ") + space + " favorite",
             .icon = PaletteRowIcon::kBookmark,
             .bookmark_group = true});
    }
    commands.push_back({.id = "new_bookmark_folder",
                        .title = "New folder",
                        .icon = PaletteRowIcon::kBookmark,
                        .bookmark_group = true});
    commands.push_back({.id = "import_bookmarks",
                        .title = "Open Bookmarks to import\xE2\x80\xA6",
                        .icon = PaletteRowIcon::kBookmark,
                        .bookmark_group = true});
    // Reorder rows: only for the page's own favorite, and never past either end of its list.
    if (bookmarks.page_bookmarkable && bookmarks.global_favorite()) {
        if (bookmarks.global_position > 0) {
            commands.push_back({.id = "move_global_favorite_left",
                                .title = "Move global favorite left",
                                .icon = PaletteRowIcon::kBookmark,
                                .bookmark_group = true});
        }
        if (bookmarks.global_position + 1 < bookmarks.global_count) {
            commands.push_back({.id = "move_global_favorite_right",
                                .title = "Move global favorite right",
                                .icon = PaletteRowIcon::kBookmark,
                                .bookmark_group = true});
        }
    }
    if (bookmarks.page_bookmarkable && bookmarks.space_favorite()) {
        if (bookmarks.space_position > 0) {
            commands.push_back({.id = "move_space_favorite_up",
                                .title = "Move " + space + " favorite up",
                                .icon = PaletteRowIcon::kBookmark,
                                .bookmark_group = true});
        }
        if (bookmarks.space_position + 1 < bookmarks.space_count) {
            commands.push_back({.id = "move_space_favorite_down",
                                .title = "Move " + space + " favorite down",
                                .icon = PaletteRowIcon::kBookmark,
                                .bookmark_group = true});
        }
    }
    commands.push_back({.id = "sleep_inactive_tabs",
                        .title = "Sleep inactive tabs now",
                        .icon = PaletteRowIcon::kMoon});
    commands.push_back({.id = std::string(kChangeSpaceIconCommandId),
                        .title = "Change space icon\xE2\x80\xA6",
                        .icon = PaletteRowIcon::kPalette});
    commands.push_back({.id = "agent_tasks",
                        .title = "Agent tasks",
                        .icon = PaletteRowIcon::kSparkles,
                        .shortcut_action_id = "toggle_agent"});
    commands.push_back(
        {.id = "summarize_page", .title = "Summarize page", .icon = PaletteRowIcon::kSparkles});
    commands.push_back({.id = "ask_about_page",
                        .title = "Ask about page\xE2\x80\xA6",
                        .icon = PaletteRowIcon::kSparkles});
    commands.push_back({.id = "collect_from_tabs",
                        .title = "Collect from tabs\xE2\x80\xA6",
                        .icon = PaletteRowIcon::kTable});
    return commands;
}

void BrowserWindow::ShowSpaceIconPicker() {
    CEF_REQUIRE_UI_THREAD();
    ++space_icon_picker_shows_;
    if (closing_ || command_palette_ == nullptr) {
        return;
    }
    space_icon_picker_open_ = true;
    command_palette_->Show({}, {}, {}, SpaceIconPaletteCommands(), {});
}

void BrowserWindow::ScheduleSpaceIconPicker() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || space_icon_picker_scheduled_) {
        return;
    }
    space_icon_picker_scheduled_ = true;
    if (!ui_tasks_enabled_for_test_) {
        // Headless tests have no CEF task queue; they run RunScheduledSpaceIconPickerForTest.
        return;
    }
    // Runs after the palette's released overlay has been destroyed by its posted task (FIFO).
    CefPostTask(TID_UI,
                CefCreateClosureTask(base::BindOnce(
                    [](CefRefPtr<BrowserWindow> window) { window->RunScheduledSpaceIconPicker(); },
                    CefRefPtr<BrowserWindow>(this))));
}

void BrowserWindow::RunScheduledSpaceIconPicker() {
    CEF_REQUIRE_UI_THREAD();
    space_icon_picker_scheduled_ = false;
    ShowSpaceIconPicker();
}

void BrowserWindow::OnCommandPaletteQueryChanged(std::string_view query) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || history_ == nullptr || space_icon_picker_open_) {
        return;
    }
    const std::string requested(query);
    history_->QueryAsync(requested, UnixNowMs(), 8,
                         [window = CefRefPtr<BrowserWindow>(this),
                          requested](std::vector<HistoryResult> results) mutable {
                             CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                                                     &BrowserWindow::ApplyHistoryResults, window,
                                                     std::move(requested), std::move(results))));
                         });
}

void BrowserWindow::ApplyHistoryResults(std::string query, std::vector<HistoryResult> results) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || command_palette_ == nullptr) {
        return;
    }
    std::vector<PaletteHistoryEntry> history;
    history.reserve(results.size());
    for (HistoryResult& result : results) {
        const std::string host = UrlHost(result.url);
        history.push_back({.title = std::move(result.title),
                           .url = std::move(result.url),
                           .host = host,
                           .visited_at_ms = result.visited_at_ms,
                           .visit_count = result.visit_count,
                           .score = result.score});
    }
    command_palette_->SetHistoryResults(query, std::move(history));
}

void BrowserWindow::RefreshStartTiles() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || history_ == nullptr) {
        return;
    }
    history_->MostVisitedAsync(kStartTileLimit, [window = CefRefPtr<BrowserWindow>(this)](
                                                    std::vector<HistoryResult> results) {
        CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::ApplyStartTiles,
                                                                window, std::move(results))));
    });
}

void BrowserWindow::ApplyStartTiles(std::vector<HistoryResult> most_visited) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    start_tiles_ = BuildStartTiles(most_visited);
    RenderStartPages();
}

void BrowserWindow::OnCommandPaletteSubmitted(const PaletteSelection& selection,
                                              std::string_view url_query) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const bool was_space_icon_picker = std::exchange(space_icon_picker_open_, false);
    if (was_space_icon_picker) {
        if (selection.kind == PaletteEntryKind::kCommand) {
            if (const std::optional<std::string> icon =
                    ParseSpaceIconCommandId(selection.command.id)) {
                static_cast<void>(SetSpaceIcon(active_space_index_, *icon));
            }
        }
        FocusBrowserView();
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
        case PaletteEntryKind::kHistory:
            OpenHistory(selection.history);
            break;
        case PaletteEntryKind::kCommand:
            if (selection.command.id == kChangeSpaceIconCommandId) {
                ScheduleSpaceIconPicker();
                return;  // the picker keeps the focus
            } else {
                RunPaletteCommand(selection.command.id);
            }
            break;
        case PaletteEntryKind::kReport:
            static_cast<void>(OpenReportTab(selection.report.id));
            break;
    }
    FocusBrowserView();
}

void BrowserWindow::OnCommandPaletteDismissed() {
    CEF_REQUIRE_UI_THREAD();
    space_icon_picker_open_ = false;
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

void BrowserWindow::AddTrustedSite(std::string host) {
    const std::vector<std::string> normalized = NormalizeTrustedSites({std::move(host)});
    if (normalized.empty() ||
        std::find(prefs_.agent_trusted_sites.begin(), prefs_.agent_trusted_sites.end(),
                  normalized.front()) != prefs_.agent_trusted_sites.end()) {
        return;
    }
    prefs_.agent_trusted_sites.push_back(normalized.front());
    SavePrefs();
    ScheduleLocalPagesRender();
}

std::chrono::steady_clock::time_point BrowserWindow::ConfirmationNow() const {
    return std::chrono::steady_clock::now() + confirmation_clock_offset_;
}

namespace {

// The origin a card is raised for: the page's committed URL, or its start URL before the first
// load.
std::string AgentTabOrigin(const Tab& tab) {
    const std::string& url = tab.navigation_state().snapshot().url;
    return FaviconOrigin(url.empty() ? tab.startup_url() : url);
}

}  // namespace

void BrowserWindow::ShowConfirmation(agent::ConfirmationRequest request,
                                     std::function<void(agent::ConfirmationAnswer)> done) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        done(agent::ConfirmationAnswer::kWithdrawn);
        return;
    }
    // The card belongs to the tab the action targets: the one ResolveAgentTab names now, which is
    // the Plan's agent page during a research and the active tab otherwise. The card keeps that
    // tab's identity, not its position, and is marked "waiting for you" there.
    const AgentTabLookup lookup = ResolveAgentTab(request.tab);
    if (!lookup.tab.has_value()) {
        done(agent::ConfirmationAnswer::kWithdrawn);
        return;
    }
    const Space& space = spaces_[lookup.tab->space];
    const Tab& tab = space.tabs()[lookup.tab->tab];
    pending_confirmations_.push_back({.request = std::move(request),
                                      .done = std::move(done),
                                      .deadline = ConfirmationNow() + kConfirmationTimeout,
                                      .tab_id = tab.id(),
                                      .space_id = space.id(),
                                      .origin = AgentTabOrigin(tab)});
    // The panel is not opened: the agent dot and the Agent button dot tell the person.
    ArmConfirmationTimer();
    RefreshConfirmationUi();
}

void BrowserWindow::ShowConfirmationFixtureForDev() {
    ShowConfirmation({.id = "dev1",
                      .tool = "browser_click",
                      .summary = "Click \"Place order\" and submit the checkout form",
                      .host = "shop.example",
                      .tab = std::nullopt,
                      .trusted_allowed = true},
                     [](agent::ConfirmationAnswer answer) {
                         fprintf(stderr, "island-dev: confirmation answered %d\n",
                                 static_cast<int>(answer));
                     });
}

void BrowserWindow::AnswerConfirmation(std::string_view id, agent::ConfirmationAnswer answer) {
    CEF_REQUIRE_UI_THREAD();
    for (std::size_t index = 0; index < pending_confirmations_.size(); ++index) {
        const agent::ConfirmationRequest& request = pending_confirmations_[index].request;
        if (request.id != id) {
            continue;
        }
        // The action runs with the tab index the request named, so a card whose tab moved is
        // withdrawn instead of acting on whatever sits at that index now. This check comes first:
        // a withdrawn card must not save a trusted site either.
        const bool grants = answer == agent::ConfirmationAnswer::kTrustSite ||
                            answer == agent::ConfirmationAnswer::kAllowForTask;
        if (answer != agent::ConfirmationAnswer::kCancel &&
            !ConfirmationTargetIsCurrent(pending_confirmations_[index])) {
            answer = agent::ConfirmationAnswer::kWithdrawn;
        } else if (grants && !request.trusted_allowed) {
            answer = agent::ConfirmationAnswer::kCancel;
        } else if (answer == agent::ConfirmationAnswer::kTrustSite) {
            AddTrustedSite(request.host);
        }
        FinishConfirmation(index, answer);
        return;
    }
}

bool BrowserWindow::ConfirmationTargetIsCurrent(const PendingConfirmation& pending) const {
    // The action re-resolves the card's tab when it runs, so the card is current only while that
    // same resolution still names the tab it was raised for. Viewing another tab or space does not
    // change the resolution of a card for the research page; a reshuffled index does.
    const AgentTabLookup lookup = ResolveAgentTab(pending.request.tab);
    if (!lookup.tab.has_value()) {
        return false;
    }
    const Space& space = spaces_[lookup.tab->space];
    const Tab& tab = space.tabs()[lookup.tab->tab];
    return space.id() == pending.space_id && tab.id() == pending.tab_id &&
           AgentTabOrigin(tab) == pending.origin;
}

void BrowserWindow::CancelStaleConfirmations() {
    for (std::size_t index = 0; index < pending_confirmations_.size();) {
        if (ConfirmationTargetIsCurrent(pending_confirmations_[index])) {
            ++index;
        } else {
            FinishConfirmation(index, agent::ConfirmationAnswer::kWithdrawn);
        }
    }
}

void BrowserWindow::FinishConfirmation(std::size_t index, agent::ConfirmationAnswer answer) {
    std::function<void(agent::ConfirmationAnswer)> done =
        std::move(pending_confirmations_[index].done);
    pending_confirmations_.erase(pending_confirmations_.begin() +
                                 static_cast<std::ptrdiff_t>(index));
    RefreshConfirmationUi();
    if (done) {
        done(answer);
    }
}

void BrowserWindow::CancelPendingConfirmations() {
    while (!pending_confirmations_.empty()) {
        FinishConfirmation(pending_confirmations_.size() - 1,
                           agent::ConfirmationAnswer::kWithdrawn);
    }
}

void BrowserWindow::ExpireConfirmations() {
    const std::chrono::steady_clock::time_point now = ConfirmationNow();
    for (std::size_t index = 0; index < pending_confirmations_.size();) {
        if (pending_confirmations_[index].deadline <= now) {
            FinishConfirmation(index, agent::ConfirmationAnswer::kTimeout);
        } else {
            ++index;
        }
    }
}

void BrowserWindow::AdvanceConfirmationClockForTest(std::chrono::seconds elapsed) {
    confirmation_clock_offset_ += elapsed;
    ExpireConfirmations();
}

void BrowserWindow::ArmConfirmationTimer() {
    // Without a chrome there is no running message loop (headless tests drive the clock by hand).
    if (confirmation_timer_armed_ || pending_confirmations_.empty() || closing_ ||
        chrome_ == nullptr) {
        return;
    }
    confirmation_timer_armed_ = true;
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(&BrowserWindow::OnConfirmationTick,
                                                           CefRefPtr<BrowserWindow>(this))),
                       1000);
}

void BrowserWindow::OnConfirmationTick() {
    confirmation_timer_armed_ = false;
    if (closing_) {
        return;
    }
    ExpireConfirmations();
    ArmConfirmationTimer();
}

void BrowserWindow::RefreshConfirmationUi() {
    if (closing_) {
        return;
    }
    RefreshAddressIndicators();
    if (chrome_ != nullptr) {
        chrome_->SetAgentNeedsYou(!pending_confirmations_.empty());
    }
    SyncQueueWaiting();
    ScheduleLocalPagesRender();
}

json::Value BrowserWindow::ConfirmationsStateJson() const {
    json::Value list = json::Value::MakeArray();
    for (const PendingConfirmation& pending : pending_confirmations_) {
        list.Push(json::Value::MakeObject()
                      .Set("id", json::Value::String(pending.request.id))
                      .Set("summary", json::Value::String(pending.request.summary))
                      .Set("host", json::Value::String(pending.request.host))
                      .Set("trusted_allowed", json::Value::Bool(pending.request.trusted_allowed)));
    }
    return list;
}

json::Value BrowserWindow::TrustedSitesJson() const {
    json::Value list = json::Value::MakeArray();
    for (const std::string& host : prefs_.agent_trusted_sites) {
        list.Push(json::Value::String(host));
    }
    return list;
}

void BrowserWindow::HandleConfirmMessage(const json::Value& message) {
    const std::string_view answer = message.StringOr("answer", "");
    std::optional<agent::ConfirmationAnswer> parsed;
    if (answer == "once") {
        parsed = agent::ConfirmationAnswer::kAllowOnce;
    } else if (answer == "task") {
        parsed = agent::ConfirmationAnswer::kAllowForTask;
    } else if (answer == "trust") {
        parsed = agent::ConfirmationAnswer::kTrustSite;
    } else if (answer == "cancel") {
        parsed = agent::ConfirmationAnswer::kCancel;
    }
    if (parsed.has_value()) {
        AnswerConfirmation(message.StringOr("id", ""), *parsed);
    }
}

void BrowserWindow::RemoveTrustedSite(std::string_view host) {
    const std::vector<std::string> normalized = NormalizeTrustedSites({std::string(host)});
    if (normalized.empty() || std::erase(prefs_.agent_trusted_sites, normalized.front()) == 0) {
        return;
    }
    SavePrefs();
    ScheduleLocalPagesRender();
}

// Pushes the provider, task progress, activity and shortcut keycap to Native's Agent entry.
void BrowserWindow::RefreshAgentEntry() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || chrome_ == nullptr) {
        return;
    }
    AgentEntryStatus status;
    const bool needs_you =
        !pending_confirmations_.empty() ||
        (research_.has_value() && research_->awaiting_approval) ||
        (agent_session_ != nullptr && agent_session_->pending_permission_count() != 0);
    if (needs_you) {
        status.activity = AgentActivity::kNeedsYou;
    } else if (agent_session_ != nullptr && agent_session_->busy()) {
        status.activity = AgentActivity::kWorking;
    }
    if (const agent::AgentTask* current = agent_queue_.current(); current != nullptr) {
        const auto clamp_int = [](std::size_t value) {
            return static_cast<int>(std::min<std::size_t>(
                value, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        };
        status.queue_done = clamp_int(current->done);
        status.queue_total = clamp_int(current->total);
    }
    status.queued_count = static_cast<int>(std::min<std::size_t>(
        agent_queue_.queued_count(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (const agent::AgentProvider* provider =
            agent::FindAgentProvider(ResolvedAgent().provider_id)) {
        status.provider_name = std::string(provider->name);
    }
    chrome_->SetAgentEntryStatus(std::move(status));
    const std::optional<KeyBinding> binding = keymap_.Binding(KeyAction::kToggleAgentPanel);
#if defined(__APPLE__)
    constexpr bool kMac = true;
#else
    constexpr bool kMac = false;
#endif
    chrome_->SetAgentShortcutLabel(binding.has_value() ? FormatKeyGlyphs(*binding, kMac)
                                                       : std::string());
}

void BrowserWindow::ApplyContentBlockerPolicy() {
    if (content_blocker_ != nullptr) {
        std::vector<std::string> allowlist;
        allowlist.reserve(prefs_.content_blocker_allowlist.size());
        for (const std::string& entry : prefs_.content_blocker_allowlist) {
            std::string normalized = NormalizeAllowlistSite(entry);
            if (!normalized.empty()) {
                allowlist.push_back(std::move(normalized));
            }
        }
        std::sort(allowlist.begin(), allowlist.end());
        allowlist.erase(std::unique(allowlist.begin(), allowlist.end()), allowlist.end());
        content_blocker_->SetPolicy(prefs_.block_ads, prefs_.block_trackers, std::move(allowlist));
    }
}

json::Value BrowserWindow::ContentBlockerStateJson() const {
    using json::Value;
    const ContentBlockerStatus status =
        content_blocker_ != nullptr ? content_blocker_->status() : ContentBlockerStatus{};
    Value allowlist = Value::MakeArray();
    for (const std::string& site : prefs_.content_blocker_allowlist) {
        allowlist.Push(Value::String(site));
    }
    return Value::MakeObject()
        .Set("block_ads", Value::Bool(prefs_.block_ads))
        .Set("block_trackers", Value::Bool(prefs_.block_trackers))
        .Set("compiled_in", Value::Bool(status.compiled_in))
        .Set("ready", Value::Bool(status.ready))
        .Set("updating", Value::Bool(status.updating))
        .Set("ads_rules", Value::Int(status.ads_rule_count))
        .Set("tracker_rules", Value::Int(status.tracker_rule_count))
        .Set("last_updated", Value::Int(status.last_updated))
        .Set("error", Value::String(status.error))
        .Set("allowlist", std::move(allowlist));
}

void BrowserWindow::RecordBlockedRequest(int browser_identifier) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    for (Space& space : spaces_) {
        for (Tab& tab : space.tabs()) {
            const CefRefPtr<CefBrowser> browser = tab.browser();
            if (browser != nullptr && browser->GetIdentifier() == browser_identifier) {
                RecordBlockedRequestForTab(tab.id());
                return;
            }
        }
    }
}

void BrowserWindow::RecordBlockedRequestForTab(TabId tab_id) {
    CEF_REQUIRE_UI_THREAD();
    Tab* tab = closing_ ? nullptr : FindTabById(tab_id);
    if (tab == nullptr) {
        return;
    }
    tab->IncrementBlockedRequests();
    if (active_tab() == tab) {
        RefreshAddressIndicators();
    }
    ScheduleLocalPagesRender();
}

bool BrowserWindow::ReaderAvailableForActiveTab() const {
    const Tab* tab = DisplayedTab();
    return tab != nullptr && agent_host_ != nullptr &&
           (ReaderActiveFor(*tab) || IsReaderEligibleUrl(tab->navigation_state().snapshot().url));
}

// The agent works on the active tab (the tools default to it), so its state is the window's
// agent state: a prompt waiting for an answer beats a running turn.
AgentTabState BrowserWindow::AgentStateForActiveTab() const {
    // A confirmation card waiting on the active tab beats everything else, even without a session
    // (an external MCP agent has none).
    if (const Tab* active = DisplayedTab(); active != nullptr) {
        for (const PendingConfirmation& pending : pending_confirmations_) {
            if (pending.tab_id == active->id()) {
                return AgentTabState::kWaitingForYou;
            }
        }
    }
    if (agent_session_ == nullptr || DisplayedTab() == nullptr || !agent_session_->busy()) {
        return AgentTabState::kIdle;
    }
    if (agent_session_->pending_permission_count() != 0) {
        return AgentTabState::kWaitingForYou;
    }
    return AgentTabState::kReading;
}

void BrowserWindow::RefreshAddressIndicators() {
    const Tab* tab = DisplayedTab();
    const AddressIndicators next =
        ComputeAddressIndicators(tab != nullptr ? tab->blocked_requests() : 0,
                                 ReaderAvailableForActiveTab(), AgentStateForActiveTab());
    if (next == address_indicators_) {
        return;
    }
    address_indicators_ = next;
    DeliverAddressIndicators();
}

void BrowserWindow::DeliverAddressIndicators() {
    if (chrome_ != nullptr) {
        chrome_->SetAddressIndicators(address_indicators_);
    }
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

void BrowserWindow::OnOverlayFocusRingChanged() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || window_ == nullptr) {
        return;
    }
    // The welcome and the palette share one neutral ring while shown; hiding either hands the
    // window's ring back to the address field (ApplyAddressFocusColors decides which).
    ApplyAddressFocusColors(window_, ResolvedTokens(ResolvedChromeTheme()));
    window_->ThemeChanged();
    ApplyPageViewVisibility();
}

bool BrowserWindow::OverlayFocusRingShown() const {
    return (welcome_ != nullptr && welcome_->visible()) ||
           (command_palette_ != nullptr && command_palette_->visible());
}

void BrowserWindow::ApplyPageViewVisibility() {
    if (chrome_ == nullptr) {
        return;
    }
    // Chromium's browser compositor can sit above a CefWindow overlay on macOS, so Native hides the
    // attached page views behind the welcome scrim. The chrome and the welcome card stay laid out.
    const bool welcome_visible = welcome_ != nullptr && welcome_->visible();
    const bool hidden =
        WelcomeHidesPageViews(ResolvedTokens(ResolvedChromeTheme()).look, welcome_visible);
    for (const CefRefPtr<CefBrowserView>& view : chrome_->visible_browser_views()) {
        if (view != nullptr) {
            view->SetVisible(!hidden);
        }
    }
}

void BrowserWindow::OnWelcomeCompleted(const std::vector<ImportSource>& sources) {
    CEF_REQUIRE_UI_THREAD();
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
        static_cast<void>(RunBookmarkMutation(
            [this, items = *items] { return bookmarks_.Import("Imported", items); }));
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
    const ValidatedAddress address =
        address_validator_ ? address_validator_(bookmark.url) : ParseAndValidate(bookmark.url);
    if (!address.is_valid()) {
        return;
    }
    SubmitPaletteUrl(bookmark.url, address);
}

void BrowserWindow::OpenHistory(const PaletteHistoryEntry& history) {
    CEF_REQUIRE_UI_THREAD();
    const ValidatedAddress address = ParseAndValidate(history.url);
    if (address.is_valid()) {
        SubmitPaletteUrl(history.url, address);
    }
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
    Tab* tab = DisplayedTab();
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
    SetSidebarExpanded(!sidebar_state_.expanded());
}

void BrowserWindow::SetSidebarExpanded(bool expanded) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const bool address_was_editing = address_bar_model_.snapshot().mode != AddressBarMode::kResting;
    sidebar_state_.SetExpanded(expanded);
    ApplySidebarState();
    if (!expanded && address_was_editing) {
        // The address field just left the layout; the keyboard returns to the page.
        CancelAddressEditing();
        FocusBrowserView();
    }
    // Only the persistent choice is saved (a transient expansion never is), so the next
    // launch starts the way the user last left the sidebar.
    if (prefs_.sidebar_collapsed != !expanded) {
        prefs_.sidebar_collapsed = !expanded;
        SavePrefs();
    }
}

void BrowserWindow::BeginTransientSidebarExpand() {
    if (!closing_ && sidebar_state_.BeginTransientExpand()) {
        ApplySidebarState();
    }
}

void BrowserWindow::ScheduleTransientSidebarCollapse() {
    constexpr int kTransientCollapseGraceMs = 200;
    if (closing_ || !sidebar_state_.transient_expanded()) {
        return;
    }
    CefPostDelayedTask(
        TID_UI,
        CefCreateClosureTask(base::BindOnce(&BrowserWindow::FinishTransientSidebarCollapse,
                                            CefRefPtr<BrowserWindow>(this))),
        kTransientCollapseGraceMs);
}

void BrowserWindow::FinishTransientSidebarCollapse() {
    // A new edit started inside the grace period keeps the sidebar open.
    if (address_bar_model_.snapshot().mode == AddressBarMode::kResting) {
        EndTransientSidebarExpand();
    }
}

void BrowserWindow::EndTransientSidebarExpand() {
    if (!closing_ && sidebar_state_.EndTransientExpand()) {
        ApplySidebarState();
        FocusBrowserView();
    }
}

void BrowserWindow::ApplySidebarState() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    if (chrome_ != nullptr) {
        chrome_->SetSidebarExpanded(sidebar_state_.expanded());
    }
    // Keep the published snapshot's rail/content split in step with the expand
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
    const bool sidebar_right = prefs_.appearance.sidebar_side == SidebarSide::kRight;
    chrome_snapshot_.rail_bounds.x = sidebar_right ? std::max(0, window_width - rail_width) : 0;
    chrome_snapshot_.content_bounds.x = sidebar_right ? panel_width : rail_width;
    chrome_snapshot_.content_bounds.width = std::max(0, window_width - rail_width - panel_width);
    ScheduleAnchoredOverlayBounds();
    ApplyDragRegions();
    SchedulePageCardRefresh();
}

std::vector<void*> BrowserWindow::NativeContentHandles() const {
    std::vector<void*> handles;
    if (chrome_ == nullptr) {
        return handles;
    }
    for (const CefRefPtr<CefBrowserView>& view : chrome_->visible_browser_views()) {
        CefRefPtr<CefBrowser> browser = view != nullptr ? view->GetBrowser() : nullptr;
        if (browser != nullptr) {
            handles.push_back(NativeHandlePointer(browser->GetHost()->GetWindowHandle()));
        }
    }
    return handles;
}

void BrowserWindow::RefreshPageCards() {
    if (closing_ || chrome_ == nullptr) {
        return;
    }
    const ChromeTokens tokens = ResolvedTokens(chrome_snapshot_.theme);
    const std::vector<void*> handles = NativeContentHandles();
    ApplyPageCardStyle(handles, {.radius_dip = tokens.card_radius_dip,
                                 .shadow = tokens.card_shadow,
                                 .canvas = tokens.canvas,
                                 .inset_dip = tokens.card_inset_dip,
                                 .dark = chrome_snapshot_.theme == ChromeTheme::kDark});
    ApplyCornerPatches(handles, corner_patches_, tokens.canvas);
}

void BrowserWindow::SchedulePageCardRefresh() {
    if (closing_ || window_ == nullptr || chrome_ == nullptr) {
        return;
    }
    RefreshPageCards();
    if (page_card_refresh_pending_) {
        return;
    }
    page_card_refresh_pending_ = true;
    for (const int64_t delay_ms : {60, 400}) {
        CefPostDelayedTask(TID_UI,
                           CefCreateClosureTask(base::BindOnce(
                               [](CefRefPtr<BrowserWindow> self, bool last) {
                                   if (last) {
                                       self->page_card_refresh_pending_ = false;
                                   }
                                   self->RefreshPageCards();
                               },
                               CefRefPtr<BrowserWindow>(this), delay_ms == 400)),
                           delay_ms);
    }
}

void BrowserWindow::ApplyDragRegions() {
    if (!FramelessWindowSupported() || window_ == nullptr || chrome_ == nullptr) {
        return;
    }
    const ChromeTokens tokens = ResolvedTokens(chrome_snapshot_.theme);
    std::vector<CefDraggableRegion> regions;
    for (const BrowserChrome::DragRegion& region : BrowserChrome::DragRegionsForBounds(
             {.x = 0, .y = 0, .width = window_width_dip_, .height = window_height_dip_}, tokens,
             sidebar_state_.expanded(), agent_panel_open())) {
        if (region.bounds.width <= 0 || region.bounds.height <= 0) {
            continue;
        }
        CefDraggableRegion draggable;
        draggable.bounds =
            CefRect(region.bounds.x, region.bounds.y, region.bounds.width, region.bounds.height);
        draggable.draggable = region.draggable ? 1 : 0;
        regions.push_back(draggable);
    }
    window_->SetDraggableRegions(regions);
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
