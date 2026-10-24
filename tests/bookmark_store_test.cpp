#include "bookmark_store.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace island {
namespace {

constexpr char kSchemaV1[] =
    R"({"version":1,"folders":[)"
    R"({"name":"Imported","items":[{"title":"Docs","url":"https://example.com/docs"},)"
    R"({"title":"Island","url":"https://island-browser.github.io/"}]},)"
    R"({"name":"Work","items":[{"title":"Tracker","url":"https://example.org/tracker"}]}]})";

constexpr char kSchemaV2Valid[] =
    R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{},)"
    R"("folders":[{"id":1,"name":"Imported","bookmark_ids":[2]}],)"
    R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})";

// A fresh, empty directory unique to one test.
std::filesystem::path TempDir(const std::string& name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("island_bookmarks_test_" + name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path Sibling(const std::filesystem::path& path, const std::string& suffix) {
    return std::filesystem::path(path.string() + suffix);
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Makes a later write to |path| fail: a directory cannot be replaced by a file rename, and a
// directory cannot be opened for writing.
void BlockPath(const std::filesystem::path& path) { std::filesystem::create_directories(path); }

TEST(BookmarkStoreTest, GivenFreshInstallWhenLoadedThenReadErrorAndNoFileIsCreated) {
    const std::filesystem::path path = TempDir("fresh") / "bookmarks.json";
    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kFileReadError);
    EXPECT_TRUE(store.state().folders.empty());
    EXPECT_FALSE(store.read_only());
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(BookmarkStoreTest, GivenAnInMemoryStoreWhenMutatedThenNothingIsWrittenToDisk) {
    BookmarkStore store;
    EXPECT_TRUE(store.AddOrFindByUrl("Island", "https://island-browser.github.io/").ok());
    EXPECT_EQ(store.state().bookmarks.size(), 1U);
    EXPECT_FALSE(store.read_only());
}

TEST(BookmarkStoreTest, GivenAStoreWithFavoritesWhenSavedAndLoadedThenStateSurvives) {
    const std::filesystem::path path = TempDir("roundtrip") / "bookmarks.json";
    BookmarkStore store(path);
    static_cast<void>(store.Load());
    const std::uint64_t island =
        store.AddOrFindByUrl("Island", "https://island-browser.github.io/", std::nullopt).id;
    const std::uint64_t docs = store.AddOrFindByUrl("Docs", "https://example.com/docs").id;
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{}, island).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{7}, docs).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{7}, island).ok());

    BookmarkStore reloaded(path);
    EXPECT_EQ(reloaded.Load(), BookmarkError::kNone);
    EXPECT_EQ(reloaded.state(), store.state());
}

TEST(BookmarkStoreTest, GivenSchemaOneFileWhenLoadedThenMigratesWithStableIdsAndBackup) {
    const std::filesystem::path path = TempDir("migrate") / "bookmarks.json";
    WriteFile(path, kSchemaV1);

    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kNone);
    const BookmarkState& state = store.state();
    EXPECT_EQ(state.version, 2);
    EXPECT_EQ(state.next_id, 6U);
    ASSERT_EQ(state.folders.size(), 2U);
    EXPECT_EQ(state.folders[0], (BookmarkFolder{1, "Imported", {2, 3}}));
    EXPECT_EQ(state.folders[1], (BookmarkFolder{4, "Work", {5}}));
    ASSERT_EQ(state.bookmarks.size(), 3U);
    EXPECT_EQ(state.bookmarks[0], (Bookmark{2, "Docs", "https://example.com/docs"}));
    EXPECT_EQ(state.bookmarks[2], (Bookmark{5, "Tracker", "https://example.org/tracker"}));
    EXPECT_TRUE(state.global_favorite_ids.empty());
    EXPECT_TRUE(state.space_favorite_ids.empty());

    // The original bytes are kept verbatim, and the file on disk is now schema 2.
    EXPECT_EQ(ReadFile(Sibling(path, ".v1.bak")), kSchemaV1);
    BookmarkStore reloaded(path);
    EXPECT_EQ(reloaded.Load(), BookmarkError::kNone);
    EXPECT_EQ(reloaded.state(), state);
    EXPECT_FALSE(reloaded.read_only());
}

