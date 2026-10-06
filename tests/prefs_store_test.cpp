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
