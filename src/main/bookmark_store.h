#ifndef ISLAND_BOOKMARK_STORE_H_
#define ISLAND_BOOKMARK_STORE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace island {

struct BookmarkItem {
    std::string title;
    std::string url;

    bool operator==(const BookmarkItem&) const = default;
};

struct BookmarkFolder {
    std::string name;
    std::vector<BookmarkItem> items;

    bool operator==(const BookmarkFolder&) const = default;
};

struct BookmarkState {
    static constexpr int kCurrentSchemaVersion = 1;

    int version = kCurrentSchemaVersion;
    std::vector<BookmarkFolder> folders;

    bool operator==(const BookmarkState&) const = default;
};

enum class BookmarkError : std::uint8_t {
    kNone = 0,
    kFileReadError,
    kFileWriteError,
    kParseError,
    kSchemaError,
};

struct BookmarkLoadResult {
    BookmarkError error = BookmarkError::kNone;
    // Empty store unless error == kNone.
    BookmarkState state;
};

class BookmarkStore {
  public:
    // Missing/unreadable/schema-invalid files behave like SessionStore: fresh
    // defaults plus a non-kNone error; callers log and continue. Stored URLs
    // are NOT policy-checked here — the allow-list applies at click time
    // through the single address-submission path.
    static BookmarkLoadResult Load(const std::filesystem::path& path);
    static BookmarkError Save(const std::filesystem::path& path, const BookmarkState& state);

    // Appends |items| into the folder named |folder_name| (created at the end
    // if missing), skipping entries whose URL already exists in the store
    // case-insensitively — including within |items| itself. Returns the number
    // of items actually added.
    static std::size_t MergeFolder(BookmarkState& state, std::string_view folder_name,
                                   const std::vector<BookmarkItem>& items);

    static std::filesystem::path DefaultBookmarksFilePath();
};

}  // namespace island

#endif  // ISLAND_BOOKMARK_STORE_H_
