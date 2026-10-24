#ifndef ISLAND_BOOKMARK_STORE_H_
#define ISLAND_BOOKMARK_STORE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace island {

// One bookmark as the browser importers produce it (before it gets an ID).
struct BookmarkItem {
    std::string title;
    std::string url;

    bool operator==(const BookmarkItem&) const = default;
};

// A stored bookmark. IDs are positive, locally allocated integers.
struct Bookmark {
    std::uint64_t id = 0;
    std::string title;
    std::string url;

    bool operator==(const Bookmark&) const = default;
};

// A folder holds an ordered list of bookmark IDs; each bookmark is in exactly one folder.
struct BookmarkFolder {
    std::uint64_t id = 0;
    std::string name;
    std::vector<std::uint64_t> bookmark_ids;

    bool operator==(const BookmarkFolder&) const = default;
};

// The persisted library (schema 2). Favorite lists hold bookmark IDs only. The global strip is
// one ordered list; each space's list is keyed by its SpaceId value (decimal in the file).
struct BookmarkState {
    static constexpr int kCurrentSchemaVersion = 2;

    int version = kCurrentSchemaVersion;
    std::uint64_t next_id = 1;
    std::vector<std::uint64_t> global_favorite_ids;
    std::map<std::uint64_t, std::vector<std::uint64_t>> space_favorite_ids;
    std::vector<BookmarkFolder> folders;
    std::vector<Bookmark> bookmarks;

    bool operator==(const BookmarkState&) const = default;
};

// Favorite list selector: std::nullopt is the global strip, otherwise a space's SpaceId value.
using FavoriteScope = std::optional<std::uint64_t>;

enum class BookmarkError : std::uint8_t {
    kNone = 0,
    kFileReadError,
    kFileWriteError,
    kParseError,
    kSchemaError,
    kNotFound,
    kDuplicate,
    kInvalidInput,
    kNotEmpty,
    kReadOnly,
};

// Outcome of a mutation or load step. |message| is short human text for inline UI and is empty
// on success. |id| is the bookmark or folder the call created or matched; |added| counts the
// bookmarks an import actually added.
struct BookmarkResult {
    BookmarkError error = BookmarkError::kNone;
    std::string message;
    std::uint64_t id = 0;
    std::size_t added = 0;

    [[nodiscard]] bool ok() const noexcept { return error == BookmarkError::kNone; }
};

// Read-only projection for palette-style callers: the bookmark's folder name, title and URL.
struct FlatBookmark {
    std::string folder_name;
    std::string title;
    std::string url;

    bool operator==(const FlatBookmark&) const = default;
};

// The bookmark library and its single mutation seam. A file-backed store (path given) reads and
// writes `bookmarks.json`; a default-constructed store is in-memory and never touches disk.
//
// Every mutation applies to a copy of the state, saves atomically, and commits only when the save
// succeeds, so readers never see an edit that is not durable. A failed save returns an error and
// leaves the state unchanged. Save failures are reported, never retried here.
class BookmarkStore {
  public:
    // Name of the folder that holds bookmarks created without a folder.
    static constexpr std::string_view kUnfiledFolderName = "Unfiled";

    BookmarkStore() = default;
    explicit BookmarkStore(std::filesystem::path path);

    // Reads the file once and must be called before use on a file-backed store. Schema 1 is
    // migrated to schema 2 (see
    // docs/superpowers/specs/2026-10-24-island-bookmarks-favorites-design.md §2.2). A missing file
    // gives kFileReadError and the store stays writable. An existing file that cannot be read also
    // gives kFileReadError, but the store is read-only so the file is never overwritten. A
    // malformed or schema-invalid file gives an empty state, and the first save copies its original
    // bytes to `bookmarks.json.invalid.bak`. When a migration write fails, the migrated state is
    // kept read-only for this run and the error is kFileWriteError.
    BookmarkError Load();

