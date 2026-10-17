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

TEST(PrefsStoreTest, GivenACustomizedNativeLookWhenSavedThenEveryKnobRoundTrips) {
    PrefsState state;
    state.appearance = ApplyLookPreset(LookPreset::kNative);
    state.appearance.density = Density::kCompact;
    const std::filesystem::path path = TempPath("native_appearance.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.appearance, state.appearance);
}

TEST(PrefsStoreTest, GivenAppearanceObjectWithoutLookWhenLoadedThenItStaysArc) {
    const std::filesystem::path path = TempPath("legacy_appearance.json");
    WriteFile(
        path,
        R"({"version":1,"onboarding_completed":true,"theme":"dark","appearance":{"sidebar_tint_percent":85,"card_radius":14,"font":"Avenir Next"}})");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.appearance.look, LookPreset::kArc);
    EXPECT_EQ(loaded.state.appearance.sidebar_tint_percent, 85);
    EXPECT_EQ(loaded.state.appearance.card_radius_dip, 14);
    EXPECT_EQ(loaded.state.appearance.font_family, "Avenir Next");
    EXPECT_EQ(AppearanceSettings{}, ApplyLookPreset(LookPreset::kNative));
}

TEST(PrefsStoreTest, GivenReleasedPrefsWithoutAppearanceWhenLoadedThenTheyUseNative) {
    const std::filesystem::path path = TempPath("released_without_appearance.json");
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.appearance, AppearanceSettings{});
    EXPECT_EQ(loaded.state.appearance, ApplyLookPreset(LookPreset::kNative));
}

TEST(PrefsStoreTest, GivenAnUnknownLookWhenLoadedThenStoredKnobsStayCustom) {
    const std::filesystem::path path = TempPath("unknown_appearance.json");
    WriteFile(
        path,
        R"({"version":1,"onboarding_completed":true,"theme":"dark","appearance":{"look":"future","sidebar_tint_percent":37,"card_radius":6,"font":"system","card_shadow":false,"full_bleed":false}})");
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.appearance.look, LookPreset::kArc);
    EXPECT_EQ(loaded.state.appearance.sidebar_tint_percent, 37);
    EXPECT_EQ(loaded.state.appearance.card_radius_dip, 6);
    EXPECT_EQ(loaded.state.appearance.font_family, "system");
    EXPECT_FALSE(loaded.state.appearance.card_shadow);
    EXPECT_FALSE(loaded.state.appearance.full_bleed);
    EXPECT_EQ(MatchingLook(loaded.state.appearance), std::nullopt);
}

TEST(PrefsStoreTest, GivenTabSleepMinutesThenTheyRoundTripAndUnknownValuesFallBackToThirty) {
    const std::filesystem::path path = TempPath("tab_sleep.json");
    EXPECT_EQ(PrefsState().tab_sleep_minutes, 30);

    for (const int minutes : {0, 15, 30, 60}) {
        PrefsState state;
        state.tab_sleep_minutes = minutes;
        ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
        const PrefsLoadResult loaded = PrefsStore::Load(path);
        EXPECT_EQ(loaded.error, PrefsError::kNone);
        EXPECT_EQ(loaded.state.tab_sleep_minutes, minutes);
    }

    // A file without the key, or with a value Settings never offers, means the default.
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    EXPECT_EQ(PrefsStore::Load(path).state.tab_sleep_minutes, 30);
    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","tab_sleep_minutes":7})");
    EXPECT_EQ(PrefsStore::Load(path).state.tab_sleep_minutes, 30);
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark",
                        "tab_sleep_minutes":"15"})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenReaderAppearanceWhenSavedThenItRoundTrips) {
    PrefsState state;
    state.reader_size = 22;
    state.reader_width = "wide";
    state.reader_theme = "sepia";
    const std::filesystem::path path = TempPath("reader.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.reader_size, 22);
    EXPECT_EQ(loaded.state.reader_width, "wide");
    EXPECT_EQ(loaded.state.reader_theme, "sepia");
}

TEST(PrefsStoreTest, GivenAgentSettingsWhenSavedThenTheyRoundTripAndStayOptional) {
    PrefsState state;
    state.agent_provider = "custom";
    state.agent_command = "npx -y \"my agent\" --acp";
    state.agent_panel_open = true;
    state.sidebar_collapsed = true;
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
    // A missing sidebar key means expanded.
    EXPECT_FALSE(legacy.state.sidebar_collapsed);

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","agent_command":7})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","sidebar_collapsed":1})");
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

TEST(PrefsStoreTest, GivenSearchSettingsWhenSavedThenTheyRoundTripAndStayOptional) {
    PrefsState defaults;
    EXPECT_EQ(defaults.search_engine, "google");
    EXPECT_EQ(defaults.start_mode, "search");

    PrefsState state;
    state.search_engine = "duckduckgo";
    state.start_mode = "agent";
    const std::filesystem::path path = TempPath("search.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state, state);

    // Files from before the search keys existed keep the defaults.
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    const PrefsLoadResult legacy = PrefsStore::Load(path);
    EXPECT_EQ(legacy.error, PrefsError::kNone);
    EXPECT_EQ(legacy.state.search_engine, "google");
    EXPECT_EQ(legacy.state.start_mode, "search");

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","search_engine":3})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark","start_mode":[]})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenHistorySettingsWhenSavedThenTheyRoundTripAndDefaultToRemembering) {
    PrefsState state;
    EXPECT_TRUE(state.remember_history);
    state.remember_history = false;
    const std::filesystem::path path = TempPath("history.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    EXPECT_FALSE(PrefsStore::Load(path).state.remember_history);

    WriteFile(path, R"({"version":1,"onboarding_completed":true,"theme":"dark"})");
    EXPECT_TRUE(PrefsStore::Load(path).state.remember_history);

    WriteFile(path,
              R"({"version":1,"onboarding_completed":true,"theme":"dark","remember_history":1})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

TEST(PrefsStoreTest, GivenContentBlockerPrefsWhenSavedThenTheyRoundTripAndStayOptional) {
    const std::filesystem::path path = TempPath("content_blocker.json");
    PrefsState state;
    state.block_ads = false;
    state.block_trackers = true;
    state.content_blocker_allowlist = {"example.com", "news.example"};
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_FALSE(loaded.state.block_ads);
    EXPECT_TRUE(loaded.state.block_trackers);
    EXPECT_EQ(loaded.state.content_blocker_allowlist, state.content_blocker_allowlist);

    WriteFile(path, R"({"version":1,"onboarding_completed":false,"theme":"system"})");
    const PrefsLoadResult legacy = PrefsStore::Load(path);
    EXPECT_EQ(legacy.error, PrefsError::kNone);
    EXPECT_TRUE(legacy.state.block_ads);
    EXPECT_TRUE(legacy.state.block_trackers);
    EXPECT_TRUE(legacy.state.content_blocker_allowlist.empty());

    WriteFile(path,
              R"({"version":1,"onboarding_completed":false,"theme":"system","block_ads":"yes"})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    WriteFile(
        path,
        R"({"version":1,"onboarding_completed":false,"theme":"system","content_blocker_allowlist":[1]})");
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
    std::filesystem::remove(path);
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
