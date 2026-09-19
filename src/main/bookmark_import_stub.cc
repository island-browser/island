#include "bookmark_import.h"

namespace island {

// Generic stub for the Safari platform seam: Safari's bookmarks plist is only
// readable through CoreFoundation on macOS. "Cannot read" is an honest
// nullopt, never an empty success.
std::optional<std::vector<BookmarkItem>> ImportSafariBookmarks(
    const std::filesystem::path& base_home) {
    static_cast<void>(base_home);
    return std::nullopt;
}

}  // namespace island
