#include "session_store.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tab_id.h"

namespace island {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Writes raw bytes into a file, creating any parent directories needed.
void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "cannot open " << path;
    out << content;
}

// Stores the value so the original can be moved from.
TabId T(TabId id) { return id; }
SpaceId S(SpaceId id) { return id; }

SessionState BuildSampleState() {
    SessionState state;

    // Space A with 3 tabs
    SpaceState& space_a = state.spaces.emplace_back();
    space_a.id = S(SpaceId{100});
    space_a.name = "Personal";
    space_a.color_argb = 0xFF3366CC;
    space_a.tabs.push_back({T(TabId{10}), "https://example.com/"});
    space_a.tabs.push_back({T(TabId{11}), "https://github.com/"});
    space_a.tabs.push_back({T(TabId{12}), "data:text/html,<h1>Hello</h1>"});
    space_a.active_tab_id = TabId{11};
    space_a.split = SplitState{TabId{11}, TabId{12}};

    // Space B with 1 tab
    SpaceState& space_b = state.spaces.emplace_back();
    space_b.id = S(SpaceId{200});
    space_b.name = "Work";
    space_b.color_argb = 0xFFCC6633;
    space_b.tabs.push_back({T(TabId{20}), "https://internal.corp.test/dashboard"});
    space_b.active_tab_id = TabId{20};

    state.active_space_id = SpaceId{100};

    return state;
}

// ---------------------------------------------------------------------------
// RED — Round-trip tests
// ---------------------------------------------------------------------------

TEST(SessionStore, SaveThenLoadRoundTripsASimpleSession) {
    // Given: a session with two spaces, four tabs, active selections, and a split
    SessionState original = BuildSampleState();

    // When: we save it to a temp file and load it back
    const auto path = std::filesystem::temp_directory_path() / "island_test_roundtrip.json";
    SessionError save_err = SessionStore::Save(path, original);
    ASSERT_EQ(save_err, SessionError::kNone);

    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then: the loaded state equals the original
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);
}

TEST(SessionStore, RoundTripsSpaceIconsAndOldOrUnknownIconsLoadAsNone) {
    SessionState original = BuildSampleState();
    original.spaces[0].icon_id = "briefcase";
    const auto path = std::filesystem::temp_directory_path() / "island_test_space_icon.json";
    ASSERT_EQ(SessionStore::Save(path, original), SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);

    WriteFile(path, R"({"version":1,"spaces":[{"id":1,"name":"Old","color_argb":1,"tabs":[]}]})");
    result = SessionStore::Load(path);
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_TRUE(result.state.spaces[0].icon_id.empty());

    WriteFile(
        path,
        R"({"version":1,"spaces":[{"id":1,"name":"Edited","color_argb":1,"icon":"unknown","tabs":[]}]})");
    result = SessionStore::Load(path);
    std::filesystem::remove(path);
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_TRUE(result.state.spaces[0].icon_id.empty());
}

TEST(SessionStore, IgnoresANonStringSpaceIconInsteadOfDroppingTheSession) {
    const auto path = std::filesystem::temp_directory_path() / "island_test_space_icon_type.json";
    for (const char* icon : {"null", "3", "true", "{}"}) {
        WriteFile(
            path,
            std::string(R"({"version":1,"spaces":[{"id":1,"name":"Kept","color_argb":1,"icon":)") +
                icon + R"(,"tabs":[]}]})");
        const LoadResult result = SessionStore::Load(path);
        ASSERT_EQ(result.error, SessionError::kNone) << icon;
        ASSERT_EQ(result.state.spaces.size(), 1U) << icon;
        EXPECT_EQ(result.state.spaces[0].name, "Kept");
        EXPECT_TRUE(result.state.spaces[0].icon_id.empty()) << icon;
    }
    std::filesystem::remove(path);
}

