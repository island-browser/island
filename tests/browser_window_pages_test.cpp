// Headless coverage of the Settings / All-tabs / import / shortcut seams of
// BrowserWindow. The CEF view types must be complete before browser_window.h.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "agent/plan_fixture.h"
#include "browser_window.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "island_version.h"
#include "json_util.h"
#include "keymap.h"
#include "plan_dev_fixture.h"
#include "plan_edits.h"
#include "sha256.h"
#include "updater.h"

namespace island {
namespace {

using json::Value;

ValidatedAddress FakeValidate(std::string_view text) {
    const bool web = text.rfind("https://", 0) == 0 || text.rfind("http://", 0) == 0;
    if (!web || text.find(' ') != std::string_view::npos) {
        return {.url = {}, .error = AddressError::kNotAbsolute};
    }
    return {.url = std::string(text), .error = std::nullopt};
}

CefRefPtr<BrowserWindow> MakeWindow() {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,x");
    window->SetAddressValidatorForTest(&FakeValidate);
    return window;
}

const Space& SpaceAt(const BrowserWindow& window, std::size_t index) {
    return window.spaces()[index];
}

TEST(BrowserWindowPagesTest, KeymapEditsTakeEffectAndShortcutsRunActions) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kNewTab, ParseKeyBinding("Mod+Shift+Y")));
    EXPECT_EQ(window->keymap().ActionFor(*ParseKeyBinding("Mod+Shift+Y")), KeyAction::kNewTab);
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kCloseTab, std::nullopt));
    EXPECT_FALSE(window->keymap().Binding(KeyAction::kCloseTab).has_value());
    ASSERT_TRUE(window->ResetKeyBinding(KeyAction::kNewTab));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+T"));
    window->ResetKeymap();
    EXPECT_EQ(window->keymap(), Keymap::Defaults());

    window->RunKeyAction(KeyAction::kNewTab);
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    window->RunKeyAction(KeyAction::kTogglePinTab);
    EXPECT_EQ(SpaceAt(*window, 0).pinned_count(), 1U);
    window->RunKeyAction(KeyAction::kNewSpace);
    EXPECT_EQ(window->space_count(), 2U);
    // Page toggles are no-ops without chrome, never crashes.
    window->RunKeyAction(KeyAction::kSettings);
    window->RunKeyAction(KeyAction::kTabOverview);
    EXPECT_FALSE(window->internal_page().has_value());
}

TEST(BrowserWindowPagesTest, SettingsStateListsShortcutsSourcesAndConflicts) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kSettings, ParseKeyBinding("Mod+T")));
    const auto state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* settings = state->FindMember("settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->StringOr("theme_pref", ""), "system");
    const Value* shortcuts = settings->FindMember("shortcuts");
    ASSERT_EQ(shortcuts->array_val.size(), kKeyActionCount);
    int conflicts = 0;
    for (const Value& row : shortcuts->array_val) {
        if (row.BoolOr("conflict", false)) ++conflicts;
        if (row.StringOr("id", "") == "settings") {
            EXPECT_EQ(row.StringOr("binding", ""), "Mod+T");
            EXPECT_EQ(row.StringOr("default_binding", ""), "Mod+,");
        }
    }
    EXPECT_EQ(conflicts, 2);  // new_tab and settings
    const Value* sources = settings->FindMember("import")->FindMember("sources");
    bool has_arc = false;
    for (const Value& source : sources->array_val) {
        if (source.StringOr("id", "") == "arc") has_arc = true;
    }
    EXPECT_TRUE(has_arc);
    EXPECT_FALSE(settings->FindMember("endpoint")->BoolOr("running", true));
    EXPECT_NE(state->FindMember("theme"), nullptr);
}

TEST(BrowserWindowPagesTest, NativeLightThemeJsonExposesPageTokens) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"set_look","look":"native"})"));
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"set_theme","value":"light"})"));

    const std::optional<Value> state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* theme = state->FindMember("theme");
    ASSERT_NE(theme, nullptr);
    EXPECT_EQ(theme->StringOr("selection", ""), "#0a64d8");
    const Value* branch = theme->FindMember("branch");
    ASSERT_NE(branch, nullptr);
    ASSERT_TRUE(branch->IsArray());
    ASSERT_EQ(branch->array_val.size(), 4U);
    for (const Value& color : branch->array_val) {
        EXPECT_TRUE(color.IsString());
    }
}

TEST(BrowserWindowPagesTest, SettingsMessagesUpdateTheWindow) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_theme"))
                                   .Set("value", Value::String("dark")));
    EXPECT_EQ(window->theme_preference(), ThemePreference::kDark);
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_binding"))
                                   .Set("action", Value::String("new_tab"))
                                   .Set("binding", Value::String("Mod+Alt+T")));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Alt+T"));
    // An invalid binding leaves the shortcut alone and surfaces a notice.
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_binding"))
                                   .Set("action", Value::String("new_tab"))
                                   .Set("binding", Value::String("T")));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Alt+T"));
    EXPECT_NE(
        json::Parse(window->SettingsStateJson())->FindMember("settings")->StringOr("message", ""),
        "");
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject().Set("type", Value::String("reset_shortcuts")));
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
}

TEST(BrowserWindowPagesTest, RecordReplaceAndResetShortcutMessages) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"record_shortcut","action":"new_tab",)"
                                            R"("binding":"Mod+B"})"));
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
    auto state = json::Parse(window->SettingsStateJson());
    const Value* pending = state->FindMember("settings")->FindMember("shortcut_conflict");
    ASSERT_NE(pending, nullptr);
    EXPECT_EQ(pending->StringOr("conflict", ""), "Toggle sidebar");
    EXPECT_EQ(pending->StringOr("action", ""), "new_tab");
    EXPECT_EQ(pending->StringOr("binding", ""), "Mod+B");

    // A free chord binds at once and clears any pending conflict.
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"record_shortcut","action":"new_tab",)"
                                            R"("binding":"Mod+Shift+Y"})"));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Shift+Y"));
    state = json::Parse(window->SettingsStateJson());
    EXPECT_EQ(state->FindMember("settings")->FindMember("shortcut_conflict"), nullptr);

    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"replace_shortcut","action":"new_tab",)"
                                            R"("binding":"Mod+B"})"));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+B"));
    EXPECT_FALSE(window->keymap().Binding(KeyAction::kToggleSidebar).has_value());
    EXPECT_TRUE(window->keymap().Conflicts().empty());

    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               *json::Parse(R"({"type":"reset_shortcuts"})"));
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
}