TEST(BookmarkStoreTest, GivenTheSameSchemaOneFileWhenMigratedTwiceThenOutputIsIdentical) {
    const std::filesystem::path first = TempDir("deterministic_a") / "bookmarks.json";
    const std::filesystem::path second = TempDir("deterministic_b") / "bookmarks.json";
    WriteFile(first, kSchemaV1);
    WriteFile(second, kSchemaV1);
    BookmarkStore a(first);
    BookmarkStore b(second);
    ASSERT_EQ(a.Load(), BookmarkError::kNone);
    ASSERT_EQ(b.Load(), BookmarkError::kNone);

    const std::string expected =
        R"({"version":2,"next_id":6,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"Imported","bookmark_ids":[2,3]},)"
        R"({"id":4,"name":"Work","bookmark_ids":[5]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"},)"
        R"({"id":3,"title":"Island","url":"https://island-browser.github.io/"},)"
        R"({"id":5,"title":"Tracker","url":"https://example.org/tracker"}]})"
        "\n";
    EXPECT_EQ(ReadFile(first), expected);
    EXPECT_EQ(ReadFile(second), expected);
}

TEST(BookmarkStoreTest, GivenMigrationBackupWriteFailsWhenLoadedThenV1FileIsKeptReadOnly) {
    const std::filesystem::path path = TempDir("migrate_backup_fail") / "bookmarks.json";
    WriteFile(path, kSchemaV1);
    BlockPath(Sibling(path, ".v1.bak"));

    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kFileWriteError);
    EXPECT_TRUE(store.read_only());
    EXPECT_EQ(store.Flattened().size(), 3U);
    EXPECT_EQ(ReadFile(path), kSchemaV1);

    const BookmarkResult refused = store.AddOrFindByUrl("Mail", "https://example.com/mail");
    EXPECT_EQ(refused.error, BookmarkError::kReadOnly);
    EXPECT_EQ(ReadFile(path), kSchemaV1);
}

TEST(BookmarkStoreTest, GivenMigrationWriteFailsWhenLoadedThenV1FileIsIntactAndReadOnly) {
    const std::filesystem::path path = TempDir("migrate_write_fail") / "bookmarks.json";
    WriteFile(path, kSchemaV1);
    BlockPath(Sibling(path, ".tmp"));

    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kFileWriteError);
    EXPECT_TRUE(store.read_only());
    EXPECT_EQ(ReadFile(path), kSchemaV1);
    EXPECT_EQ(ReadFile(Sibling(path, ".v1.bak")), kSchemaV1);
    EXPECT_EQ(store.AddFavorite(FavoriteScope{}, 2).error, BookmarkError::kReadOnly);
}

TEST(BookmarkStoreTest, GivenACorruptFileWhenMutatedThenItIsBackedUpBeforeTheFirstSave) {
    const std::filesystem::path path = TempDir("corrupt") / "bookmarks.json";
    WriteFile(path, "not json at all");

    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kParseError);
    EXPECT_TRUE(store.state().folders.empty());
    EXPECT_TRUE(store.AddOrFindByUrl("Island", "https://island-browser.github.io/").ok());

    EXPECT_EQ(ReadFile(Sibling(path, ".invalid.bak")), "not json at all");
    BookmarkStore reloaded(path);
    EXPECT_EQ(reloaded.Load(), BookmarkError::kNone);
    EXPECT_TRUE(reloaded.FindBookmarkByUrl("https://island-browser.github.io/").has_value());
}

TEST(BookmarkStoreTest, GivenACorruptFileWhenBackupFailsThenTheMutationRollsBack) {
    const std::filesystem::path path = TempDir("corrupt_backup_fail") / "bookmarks.json";
    WriteFile(path, "not json at all");
    BlockPath(Sibling(path, ".invalid.bak"));

    BookmarkStore store(path);
    static_cast<void>(store.Load());
    const BookmarkResult result =
        store.AddOrFindByUrl("Island", "https://island-browser.github.io/");
    EXPECT_EQ(result.error, BookmarkError::kFileWriteError);
    EXPECT_TRUE(store.state().bookmarks.empty());
    EXPECT_TRUE(store.state().folders.empty());
    EXPECT_EQ(ReadFile(path), "not json at all");
}