TEST(SessionStore, RoundTripsPinnedTabsAndLoadsSessionsWithoutThePinnedKey) {
    SessionState original = BuildSampleState();
    original.spaces[0].tabs[0].pinned = true;
    const auto path = std::filesystem::temp_directory_path() / "island_test_pinned.json";
    ASSERT_EQ(SessionStore::Save(path, original), SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);
    EXPECT_TRUE(result.state.spaces[0].tabs[0].pinned);
    EXPECT_FALSE(result.state.spaces[0].tabs[1].pinned);

    // A wrong type is a schema error, never a silent default.
    WriteFile(path,
              R"({"version":1,"active_space_id":1,"spaces":[{"id":1,"name":"A","color_argb":1,)"
              R"("tabs":[{"id":1,"url":"https://a.test/","pinned":"yes"}]}]})");
    EXPECT_EQ(SessionStore::Load(path).error, SessionError::kSchemaError);
    std::filesystem::remove(path);
}

TEST(SessionStore, RoundTripsTabGroupParentAndLabel) {
    SessionState original = BuildSampleState();
    original.spaces[0].tabs[1].group_parent = original.spaces[0].tabs[0].id;
    original.spaces[0].tabs[1].group_label = "Branch A";
    const auto path = std::filesystem::temp_directory_path() / "island_test_tab_group.json";

    ASSERT_EQ(SessionStore::Save(path, original), SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);
    EXPECT_EQ(result.state.spaces[0].tabs[1].group_parent, original.spaces[0].tabs[0].id);
    EXPECT_EQ(result.state.spaces[0].tabs[1].group_label, "Branch A");
}

TEST(SessionStore, RoundTripsDefaultEmptySession) {
    // Given: a fresh default session (zero spaces, zero tabs)
    SessionState empty = SessionState::Default();

    const auto path = std::filesystem::temp_directory_path() / "island_test_empty_roundtrip.json";

    // When
    SessionError save_err = SessionStore::Save(path, empty);
    ASSERT_EQ(save_err, SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state.version, SessionState::kCurrentSchemaVersion);
    EXPECT_TRUE(result.state.spaces.empty());
    EXPECT_FALSE(result.state.active_space_id.has_value());
}

TEST(SessionStore, RoundTripsASpaceWithNoTabs) {
    // Given: a single space with zero tabs
    SessionState state;
    SpaceState& space = state.spaces.emplace_back();
    space.id = S(SpaceId{1});
    space.name = "Empty";
    space.color_argb = 0xFFAAAAAA;

    const auto path = std::filesystem::temp_directory_path() / "island_test_empty_space.json";

    // When
    SessionError save_err = SessionStore::Save(path, state);
    ASSERT_EQ(save_err, SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.spaces.size(), 1U);
    EXPECT_EQ(result.state.spaces[0].name, "Empty");
    EXPECT_TRUE(result.state.spaces[0].tabs.empty());
    EXPECT_FALSE(result.state.spaces[0].active_tab_id.has_value());
}

TEST(SessionStore, RoundTripsTabStatesWithoutActiveSelections) {
    // Given: a space with tabs but no active tab selected
    SessionState state;
    SpaceState& space = state.spaces.emplace_back();
    space.id = S(SpaceId{50});
    space.name = "NoSelection";
    space.tabs.push_back({T(TabId{1}), "https://a.test/"});
    space.tabs.push_back({T(TabId{2}), "https://b.test/"});
    // active_tab_id left nullopt intentionally

    const auto path = std::filesystem::temp_directory_path() / "island_test_no_selection.json";

    // When
    SessionError save_err = SessionStore::Save(path, state);
    ASSERT_EQ(save_err, SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.spaces.size(), 1U);
    EXPECT_FALSE(result.state.spaces[0].active_tab_id.has_value());
}

// ---------------------------------------------------------------------------
// RED — Malformed / invalid file tests
// ---------------------------------------------------------------------------

TEST(SessionStore, LoadReturnsFileReadErrorForMissingFile) {
    // Given: a path that does not exist
    const auto path = std::filesystem::temp_directory_path() / "island_test_does_not_exist.json";
    std::filesystem::remove(path);  // ensure absent

    // When
    LoadResult result = SessionStore::Load(path);

    // Then: error is kFileReadError, state is Default()
    EXPECT_EQ(result.error, SessionError::kFileReadError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsParseErrorForEmptyFile) {
    // Given: an empty JSON file
    const auto path = std::filesystem::temp_directory_path() / "island_test_empty_file.json";
    WriteFile(path, "");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then: falls back cleanly
    EXPECT_EQ(result.error, SessionError::kParseError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsParseErrorForGarbageBytes) {
    // Given: a file with completely invalid content
    const auto path = std::filesystem::temp_directory_path() / "island_test_garbage.json";
    WriteFile(path, "not even json {{{[");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kParseError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsParseErrorForTruncatedJson) {
    // Given: valid JSON that is cut off mid-stream
    const auto path = std::filesystem::temp_directory_path() / "island_test_truncated.json";
    WriteFile(path, R"({"version": 1, "spaces)");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kParseError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsVersionMismatchForWrongVersion) {
    // Given: a file with a different schema version number
    const auto path = std::filesystem::temp_directory_path() / "island_test_version.json";
    WriteFile(path, R"({"version": 999, "spaces": []})");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kVersionMismatch);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorWhenMissingVersion) {
    // Given: JSON that is valid JSON but lacks the required "version" key
    const auto path = std::filesystem::temp_directory_path() / "island_test_no_version.json";
    WriteFile(path, R"({"spaces": []})");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorForVersionOfWrongType) {
    // Given: "version" present but not an integer
    const auto path = std::filesystem::temp_directory_path() / "island_test_version_type.json";
    WriteFile(path, R"({"version": "one", "spaces": []})");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorWhenSpacesIsMissing) {
    // Given: valid version but no "spaces" key
    const auto path = std::filesystem::temp_directory_path() / "island_test_no_spaces.json";
    WriteFile(path, R"({"version": 1})");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorWhenSpacesIsNotAnArray) {
    // Given: "spaces" present but not an array
    const auto path = std::filesystem::temp_directory_path() / "island_test_spaces_not_array.json";
    WriteFile(path, R"({"version": 1, "spaces": "not-an-array"})");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorForCorruptTabEntry) {
    // Given: a well-formed outer document with a malformed tab entry
    const auto path = std::filesystem::temp_directory_path() / "island_test_corrupt_tab.json";
    WriteFile(path, R"({
        "version": 1,
        "spaces": [{
            "id": 1,
            "name": "BadSpace",
            "color_argb": 0,
            "tabs": [{"id": "not-a-number"}]
        }]
    })");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

TEST(SessionStore, LoadReturnsSchemaErrorForPartialSpaceData) {
    // Given: a file missing a required space field ("name")
    const auto path = std::filesystem::temp_directory_path() / "island_test_partial_space.json";
    WriteFile(path, R"({
        "version": 1,
        "spaces": [{"id": 1, "color_argb": 0, "tabs": []}]
    })");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then: falls back cleanly, does not crash or leak
    EXPECT_EQ(result.error, SessionError::kSchemaError);
    EXPECT_EQ(result.state, SessionState::Default());
}

// ---------------------------------------------------------------------------
// RED — Path safety tests
// ---------------------------------------------------------------------------

TEST(SessionStore, SaveToNewDirectoryCreatesParents) {
    // Given: a path nested inside a directory that does not exist yet
    const auto dir = std::filesystem::temp_directory_path() / "island_test_new_dir";
    std::filesystem::remove_all(dir);  // ensure clean start
    const auto path = dir / "sub" / "session.json";

    SessionState state;
    state.spaces.emplace_back().name = "Single";

    // When
    SessionError err = SessionStore::Save(path, state);

    // Then: save succeeds, file exists at the expected location
    EXPECT_EQ(err, SessionError::kNone);
    EXPECT_TRUE(std::filesystem::exists(path));

    // Cleanup
    std::filesystem::remove_all(dir);
}

TEST(SessionStore, LoadDoesNotLeakFilePathsIntoErrorMessage) {
    // Given: a file with invalid JSON
    const auto path = std::filesystem::temp_directory_path() / "island_test_no_leak.json";
    WriteFile(path, "<<<invalid>>>");

    // When
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);

    // Then: error is kParseError and state is Default — the implementation
    // must not leak the raw file path into the recoverable state or produce
    // a partial session that embeds disk layout.
    EXPECT_EQ(result.error, SessionError::kParseError);
    EXPECT_EQ(result.state, SessionState::Default());
    EXPECT_TRUE(result.state.spaces.empty());
}