TEST(BrowserWindowPagesTest, SettingsPrivacyMessagesUpdateNativeBlockerPolicy) {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("");
    window->OnLocalPageMessage(
        LocalPageKind::kSettings,
        *json::Parse(R"({"type":"set_content_blocker_pref","key":"ads","value":false})"));
    window->OnLocalPageMessage(
        LocalPageKind::kSettings,
        *json::Parse(R"({"type":"add_content_blocker_site","site":" HTTPS://Example.COM/path "})"));

    std::optional<Value> state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* privacy = state->FindMember("settings")->FindMember("privacy");
    ASSERT_NE(privacy, nullptr);
    ASSERT_TRUE(privacy->FindMember("block_ads")->IsBool());
    ASSERT_TRUE(privacy->FindMember("block_trackers")->IsBool());
    EXPECT_FALSE(privacy->FindMember("block_ads")->bool_val);
    EXPECT_TRUE(privacy->FindMember("block_trackers")->bool_val);
    ASSERT_EQ(privacy->FindMember("allowlist")->array_val.size(), 1U);
    EXPECT_EQ(privacy->FindMember("allowlist")->array_val[0].string_val, "example.com");

    window->OnLocalPageMessage(
        LocalPageKind::kSettings,
        *json::Parse(R"({"type":"remove_content_blocker_site","site":"example.com"})"));
    state = json::Parse(window->SettingsStateJson());
    privacy = state->FindMember("settings")->FindMember("privacy");
    EXPECT_TRUE(privacy->FindMember("allowlist")->array_val.empty());
}

TEST(BrowserWindowPagesTest, OverviewOperationsWorkAcrossSpaces) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->OpenNewTab("https://a.test/"));
    ASSERT_TRUE(window->OpenNewTab("https://b.test/"));
    ASSERT_TRUE(window->CreateSpace("Research"));
    ASSERT_TRUE(window->OpenNewTab("https://r.test/"));
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    ASSERT_EQ(SpaceAt(*window, 0).tab_count(), 3U);

    // Pin the last tab of space 0: it joins the pinned tray at the front.
    ASSERT_TRUE(window->SetTabPinnedAt(0, 2, true));
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), "https://b.test/");
    // A regular tab cannot be dragged into the pinned tray.
    ASSERT_TRUE(window->MoveTabWithinSpace(0, 2, 0));
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), "https://b.test/");
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[1].startup_url(), "https://a.test/");

    // Close a tab in the inactive space without switching to it.
    ASSERT_TRUE(window->CloseTabAt(1, 0));
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(SpaceAt(*window, 1).tab_count(), 1U);

    // Moving reopens the URL in the target space and keeps the pin.
    ASSERT_TRUE(window->MoveTabToSpace(0, 0, 1));
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    ASSERT_EQ(SpaceAt(*window, 1).tab_count(), 2U);
    EXPECT_EQ(SpaceAt(*window, 1).tabs()[0].startup_url(), "https://b.test/");
    EXPECT_TRUE(SpaceAt(*window, 1).tabs()[0].pinned());
    EXPECT_FALSE(window->MoveTabToSpace(0, 0, 0));
    EXPECT_FALSE(window->CloseTabAt(9, 0));

    // The overview state mirrors the model.
    const auto overview = json::Parse(window->TabOverviewStateJson());
    ASSERT_TRUE(overview.has_value());
    const Value& spaces = *overview->FindMember("overview")->FindMember("spaces");
    ASSERT_EQ(spaces.array_val.size(), 2U);
    EXPECT_EQ(spaces.array_val[1].StringOr("name", ""), "Research");
    EXPECT_EQ(spaces.array_val[1].FindMember("tabs")->array_val.size(), 2U);
    EXPECT_TRUE(spaces.array_val[0].BoolOr("active", false));

    // Activating from the overview switches space and tab.
    ASSERT_TRUE(window->ActivateTabAt(1, 1));
    EXPECT_EQ(window->active_space_index(), 1U);
    EXPECT_EQ(SpaceAt(*window, 1).active_tab_index(), 1U);
}

TEST(BrowserWindowPagesTest, ArcSpacesImportAsPinnedSpaces) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    std::size_t tabs = 0;
    const std::size_t added = window->AddImportedSpaces(
        {{.name = "Work",
          .color_argb = 0xFF3366FFU,
          .pinned_tabs = {{"Linear", "https://linear.app/"}, {"Bad", "javascript:alert(1)"}}},
         {.name = "Empty", .color_argb = std::nullopt, .pinned_tabs = {}}},
        &tabs);
    EXPECT_EQ(added, 1U);
    EXPECT_EQ(tabs, 1U);
    ASSERT_EQ(window->space_count(), 2U);
    const Space& work = SpaceAt(*window, 1);
    EXPECT_EQ(work.name(), "Work");
    EXPECT_EQ(work.color().argb, 0xFF3366FFU);
    ASSERT_EQ(work.tab_count(), 1U);
    EXPECT_TRUE(work.tabs()[0].pinned());
    EXPECT_EQ(window->active_space_index(), 0U);  // importing never steals focus
    EXPECT_EQ(window->ImportFromBrowser("netscape"), "Unknown import source.");
}

// Answers updater requests from canned bodies keyed by a URL substring.
class CannedFetcher final : public update::UpdateFetcher {
  public:
    explicit CannedFetcher(std::vector<std::pair<std::string, std::string>> bodies)
        : bodies_(std::move(bodies)) {}
    void Start(update::FetchRequest request, Progress, Done done) override {
        update::FetchResult result;
        for (const auto& [needle, body] : bodies_) {
            if (request.url.find(needle) == std::string::npos) continue;
            result.ok = true;
            result.http_status = 200;
            result.size = static_cast<std::int64_t>(body.size());
            result.sha256_hex = Sha256::HexOf(body);
            if (request.destination.empty()) {
                result.body = body;
            } else {
                std::ofstream(request.destination, std::ios::binary) << body;
            }
            break;
        }
        if (!result.ok) result.http_status = 404;
        done(std::move(result));
    }
    void Cancel() override {}

  private:
    std::vector<std::pair<std::string, std::string>> bodies_;
};

Value UpdateState(const BrowserWindow& window) {
    const std::optional<Value> state = json::Parse(window.SettingsStateJson());
    const Value* settings = state.has_value() ? state->FindMember("settings") : nullptr;
    const Value* update = settings != nullptr ? settings->FindMember("update") : nullptr;
    return update != nullptr ? *update : Value::MakeObject();
}

void SendSettings(BrowserWindow& window, std::string_view json_text) {
    const std::optional<Value> message = json::Parse(json_text);
    ASSERT_TRUE(message.has_value()) << json_text;
    window.OnLocalPageMessage(LocalPageKind::kSettings, *message);
}

TEST(BrowserWindowPagesTest, SettingsUpdatesStateAndPreferences) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    Value update = UpdateState(*window);
    EXPECT_EQ(update.StringOr("status", ""), "idle");
    EXPECT_EQ(update.StringOr("current_version", ""), ISLAND_VERSION_STRING);
    EXPECT_TRUE(update.BoolOr("auto_check", false));
    EXPECT_FALSE(update.BoolOr("include_prereleases", true));
    // The test binary runs from a build tree, which never installs updates.
    EXPECT_FALSE(update.BoolOr("can_install", true));

    SendSettings(*window, R"({"type":"set_update_pref","key":"auto_check","value":false})");
    SendSettings(*window, R"({"type":"set_update_pref","key":"include_prereleases","value":true})");
    SendSettings(*window, R"({"type":"set_update_pref","key":"bogus","value":true})");
    SendSettings(*window, R"({"type":"set_update_pref","key":"auto_check","value":"yes"})");
    update = UpdateState(*window);
    EXPECT_FALSE(update.BoolOr("auto_check", true));
    EXPECT_TRUE(update.BoolOr("include_prereleases", false));
}