TEST(BookmarkStoreTest, GivenAFailedSaveWhenMutatedThenTheStateAndFileAreUnchanged) {
    const std::filesystem::path path = TempDir("atomic") / "bookmarks.json";
    BookmarkStore store(path);
    static_cast<void>(store.Load());
    ASSERT_TRUE(store.AddOrFindByUrl("Island", "https://island-browser.github.io/").ok());
    const std::string before = ReadFile(path);
    const BookmarkState state_before = store.state();

    BlockPath(Sibling(path, ".tmp"));
    const BookmarkResult failed = store.AddOrFindByUrl("Mail", "https://example.com/mail");
    EXPECT_EQ(failed.error, BookmarkError::kFileWriteError);
    EXPECT_FALSE(failed.message.empty());
    EXPECT_EQ(store.state(), state_before);
    EXPECT_EQ(ReadFile(path), before);

    std::filesystem::remove_all(Sibling(path, ".tmp"));
    EXPECT_TRUE(store.AddOrFindByUrl("Mail", "https://example.com/mail").ok());
}

TEST(BookmarkStoreTest, GivenNoFolderWhenBookmarkAddedThenItGoesToUnfiledAndIsFoundByUrl) {
    BookmarkStore store;
    const BookmarkResult first =
        store.AddOrFindByUrl("Island", "https://island-browser.github.io/");
    ASSERT_TRUE(first.ok());
    ASSERT_EQ(store.state().folders.size(), 1U);
    EXPECT_EQ(store.state().folders[0].name, "Unfiled");
    EXPECT_EQ(store.state().folders[0].bookmark_ids, std::vector<std::uint64_t>{first.id});

    // The exact URL matches case-insensitively and returns the same bookmark without a new record.
    const BookmarkResult again =
        store.AddOrFindByUrl("Island again", "HTTPS://ISLAND-BROWSER.GITHUB.IO/");
    EXPECT_TRUE(again.ok());
    EXPECT_EQ(again.id, first.id);
    EXPECT_EQ(store.state().bookmarks.size(), 1U);

    // A second bookmark reuses the Unfiled folder; a folder choice places it elsewhere.
    static_cast<void>(store.AddOrFindByUrl("Docs", "https://example.com/docs"));
    EXPECT_EQ(store.state().folders.size(), 1U);
    EXPECT_EQ(store.state().folders[0].bookmark_ids.size(), 2U);
    const BookmarkResult work = store.CreateFolder("Work");
    ASSERT_TRUE(work.ok());
    ASSERT_TRUE(store.AddOrFindByUrl("Tracker", "https://example.org/tracker", work.id).ok());
    EXPECT_EQ(store.FindFolder(work.id)->bookmark_ids.size(), 1U);

    const std::vector<FlatBookmark> flat = store.Flattened();
    ASSERT_EQ(flat.size(), 3U);
    EXPECT_EQ(flat[0], (FlatBookmark{"Unfiled", "Island", "https://island-browser.github.io/"}));
    EXPECT_EQ(flat[2], (FlatBookmark{"Work", "Tracker", "https://example.org/tracker"}));
}

TEST(BookmarkStoreTest, GivenFoldersWhenCreatedRenamedMovedAndDeletedThenOrderHolds) {
    BookmarkStore store;
    const std::uint64_t work = store.CreateFolder("Work").id;
    const std::uint64_t home = store.CreateFolder("Home").id;
    EXPECT_NE(work, home);
    EXPECT_EQ(store.CreateFolder("   ").error, BookmarkError::kInvalidInput);

    EXPECT_TRUE(store.RenameFolder(work, "Projects").ok());
    EXPECT_EQ(store.FindFolder(work)->name, "Projects");
    EXPECT_TRUE(store.MoveFolder(home, 0).ok());
    EXPECT_EQ(store.state().folders[0].id, home);
    EXPECT_EQ(store.MoveFolder(home, 9).error, BookmarkError::kInvalidInput);

    EXPECT_TRUE(store.DeleteFolder(work).ok());
    EXPECT_EQ(store.FindFolder(work), nullptr);
    EXPECT_EQ(store.DeleteFolder(work).error, BookmarkError::kNotFound);
}

