#include "browser_import.h"

#include <gtest/gtest.h>
#if defined(ISLAND_HAS_SQLITE3)
#include <sqlite3.h>
#endif

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "bookmark_import.h"

namespace island {
namespace {

// A real Firefox-style backup: the bookmarks JSON compressed by the reference
// LZ4 implementation (python-lz4 block mode) inside Mozilla's container.
constexpr unsigned char kRealBackup[] = {
    0x6d, 0x6f, 0x7a, 0x4c, 0x7a, 0x34, 0x30, 0x00, 0xca, 0x01, 0x00, 0x00, 0xe3, 0x7b, 0x22, 0x67,
    0x75, 0x69, 0x64, 0x22, 0x3a, 0x22, 0x72, 0x6f, 0x6f, 0x74, 0x5f, 0x01, 0x00, 0xf5, 0x2e, 0x22,
    0x2c, 0x22, 0x74, 0x69, 0x74, 0x6c, 0x65, 0x22, 0x3a, 0x22, 0x22, 0x2c, 0x22, 0x74, 0x79, 0x70,
    0x65, 0x22, 0x3a, 0x22, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x78, 0x2d, 0x6d, 0x6f, 0x7a, 0x2d, 0x70,
    0x6c, 0x61, 0x63, 0x65, 0x2d, 0x63, 0x6f, 0x6e, 0x74, 0x61, 0x69, 0x6e, 0x65, 0x72, 0x22, 0x2c,
    0x22, 0x63, 0x68, 0x69, 0x6c, 0x64, 0x72, 0x65, 0x6e, 0x22, 0x3a, 0x5b, 0x52, 0x00, 0x71, 0x74,
    0x6f, 0x6f, 0x6c, 0x62, 0x61, 0x72, 0x54, 0x00, 0x07, 0x52, 0x00, 0x03, 0x17, 0x00, 0x0f, 0x59,
    0x00, 0x21, 0x04, 0x95, 0x00, 0xbf, 0x49, 0x73, 0x6c, 0x61, 0x6e, 0x64, 0x20, 0x64, 0x6f, 0x63,
    0x73, 0x47, 0x00, 0x07, 0xf1, 0x03, 0x22, 0x2c, 0x22, 0x75, 0x72, 0x69, 0x22, 0x3a, 0x22, 0x68,
    0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x69, 0x36, 0x00, 0x61, 0x2e, 0x74, 0x65, 0x73, 0x74,
    0x2f, 0x3b, 0x00, 0x2f, 0x7d, 0x2c, 0x53, 0x00, 0x02, 0x7f, 0x20, 0x6d, 0x69, 0x72, 0x72, 0x6f,
    0x72, 0x5a, 0x00, 0x28, 0x13, 0x2f, 0x42, 0x00, 0x01, 0x61, 0x00, 0x0f, 0x3e, 0x01, 0x04, 0x74,
    0x73, 0x65, 0x70, 0x61, 0x72, 0x61, 0x74, 0x26, 0x00, 0x03, 0x6f, 0x01, 0x6f, 0x52, 0x65, 0x63,
    0x65, 0x6e, 0x74, 0x7b, 0x00, 0x10, 0x01, 0x83, 0x01, 0xd0, 0x3a, 0x73, 0x6f, 0x72, 0x74, 0x3d,
    0x38, 0x22, 0x7d, 0x5d, 0x7d, 0x5d, 0x7d,
};

std::string RealBackup() {
    return std::string(reinterpret_cast<const char*>(kRealBackup), sizeof(kRealBackup));
}

// Wraps `text` as a valid literal-only LZ4 block inside a mozLz4 container.
std::string LiteralMozLz4(const std::string& text) {
    std::string out("mozLz40\0", 8);
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((size >> (8 * i)) & 0xFF));
    if (text.size() < 15) {
        out.push_back(static_cast<char>(text.size() << 4));
    } else {
        out.push_back(static_cast<char>(0xF0));
        std::size_t rest = text.size() - 15;
        while (rest >= 255) {
            out.push_back(static_cast<char>(255));
            rest -= 255;
        }
        out.push_back(static_cast<char>(rest));
    }
    return out + text;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

std::string SqlitePath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

void WriteFirefoxPlaces(const std::filesystem::path& path,
                        std::string_view url = "https://profile.test/") {
#if defined(ISLAND_HAS_SQLITE3)
    std::filesystem::create_directories(path.parent_path());
    sqlite3* database = nullptr;
    const std::string sqlite_path = SqlitePath(path);
    ASSERT_EQ(sqlite3_open(sqlite_path.c_str(), &database), SQLITE_OK);
    ASSERT_NE(database, nullptr);
    ASSERT_EQ(
        sqlite3_exec(database,
                     "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT);"
                     "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, fk "
                     "INTEGER, title TEXT, parent INTEGER, position INTEGER, guid TEXT);"
                     "INSERT INTO moz_places VALUES (1, 'https://profile.test/');"
                     "INSERT INTO moz_bookmarks VALUES (1, 1, 1, 'Profile bookmark', 1, 0, '');",
                     nullptr, nullptr, nullptr),
        SQLITE_OK);
    if (url != "https://profile.test/") {
        ASSERT_EQ(sqlite3_exec(database, "UPDATE moz_places SET url = 'https://alternate.test/';",
                               nullptr, nullptr, nullptr),
                  SQLITE_OK);
    }
    ASSERT_EQ(sqlite3_close(database), SQLITE_OK);
#else
    static_cast<void>(path);
#endif
}

std::filesystem::path TempHome(const std::string& name) {
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() / ("island_import_test_" + name);
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    return home;
}

TEST(BrowserImportTest, DecodesRealLz4OutputWithMatches) {
    const auto decoded = DecodeMozLz4(RealBackup());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->rfind("{\"guid\":\"root________\"", 0), 0U);
    const auto bookmarks = ParseFirefoxBookmarks(*decoded);
    ASSERT_TRUE(bookmarks.has_value());
    // The place: query and the separator are not bookmarks.
    EXPECT_EQ(*bookmarks,
              (std::vector<BookmarkItem>{
                  {.title = "Island docs", .url = "https://island.test/docs"},
                  {.title = "Island docs mirror", .url = "https://island.test/docs/mirror"}}));
}

TEST(BrowserImportTest, DecodesOverlappingMatchesAndLongLiterals) {
    // "abc" then a 9-byte match at offset 3 (overlapping), then "!".
    std::string block("mozLz40\0", 8);
    block += std::string("\x0d\x00\x00\x00", 4);
    block += std::string(
        "\x35"
        "abc"
        "\x03\x00"
        "\x10"
        "!",
        8);
    EXPECT_EQ(DecodeMozLz4(block), "abcabcabcabc!");
    const std::string long_text(700, 'x');
    EXPECT_EQ(DecodeMozLz4(LiteralMozLz4(long_text)), long_text);
}

TEST(BrowserImportTest, RejectsCorruptContainers) {
    std::string bad_magic = RealBackup();
    bad_magic[0] = 'X';
    EXPECT_FALSE(DecodeMozLz4(bad_magic).has_value());
    EXPECT_FALSE(DecodeMozLz4(RealBackup().substr(0, RealBackup().size() - 5)).has_value());
    std::string bad_offset("mozLz40\0", 8);
    bad_offset += std::string("\x08\x00\x00\x00", 4);
    bad_offset += std::string(
        "\x14"
        "a"
        "\x09\x00",
        4);  // offset 9 > 1 byte of output
    EXPECT_FALSE(DecodeMozLz4(bad_offset).has_value());
    std::string wrong_size = LiteralMozLz4("hello");
    wrong_size[8] = 9;  // claims 9 bytes, carries 5
    EXPECT_FALSE(DecodeMozLz4(wrong_size).has_value());
    EXPECT_FALSE(DecodeMozLz4(LiteralMozLz4("hello"), 3).has_value());  // over the cap
    EXPECT_FALSE(DecodeMozLz4("").has_value());
}

TEST(BrowserImportTest, FirefoxImportPicksTheNewestBackupOfAnyProfile) {
    const std::filesystem::path home = TempHome("firefox");
#if defined(_WIN32)
    const std::filesystem::path root = home / "AppData/Roaming/Mozilla/Firefox/Profiles";
#elif defined(__APPLE__)
    const std::filesystem::path root = home / "Library/Application Support/Firefox/Profiles";
#else
    const std::filesystem::path root = home / ".mozilla/firefox";
#endif
    const std::string old_json =
        R"({"type":"text/x-moz-place-container","children":[{"type":"text/x-moz-place","title":"Old","uri":"https://old.test/"}]})";
    WriteFile(root / "a.default/bookmarkbackups/bookmarks-2024-01-01.jsonlz4",
              LiteralMozLz4(old_json));
    WriteFile(root / "b.default-release/bookmarkbackups/bookmarks-2026-10-01.jsonlz4",
              RealBackup());
    std::filesystem::last_write_time(
        root / "a.default/bookmarkbackups/bookmarks-2024-01-01.jsonlz4",
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(48));

    EXPECT_EQ(FindFirefoxBookmarksBackup(home).filename(), "bookmarks-2026-10-01.jsonlz4");
    const auto imported = ImportFromSource(ImportSource::kFirefox, home);
    ASSERT_TRUE(imported.has_value());
    ASSERT_EQ(imported->size(), 2U);
    EXPECT_EQ((*imported)[0].title, "Island docs");

    bool listed = false;
    for (const ImportSourceInfo& info : DetectInstalledSources(home)) {
        if (info.id == ImportSource::kFirefox) {
            listed = true;
            EXPECT_TRUE(info.available);
        }
    }
    EXPECT_TRUE(listed);
    std::filesystem::remove_all(home);
}

TEST(BrowserImportTest, FirefoxProfilesIniNonDefaultPlacesDatabaseImportsWhileSourceIsBusy) {
#if !defined(ISLAND_HAS_SQLITE3)
    GTEST_SKIP() << "SQLite is unavailable in this toolchain.";
#else
    const std::filesystem::path home = TempHome("firefox_places");
#if defined(_WIN32)
    const std::filesystem::path data_root = home / "AppData/Roaming/Mozilla/Firefox";
#elif defined(__APPLE__)
    const std::filesystem::path data_root = home / "Library/Application Support/Firefox";
#else
    const std::filesystem::path data_root = home / ".mozilla/firefox";
#endif
    const std::filesystem::path profile = data_root / "outside-default";
    WriteFile(data_root / "profiles.ini",
              "[Profile0]\nPath=outside-default\nIsRelative=1\nDefault=1\n");
    WriteFirefoxPlaces(profile / "places.sqlite");

    sqlite3* busy = nullptr;
    const std::string sqlite_path = SqlitePath(profile / "places.sqlite");
    ASSERT_EQ(sqlite3_open(sqlite_path.c_str(), &busy), SQLITE_OK);
    ASSERT_NE(busy, nullptr);
    ASSERT_EQ(sqlite3_exec(busy, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(FindFirefoxPlacesDatabase(home), profile / "places.sqlite");
    const auto imported = ImportFromSource(ImportSource::kFirefox, home);
    ASSERT_TRUE(imported.has_value());
    ASSERT_EQ(imported->size(), 1U);
    EXPECT_EQ((*imported)[0].url, "https://profile.test/");
    ASSERT_EQ(sqlite3_exec(busy, "ROLLBACK;", nullptr, nullptr, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(busy), SQLITE_OK);
#endif
}

TEST(BrowserImportTest, FirefoxWalDatabaseCopiesItsLiveBookmarks) {
#if !defined(ISLAND_HAS_SQLITE3)
    GTEST_SKIP() << "SQLite is unavailable in this toolchain.";
#else
    const std::filesystem::path home = TempHome("firefox_wal");
#if defined(_WIN32)
    const std::filesystem::path root = home / "AppData/Roaming/Mozilla/Firefox/Profiles";
#elif defined(__APPLE__)
    const std::filesystem::path root = home / "Library/Application Support/Firefox/Profiles";
#else
    const std::filesystem::path root = home / ".mozilla/firefox";
#endif
    const std::filesystem::path places = root / "wal.default-release/places.sqlite";
    std::filesystem::create_directories(places.parent_path());
    sqlite3* writer = nullptr;
    const std::string sqlite_path = SqlitePath(places);
    ASSERT_EQ(sqlite3_open(sqlite_path.c_str(), &writer), SQLITE_OK);
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(sqlite3_exec(writer,
                           "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0;"
                           "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT);"
                           "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, fk "
                           "INTEGER, title TEXT, parent INTEGER, position INTEGER, guid TEXT);"
                           "INSERT INTO moz_places VALUES (1, 'https://wal-only.test/');"
                           "INSERT INTO moz_bookmarks VALUES (1, 1, 1, 'WAL only', 1, 0, '');",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    ASSERT_TRUE(std::filesystem::is_regular_file(places.string() + "-wal"));

    const auto imported = ImportFromSource(ImportSource::kFirefox, home);
    ASSERT_TRUE(imported.has_value());
    ASSERT_EQ(imported->size(), 1U);
    EXPECT_EQ((*imported)[0].url, "https://wal-only.test/");
    ASSERT_EQ(sqlite3_close(writer), SQLITE_OK);
    std::filesystem::remove_all(home);
#endif
}

TEST(BrowserImportTest, FirefoxPlacesMatchesBackupBookmarkRulesAndPositionOrder) {
#if !defined(ISLAND_HAS_SQLITE3)
    GTEST_SKIP() << "SQLite is unavailable in this toolchain.";
#else
    const std::filesystem::path home = TempHome("firefox_places_rules");
#if defined(_WIN32)
    const std::filesystem::path root = home / "AppData/Roaming/Mozilla/Firefox/Profiles";
#elif defined(__APPLE__)
    const std::filesystem::path root = home / "Library/Application Support/Firefox/Profiles";
#else
    const std::filesystem::path root = home / ".mozilla/firefox";
#endif
    const std::filesystem::path places = root / "rules.default/places.sqlite";
    std::filesystem::create_directories(places.parent_path());
    sqlite3* database = nullptr;
    const std::string sqlite_path = SqlitePath(places);
    ASSERT_EQ(sqlite3_open(sqlite_path.c_str(), &database), SQLITE_OK);
    ASSERT_EQ(
        sqlite3_exec(database,
                     "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT);"
                     "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, fk "
                     "INTEGER, title TEXT, parent INTEGER, position INTEGER, guid TEXT);"
                     "INSERT INTO moz_places VALUES (1, 'https://second.test/'),"
                     "(2, 'place:sort=8'), (3, 'javascript:alert(1)'),"
                     "(4, 'https://tagged.test/'), (5, 'https://first.test/');"
                     "INSERT INTO moz_bookmarks VALUES (10, 2, NULL, 'Tags', 0, 0, 'tags________'),"
                     "(11, 2, NULL, 'Tag', 10, 0, ''),"
                     "(12, 1, 4, 'Tagged', 11, 0, ''),"
                     "(13, 1, 2, 'Smart', 1, 0, ''),"
                     "(14, 1, 3, 'Script', 1, 1, ''),"
                     "(15, 1, 1, 'Second', 1, 2, ''),"
                     "(16, 1, 5, 'First', 1, 1, '');",
                     nullptr, nullptr, nullptr),
        SQLITE_OK);
    ASSERT_EQ(sqlite3_close(database), SQLITE_OK);

    const auto imported = ImportFromSource(ImportSource::kFirefox, home);
    ASSERT_TRUE(imported.has_value());
    EXPECT_EQ(*imported,
              (std::vector<BookmarkItem>{{.title = "First", .url = "https://first.test/"},
                                         {.title = "Second", .url = "https://second.test/"}}));
    std::filesystem::remove_all(home);
#endif
}

TEST(BrowserImportTest, FirefoxPlacesPrefersProfilesIniDefaultThenNewestDatabase) {
#if !defined(ISLAND_HAS_SQLITE3)
    GTEST_SKIP() << "SQLite is unavailable in this toolchain.";
#else
    const std::filesystem::path home = TempHome("firefox_profile_choice");
#if defined(_WIN32)
    const std::filesystem::path root = home / "AppData/Roaming/Mozilla/Firefox";
#elif defined(__APPLE__)
    const std::filesystem::path root = home / "Library/Application Support/Firefox";
#else
    const std::filesystem::path root = home / ".mozilla/firefox";
#endif
    const std::filesystem::path stale = root / "Profiles/a.default/places.sqlite";
    const std::filesystem::path preferred = root / "Profiles/b.default-release/places.sqlite";
    WriteFirefoxPlaces(stale);
    WriteFirefoxPlaces(preferred, "https://alternate.test/");
    std::filesystem::last_write_time(stale, std::filesystem::file_time_type::clock::now());
    WriteFile(root / "profiles.ini",
              "[InstallABC]\nDefault=Profiles/b.default-release\n\n[Profile0]\n"
              "Path=Profiles/b.default-release\nIsRelative=1\nDefault=1\n");
    EXPECT_EQ(FindFirefoxPlacesDatabase(home), preferred);
    const auto imported = ImportFromSource(ImportSource::kFirefox, home);
    ASSERT_TRUE(imported.has_value());
    ASSERT_EQ(imported->size(), 1U);
    EXPECT_EQ((*imported)[0].url, "https://alternate.test/");
    std::filesystem::remove_all(home);
#endif
}

TEST(BrowserImportTest, FirefoxPlacesPrefersInstallDefaultOverProfileDefaultInEitherOrder) {
#if !defined(ISLAND_HAS_SQLITE3)
    GTEST_SKIP() << "SQLite is unavailable in this toolchain.";
#else
    const std::array<std::string, 2> ini_files{
        "[Profile0]\nPath=Profiles/a.profile-default\nIsRelative=1\nDefault=1\n\n"
        "[InstallABC]\nDefault=Profiles/b.install-default\n",
        "[InstallABC]\nDefault=Profiles/b.install-default\n\n"
        "[Profile0]\nPath=Profiles/a.profile-default\nIsRelative=1\nDefault=1\n"};
    for (std::size_t index = 0; index < ini_files.size(); ++index) {
        const std::filesystem::path home =
            TempHome("firefox_install_choice_" + std::to_string(index));
#if defined(_WIN32)
        const std::filesystem::path root = home / "AppData/Roaming/Mozilla/Firefox";
#elif defined(__APPLE__)
        const std::filesystem::path root = home / "Library/Application Support/Firefox";
#else
        const std::filesystem::path root = home / ".mozilla/firefox";
#endif
        const std::filesystem::path profile_default =
            root / "Profiles/a.profile-default/places.sqlite";
        const std::filesystem::path install_default =
            root / "Profiles/b.install-default/places.sqlite";
        WriteFirefoxPlaces(profile_default);
        WriteFirefoxPlaces(install_default, "https://alternate.test/");
        WriteFile(root / "profiles.ini", ini_files[index]);

        SCOPED_TRACE(index);
        EXPECT_EQ(FindFirefoxPlacesDatabase(home), install_default);
        const auto imported = ImportFromSource(ImportSource::kFirefox, home);
        ASSERT_TRUE(imported.has_value());
        ASSERT_EQ(imported->size(), 1U);
        EXPECT_EQ((*imported)[0].url, "https://alternate.test/");
        std::filesystem::remove_all(home);
    }
#endif
}

constexpr std::string_view kArcSidebar = R"({
  "sidebar": {"containers": [
    {"global": {}},
    {"spaces": [
       "S1", {"id": "S1", "title": "Work", "containerIDs": ["pinned", "P1", "unpinned", "U1"],
              "customInfo": {"windowTheme": {"background": {"single": {"_0": {"style": {"color": {"_0": {
                  "blendedSingleColor": {"_0": {"color": {"red": 0.2, "green": 0.4, "blue": 1.0, "alpha": 1}}}}}}}}}}}},
       "S2", {"id": "S2", "title": "", "containerIDs": ["pinned", "P2", "unpinned", "U2"]}
     ],
     "items": [
       "P1", {"id": "P1", "childrenIds": ["T1", "F1"], "data": {"itemContainer": {}}},
       "T1", {"id": "T1", "title": null, "childrenIds": [], "data": {"tab": {"savedURL": "https://linear.app/", "savedTitle": "Linear"}}},
       "F1", {"id": "F1", "title": "Docs", "childrenIds": ["T2"], "data": {"list": {}}},
       "T2", {"id": "T2", "title": "Spec", "childrenIds": [], "data": {"tab": {"savedURL": "https://spec.test/", "savedTitle": "x"}}},
       "U1", {"id": "U1", "childrenIds": ["T3"], "data": {"itemContainer": {}}},
       "T3", {"id": "T3", "title": "Today tab", "childrenIds": [], "data": {"tab": {"savedURL": "https://today.test/"}}},
       "P2", {"id": "P2", "childrenIds": [], "data": {"itemContainer": {}}}
     ]}
  ]}
})";

TEST(BrowserImportTest, ArcSpacesKeepTitlesColorsAndPinnedTabsInOrder) {
    const auto spaces = ParseArcSidebar(kArcSidebar);
    ASSERT_TRUE(spaces.has_value());
    ASSERT_EQ(spaces->size(), 2U);
    EXPECT_EQ((*spaces)[0].name, "Work");
    EXPECT_EQ((*spaces)[0].color_argb, 0xFF3366FFU);
    // "Spec" sits in the pinned folder "Docs", so it is no longer a top-level pinned tab (R-I1).
    EXPECT_EQ((*spaces)[0].pinned_tabs,
              (std::vector<BookmarkItem>{{.title = "Linear", .url = "https://linear.app/"}}));
    EXPECT_EQ((*spaces)[1].name, "Space 2");
    EXPECT_FALSE((*spaces)[1].color_argb.has_value());
    EXPECT_TRUE((*spaces)[1].pinned_tabs.empty());
    EXPECT_FALSE(ParseArcSidebar("{}").has_value());
    EXPECT_FALSE(ParseArcSidebar("not json").has_value());
}

// tests/fixtures/arc/synthetic-folders.json, which the anonymizer test also uses.
std::filesystem::path SyntheticFoldersPath() {
    return std::filesystem::path(ISLAND_ARC_FIXTURE_DIR) / "synthetic-folders.json";
}

std::optional<ArcImport> ParseSyntheticFolders() {
    const std::optional<std::string> text = ReadWholeFile(SyntheticFoldersPath());
    if (!text) return std::nullopt;
    return ParseArcSidebarStructure(*text);
}

TEST(BrowserImportTest, ArcFoldersKeepTitlesAndOrder) {
    const std::optional<ArcImport> arc = ParseSyntheticFolders();
    ASSERT_TRUE(arc.has_value());
    ASSERT_EQ(arc->spaces.size(), 2U);
    const ImportedSpace& work = arc->spaces[0];
    EXPECT_EQ(work.pinned_tabs,
              (std::vector<BookmarkItem>{{.title = "Linear", .url = "https://linear.app/"},
                                         {.title = "Mail", .url = "https://mail.test/"}}));
    // The empty folder "Empty" yields no entry; "Work" comes before its nested "Specs".
    ASSERT_EQ(work.folders.size(), 2U);
    EXPECT_EQ(work.folders[0].path, "Work");
    EXPECT_EQ(work.folders[0].items,
              (std::vector<BookmarkItem>{{.title = "Spec", .url = "https://spec.test/"}}));
    const ImportedSpace& second = arc->spaces[1];
    EXPECT_EQ(second.name, "Space 2");
    EXPECT_TRUE(second.pinned_tabs.empty());
    ASSERT_EQ(second.folders.size(), 1U);
    EXPECT_EQ(second.folders[0].path, "Reading");
}

TEST(BrowserImportTest, ArcNestedFoldersFlattenToPathNames) {
    const std::optional<ArcImport> arc = ParseSyntheticFolders();
    ASSERT_TRUE(arc.has_value());
    ASSERT_EQ(arc->spaces.size(), 2U);
    ASSERT_GE(arc->spaces[0].folders.size(), 2U);
    const ImportedFolder& nested = arc->spaces[0].folders[1];
    EXPECT_EQ(nested.path, "Work / Specs");
    EXPECT_EQ(nested.items,
              (std::vector<BookmarkItem>{{.title = "Plan", .url = "https://plan.test/"}}));
}

TEST(BrowserImportTest, ArcFavoritesInOrder) {
    const std::optional<ArcImport> arc = ParseSyntheticFolders();
    ASSERT_TRUE(arc.has_value());
    EXPECT_EQ(arc->favorites,
              (std::vector<BookmarkItem>{{.title = "Docs", .url = "https://docs.test/"},
                                         {.title = "Calendar", .url = "https://cal.test/"}}));
}

TEST(BrowserImportTest, ArcSpaceIdsAreReturned) {
    const std::optional<ArcImport> arc = ParseSyntheticFolders();
    ASSERT_TRUE(arc.has_value());
    ASSERT_EQ(arc->spaces.size(), 2U);
    EXPECT_EQ(arc->spaces[0].arc_id, "S1");
    EXPECT_EQ(arc->spaces[1].arc_id, "S2");
}

TEST(BrowserImportTest, ArcImportFileReadsStructureAndRejectsMissingFiles) {
    const std::filesystem::path home = TempHome("arc_file");
    const std::filesystem::path file = home / "StorableSidebar.json";
    WriteFile(file, ReadWholeFile(SyntheticFoldersPath()).value_or(""));
    const std::optional<ArcImport> arc = ImportArcFile(file);
    ASSERT_TRUE(arc.has_value());
    EXPECT_EQ(arc->spaces.size(), 2U);
    EXPECT_EQ(arc->favorites.size(), 2U);
    EXPECT_FALSE(ImportArcFile(home / "missing.json").has_value());
    std::filesystem::remove_all(home);
}

// The flat import (ImportFromSource(kArc), plain bookmarks) keeps Arc's pinned
// order: a folder's tabs sit where the folder sits, not after the top-level tabs.
TEST(BrowserImportTest, ArcFlatImportKeepsPinnedOrderAcrossFolders) {
#if defined(__APPLE__)
    const std::filesystem::path home = TempHome("arc_flat");
    WriteFile(home / "Library/Application Support/Arc/StorableSidebar.json",
              ReadWholeFile(SyntheticFoldersPath()).value_or(""));
    const std::optional<std::vector<BookmarkItem>> items =
        ImportFromSource(ImportSource::kArc, home);
    std::filesystem::remove_all(home);
    ASSERT_TRUE(items.has_value());
    EXPECT_EQ(*items,
              (std::vector<BookmarkItem>{{.title = "Linear", .url = "https://linear.app/"},
                                         {.title = "Spec", .url = "https://spec.test/"},
                                         {.title = "Plan", .url = "https://plan.test/"},
                                         {.title = "Mail", .url = "https://mail.test/"},
                                         {.title = "Article", .url = "https://read.test/a"}}));
#else
    GTEST_SKIP() << "Arc's sidebar file is only located on macOS";
#endif
}

TEST(BrowserImportTest, SourceIdsRoundTrip) {
    for (const ImportSourceInfo& info : DetectInstalledSources(TempHome("ids"))) {
        EXPECT_EQ(ImportSourceFromId(ImportSourceId(info.id)), info.id);
        EXPECT_FALSE(ImportSourceDescription(info).empty());
    }
    EXPECT_FALSE(ImportSourceFromId("netscape").has_value());
}

}  // namespace
}  // namespace island