TEST(BrowserWindowPagesTest, SettingsUpdateMessagesCheckDownloadAndOpenNotes) {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        ("island_window_update_" + std::to_string(update::CurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(base / "island", error);

    const std::string archive = "release archive";
    const std::string name = update::ArchiveName("99.0.0", "linux64");
    const std::string url = "https://github.com/island-browser/island/releases/download/v99.0.0/";
    const std::string releases =
        R"j([{"tag_name":"nightly","draft":false,"prerelease":true,"assets":[]},)j"
        R"j({"tag_name":"v99.0.0","name":"Island 99.0.0 (unsigned)","draft":false,)j"
        R"j("prerelease":true,"html_url":"https://github.com/island-browser/island/releases/tag/v99.0.0",)j"
        R"j("assets":[{"name":")j" +
        name + R"j(","size":)j" + std::to_string(archive.size()) +
        R"j(,"browser_download_url":")j" + url + name +
        R"j("},{"name":"SHA256SUMS.txt","size":100,"browser_download_url":")j" + url +
        R"j(SHA256SUMS.txt"}]}])j";

    CefRefPtr<BrowserWindow> window = MakeWindow();
    update::Updater::Config config;
    config.current_version = ISLAND_VERSION_STRING;
    config.target = "linux64";
    config.install.support = update::InstallSupport::kSupported;
    config.install.platform = update::Platform::kLinux;
    config.install.install_root = base / "island";
    config.install.executable_name = "island_browser";
    config.install.staging_dir = base / ".island.island-update";
    window->ConfigureUpdater(
        config, std::make_unique<CannedFetcher>(std::vector<std::pair<std::string, std::string>>{
                    {"api.github.com/repos/island-browser/island/releases", releases},
                    {"SHA256SUMS.txt", Sha256::HexOf(archive) + "  " + name + "\n"},
                    {name, archive}}));

    SendSettings(*window, R"({"type":"check_updates"})");
    Value update = UpdateState(*window);
    EXPECT_EQ(update.StringOr("status", ""), "available");
    EXPECT_EQ(update.StringOr("latest_version", ""), "99.0.0");
    EXPECT_EQ(update.StringOr("release_url", ""),
              "https://github.com/island-browser/island/releases/tag/v99.0.0");
    EXPECT_TRUE(update.BoolOr("can_install", false));

    SendSettings(*window, R"({"type":"install_update"})");
    update = UpdateState(*window);
    EXPECT_EQ(update.StringOr("status", ""), "ready");
    EXPECT_TRUE(std::filesystem::exists(config.install.staging_dir / name));

    // Release notes open the updater's own URL in a new tab; the page cannot
    // supply one.
    SendSettings(*window, R"({"type":"open_release_notes","url":"https://evil.example/"})");
    ASSERT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[1].startup_url(),
              "https://github.com/island-browser/island/releases/tag/v99.0.0");
    std::filesystem::remove_all(base, error);
}

// Clears ISLAND_AGENT_COMMAND for one test and restores it afterwards.
class ScopedNoAgentEnv {
  public:
    ScopedNoAgentEnv() {
        const char* saved = std::getenv("ISLAND_AGENT_COMMAND");
        if (saved != nullptr) saved_ = saved;
        Set(nullptr);
    }
    ~ScopedNoAgentEnv() { Set(saved_ ? saved_->c_str() : nullptr); }

  private:
    static void Set(const char* value) {
#if defined(_WIN32)
        _putenv_s("ISLAND_AGENT_COMMAND", value != nullptr ? value : "");
#else
        if (value != nullptr) {
            setenv("ISLAND_AGENT_COMMAND", value, 1);
        } else {
            unsetenv("ISLAND_AGENT_COMMAND");
        }
#endif
    }
    std::optional<std::string> saved_;
};

// A directory holding fake `npx` and `opencode` executables.
std::filesystem::path FakeAgentBin() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("island_window_agents_" + std::to_string(update::CurrentProcessId()));
    std::filesystem::create_directories(dir);
    for (const char* name : {"npx", "opencode"}) {
        std::ofstream(dir / name) << "#!/bin/sh\n";
        std::filesystem::permissions(dir / name, std::filesystem::perms::owner_all);
    }
    return dir;
}

const Value* FindProvider(const Value& providers, std::string_view id) {
    for (const Value& provider : providers.array_val) {
        if (provider.StringOr("id", "") == id) return &provider;
    }
    return nullptr;
}

Value SettingsAgent(const BrowserWindow& window) {
    const std::optional<Value> state = json::Parse(window.SettingsStateJson());
    return *state->FindMember("settings")->FindMember("agent");
}

TEST(BrowserWindowPagesTest, SettingsListsAgentProvidersAndSwitchesThem) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetAgentSearchDirsForTest({FakeAgentBin()});

    Value agent = SettingsAgent(*window);
    EXPECT_EQ(agent.StringOr("provider", ""), "claude");
    EXPECT_FALSE(agent.BoolOr("env_override", true));
    EXPECT_EQ(agent.StringOr("effective_command", ""),
              "npx -y @agentclientprotocol/claude-agent-acp");
    EXPECT_EQ(agent.StringOr("default_command", ""),
              "npx -y @agentclientprotocol/claude-agent-acp");
    const Value& providers = *agent.FindMember("providers");
    ASSERT_EQ(providers.array_val.size(), agent::AgentProviders().size());
    EXPECT_TRUE(FindProvider(providers, "claude")->BoolOr("available", false));
    EXPECT_TRUE(FindProvider(providers, "opencode")->BoolOr("available", false));
    const Value* gemini = FindProvider(providers, "gemini");
    ASSERT_NE(gemini, nullptr);
    EXPECT_FALSE(gemini->BoolOr("available", true));
    EXPECT_EQ(gemini->StringOr("name", ""), "Gemini CLI");
    EXPECT_EQ(gemini->StringOr("command", ""), "gemini --acp");
    EXPECT_EQ(gemini->StringOr("install", ""), "npm i -g @google/gemini-cli");

    SendSettings(*window, R"({"type":"set_agent_provider","id":"opencode"})");
    EXPECT_EQ(window->agent_provider(), "opencode");
    EXPECT_EQ(window->ResolvedAgentCommand(), "opencode acp");
    SendSettings(*window, R"({"type":"set_agent_provider","id":"not-a-provider"})");
    EXPECT_EQ(window->agent_provider(), "opencode");

    // The custom command is kept beside the selection and used once picked.
    SendSettings(*window, R"({"type":"set_agent_command","command":"  my-agent --acp "})");
    EXPECT_EQ(window->custom_agent_command(), "my-agent --acp");
    EXPECT_EQ(window->ResolvedAgentCommand(), "opencode acp");
    SendSettings(*window, R"({"type":"set_agent_provider","id":"custom"})");
    EXPECT_EQ(window->ResolvedAgentCommand(), "my-agent --acp");
    agent = SettingsAgent(*window);
    EXPECT_EQ(agent.StringOr("provider", ""), "custom");
    EXPECT_EQ(agent.StringOr("command", ""), "my-agent --acp");
    const Value* custom = FindProvider(*agent.FindMember("providers"), "custom");
    EXPECT_EQ(custom->StringOr("command", ""), "my-agent --acp");
    EXPECT_FALSE(custom->BoolOr("available", true));

    // Docs open the registry's URL only.
    SendSettings(*window,
                 R"({"type":"open_agent_docs","id":"goose","url":"https://evil.example/"})");
    SendSettings(*window, R"({"type":"open_agent_docs","id":"bogus"})");
    ASSERT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[1].startup_url(), "https://block.github.io/goose/");
}

