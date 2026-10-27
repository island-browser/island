#ifndef ISLAND_BROWSER_IMPORT_H_
#define ISLAND_BROWSER_IMPORT_H_

// Readers for the browsers outside the Chromium family. CEF-free; Firefox's
// places.sqlite reader is enabled when SQLite3 is available:
//  - Firefox: bookmarks from profile `places.sqlite`, copied to a temporary
//    file before opening so a running Firefox cannot hold the import open.
//    The newest compressed JSON backup remains a fallback for old profiles.
//  - Arc: spaces, their colors, pinned tabs and pinned folders, and Favorites
//    from `StorableSidebar.json`.
// Every reader returns std::nullopt for "cannot read" and never throws.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bookmark_store.h"

namespace island {

// Decodes a Mozilla `mozLz40\0` container (magic, little-endian decompressed
// size, one LZ4 block). Rejects outputs larger than `max_output_bytes`.
[[nodiscard]] std::optional<std::string> DecodeMozLz4(std::string_view bytes,
                                                      std::size_t max_output_bytes = 64U << 20);

// Parses Firefox's bookmarks JSON tree (`text/x-moz-place` leaves under
// `text/x-moz-place-container` nodes). Skips place: and javascript: URIs and
// caps at kMaxImportedBookmarks.
[[nodiscard]] std::optional<std::vector<BookmarkItem>> ParseFirefoxBookmarks(std::string_view text);

// The newest `bookmarkbackups/*.jsonlz4` under any Firefox profile below
// `base_home` (platform profile roots, snap included); empty if none.
[[nodiscard]] std::filesystem::path FindFirefoxBookmarksBackup(
    const std::filesystem::path& base_home);
[[nodiscard]] std::filesystem::path FindFirefoxPlacesDatabase(
    const std::filesystem::path& base_home);
[[nodiscard]] std::optional<std::vector<BookmarkItem>> ImportFirefoxBookmarks(
    const std::filesystem::path& base_home);

// A pinned Arc folder as a bookmark folder. `path` joins the titles of the
// folder and its ancestors with " / " (0.9 folders are one level deep).
struct ImportedFolder {
    std::string path;
    std::vector<BookmarkItem> items;

    bool operator==(const ImportedFolder&) const = default;
};

// One Arc space as Island recreates it. `pinned_tabs` holds top-level pinned
// tabs only; folders are kept apart in `folders`, in Arc order.
struct ImportedSpace {
    std::string name;
    std::string arc_id;
    std::optional<std::uint32_t> color_argb;
    std::vector<BookmarkItem> pinned_tabs;
    std::vector<ImportedFolder> folders;

    bool operator==(const ImportedSpace&) const = default;
};

// Everything one StorableSidebar.json yields: the spaces, and Arc's Favorites
// (top apps), which become global favorites in Arc order.
struct ArcImport {
    std::vector<ImportedSpace> spaces;
    std::vector<BookmarkItem> favorites;

    bool operator==(const ArcImport&) const = default;
};

// Parses Arc's StorableSidebar.json: each space's id, title, theme color, its
// pinned tabs and folders, and the Favorites container.
[[nodiscard]] std::optional<ArcImport> ParseArcSidebarStructure(std::string_view text);
// Reads and parses a StorableSidebar.json file; std::nullopt when unreadable
// or larger than 64 MiB.
[[nodiscard]] std::optional<ArcImport> ImportArcFile(const std::filesystem::path& file);
// The spaces of ParseArcSidebarStructure, for callers that need no folders.
[[nodiscard]] std::optional<std::vector<ImportedSpace>> ParseArcSidebar(std::string_view text);
[[nodiscard]] std::filesystem::path FindArcSidebarFile(const std::filesystem::path& base_home);
[[nodiscard]] std::optional<std::vector<ImportedSpace>> ImportArcSpaces(
    const std::filesystem::path& base_home);

// Reads a whole file; std::nullopt when missing, unreadable, or larger than
// `max_bytes`.
[[nodiscard]] std::optional<std::string> ReadWholeFile(const std::filesystem::path& path,
                                                       std::size_t max_bytes = 64U << 20);

}  // namespace island

#endif  // ISLAND_BROWSER_IMPORT_H_
