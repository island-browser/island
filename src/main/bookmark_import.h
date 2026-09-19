#ifndef ISLAND_BOOKMARK_IMPORT_H_
#define ISLAND_BOOKMARK_IMPORT_H_

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "bookmark_store.h"

namespace island {

// Browsers the welcome flow can import from. The Chromium family shares one
// `Bookmarks` JSON schema; Safari is a macOS plist read behind the platform
// seam. Firefox is deliberately absent: its bookmarks live in a SQLite
// database this project cannot read without a new dependency, and the welcome
// flow never fakes an import it cannot perform.
enum class ImportSource : std::uint8_t {
    kChrome,
    kEdge,
    kBrave,
    kChromium,
    kVivaldi,
    kOpera,
    kSafari,
};

struct ImportSourceInfo {
    ImportSource id;
    std::string display_name;
    // True when the source's bookmarks file exists and this build can read it.
    bool available = false;
    // The bookmarks file that was probed (exists or not); empty for sources
    // whose path does not apply on this platform.
    std::filesystem::path bookmarks_path;
};

// Probes the well-known bookmark locations under |base_home| (the user's home
// directory or a test fixture). Reads no file contents.
std::vector<ImportSourceInfo> DetectInstalledSources(const std::filesystem::path& base_home);

// Parses the shared Chromium `Bookmarks` JSON (roots.bookmark_bar / other /
// synced, recursive `type:"url"` nodes). Keeps traversal order, drops entries
// with an empty url, caps at kMaxImportedBookmarks. nullopt when the text is
// not a recognizable Bookmarks file. URL *policy* (allow-list) is applied
// later, at click time, through the address path — import is permissive.
inline constexpr std::size_t kMaxImportedBookmarks = 500;
std::optional<std::vector<BookmarkItem>> ParseChromiumBookmarks(std::string_view text);

// Reads and parses one source. nullopt when the file is missing, unreadable,
// unrecognized, or the platform seam cannot read this source (Safari off
// macOS). Never throws.
std::optional<std::vector<BookmarkItem>> ImportFromSource(ImportSource source,
                                                          const std::filesystem::path& base_home);

// Platform seam, mirroring the sidebar-hover seam: a macOS implementation in
// src/main/macos/bookmark_import_mac.mm reads the Safari bookmarks plist via
// CoreFoundation; the generic stub always reports unavailability. nullopt
// means "cannot read", never an empty success.
std::optional<std::vector<BookmarkItem>> ImportSafariBookmarks(
    const std::filesystem::path& base_home);

}  // namespace island

#endif  // ISLAND_BOOKMARK_IMPORT_H_