void SendStart(BrowserWindow& window, std::string_view json_text) {
    const std::optional<Value> message = json::Parse(json_text);
    ASSERT_TRUE(message.has_value()) << json_text;
    window.OnLocalPageMessage(LocalPageKind::kStart, *message);
}

TEST(BrowserWindowPagesTest, AddressBarSearchesInsteadOfRejectingNonUrls) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(window->search_engine().key, "google");
    EXPECT_EQ(window->ResolveAddressInput("google").url, "https://www.google.com/search?q=google");
    EXPECT_EQ(window->ResolveAddressInput("  island browser  ").url,
              "https://www.google.com/search?q=island%20browser");
    EXPECT_TRUE(window->ResolveAddressInput("what is 2+2?").is_valid());
    // Hosts get https:// and go through the validator; explicit URLs are unchanged.
    EXPECT_EQ(window->ResolveAddressInput("example.com/a").url, "https://example.com/a");
    EXPECT_EQ(window->ResolveAddressInput("http://example.com/").url, "http://example.com/");
    // Validation still rejects what it always rejected.
    EXPECT_EQ(window->ResolveAddressInput("javascript:alert(1)").error, AddressError::kNotAbsolute);
    EXPECT_FALSE(window->ResolveAddressInput("").is_valid());

    SendSettings(*window, R"({"type":"set_search_engine","id":"yandex"})");
    EXPECT_EQ(window->ResolveAddressInput("cats").url, "https://yandex.com/search/?text=cats");
    SendSettings(*window, R"({"type":"set_search_engine","id":"bing"})");
    EXPECT_EQ(window->search_engine().key, "yandex");
    SendSettings(*window, R"({"type":"set_search_engine","id":"duckduckgo"})");
    EXPECT_EQ(window->ResolveAddressInput("cats").url, "https://duckduckgo.com/?q=cats");

    const std::optional<Value> state = json::Parse(window->SettingsStateJson());
    const Value* search = state->FindMember("settings")->FindMember("search");
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->StringOr("engine", ""), "duckduckgo");
    EXPECT_EQ(search->FindMember("engines")->array_val.size(), 3U);
}

TEST(BrowserWindowPagesTest, HistorySettingsToggleAndClearAreExposedToTheLocalPage) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    std::optional<Value> state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* history = state->FindMember("settings")->FindMember("history");
    ASSERT_NE(history, nullptr);
    EXPECT_TRUE(history->BoolOr("remember", false));

    SendSettings(*window, R"({"type":"set_remember_history","value":false})");
    state = json::Parse(window->SettingsStateJson());
    EXPECT_FALSE(state->FindMember("settings")->FindMember("history")->BoolOr("remember", true));

    SendSettings(*window, R"({"type":"set_remember_history","value":"no"})");
    state = json::Parse(window->SettingsStateJson());
    EXPECT_FALSE(state->FindMember("settings")->FindMember("history")->BoolOr("remember", true));

    SendSettings(*window, R"({"type":"clear_history"})");
    state = json::Parse(window->SettingsStateJson());
    EXPECT_EQ(state->FindMember("settings")->StringOr("message", ""), "Browsing history cleared.");
}

TEST(BrowserWindowPagesTest, SettingsChoosesTheTabSleepTimeoutAmongTheOfferedValues) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(window->tab_sleep_minutes(), 30);

    SendSettings(*window, R"({"type":"set_tab_sleep","minutes":15})");
    EXPECT_EQ(window->tab_sleep_minutes(), 15);
    SendSettings(*window, R"({"type":"set_tab_sleep","minutes":7})");
    EXPECT_EQ(window->tab_sleep_minutes(), 15);
    SendSettings(*window, R"({"type":"set_tab_sleep","minutes":0})");
    EXPECT_EQ(window->tab_sleep_minutes(), 0);

    const std::optional<Value> state = json::Parse(window->SettingsStateJson());
    const Value* tabs = state->FindMember("settings")->FindMember("tabs");
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->IntOr("sleep_minutes", -1), 0);
}

TEST(BrowserWindowPagesTest, SettingsChoosesPersistedReaderAppearance) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    std::optional<Value> state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* reader = state->FindMember("settings")->FindMember("reader");
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(reader->IntOr("size", -1), 18);
    EXPECT_EQ(reader->StringOr("width", ""), "medium");
    EXPECT_EQ(reader->StringOr("theme", ""), "auto");

    SendSettings(*window,
                 R"({"type":"set_reader_settings","size":22,"width":"wide","theme":"sepia"})");
    state = json::Parse(window->SettingsStateJson());
    reader = state->FindMember("settings")->FindMember("reader");
    EXPECT_EQ(reader->IntOr("size", -1), 22);
    EXPECT_EQ(reader->StringOr("width", ""), "wide");
    EXPECT_EQ(reader->StringOr("theme", ""), "sepia");

    SendSettings(*window,
                 R"({"type":"set_reader_settings","size":999,"width":"huge","theme":"neon"})");
    state = json::Parse(window->SettingsStateJson());
    reader = state->FindMember("settings")->FindMember("reader");
    EXPECT_EQ(reader->IntOr("size", -1), ReaderSettings::kMaxSize);
    EXPECT_EQ(reader->StringOr("width", ""), "medium");
    EXPECT_EQ(reader->StringOr("theme", ""), "auto");
}

TEST(BrowserWindowPagesTest, ASleepingTabKeepsItsPageAndTheActiveTabNeverSleeps) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->OpenNewTab("https://example.com/a"));
    ASSERT_TRUE(window->OpenNewTab("https://example.com/b"));
    const Space& space = window->spaces().front();
    ASSERT_EQ(space.tab_count(), 3U);
    const TabId first = space.tabs()[0].id();
    const TabId active = space.active_tab_id();
    EXPECT_EQ(window->sleeping_tab_count(), 0U);

    // The tab on screen is refused; a background tab sleeps and keeps its URL.
    EXPECT_FALSE(window->SleepTab(active));
    EXPECT_TRUE(window->SleepTab(space.tabs()[1].id(), 0.0, 320.0));
    EXPECT_EQ(window->sleeping_tab_count(), 1U);
    const Tab& slept = window->spaces().front().tabs()[1];
    EXPECT_TRUE(slept.sleeping());
    EXPECT_EQ(slept.startup_url(), "https://example.com/a");
    EXPECT_DOUBLE_EQ(slept.saved_scroll_y(), 320.0);
    EXPECT_FALSE(window->SleepTab(slept.id()));  // already asleep
    EXPECT_FALSE(window->SleepTab(first));       // never loaded a page: nothing to come back to
    EXPECT_EQ(window->sleeping_tab_count(), 1U);
}

