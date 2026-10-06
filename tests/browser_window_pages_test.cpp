// Headless coverage of the Settings / All-tabs / import / shortcut seams of
// BrowserWindow. The CEF view types must be complete before browser_window.h.
#include <gtest/gtest.h>

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

}  // namespace
}  // namespace island