    [[nodiscard]] const BookmarkState& state() const noexcept { return state_; }
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }

    [[nodiscard]] const Bookmark* FindBookmark(std::uint64_t bookmark_id) const;
    [[nodiscard]] const BookmarkFolder* FindFolder(std::uint64_t folder_id) const;
    // Case-insensitive exact URL match.
    [[nodiscard]] std::optional<std::uint64_t> FindBookmarkByUrl(std::string_view url) const;
    // Folder order, then bookmark order within each folder.
    [[nodiscard]] std::vector<FlatBookmark> Flattened() const;
    // Largest SpaceId value with a favorite list, or 0 when none. BrowserWindow reserves SpaceIds
    // above this value when it restores a session.
    [[nodiscard]] std::uint64_t MaxSpaceKey() const;

    // Finds the bookmark with this URL (case-insensitive) or creates it. A new bookmark goes into
    // |folder_id|, or into Unfiled (created on demand) when none is given. An existing match is
    // returned unchanged; |folder_id| is ignored for it.
    BookmarkResult AddOrFindByUrl(std::string_view title, std::string_view url,
                                  std::optional<std::uint64_t> folder_id = std::nullopt);
    BookmarkResult CreateFolder(std::string_view name);
    BookmarkResult RenameFolder(std::uint64_t folder_id, std::string_view name);
    // Moves the folder to |index| in folder order.
    BookmarkResult MoveFolder(std::uint64_t folder_id, std::size_t index);
    // Refuses a folder that still holds bookmarks (kNotEmpty); move them first.
    BookmarkResult DeleteFolder(std::uint64_t folder_id);
    // Moves a bookmark to the end of |folder_id|, out of its current folder.
    BookmarkResult MoveBookmark(std::uint64_t bookmark_id, std::uint64_t folder_id);
    // Rejects a URL that another bookmark already has (kDuplicate).
    BookmarkResult EditBookmark(std::uint64_t bookmark_id, std::string_view title,
                                std::string_view url);
    // Removes the bookmark from its folder and from every favorite list.
    BookmarkResult DeleteBookmark(std::uint64_t bookmark_id);

    // Appends a bookmark to a favorite list. kDuplicate if it is already in that list.
    BookmarkResult AddFavorite(FavoriteScope scope, std::uint64_t bookmark_id);
    // Finds or creates the bookmark for |url| and adds it to the list in one save.
    BookmarkResult AddFavoriteByUrl(FavoriteScope scope, std::string_view title,
                                    std::string_view url);
    // Removes a favorite. A space's list is dropped when it becomes empty.
    BookmarkResult RemoveFavorite(FavoriteScope scope, std::uint64_t bookmark_id);
    // Moves a favorite to |index| within its list.
    BookmarkResult ReorderFavorite(FavoriteScope scope, std::uint64_t bookmark_id,
                                   std::size_t index);
    // Drops one space's favorite list. Called when the session really deletes that space. The
    // store does not check which spaces exist.
    BookmarkResult RemoveSpaceFavorites(std::uint64_t space_key);

    // Appends |items| into the folder named |folder_name| (created at the end when missing),
    // skipping URLs already in the library case-insensitively, including duplicates within
    // |items|. The folder is created only when at least one bookmark is added. The result's
    // |added| holds the count.
    BookmarkResult Import(std::string_view folder_name, const std::vector<BookmarkItem>& items);

    static std::filesystem::path DefaultBookmarksFilePath();

  private:
    template <typename Apply>
    BookmarkResult Commit(Apply&& apply);
    BookmarkError WriteState(const BookmarkState& next);

    std::filesystem::path path_;
    BookmarkState state_;
    bool read_only_ = false;
    // Original bytes of an existing file that failed to load. The next save copies them to
    // `bookmarks.json.invalid.bak` first, and the save fails if that copy fails.
    std::optional<std::string> invalid_original_;
};

}  // namespace island

#endif  // ISLAND_BOOKMARK_STORE_H_