TEST(BrowserWindowPagesTest, TheStartPageRemembersItsModeAndStatesTheEngine) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(window->start_mode(), "search");
    SendStart(*window, R"({"type":"set_start_mode","mode":"agent"})");
    EXPECT_EQ(window->start_mode(), "agent");
    SendStart(*window, R"({"type":"set_start_mode","mode":"bogus"})");
    EXPECT_EQ(window->start_mode(), "search");

    SendSettings(*window, R"({"type":"set_search_engine","id":"yandex"})");
    SendStart(*window, R"({"type":"set_start_mode","mode":"agent"})");
    const std::optional<Value> state = json::Parse(window->StartPageStateJson());
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->FindMember("start")->StringOr("mode", ""), "agent");
    EXPECT_EQ(state->FindMember("start")->StringOr("engine", ""), "Yandex");
    EXPECT_NE(state->FindMember("theme"), nullptr);

    // A search with no live tab browser is a harmless no-op that still records the mode.
    SendStart(*window, R"({"type":"search","query":"cats"})");
    EXPECT_EQ(window->start_mode(), "search");
    SendStart(*window, R"({"type":"search","query":"   "})");
    SendStart(*window, R"({"type":"bogus"})");
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 1U);
}

TEST(BrowserWindowPagesTest, AskingTheAgentFromTheStartPageUsesThePanelSendPath) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    // A command that cannot launch, so the headless test never spawns a process.
    SendSettings(*window, R"({"type":"set_agent_command","command":"\"unbalanced"})");
    SendSettings(*window, R"({"type":"set_agent_provider","id":"custom"})");
    EXPECT_EQ(window->agent_session(), nullptr);

    SendStart(*window, R"({"type":"ask_agent","prompt":"   "})");
    EXPECT_EQ(window->agent_session(), nullptr);

    SendStart(*window, R"({"type":"ask_agent","prompt":"summarize my tabs"})");
    EXPECT_EQ(window->start_mode(), "agent");
    ASSERT_NE(window->agent_session(), nullptr);
    // The turn reached AgentSession::Send, which tried to start the agent.
    EXPECT_FALSE(window->agent_session()->error().empty());
    EXPECT_EQ(window->spaces()[0].tab_count(), 1U);
}

void SendAgent(BrowserWindow& window, std::string_view json_text) {
    const std::optional<Value> message = json::Parse(json_text);
    ASSERT_TRUE(message.has_value()) << json_text;
    window.OnLocalPageMessage(LocalPageKind::kAgent, *message);
}

TEST(BrowserWindowPagesTest, AgentPanelSwitchesProvidersWithoutLaunching) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetAgentSearchDirsForTest({FakeAgentBin()});

    std::optional<Value> state = json::Parse(window->AgentPanelStateJson());
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->StringOr("provider", ""), "claude");
    EXPECT_EQ(state->FindMember("providers")->array_val.size(), agent::AgentProviders().size());
    EXPECT_FALSE(state->BoolOr("env_override", true));

    // Picking a provider in the panel persists it and configures the session;
    // an idle session does not launch anything.
    SendAgent(*window, R"({"type":"set_provider","id":"gemini"})");
    EXPECT_EQ(window->agent_provider(), "gemini");
    ASSERT_NE(window->agent_session(), nullptr);
    EXPECT_FALSE(window->agent_session()->running());
    EXPECT_EQ(window->agent_session()->config().command, "gemini --acp");
    EXPECT_EQ(window->agent_session()->config().provider_id, "gemini");
    state = json::Parse(window->AgentPanelStateJson());
    EXPECT_EQ(state->StringOr("provider", ""), "gemini");
    const Value* session = state->FindMember("session");
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->StringOr("provider", ""), "gemini");
    EXPECT_EQ(session->StringOr("agent", ""), "Gemini CLI");
    EXPECT_EQ(session->StringOr("state", ""), "idle");

    // Unknown ids are ignored; "Custom..." asks for Settings (a no-op without
    // chrome, never a crash).
    SendAgent(*window, R"({"type":"set_provider","id":"<script>"})");
    EXPECT_EQ(window->agent_provider(), "gemini");
    SendAgent(*window, R"({"type":"open_settings"})");
    EXPECT_EQ(window->agent_provider(), "gemini");

    // The custom provider without a command reports that instead of launching.
    SendAgent(*window, R"({"type":"set_provider","id":"custom"})");
    EXPECT_EQ(window->agent_session()->config().command, "");
    SendAgent(*window, R"({"type":"send","text":"hello"})");
    EXPECT_FALSE(window->agent_session()->running());
    EXPECT_NE(window->agent_session()->error().find("No agent command is set"), std::string::npos);
}

void SendReport(BrowserWindow& window, std::string_view id, std::string_view json_text) {
    const std::optional<Value> message = json::Parse(json_text);
    ASSERT_TRUE(message.has_value()) << json_text;
    window.HandleReportPageMessage(*message, id);
}

constexpr std::string_view kReportUrl = "data:text/html;charset=utf-8,report";

ResearchOutcome ReportOutcome(std::string markdown) {
    ResearchOutcome outcome;
    outcome.kind = ResearchOutcome::Kind::kReport;
    outcome.markdown = std::move(markdown);
    return outcome;
}

TEST(BrowserWindowPagesTest, ResearchModeIsRememberedAndStatedToTheStartPage) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    SendStart(*window, R"({"type":"set_start_mode","mode":"research"})");
    EXPECT_EQ(window->start_mode(), "research");
    const std::optional<Value> state = json::Parse(window->StartPageStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* start = state->FindMember("start");
    EXPECT_EQ(start->StringOr("mode", ""), "research");
    const Value* research = start->FindMember("research");
    ASSERT_NE(research, nullptr);
    EXPECT_EQ(research->StringOr("state", ""), "idle");
    SendStart(*window, R"({"type":"research","question":"   "})");
    EXPECT_FALSE(window->research_running());
    EXPECT_EQ(window->research_status().state, "idle");
}

TEST(BrowserWindowPagesTest, ResearchSaysSoWhenTheAgentIsNotInstalled) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    const std::filesystem::path empty =
        std::filesystem::temp_directory_path() /
        ("island_no_agents_" + std::to_string(update::CurrentProcessId()));
    std::filesystem::create_directories(empty);
    window->SetAgentSearchDirsForTest({empty});

    SendStart(*window, R"({"type":"research","question":"why is the sky blue"})");
    EXPECT_EQ(window->start_mode(), "research");
    EXPECT_FALSE(window->research_running());
    EXPECT_EQ(window->research_status().state, "not_installed");
    EXPECT_NE(window->research_status().message.find("Settings"), std::string::npos);
    // Nothing was sent: the agent never started.
    ASSERT_NE(window->agent_session(), nullptr);
    EXPECT_FALSE(window->agent_session()->running());
    EXPECT_TRUE(window->agent_session()->transcript().items().empty());

    const std::optional<Value> state = json::Parse(window->StartPageStateJson());
    const Value* research = state->FindMember("start")->FindMember("research");
    EXPECT_EQ(research->StringOr("state", ""), "not_installed");
    EXPECT_EQ(research->StringOr("question", ""), "why is the sky blue");
    EXPECT_GT(research->IntOr("seq", 0), 0);
}

