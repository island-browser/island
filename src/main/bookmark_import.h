#ifndef ISLAND_BOOKMARK_IMPORT_H_
#define ISLAND_BOOKMARK_IMPORT_H_

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "bookmark_store.h"

namespace island {

// Browsers Island can import from. The Chromium family shares one
// `Bookmarks` JSON schema; Safari is a macOS plist read behind the platform
// seam; Firefox bookmarks come from places.sqlite when SQLite is available,
// otherwise from compressed JSON backups;
// Arc contributes spaces and pinned tabs (browser_import.h).
enum class ImportSource : std::uint8_t {
    kChrome,
    kEdge,
    kBrave,
    kChromium,
    kVivaldi,
    kOpera,
    kSafari,
    kFirefox,
    kArc,
};

// Stable ids for Settings ("chrome", "firefox", "arc", ...), and back.
[[nodiscard]] std::string_view ImportSourceId(ImportSource source) noexcept;
[[nodiscard]] std::optional<ImportSource> ImportSourceFromId(std::string_view id) noexcept;

struct ImportSourceInfo {
    ImportSource id;
    std::string display_name;
    // True when a profile or source file was found. Sources with no local
    // profile are omitted from the Welcome card rather than called unreadable.
    bool detected = false;
    // True when the source's bookmarks file exists and this build can read it.
    bool available = false;
    // The bookmarks file that was probed (exists or not); empty for sources
    // whose path does not apply on this platform.
    std::filesystem::path bookmarks_path;
    // A short, user-facing reason when a detected source is unavailable. It
    // is empty for absent and readable sources.
    std::string unavailable_reason;
};

// Probes the well-known bookmark locations under |base_home| (the user's home
// directory or a test fixture). Reads no file contents.
std::vector<ImportSourceInfo> DetectInstalledSources(const std::filesystem::path& base_home);
// A one-line description of what importing the source brings over.
[[nodiscard]] std::string ImportSourceDescription(const ImportSourceInfo& info);

// Parses the shared Chromium `Bookmarks` JSON (roots.bookmark_bar / other /
// synced, recursive `type:"url"` nodes). Keeps traversal order, drops entries
// with an empty url, caps at kMaxImportedBookmarks. nullopt when the text is
// not a recognizable Bookmarks file. URL *policy* (allow-list) is applied
// later, at click time, through the address path — import is permissive.
inline constexpr std::size_t kMaxImportedBookmarks = 500;
std::optional<std::vector<BookmarkItem>> ParseChromiumBookmarks(std::string_view text);

// Reads and parses one source. nullopt when the file is missing, unreadable,
// unrecognized, or the platform seam cannot read this source (Safari off
// macOS). When supplied, |unreadable_chromium_profiles| receives the number
// of Chrome-family profile files that existed but could not be read or parsed.
// Never throws.
std::optional<std::vector<BookmarkItem>> ImportFromSource(
    ImportSource source, const std::filesystem::path& base_home,
    std::size_t* unreadable_chromium_profiles = nullptr);

// Platform seam, mirroring the sidebar-hover seam: a macOS implementation in
// src/main/macos/bookmark_import_mac.mm reads the Safari bookmarks plist via
// CoreFoundation; the generic stub always reports unavailability. nullopt
// means "cannot read", never an empty success.
std::optional<std::vector<BookmarkItem>> ImportSafariBookmarks(
    const std::filesystem::path& base_home);

}  // namespace island

#endif  // ISLAND_BOOKMARK_IMPORT_H_
