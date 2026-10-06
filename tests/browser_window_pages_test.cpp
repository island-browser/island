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

#include "browser_window.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "island_version.h"
#include "json_util.h"
#include "keymap.h"
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

}  // namespace
}  // namespace island