TEST(BrowserWindowPagesTest, ResearchExplainsAMissingOrBlankCustomAgent) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetAgentSearchDirsForTest({FakeAgentBin()});
    SendSettings(*window, R"({"type":"set_agent_provider","id":"custom"})");
    SendStart(*window, R"({"type":"research","question":"q"})");
    EXPECT_FALSE(window->research_running());
    EXPECT_EQ(window->research_status().state, "not_installed");
    EXPECT_NE(window->research_status().message.find("No agent command is set"),
              std::string::npos);
    const std::uint64_t first = window->research_status().seq;

    SendSettings(*window, R"({"type":"set_agent_command","command":"no-such-agent --acp"})");
    SendStart(*window, R"({"type":"research","question":"q"})");
    EXPECT_EQ(window->research_status().state, "not_installed");
    EXPECT_NE(window->research_status().message.find("was not found"), std::string::npos);
    EXPECT_GT(window->research_status().seq, first);
    EXPECT_TRUE(window->agent_session()->transcript().items().empty());
}

TEST(BrowserWindowPagesTest, ASecondResearchOrAnAgentTurnInFlightIsRefusedNotQueued) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetResearchTaskForTest("first question");
    const BrowserWindow::ResearchStartResult result = window->StartResearch("second question");
    EXPECT_EQ(result.code, BrowserWindow::ResearchStart::kBusy);
    EXPECT_NE(result.message.find("already running"), std::string::npos);
    // The running state is not overwritten by the refusal.
    EXPECT_TRUE(window->research_running());
    EXPECT_EQ(window->research_status().state, "running");
    EXPECT_EQ(window->research_status().question, "first question");
}

TEST(BrowserWindowPagesTest, AResearchTurnThatNeverEndedBecomesAFailureNotAHang) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    SendSettings(*window, R"({"type":"set_agent_command","command":"\"unbalanced"})");
    SendSettings(*window, R"({"type":"set_agent_provider","id":"custom"})");
    SendStart(*window, R"({"type":"ask_agent","prompt":"hello"})");  // creates the session (it fails)
    ASSERT_NE(window->agent_session(), nullptr);
    window->SetResearchTaskForTest("q", /*turns_ended_before=*/0);
    window->FinishResearch();
    EXPECT_FALSE(window->research_running());
    EXPECT_EQ(window->research_status().state, "failed");
}

TEST(BrowserWindowPagesTest, ACompletedResearchStoresTheReportAndOpensItInTheResearchSpace) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetReportPageUrlForTest(std::string(kReportUrl));
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() /
        ("island_reports_" + std::to_string(update::CurrentProcessId())) / "reports.json";
    std::filesystem::remove_all(file.parent_path());
    window->SetReportsFileForTest(file);

    // The agent creates the research space (it becomes active), then the user
    // wanders off to another space while it works.
    ASSERT_TRUE(window->CreateSpace("what is rust"));
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    ASSERT_EQ(window->active_space_index(), 0U);
    window->SetResearchTaskForTest("what is rust");
    window->CompleteResearch(ReportOutcome("# Rust in short\n\nA language [site](https://rust-lang.org)."));

    EXPECT_FALSE(window->research_running());
    ASSERT_EQ(window->reports().reports.size(), 1U);
    const ResearchReport& report = window->reports().reports[0];
    EXPECT_EQ(report.title, "Rust in short");
    EXPECT_EQ(report.question, "what is rust");
    EXPECT_EQ(window->research_status().state, "done");
    EXPECT_EQ(window->research_status().report_id, report.id);
    EXPECT_EQ(window->research_status().report_title, "Rust in short");

    // It opened in the research space as a new tab on the report URL.
    EXPECT_EQ(window->active_space_index(), 1U);
    const Space& space = SpaceAt(*window, 1);
    ASSERT_EQ(space.tab_count(), 2U);
    EXPECT_EQ(space.tabs()[1].startup_url(), std::string(kReportUrl) + "#" + report.id);
    EXPECT_EQ(window->ReportIdForUrl(space.tabs()[1].startup_url()), report.id);
    EXPECT_EQ(window->ReportIdForUrl(std::string(kReportUrl) + "#rX"), std::nullopt);
    EXPECT_EQ(window->ReportIdForUrl("https://a.test/#" + report.id), std::nullopt);
    EXPECT_EQ(window->ReportIdForUrl(std::string(kReportUrl)), std::nullopt);

    // Persisted: a second window on the same file sees it.
    CefRefPtr<BrowserWindow> other = MakeWindow();
    other->SetReportsFileForTest(file);
    ASSERT_EQ(other->reports().reports.size(), 1U);
    EXPECT_EQ(other->reports().reports[0].markdown, report.markdown);
    std::filesystem::remove_all(file.parent_path());
}

TEST(BrowserWindowPagesTest, CancelledAndFailedResearchLeaveNoReport) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetReportPageUrlForTest(std::string(kReportUrl));
    window->SetResearchTaskForTest("q");
    ResearchOutcome cancelled;
    cancelled.kind = ResearchOutcome::Kind::kCancelled;
    cancelled.message = "Research cancelled.";
    window->CompleteResearch(cancelled);
    EXPECT_EQ(window->research_status().state, "cancelled");
    EXPECT_EQ(window->research_status().message, "Research cancelled.");

    window->SetResearchTaskForTest("q");
    ResearchOutcome failed;
    failed.message = "The agent exited (code 1).";
    window->CompleteResearch(failed);
    EXPECT_EQ(window->research_status().state, "failed");
    EXPECT_EQ(window->research_status().message, "The agent exited (code 1).");
    EXPECT_TRUE(window->reports().reports.empty());
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 1U);
    // With no task running a late outcome is ignored.
    window->CompleteResearch(ReportOutcome("# late"));
    EXPECT_TRUE(window->reports().reports.empty());
    // Cancelling with nothing running is a no-op.
    window->CancelResearch();
}