// ---------------------------------------------------------------------------
// RED — Atomic write safety
// ---------------------------------------------------------------------------

TEST(SessionStore, SaveDoesNotLeaveATruncatedFileAfterWrite) {
    // Given: a populated session
    SessionState original = BuildSampleState();
    const auto path = std::filesystem::temp_directory_path() / "island_test_atomic.json";
    std::filesystem::remove(path);

    // When
    SessionError err = SessionStore::Save(path, original);

    // Then: file exists and is self-contained complete JSON
    EXPECT_EQ(err, SessionError::kNone);
    EXPECT_TRUE(std::filesystem::exists(path));

    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Windows refuses to remove a file while a handle is still open, so the
    // reader must be closed before the cleanup below rather than at scope exit.
    in.close();
    EXPECT_TRUE(content.starts_with('{'));
    EXPECT_TRUE(content.ends_with('\n'));

    // The file round-tripped above already proves completeness; this test
    // adds the start/end bracket guard.
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// RED — Deterministic round-trip
// ---------------------------------------------------------------------------

TEST(SessionStore, ProducesDeterministicOutputForSameInput) {
    // Given: the same session state
    SessionState state = BuildSampleState();
    const auto path_a = std::filesystem::temp_directory_path() / "island_test_det_a.json";
    const auto path_b = std::filesystem::temp_directory_path() / "island_test_det_b.json";

    // When: saved twice
    SessionError err_a = SessionStore::Save(path_a, state);
    SessionError err_b = SessionStore::Save(path_b, state);
    ASSERT_EQ(err_a, SessionError::kNone);
    ASSERT_EQ(err_b, SessionError::kNone);

    // Then: both files are byte-identical
    std::ifstream in_a(path_a, std::ios::binary);
    std::ifstream in_b(path_b, std::ios::binary);
    std::string content_a((std::istreambuf_iterator<char>(in_a)), std::istreambuf_iterator<char>());
    std::string content_b((std::istreambuf_iterator<char>(in_b)), std::istreambuf_iterator<char>());
    // Windows refuses to remove a file while a handle is still open.
    in_a.close();
    in_b.close();

    EXPECT_EQ(content_a, content_b);

    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
}

// ---------------------------------------------------------------------------
// RED — Default() correctness
// ---------------------------------------------------------------------------

TEST(SessionStore, DefaultSessionHasCurrentVersionAndNoSpaces) {
    SessionState def = SessionState::Default();

    EXPECT_EQ(def.version, SessionState::kCurrentSchemaVersion);
    EXPECT_TRUE(def.spaces.empty());
    EXPECT_FALSE(def.active_space_id.has_value());
}

// ---------------------------------------------------------------------------
// RED — Save actually writes JSON
// ---------------------------------------------------------------------------

TEST(SessionStore, SaveWritesReadableJsonWithExpectedKeys) {
    // Given: a minimal session
    SessionState state;
    SpaceState& space = state.spaces.emplace_back();
    space.id = S(SpaceId{42});
    space.name = "Check";
    space.color_argb = 0xAABBCCDD;

    const auto path = std::filesystem::temp_directory_path() / "island_test_readable.json";

    // When
    SessionError err = SessionStore::Save(path, state);
    ASSERT_EQ(err, SessionError::kNone);

    // Then: the file contains the expected keys
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Windows refuses to remove a file while a handle is still open.
    in.close();
    EXPECT_TRUE(content.find("\"version\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"spaces\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"id\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"name\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"Check\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"color_argb\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"tabs\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(SessionStore, OpenedByAgentRoundTripsAndOldFilesLoad) {
    SessionState original = BuildSampleState();
    original.spaces[0].tabs[1].opened_by_agent = true;
    const auto path = std::filesystem::temp_directory_path() / "island_test_opened_by_agent.json";
    ASSERT_EQ(SessionStore::Save(path, original), SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);
    EXPECT_TRUE(result.state.spaces[0].tabs[1].opened_by_agent);
    EXPECT_FALSE(result.state.spaces[0].tabs[0].opened_by_agent);

    // Sessions written before the key existed load with every tab the person's own.
    WriteFile(path,
              R"({"version":1,"active_space_id":1,"spaces":[{"id":1,"name":"A","color_argb":1,)"
              R"("tabs":[{"id":1,"url":"https://a.test/"}]}]})");
    result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_FALSE(result.state.spaces[0].tabs[0].opened_by_agent);

    // A wrong type is a schema error, never a silent default.
    WriteFile(path,
              R"({"version":1,"active_space_id":1,"spaces":[{"id":1,"name":"A","color_argb":1,)"
              R"("tabs":[{"id":1,"url":"https://a.test/","opened_by_agent":"yes"}]}]})");
    EXPECT_EQ(SessionStore::Load(path).error, SessionError::kSchemaError);
    std::filesystem::remove(path);
}

TEST(SessionStore, FavoriteTabRecordsRoundTripAndOldFilesLoad) {
    SessionState original = BuildSampleState();
    original.favorite_tabs.push_back({std::nullopt, 7, "https://a.test/", "https://a.test/moved"});
    original.favorite_tabs.push_back({SpaceId{100}, 8, "https://b.test/", "https://b.test/"});
    const auto path = std::filesystem::temp_directory_path() / "island_test_favorite_tabs.json";
    ASSERT_EQ(SessionStore::Save(path, original), SessionError::kNone);
    LoadResult result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state, original);
    EXPECT_EQ(result.state.version, SessionState::kCurrentSchemaVersion);

    // Sessions written before the key existed load with no favorite virtual tabs.
    WriteFile(path,
              R"({"version":1,"active_space_id":1,"spaces":[{"id":1,"name":"A","color_argb":1,)"
              R"("tabs":[{"id":1,"url":"https://a.test/"}]}]})");
    result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    EXPECT_TRUE(result.state.favorite_tabs.empty());

    // A global record has no space_id; a wrong type is a schema error, never a silent default.
    WriteFile(path, R"({"version":1,"spaces":[],"favorite_tabs":[{"bookmark_id":3,)"
                    R"("reset_url":"https://a.test/","last_url":"https://a.test/"}]})");
    result = SessionStore::Load(path);
    EXPECT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.favorite_tabs.size(), 1U);
    EXPECT_FALSE(result.state.favorite_tabs[0].space_id.has_value());
    WriteFile(path, R"({"version":1,"spaces":[],"favorite_tabs":[{"bookmark_id":"three",)"
                    R"("reset_url":"https://a.test/","last_url":"https://a.test/"}]})");
    EXPECT_EQ(SessionStore::Load(path).error, SessionError::kSchemaError);
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 0.10 optional members (R-D3, R-D4, R-R1): web_data_key, import_key, routes
// ---------------------------------------------------------------------------

// Saves |state| through the public writer and returns the bytes it wrote.
std::string SaveToText(const SessionState& state) {
    const auto path = std::filesystem::temp_directory_path() / "island_test_0_10_save.json";
    EXPECT_EQ(SessionStore::Save(path, state), SessionError::kNone);
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);
    return text;
}

// Loads |text| through the public reader.
LoadResult LoadText(std::string_view text) {
    const auto path = std::filesystem::temp_directory_path() / "island_test_0_10_load.json";
    WriteFile(path, text);
    LoadResult result = SessionStore::Load(path);
    std::filesystem::remove(path);
    return result;
}

TEST(SessionStore, WebDataKeyRoundTripsAndIsOptional) {
    SessionState state;
    SpaceState keyed;
    keyed.id = SpaceId{1};
    keyed.name = "Keyed";
    keyed.color_argb = 1;
    keyed.web_data_key = "0123456789abcdef0123456789abcdef";
    SpaceState plain;
    plain.id = SpaceId{2};
    plain.name = "Plain";
    plain.color_argb = 1;
    state.spaces = {keyed, plain};

    const std::string json = SaveToText(state);
    EXPECT_NE(json.find("\"web_data_key\":\"0123456789abcdef0123456789abcdef\""),
              std::string::npos);
    EXPECT_EQ(json.find("\"web_data_key\"", json.find("\"Plain\"")), std::string::npos);

    const LoadResult result = LoadText(json);
    ASSERT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.spaces.size(), 2u);
    EXPECT_EQ(result.state.spaces[0].web_data_key, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(result.state.spaces[1].web_data_key, "");
}

TEST(SessionStore, RoutesRoundTripAndAreOptional) {
    SessionState state;
    SpaceState space;
    space.id = SpaceId{1};
    space.name = "Dev";
    space.color_argb = 1;
    space.routes = {"github.com", "linear.app"};
    state.spaces = {space};

    LoadResult result = LoadText(SaveToText(state));
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state.spaces[0].routes,
              (std::vector<std::string>{"github.com", "linear.app"}));

    // A routes member that is not an array loads as no routes, and the session still loads.
    result = LoadText(
        R"({"version":1,"spaces":[{"id":1,"name":"Dev","color_argb":1,"routes":"github.com","tabs":[]}]})");
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_TRUE(result.state.spaces[0].routes.empty());
}

TEST(SessionStore, ImportKeyRoundTrips) {
    SessionState state;
    SpaceState space;
    space.id = SpaceId{1};
    space.name = "Arc";
    space.color_argb = 1;
    space.import_key = "arc:space:4f2a";
    state.spaces = {space};

    const LoadResult result = LoadText(SaveToText(state));
    ASSERT_EQ(result.error, SessionError::kNone);
    EXPECT_EQ(result.state.spaces[0].import_key, "arc:space:4f2a");
}

TEST(SessionStore, Loads090SessionFixture) {
    const auto path =
        std::filesystem::path(ISLAND_TEST_FIXTURE_DIR) / "session" / "session-0.9.0.json";
    const LoadResult result = SessionStore::Load(path);
    ASSERT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.spaces.size(), 2u);
    EXPECT_EQ(result.state.spaces[0].name, "Work");
    EXPECT_EQ(result.state.spaces[0].icon_id, "briefcase");
    EXPECT_EQ(result.state.spaces[0].tabs.size(), 3u);
    EXPECT_TRUE(result.state.spaces[0].tabs[0].pinned);
    EXPECT_TRUE(result.state.spaces[0].split.has_value());
    EXPECT_EQ(result.state.spaces[1].name, "Home");
    EXPECT_EQ(result.state.favorite_tabs.size(), 2u);
    // Fields that 0.9 never wrote come back empty.
    for (const SpaceState& space : result.state.spaces) {
        EXPECT_EQ(space.web_data_key, "");
        EXPECT_EQ(space.import_key, "");
        EXPECT_TRUE(space.routes.empty());
    }
}

TEST(SessionStore, UnknownMembersIgnored) {
    const std::string json =
        R"({"version":1,"future_top":{"a":[1,2]},"spaces":[{"id":1,"name":"Kept","color_argb":1,)"
        R"("future_space":true,"tabs":[{"id":1,"url":"https://a.test/","future_tab":null}]}]})";
    const LoadResult result = LoadText(json);
    ASSERT_EQ(result.error, SessionError::kNone);
    ASSERT_EQ(result.state.spaces.size(), 1u);
    EXPECT_EQ(result.state.spaces[0].name, "Kept");
    ASSERT_EQ(result.state.spaces[0].tabs.size(), 1u);
    EXPECT_EQ(result.state.spaces[0].tabs[0].url, "https://a.test/");
}

}  // namespace
}  // namespace island
