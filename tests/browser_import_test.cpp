#include "browser_import.h"

#include <gtest/gtest.h>

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
    EXPECT_EQ((*spaces)[0].pinned_tabs,
              (std::vector<BookmarkItem>{{.title = "Linear", .url = "https://linear.app/"},
                                         {.title = "Spec", .url = "https://spec.test/"}}));
    EXPECT_EQ((*spaces)[1].name, "Space 2");
    EXPECT_FALSE((*spaces)[1].color_argb.has_value());
    EXPECT_TRUE((*spaces)[1].pinned_tabs.empty());
    EXPECT_FALSE(ParseArcSidebar("{}").has_value());
    EXPECT_FALSE(ParseArcSidebar("not json").has_value());
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
