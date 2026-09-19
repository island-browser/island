#include "bookmark_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace island {
namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("island_bookmarks_test_" + name);
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

TEST(BookmarkStoreTest, GivenFreshInstallWhenLoadedThenEmptyStoreAndReadError) {
    const BookmarkLoadResult result = BookmarkStore::Load(TempPath("missing.json"));
    EXPECT_EQ(result.error, BookmarkError::kFileReadError);
    EXPECT_TRUE(result.state.folders.empty());
}

TEST(BookmarkStoreTest, GivenARoundTripWhenSavedAndLoadedThenFoldersSurvive) {
    BookmarkState state;
    state.folders.push_back(
        {"Imported", {{"Docs", "https://example.com/docs"}, {"Island", "data:text/html,Island"}}});
    state.folders.push_back({"Work", {{"Tracker", "https://example.org/tracker"}}});

    const std::filesystem::path path = TempPath("roundtrip.json");
    ASSERT_EQ(BookmarkStore::Save(path, state), BookmarkError::kNone);
    const BookmarkLoadResult loaded = BookmarkStore::Load(path);
    EXPECT_EQ(loaded.error, BookmarkError::kNone);
    EXPECT_EQ(loaded.state, state);
}

TEST(BookmarkStoreTest, GivenCorruptJsonWhenLoadedThenParseErrorAndEmptyStore) {
    const std::filesystem::path path = TempPath("corrupt.json");
    WriteFile(path, "not json at all");
    const BookmarkLoadResult loaded = BookmarkStore::Load(path);
    EXPECT_EQ(loaded.error, BookmarkError::kParseError);
    EXPECT_TRUE(loaded.state.folders.empty());
}

TEST(BookmarkStoreTest, GivenAMalformedFolderWhenLoadedThenSchemaErrorAndEmptyStore) {
    const std::filesystem::path path = TempPath("schema.json");
    WriteFile(path, R"({"version":1,"folders":[{"name":"Broken"}]})");
    const BookmarkLoadResult loaded = BookmarkStore::Load(path);
    EXPECT_EQ(loaded.error, BookmarkError::kSchemaError);
    EXPECT_TRUE(loaded.state.folders.empty());
}

TEST(BookmarkStoreTest, GivenImportedItemsWhenMergedThenDupesAreSkippedCaseInsensitively) {
    BookmarkState state;
    state.folders.push_back({"Imported", {{"Docs", "https://example.com/docs"}}});

    // Same URL with different case, plus two genuinely new entries, plus an
    // intra-batch duplicate of one of those new entries.
    const std::size_t added =
        BookmarkStore::MergeFolder(state, "Imported",
                                   {{"Docs copy", "HTTPS://EXAMPLE.COM/docs"},
                                    {"Mail", "https://example.com/mail"},
                                    {"News", "https://example.com/news"},
                                    {"News again", "https://example.com/news"}});
    EXPECT_EQ(added, 2U);
    EXPECT_EQ(state.folders.size(), 1U);
    EXPECT_EQ(state.folders[0].items.size(), 3U);
    EXPECT_EQ(state.folders[0].items[1].title, "Mail");
    EXPECT_EQ(state.folders[0].items[2].title, "News");
}

TEST(BookmarkStoreTest, GivenAMissingFolderNameWhenMergedThenTheFolderIsCreatedAtTheEnd) {
    BookmarkState state;
    state.folders.push_back({"Imported", {{"Docs", "https://example.com/docs"}}});

    EXPECT_EQ(BookmarkStore::MergeFolder(state, "User picks", {{"Home", "https://example.org/"}}),
              1U);
    ASSERT_EQ(state.folders.size(), 2U);
    EXPECT_EQ(state.folders[1].name, "User picks");
    EXPECT_EQ(state.folders[1].items.size(), 1U);
}

}  // namespace
}  // namespace island