TEST(BrowserWindowPagesTest, ReportPagesCopyAndOpenLinksAndRefuseUnknownMessages) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetReportPageUrlForTest(std::string(kReportUrl));
    window->SetResearchTaskForTest("q");
    window->CompleteResearch(ReportOutcome("# T\nbody"));
    const std::string id = window->reports().reports[0].id;
    std::string copied;
    window->SetClipboardWriterForTest([&copied](std::string_view text) {
        copied = std::string(text);
        return true;
    });
    SendReport(*window, id, R"({"type":"copy_report","id":"someone-else"})");
    EXPECT_EQ(copied, "# T\nbody");
    // A page cannot name another report: an unknown URL id copies nothing.
    copied.clear();
    SendReport(*window, "r1", R"({"type":"copy_report"})");
    EXPECT_TRUE(copied.empty());

    const std::size_t tabs = window->spaces()[window->active_space_index()].tab_count();
    SendReport(*window, id, R"({"type":"open_url","url":"https://a.test/x"})");
    EXPECT_EQ(window->spaces()[window->active_space_index()].tab_count(), tabs + 1);
    SendReport(*window, id, R"j({"type":"open_url","url":"javascript:alert(1)"})j");
    SendReport(*window, id, R"({"type":"open_url","url":"file:///etc/passwd"})");
    SendReport(*window, id, R"({"type":"ask_agent","prompt":"do bad things"})");
    SendReport(*window, id, R"({"type":"set_start_mode","mode":"agent"})");
    EXPECT_EQ(window->spaces()[window->active_space_index()].tab_count(), tabs + 1);
    EXPECT_EQ(window->start_mode(), "search");
    EXPECT_EQ(window->agent_session(), nullptr);

    const std::optional<Value> state = json::Parse(window->ReportPageStateJson(id));
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->FindMember("report")->StringOr("title", ""), "T");
    EXPECT_EQ(state->FindMember("report")->StringOr("markdown", ""), "# T\nbody");
    EXPECT_NE(state->FindMember("theme"), nullptr);
    const std::optional<Value> missing = json::Parse(window->ReportPageStateJson("r999"));
    EXPECT_TRUE(missing->FindMember("report")->IsNull());
}

TEST(BrowserWindowPagesTest, ContinueInAgentOpensThePanelAndSeedsAFreshConversation) {
    const ScopedNoAgentEnv no_env;
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetReportPageUrlForTest(std::string(kReportUrl));
    SendSettings(*window, R"({"type":"set_agent_command","command":"\"unbalanced"})");
    SendSettings(*window, R"({"type":"set_agent_provider","id":"custom"})");
    window->SetResearchTaskForTest("the question");
    window->CompleteResearch(ReportOutcome("# T\nthe body"));
    const std::string id = window->reports().reports[0].id;
    SendReport(*window, id, R"({"type":"continue_in_agent"})");
    // The session was created and the report went out as a turn (the agent
    // cannot launch here, which is reported, not thrown).
    ASSERT_NE(window->agent_session(), nullptr);
    EXPECT_FALSE(window->agent_session()->error().empty());
}

TEST(BrowserWindowPagesTest, SettingsListsOpensAndDeletesReports) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetReportPageUrlForTest(std::string(kReportUrl));
    window->SetResearchTaskForTest("q one");
    window->CompleteResearch(ReportOutcome("# One"));
    window->SetResearchTaskForTest("q two");
    window->CompleteResearch(ReportOutcome("# Two"));
    std::optional<Value> state = json::Parse(window->SettingsStateJson());
    const Value* reports = state->FindMember("settings")->FindMember("reports");
    ASSERT_EQ(reports->array_val.size(), 2U);
    EXPECT_EQ(reports->array_val[0].StringOr("title", ""), "Two");  // newest first
    const std::string newest = std::string(reports->array_val[0].StringOr("id", ""));
    const std::string older = std::string(reports->array_val[1].StringOr("id", ""));

    const std::size_t tabs = window->spaces()[window->active_space_index()].tab_count();
    SendSettings(*window, R"({"type":"open_report","id":")" + older + R"("})");
    EXPECT_EQ(window->spaces()[window->active_space_index()].tab_count(), tabs + 1);
    SendSettings(*window, R"({"type":"open_report","id":"r0"})");
    EXPECT_EQ(window->spaces()[window->active_space_index()].tab_count(), tabs + 1);

    SendSettings(*window, R"({"type":"delete_report","id":")" + newest + R"("})");
    ASSERT_EQ(window->reports().reports.size(), 1U);
    EXPECT_EQ(window->reports().reports[0].id, older);
    // The "Open report" button of the start page followed the deleted report.
    EXPECT_NE(window->research_status().state, "done");
}

// --- Plan tab (native-look spec 4.6, Task 10) ----------------------------------

constexpr std::string_view kPlanUrl = "data:text/html;charset=utf-8,plan";

// Sets an environment variable for one test and restores the previous value.
class ScopedAgentCommand {
  public:
    explicit ScopedAgentCommand(const char* value) {
        const char* saved = std::getenv("ISLAND_AGENT_COMMAND");
        if (saved != nullptr) saved_ = saved;
        Set(value);
    }
    ~ScopedAgentCommand() { Set(saved_ ? saved_->c_str() : nullptr); }

  private:
    static void Set(const char* value) {
#if defined(_WIN32)
        _putenv_s("ISLAND_AGENT_COMMAND", value != nullptr ? value : "");
#else
        if (value != nullptr) {
            setenv("ISLAND_AGENT_COMMAND", value, 1);
        } else {
            unsetenv("ISLAND_AGENT_COMMAND");
        }
#endif
    }
    std::optional<std::string> saved_;
};

void SendPlanMessage(BrowserWindow& window, std::string_view json_text) {
    const std::optional<Value> message = json::Parse(json_text);
    ASSERT_TRUE(message.has_value()) << json_text;
    window.HandlePlanPageMessage(*message);
}

TEST(BrowserWindowPagesTest, StartingResearchOpensAPinnedPlanTabAndSendsTheResearchPrompt) {
    const ScopedAgentCommand command("true");  // pins the command; nothing is launched
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    std::vector<std::string> sent;
    window->SetAgentSendForTest([&sent](std::string text) {
        sent.push_back(std::move(text));
        return true;
    });

    const BrowserWindow::ResearchStartResult result = window->StartResearch("why is the sky blue");
    EXPECT_EQ(result.code, BrowserWindow::ResearchStart::kStarted);
    EXPECT_EQ(window->research_status().state, "running");
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_NE(sent[0].find("why is the sky blue"), std::string::npos);

    // The Plan tab is the first tab of the space and pinned.
    const Space& space = SpaceAt(*window, 0);
    ASSERT_EQ(space.tab_count(), 2U);
    EXPECT_EQ(space.tabs()[0].startup_url(), std::string(kPlanUrl));
    EXPECT_TRUE(space.tabs()[0].pinned());
}

TEST(BrowserWindowPagesTest, APlanWithoutAReportWaitsForApprovalWithoutCompleting) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    window->SetResearchTaskForTest("q");
    ResearchOutcome waiting;
    waiting.kind = ResearchOutcome::Kind::kAwaitingApproval;
    waiting.message = "Waiting for approval";
    window->CompleteResearch(waiting);

    EXPECT_TRUE(window->research_running());
    EXPECT_EQ(window->research_status().state, "awaiting_approval");
    EXPECT_EQ(window->research_status().message, "Waiting for approval");
    EXPECT_TRUE(window->reports().reports.empty());
}

