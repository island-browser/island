#include "prefs_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>

namespace island {
namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("island_prefs_test_" + name);
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

TEST(PrefsStoreTest, GivenFreshInstallWhenLoadedThenDefaultsAndReadError) {
    const PrefsLoadResult result = PrefsStore::Load(TempPath("missing.json"));
    EXPECT_EQ(result.error, PrefsError::kFileReadError);
    EXPECT_EQ(result.state, PrefsState{});
    EXPECT_EQ(result.state.theme, ThemePreference::kSystem);
    EXPECT_FALSE(result.state.onboarding_completed);
}

TEST(PrefsStoreTest, GivenARoundTripWhenSavedAndLoadedThenTheStateSurvives) {
    for (const ThemePreference theme :
         {ThemePreference::kSystem, ThemePreference::kLight, ThemePreference::kDark}) {
        PrefsState state;
        state.onboarding_completed = true;
        state.theme = theme;

        const std::filesystem::path path = TempPath("roundtrip.json");
        ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
        const PrefsLoadResult loaded = PrefsStore::Load(path);
        EXPECT_EQ(loaded.error, PrefsError::kNone);
        EXPECT_EQ(loaded.state, state);
    }
}

TEST(PrefsStoreTest, GivenAgentSettingsWhenSavedThenTheyRoundTripAndStayOptional) {
    PrefsState state;
    state.agent_provider = "custom";
    state.agent_command = "npx -y \"my agent\" --acp";
    state.agent_panel_open = true;
    state.keybindings = {{"new_tab", "Mod+Shift+T"}, {"close_tab", ""}};
    const std::filesystem::path path = TempPath("agent.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state, state);

    // Files written before the agent keys existed still load.
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    const PrefsLoadResult legacy = PrefsStore::Load(path);
    EXPECT_EQ(legacy.error, PrefsError::kNone);
    EXPECT_TRUE(legacy.state.agent_command.empty());
    EXPECT_FALSE(legacy.state.agent_panel_open);

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","agent_command":7})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","keybindings":{"a":1}})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenAnAgentProviderWhenSavedThenItRoundTripsBesideTheCustomCommand) {
    PrefsState state;
    EXPECT_EQ(state.agent_provider, "claude");
    state.agent_provider = "gemini";
    state.agent_command = "my-agent --acp";
    const std::filesystem::path path = TempPath("provider.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state, state);

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","agent_provider":1})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenPrefsFromBeforeProvidersWhenLoadedThenTheCommandMigrates) {
    const std::filesystem::path path = TempPath("migrate.json");
    const std::string head = R"({"version":1,"onboarding_completed":true,"theme":"dark")";

    // No command: the default provider.
    WriteFile(path, head + "}");
    PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.agent_provider, "claude");
    EXPECT_TRUE(loaded.state.agent_command.empty());

    // The old built-in default (the deprecated adapter): Claude Code, and
    // the stale command is dropped.
    WriteFile(path, head + R"(,"agent_command":"npx -y @zed-industries/claude-code-acp"})");
    loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.agent_provider, "claude");
    EXPECT_TRUE(loaded.state.agent_command.empty());

    // Any other saved command: the custom provider keeps running it.
    WriteFile(path, head + R"(,"agent_command":"gemini --experimental-acp"})");
    loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.agent_provider, "custom");
    EXPECT_EQ(loaded.state.agent_command, "gemini --experimental-acp");

    // Once a provider is saved, the command is never reinterpreted.
    WriteFile(path, head + R"(,"agent_provider":"codex","agent_command":"my-agent"})");
    loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.agent_provider, "codex");
    EXPECT_EQ(loaded.state.agent_command, "my-agent");

    // A migrated file saves the provider, so the next load is stable.
    WriteFile(path, head + R"(,"agent_command":"npx -y @zed-industries/claude-code-acp"})");
    ASSERT_EQ(PrefsStore::Save(path, PrefsStore::Load(path).state), PrefsError::kNone);
    loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.state.agent_provider, "claude");
    EXPECT_TRUE(loaded.state.agent_command.empty());
}

TEST(PrefsStoreTest, GivenUpdatePrefsWhenSavedThenTheyRoundTripAndStayOptional) {
    PrefsState state;
    state.auto_check_updates = false;
    state.include_prereleases = true;
    state.last_update_check = 1'800'000'000;
    const std::filesystem::path path = TempPath("updates.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state, state);

    // Older files lack the keys: automatic checks on, stable releases only.
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    const PrefsLoadResult legacy = PrefsStore::Load(path);
    EXPECT_EQ(legacy.error, PrefsError::kNone);
    EXPECT_TRUE(legacy.state.auto_check_updates);
    EXPECT_FALSE(legacy.state.include_prereleases);
    EXPECT_EQ(legacy.state.last_update_check, 0);

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","auto_check_updates":1})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    WriteFile(
        path,
        R"({"version":1,"onboarding_completed":true,"theme":"dark","last_update_check":"x"})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenCorruptJsonWhenLoadedThenParseErrorAndDefaults) {
    const std::filesystem::path path = TempPath("corrupt.json");
    WriteFile(path, "{ not json");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kParseError);
    EXPECT_EQ(loaded.state, PrefsState{});
}

TEST(PrefsStoreTest, GivenAnUnknownThemeTokenWhenLoadedThenSchemaErrorAndDefaults) {
    const std::filesystem::path path = TempPath("bad_theme.json");
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"sepia"})");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kSchemaError);
    EXPECT_EQ(loaded.state, PrefsState{});
}

TEST(PrefsStoreTest, GivenAFutureSchemaWhenLoadedThenSchemaErrorAndDefaults) {
    const std::filesystem::path path = TempPath("future.json");
    WriteFile(path, R"({"version":99,"onboarding_completed":true,"theme":"dark"})");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kSchemaError);
    EXPECT_EQ(loaded.state, PrefsState{});
}

TEST(PrefsStoreTest, GivenThemeTokensWhenConvertedThenTheyRoundTripStrictly) {
    ThemePreference parsed = ThemePreference::kSystem;
    EXPECT_TRUE(ThemePreferenceFromString("system", parsed));
    EXPECT_EQ(parsed, ThemePreference::kSystem);
    EXPECT_TRUE(ThemePreferenceFromString("dark", parsed));
    EXPECT_EQ(parsed, ThemePreference::kDark);
    EXPECT_FALSE(ThemePreferenceFromString("DARK", parsed));
    EXPECT_FALSE(ThemePreferenceFromString("", parsed));
    EXPECT_EQ(ThemePreferenceToString(ThemePreference::kLight), "light");
}

}  // namespace
}  // namespace island