TEST(BookmarkStoreTest, GivenAFolderWithBookmarksWhenDeletedThenItIsRefused) {
    BookmarkStore store;
    const std::uint64_t work = store.CreateFolder("Work").id;
    const std::uint64_t bookmark =
        store.AddOrFindByUrl("Docs", "https://example.com/docs", work).id;

    const BookmarkResult refused = store.DeleteFolder(work);
    EXPECT_EQ(refused.error, BookmarkError::kNotEmpty);
    EXPECT_NE(store.FindFolder(work), nullptr);

    // Moving the bookmark out makes the folder deletable without losing the bookmark.
    const std::uint64_t home = store.CreateFolder("Home").id;
    EXPECT_TRUE(store.MoveBookmark(bookmark, home).ok());
    EXPECT_TRUE(store.FindFolder(work)->bookmark_ids.empty());
    EXPECT_EQ(store.FindFolder(home)->bookmark_ids, std::vector<std::uint64_t>{bookmark});
    EXPECT_TRUE(store.DeleteFolder(work).ok());
}

TEST(BookmarkStoreTest, GivenABookmarkWhenEditedToAnotherBookmarksUrlThenItIsRejected) {
    BookmarkStore store;
    const std::uint64_t a = store.AddOrFindByUrl("A", "https://example.com/a").id;
    const std::uint64_t b = store.AddOrFindByUrl("B", "https://example.com/b").id;
    const BookmarkState before = store.state();

    EXPECT_EQ(store.EditBookmark(b, "B", "HTTPS://EXAMPLE.COM/A").error, BookmarkError::kDuplicate);
    EXPECT_EQ(store.state(), before);

    // Keeping its own URL (any case) and changing the title is fine.
    EXPECT_TRUE(store.EditBookmark(a, "Renamed", "HTTPS://EXAMPLE.COM/A").ok());
    EXPECT_EQ(store.FindBookmark(a)->title, "Renamed");
    EXPECT_EQ(store.EditBookmark(999, "X", "https://example.org/").error, BookmarkError::kNotFound);
}

TEST(BookmarkStoreTest, GivenAFavoriteBookmarkWhenDeletedThenEveryReferenceIsRemoved) {
    BookmarkStore store;
    const std::uint64_t bookmark =
        store.AddOrFindByUrl("Island", "https://island-browser.github.io/").id;
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{}, bookmark).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{7}, bookmark).ok());

    EXPECT_TRUE(store.DeleteBookmark(bookmark).ok());
    EXPECT_TRUE(store.state().bookmarks.empty());
    EXPECT_TRUE(store.state().global_favorite_ids.empty());
    EXPECT_TRUE(store.state().space_favorite_ids.empty());
    EXPECT_TRUE(store.state().folders[0].bookmark_ids.empty());
    EXPECT_EQ(store.DeleteBookmark(bookmark).error, BookmarkError::kNotFound);
}

TEST(BookmarkStoreTest, GivenFavoritesWhenAddedReorderedAndRemovedThenScopesStayIndependent) {
    BookmarkStore store;
    const std::uint64_t a = store.AddOrFindByUrl("A", "https://example.com/a").id;
    const std::uint64_t b = store.AddOrFindByUrl("B", "https://example.com/b").id;

    ASSERT_TRUE(store.AddFavorite(FavoriteScope{}, a).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{}, b).ok());
    EXPECT_EQ(store.AddFavorite(FavoriteScope{}, a).error, BookmarkError::kDuplicate);
    // The same bookmark may be a favorite globally and in a space at once.
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{7}, a).ok());
    EXPECT_EQ(store.state().global_favorite_ids, (std::vector<std::uint64_t>{a, b}));

    EXPECT_TRUE(store.ReorderFavorite(FavoriteScope{}, b, 0).ok());
    EXPECT_EQ(store.state().global_favorite_ids, (std::vector<std::uint64_t>{b, a}));
    EXPECT_EQ(store.ReorderFavorite(FavoriteScope{}, b, 5).error, BookmarkError::kInvalidInput);

    EXPECT_TRUE(store.RemoveFavorite(FavoriteScope{7}, a).ok());
    EXPECT_TRUE(store.state().space_favorite_ids.empty());
    EXPECT_EQ(store.RemoveFavorite(FavoriteScope{7}, a).error, BookmarkError::kNotFound);
    EXPECT_EQ(store.state().global_favorite_ids, (std::vector<std::uint64_t>{b, a}));

    EXPECT_EQ(store.AddFavorite(FavoriteScope{}, 999).error, BookmarkError::kNotFound);
    EXPECT_EQ(store.AddFavorite(FavoriteScope{0}, a).error, BookmarkError::kInvalidInput);
}

