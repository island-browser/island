#include "bookmark_import.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace island {
namespace {

std::filesystem::path TempHome(const std::string& name) {
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() / ("island_import_test_" + name);
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    return home;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

TEST(BookmarkImportTest, GivenAChromiumBookmarksFileWhenParsedThenUrlEntriesAppearInOrder) {
    const std::string fixture = R"({
        "roots": {
            "bookmark_bar": {
                "children": [
                    {"type": "url", "name": "Docs", "url": "https://example.com/docs"},
                    {"type": "folder", "name": "Reading", "children": [
                        {"type": "url", "name": "Spec", "url": "https://example.com/spec"}
                    ]}
                ]
            },
            "other": {"children": [
                {"type": "url", "name": "Mail", "url": "https://example.com/mail"}
            ]},
            "synced": {"children": []}
        },
        "version": 1
    })";

    const auto parsed = ParseChromiumBookmarks(fixture);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 3U);
    EXPECT_EQ((*parsed)[0].title, "Docs");
    EXPECT_EQ((*parsed)[0].url, "https://example.com/docs");
    EXPECT_EQ((*parsed)[1].url, "https://example.com/spec");
    EXPECT_EQ((*parsed)[2].url, "https://example.com/mail");
}

TEST(BookmarkImportTest, GivenEmptyEntriesAndNoiseWhenParsedThenOnlyRealUrlsSurvive) {
    const std::string fixture = R"({
        "roots": {
            "bookmark_bar": {"children": [
                {"type": "url", "name": "No url", "url": ""},
                {"type": "folder", "name": "Empty", "children": []},
                {"type": "url", "url": "https://example.com/untitled"}
            ]}
        }
    })";

    const auto parsed = ParseChromiumBookmarks(fixture);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1U);
    EXPECT_EQ((*parsed)[0].url, "https://example.com/untitled");
    // A missing name leaves the title empty; the palette later falls back to
    // the URL itself.
    EXPECT_TRUE((*parsed)[0].title.empty());
}

TEST(BookmarkImportTest, GivenTextThatIsNotABookmarksFileWhenParsedThenItFails) {
    EXPECT_FALSE(ParseChromiumBookmarks("not json").has_value());
    EXPECT_FALSE(ParseChromiumBookmarks("[1, 2, 3]").has_value());
    EXPECT_FALSE(ParseChromiumBookmarks(R"({"version": 1})").has_value());
}

TEST(BookmarkImportTest, GivenTheEntryCapWhenParsedThenTheListStopsAtTheLimit) {
    std::string fixture = R"({"roots": {"bookmark_bar": {"children": [)";
    for (int i = 0; i < 600; ++i) {
        if (i > 0) {
            fixture += ',';
        }
        fixture += "{\"type\":\"url\",\"name\":\"s" + std::to_string(i) +
                   "\",\"url\":\"https://example.com/" + std::to_string(i) + "\"}";
    }
    fixture += "]}}}";
    const auto parsed = ParseChromiumBookmarks(fixture);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->size(), kMaxImportedBookmarks);
}

TEST(BookmarkImportTest, GivenInstalledBrowsersWhenDetectedThenOnlyReadableOnesAreAvailable) {
    const std::filesystem::path home = TempHome("detect");
    // Chrome (Chromium-family path) and nothing else.
    WriteFile(home / "Library/Application Support/Google/Chrome/Default/Bookmarks",
              R"({"roots": {"bookmark_bar": {"children": []}}})");

    const std::vector<ImportSourceInfo> sources = DetectInstalledSources(home);
    ASSERT_FALSE(sources.empty());
    bool saw_chrome = false;
    for (const ImportSourceInfo& info : sources) {
        if (info.id == ImportSource::kChrome) {
            saw_chrome = true;
            EXPECT_TRUE(info.available);
            EXPECT_EQ(info.display_name, "Google Chrome");
        } else {
            EXPECT_FALSE(info.available) << info.display_name;
        }
    }
    EXPECT_TRUE(saw_chrome);
}

TEST(BookmarkImportTest, GivenAnImportableSourceWhenImportedThenItsBookmarksAreReturned) {
    const std::filesystem::path home = TempHome("import");
    WriteFile(home / "Library/Application Support/Google/Chrome/Default/Bookmarks",
              R"({"roots": {"bookmark_bar": {"children": [
                  {"type": "url", "name": "Docs", "url": "https://example.com/docs"}
              ]}}})");

    const auto imported = ImportFromSource(ImportSource::kChrome, home);
    ASSERT_TRUE(imported.has_value());
    ASSERT_EQ(imported->size(), 1U);
    EXPECT_EQ((*imported)[0].title, "Docs");

    // A source whose file is absent reports unavailability rather than an
    // empty success.
    const auto missing = ImportFromSource(ImportSource::kEdge, home);
    EXPECT_FALSE(missing.has_value());
}

}  // namespace
}  // namespace island