TEST(BrowserWindowPagesTest, RunSendsExactlyOneAgentMessageBuiltByTheRunBuilder) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    std::vector<std::string> sent;
    window->SetAgentSendForTest([&sent](std::string text) {
        sent.push_back(std::move(text));
        return true;
    });
    window->SetResearchTaskForTest("q");
    window->SetPlanEntriesForTest(agent::SpecPlan());
    ResearchOutcome waiting;
    waiting.kind = ResearchOutcome::Kind::kAwaitingApproval;
    waiting.message = "Waiting for approval";
    window->CompleteResearch(waiting);

    SendPlanMessage(*window, R"({"type":"run"})");
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0], agent::BuildPlanRunMessage(agent::SpecPlan(), {}, std::nullopt));
    EXPECT_EQ(window->research_status().state, "running");

    // Running already: a second run is refused, not sent again.
    SendPlanMessage(*window, R"({"type":"run"})");
    EXPECT_EQ(sent.size(), 1U);
}

TEST(BrowserWindowPagesTest, RunBranchSendsTheBranchRunMessage) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    std::vector<std::string> sent;
    window->SetAgentSendForTest([&sent](std::string text) {
        sent.push_back(std::move(text));
        return true;
    });
    window->SetResearchTaskForTest("q");
    window->SetPlanEntriesForTest(agent::SpecPlan());
    ResearchOutcome waiting;
    waiting.kind = ResearchOutcome::Kind::kAwaitingApproval;
    waiting.message = "Waiting for approval";
    window->CompleteResearch(waiting);

    SendPlanMessage(*window, R"({"type":"run_branch","branch":"A"})");
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0], agent::BuildPlanRunMessage(agent::SpecPlan(), {}, std::string("A")));
}

TEST(BrowserWindowPagesTest, SkipReRendersThePlanWithTheNodeInTheEdits) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanEntriesForTest(agent::SpecPlan());
    SendPlanMessage(*window, R"({"type":"skip","id":"c"})");
    SendPlanMessage(*window, R"({"type":"skip","id":"not-in-the-plan"})");

    const std::optional<Value> state = json::Parse(window->PlanStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* skipped = state->FindMember("edits")->FindMember("skipped");
    ASSERT_NE(skipped, nullptr);
    ASSERT_EQ(skipped->array_val.size(), 1U);
    EXPECT_EQ(skipped->array_val[0].string_val, "c");

    // Restoring takes it back out.
    SendPlanMessage(*window, R"({"type":"restore","id":"c"})");
    const std::optional<Value> restored = json::Parse(window->PlanStateJson());
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(restored->FindMember("edits")->FindMember("skipped")->array_val.empty());
}

TEST(BrowserWindowPagesTest, PlanStateCarriesTheThemeGraphAndStatus) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanEntriesForTest(agent::SpecPlan());
    const std::optional<Value> state = json::Parse(window->PlanStateJson());
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(state->FindMember("theme"), nullptr);
    const Value* graph = state->FindMember("graph");
    ASSERT_NE(graph, nullptr);
    EXPECT_TRUE(graph->BoolOr("branching", false));
    EXPECT_EQ(graph->FindMember("nodes")->array_val.size(), agent::SpecPlan().size());
    EXPECT_EQ(state->FindMember("status")->StringOr("state", ""), "idle");
    EXPECT_EQ(state->StringOr("view", ""), "graph");
}

// The Plan tabs of the first space, by their startup URL.
std::size_t PlanTabCount(const BrowserWindow& window) {
    std::size_t count = 0;
    for (const Tab& tab : SpaceAt(window, 0).tabs()) {
        count += tab.startup_url() == std::string(kPlanUrl) ? 1 : 0;
    }
    return count;
}

TEST(BrowserWindowPagesTest, APlainPlanOpensInListViewAndTakesEdits) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    std::vector<std::string> sent;
    window->SetAgentSendForTest([&sent](std::string text) {
        sent.push_back(std::move(text));
        return true;
    });
    window->SetPlanEntriesForTest(agent::PlainPlan());
    window->SetResearchTaskForTest("q");
    ResearchOutcome waiting;
    waiting.kind = ResearchOutcome::Kind::kAwaitingApproval;
    waiting.message = "Waiting for approval";
    window->CompleteResearch(waiting);

    // Without _meta.island the plan opens in List view, and Graph stays available.
    const std::optional<Value> listed = json::Parse(window->PlanStateJson());
    ASSERT_TRUE(listed.has_value());
    EXPECT_EQ(listed->StringOr("view", ""), "list");
    SendPlanMessage(*window, R"({"type":"view","mode":"graph"})");
    EXPECT_EQ(json::Parse(window->PlanStateJson())->StringOr("view", ""), "graph");
    SendPlanMessage(*window, R"({"type":"view","mode":"list"})");

    // The generated ids are real: select, skip and instruction reach the plan.
    SendPlanMessage(*window, R"({"type":"select","id":"p2"})");
    SendPlanMessage(*window, R"({"type":"skip","id":"p1"})");
    SendPlanMessage(*window, R"({"type":"instruction","id":"p0","text":"Use the forum"})");
    const std::optional<Value> state = json::Parse(window->PlanStateJson());
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->StringOr("selected", ""), "p2");
    const Value* skipped = state->FindMember("edits")->FindMember("skipped");
    ASSERT_EQ(skipped->array_val.size(), 1U);
    EXPECT_EQ(skipped->array_val[0].string_val, "p1");
    EXPECT_EQ(state->FindMember("edits")->FindMember("instructions")->StringOr("p0", ""),
              "Use the forum");

    SendPlanMessage(*window, R"({"type":"run"})");
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_NE(sent[0].find("Skip: p1"), std::string::npos) << sent[0];
    EXPECT_NE(sent[0].find("Instruction for p0: Use the forum"), std::string::npos) << sent[0];
}

TEST(BrowserWindowPagesTest, OneResearchKeepsOneSpacePinnedPlanTab) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    window->OpenPlanFixtureTab();
    window->OpenPlanFixtureTab();
    EXPECT_EQ(PlanTabCount(*window), 1U);
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), std::string(kPlanUrl));
    EXPECT_TRUE(SpaceAt(*window, 0).tabs()[0].pinned());
}

TEST(BrowserWindowPagesTest, ConsecutiveResearchReusesThePlanTab) {
    const ScopedAgentCommand command("true");  // pins the command; nothing is launched
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->SetPlanPageUrlForTest(std::string(kPlanUrl));
    window->SetAgentSendForTest([](std::string) { return true; });

    ASSERT_EQ(window->StartResearch("first question").code, BrowserWindow::ResearchStart::kStarted);
    ResearchOutcome cancelled;
    cancelled.kind = ResearchOutcome::Kind::kCancelled;
    window->CompleteResearch(cancelled);
    ASSERT_FALSE(window->research_running());
    ASSERT_EQ(window->StartResearch("second question").code,
              BrowserWindow::ResearchStart::kStarted);

    EXPECT_EQ(PlanTabCount(*window), 1U);
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), std::string(kPlanUrl));
    EXPECT_TRUE(SpaceAt(*window, 0).tabs()[0].pinned());
}

TEST(BrowserWindowPagesTest, PlanDevFixtureIsTheSpecPlan) {
    EXPECT_EQ(DevPlanFixture(), agent::SpecPlan());
}

}  // namespace
}  // namespace island