TEST(BookmarkStoreTest, GivenAPageWhenAddedAsFavoriteThenBookmarkAndFavoriteAreOneSave) {
    const std::filesystem::path path = TempDir("favorite_by_url") / "bookmarks.json";
    BookmarkStore store(path);
    static_cast<void>(store.Load());

    const BookmarkResult added =
        store.AddFavoriteByUrl(FavoriteScope{7}, "Island", "https://island-browser.github.io/");
    ASSERT_TRUE(added.ok());
    EXPECT_EQ(store.state().space_favorite_ids.at(7), std::vector<std::uint64_t>{added.id});
    EXPECT_EQ(
        store.AddFavoriteByUrl(FavoriteScope{7}, "Island", "HTTPS://ISLAND-BROWSER.GITHUB.IO/")
            .error,
        BookmarkError::kDuplicate);

    // A failed save leaves neither the bookmark nor the favorite behind.
    BlockPath(Sibling(path, ".tmp"));
    EXPECT_EQ(store.AddFavoriteByUrl(FavoriteScope{}, "Docs", "https://example.com/docs").error,
              BookmarkError::kFileWriteError);
    EXPECT_FALSE(store.FindBookmarkByUrl("https://example.com/docs").has_value());
    EXPECT_TRUE(store.state().global_favorite_ids.empty());
}

TEST(BookmarkStoreTest, GivenSpaceFavoritesWhenOneSpaceIsRemovedThenOnlyItsListGoes) {
    BookmarkStore store;
    const std::uint64_t a = store.AddOrFindByUrl("A", "https://example.com/a").id;
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{3}, a).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{9}, a).ok());
    ASSERT_TRUE(store.AddFavorite(FavoriteScope{}, a).ok());
    EXPECT_EQ(store.MaxSpaceKey(), 9U);

    EXPECT_TRUE(store.RemoveSpaceFavorites(9).ok());
    EXPECT_EQ(store.MaxSpaceKey(), 3U);
    EXPECT_EQ(store.state().space_favorite_ids.size(), 1U);
    EXPECT_EQ(store.state().global_favorite_ids, std::vector<std::uint64_t>{a});
    EXPECT_TRUE(store.RemoveSpaceFavorites(42).ok());
    EXPECT_EQ(store.RemoveSpaceFavorites(0).error, BookmarkError::kInvalidInput);
    EXPECT_EQ(BookmarkStore().MaxSpaceKey(), 0U);
}

TEST(BookmarkStoreTest, GivenImportedItemsWhenMergedThenDupesAreSkippedCaseInsensitively) {
    BookmarkStore store;
    ASSERT_EQ(store.Import("Imported", {{"Docs", "https://example.com/docs"}}).added, 1U);

    // Same URL with different case, plus two genuinely new entries, plus an
    // intra-batch duplicate of one of those new entries.
    const BookmarkResult merged =
        store.Import("Imported", {{"Docs copy", "HTTPS://EXAMPLE.COM/docs"},
                                  {"Mail", "https://example.com/mail"},
                                  {"News", "https://example.com/news"},
                                  {"News again", "https://example.com/news"}});
    EXPECT_TRUE(merged.ok());
    EXPECT_EQ(merged.added, 2U);
    ASSERT_EQ(store.state().folders.size(), 1U);
    EXPECT_EQ(store.state().folders[0].bookmark_ids.size(), 3U);
    const std::vector<FlatBookmark> flat = store.Flattened();
    ASSERT_EQ(flat.size(), 3U);
    EXPECT_EQ(flat[1].title, "Mail");
    EXPECT_EQ(flat[2].title, "News");
}

TEST(BookmarkStoreTest, GivenImportDedupesAcrossFoldersThenNoNewFolderIsCreatedForNothing) {
    BookmarkStore store;
    ASSERT_EQ(store.Import("Imported", {{"Docs", "https://example.com/docs"}}).added, 1U);

    // Library-wide dedupe: the URL already lives in another folder, so nothing is added and
    // the requested folder is not created.
    const BookmarkState before = store.state();
    EXPECT_EQ(store.Import("Elsewhere", {{"Docs", "https://EXAMPLE.com/docs"}}).added, 0U);
    EXPECT_EQ(store.state(), before);
    EXPECT_EQ(store.Import("", {{"X", "https://example.org/"}}).error,
              BookmarkError::kInvalidInput);

    const BookmarkResult created = store.Import("User picks", {{"Home", "https://example.org/"}});
    EXPECT_EQ(created.added, 1U);
    ASSERT_EQ(store.state().folders.size(), 2U);
    EXPECT_EQ(store.state().folders[1].name, "User picks");
}

TEST(BookmarkStoreTest, GivenInvalidReferencesOrIdsWhenLoadedThenSchemaErrorAndEmptyStore) {
    const std::vector<std::string> invalid = {
        // A folder refers to a bookmark that does not exist.
        R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[9]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // One bookmark is a member of two folders.
        R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]},{"id":3,"name":"B","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // A bookmark that is in no folder.
        R"({"version":2,"next_id":5,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[]}],)"
        R"("bookmarks":[{"id":4,"title":"Docs","url":"https://example.com/docs"}]})",
        // A favorite list repeats an ID.
        R"({"version":2,"next_id":4,"global_favorite_ids":[2,2],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // A global favorite refers to a missing bookmark.
        R"({"version":2,"next_id":4,"global_favorite_ids":[9],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // next_id does not exceed the largest existing ID.
        R"({"version":2,"next_id":2,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // A bookmark ID repeats, and a folder shares an ID with a bookmark.
        R"({"version":2,"next_id":5,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"},)"
        R"({"id":2,"title":"Other","url":"https://example.org/"}]})",
        R"({"version":2,"next_id":5,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":2,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // Zero and negative IDs are never valid.
        R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":0,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})",
        // A bookmark is missing its URL.
        R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{},)"
        R"("folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)"
        R"("bookmarks":[{"id":2,"title":"Docs"}]})",
    };
    // Bad space-ID keys: leading zero, zero, letters, sign, empty.
    for (const char* key : {"01", "0", "abc", "-1", ""}) {
        const std::string json =
            std::string(
                R"({"version":2,"next_id":4,"global_favorite_ids":[],"space_favorite_ids":{")") +
            key + R"(":[2]},"folders":[{"id":1,"name":"A","bookmark_ids":[2]}],)" +
            R"("bookmarks":[{"id":2,"title":"Docs","url":"https://example.com/docs"}]})";
        SCOPED_TRACE(json);
        const std::filesystem::path path =
            TempDir(std::string("bad_space_key_") + (*key ? key : "empty")) / "bookmarks.json";
        WriteFile(path, json);
        BookmarkStore store(path);
        EXPECT_EQ(store.Load(), BookmarkError::kSchemaError);
        EXPECT_TRUE(store.state().bookmarks.empty());
    }

    for (std::size_t i = 0; i < invalid.size(); ++i) {
        SCOPED_TRACE(invalid[i]);
        const std::filesystem::path path =
            TempDir("invalid_" + std::to_string(i)) / "bookmarks.json";
        WriteFile(path, invalid[i]);
        BookmarkStore store(path);
        EXPECT_EQ(store.Load(), BookmarkError::kSchemaError);
        EXPECT_TRUE(store.state().bookmarks.empty());
        EXPECT_TRUE(store.state().folders.empty());
    }
}

TEST(BookmarkStoreTest, GivenAValidSchemaTwoFileWhenLoadedThenItIsAcceptedAsIs) {
    const std::filesystem::path path = TempDir("valid_v2") / "bookmarks.json";
    WriteFile(path, kSchemaV2Valid);
    BookmarkStore store(path);
    EXPECT_EQ(store.Load(), BookmarkError::kNone);
    EXPECT_EQ(store.state().next_id, 4U);
    EXPECT_EQ(store.FindBookmarkByUrl("HTTPS://EXAMPLE.COM/DOCS"), std::optional<std::uint64_t>(2));
    EXPECT_EQ(store.Flattened().size(), 1U);
}

}  // namespace
}  // namespace island
