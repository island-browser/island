#include "bookmark_import.h"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "browser_import.h"
#include "json_util.h"

namespace island {
namespace {

struct SourcePaths {
    const char* display_name;
    // Bookmark-file locations relative to the user's home directory, per
    // platform. Forward slashes only: std::filesystem::path composes them
    // portably.
    const char* windows_path;
    const char* macos_path;
    const char* linux_path;
};

constexpr std::array<SourcePaths, 6> kChromiumSources{{
    {"Google Chrome", "AppData/Local/Google/Chrome/User Data/Default/Bookmarks",
     "Library/Application Support/Google/Chrome/Default/Bookmarks",
     ".config/google-chrome/Default/Bookmarks"},
    {"Microsoft Edge", "AppData/Local/Microsoft/Edge/User Data/Default/Bookmarks",
     "Library/Application Support/Microsoft Edge/Default/Bookmarks",
     ".config/microsoft-edge/Default/Bookmarks"},
    {"Brave", "AppData/Local/BraveSoftware/Brave-Browser/User Data/Default/Bookmarks",
     "Library/Application Support/BraveSoftware/Brave-Browser/Default/Bookmarks",
     ".config/BraveSoftware/Brave-Browser/Default/Bookmarks"},
    {"Chromium", "AppData/Local/Chromium/User Data/Default/Bookmarks",
     "Library/Application Support/Chromium/Default/Bookmarks",
     ".config/chromium/Default/Bookmarks"},
    {"Vivaldi", "AppData/Local/Vivaldi/User Data/Default/Bookmarks",
     "Library/Application Support/Vivaldi/Default/Bookmarks", ".config/vivaldi/Default/Bookmarks"},
    {"Opera", "AppData/Local/Opera Software/Opera Stable/User Data/Default/Bookmarks",
     "Library/Application Support/com.operasoftware.Opera/Bookmarks", ".config/opera/Bookmarks"},
}};

// The Safari plist is only reachable behind the macOS seam; other platforms
// compile the stub, which reports "not applicable".
#if defined(__APPLE__)
bool PlatformCanReadSafari() { return true; }
#else
bool PlatformCanReadSafari() { return false; }
#endif

std::filesystem::path SourceBookmarksPath(const SourcePaths& source,
                                          const std::filesystem::path& base_home) {
#if defined(_WIN32)
    const std::filesystem::path primary = base_home / source.windows_path;
#elif defined(__APPLE__)
    const std::filesystem::path primary = base_home / source.macos_path;
#else
    const std::filesystem::path primary = base_home / source.linux_path;
#endif
    std::error_code ec;
    if (std::filesystem::is_regular_file(primary, ec)) {
        return primary;
    }
    // Many setups keep bookmarks in a non-Default profile ("Profile 1", …):
    // fall back to any profile's Bookmarks file under the browser's user-data
    // root (the primary path's parent directory minus its last segment).
    const std::filesystem::path user_data = primary.parent_path().parent_path();
    if (!std::filesystem::is_directory(user_data, ec)) {
        return primary;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(user_data, ec)) {
        if (ec) {
            break;
        }
        const std::filesystem::path candidate = entry.path() / "Bookmarks";
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return primary;
}

// Chromium stores URL entries as {"type":"url","name":...,"url":...} nested in
// {"type":"folder","children":[...]} under roots.*; walk depth-first. The
// folder type token is advisory — some builds omit it on container nodes — so
// any node carrying a children array recurses.
void CollectUrlEntries(const json::Value& node, std::vector<BookmarkItem>& out) {
    if (out.size() >= kMaxImportedBookmarks || !node.IsObject()) {
        return;
    }
    const json::Value* type = node.FindMember("type");
    if (type != nullptr && type->IsString() && type->string_val == "url") {
        const json::Value* url = node.FindMember("url");
        if (url == nullptr || !url->IsString() || url->string_val.empty()) {
            return;
        }
        BookmarkItem item;
        const json::Value* name = node.FindMember("name");
        if (name != nullptr && name->IsString()) {
            item.title = name->string_val;
        }
        item.url = url->string_val;
        out.push_back(std::move(item));
        return;
    }
    const json::Value* children = node.FindMember("children");
    if (children == nullptr || !children->IsArray()) {
        return;
    }

    for (const json::Value& child : children->array_val) {
        CollectUrlEntries(child, out);
        if (out.size() >= kMaxImportedBookmarks) {
            return;
        }
    }
}

std::optional<std::vector<BookmarkItem>> ReadAndParse(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::nullopt;
    }
    return ParseChromiumBookmarks(content);
}

}  // namespace

std::optional<std::vector<BookmarkItem>> ParseChromiumBookmarks(std::string_view text) {
    std::optional<json::Value> root = json::Parser(text).Parse();
    if (!root.has_value() || !root->IsObject()) {
        return std::nullopt;
    }
    const json::Value* roots = root->FindMember("roots");
    if (roots == nullptr || !roots->IsObject()) {
        return std::nullopt;
    }

    std::vector<BookmarkItem> items;
    for (const auto& [name, root_node] : roots->object_val) {
        static_cast<void>(name);
        CollectUrlEntries(root_node, items);
        if (items.size() >= kMaxImportedBookmarks) {
            break;
        }
    }
    return items;
}

std::vector<ImportSourceInfo> DetectInstalledSources(const std::filesystem::path& base_home) {
    std::vector<ImportSourceInfo> sources;
    std::uint8_t chromium_index = 0;
    for (const SourcePaths& entry : kChromiumSources) {
        const std::filesystem::path path = SourceBookmarksPath(entry, base_home);
        std::error_code ec;
        const bool available = std::filesystem::is_regular_file(path, ec);
        sources.push_back(ImportSourceInfo{static_cast<ImportSource>(chromium_index),
                                           entry.display_name, available, path});
        ++chromium_index;
    }

    std::error_code ec;
    const std::filesystem::path safari_path = base_home / "Library/Safari/Bookmarks.plist";
    const bool safari_available =
        PlatformCanReadSafari() && std::filesystem::is_regular_file(safari_path, ec);
    sources.push_back(
        ImportSourceInfo{ImportSource::kSafari, "Safari", safari_available,
                         PlatformCanReadSafari() ? safari_path : std::filesystem::path{}});
    const std::filesystem::path firefox = FindFirefoxBookmarksBackup(base_home);
    sources.push_back(
        ImportSourceInfo{ImportSource::kFirefox, "Firefox", !firefox.empty(), firefox});
    const std::filesystem::path arc = FindArcSidebarFile(base_home);
    sources.push_back(ImportSourceInfo{ImportSource::kArc, "Arc", !arc.empty(), arc});
    return sources;
}

std::optional<std::vector<BookmarkItem>> ImportFromSource(ImportSource source,
                                                          const std::filesystem::path& base_home) {
    if (source == ImportSource::kFirefox) {
        return ImportFirefoxBookmarks(base_home);
    }
    if (source == ImportSource::kArc) {
        // As plain bookmarks, Arc contributes every space's pinned tabs.
        const std::optional<std::vector<ImportedSpace>> spaces = ImportArcSpaces(base_home);
        if (!spaces.has_value()) {
            return std::nullopt;
        }
        std::vector<BookmarkItem> items;
        for (const ImportedSpace& space : *spaces) {
            items.insert(items.end(), space.pinned_tabs.begin(), space.pinned_tabs.end());
        }
        return items;
    }
    if (source == ImportSource::kSafari) {
#if defined(__APPLE__)
        return ImportSafariBookmarks(base_home);
#else
        static_cast<void>(base_home);
        return std::nullopt;
#endif
    }

    std::uint8_t index = static_cast<std::uint8_t>(source);
    if (index >= kChromiumSources.size()) {
        return std::nullopt;
    }
    return ReadAndParse(SourceBookmarksPath(kChromiumSources[index], base_home));
}

namespace {

constexpr std::array<std::string_view, 9> kImportSourceIds = {
    "chrome", "edge", "brave", "chromium", "vivaldi", "opera", "safari", "firefox", "arc"};

}  // namespace

std::string_view ImportSourceId(ImportSource source) noexcept {
    const auto index = static_cast<std::size_t>(source);
    return index < kImportSourceIds.size() ? kImportSourceIds[index] : std::string_view("unknown");
}

std::optional<ImportSource> ImportSourceFromId(std::string_view id) noexcept {
    for (std::size_t index = 0; index < kImportSourceIds.size(); ++index) {
        if (kImportSourceIds[index] == id) {
            return static_cast<ImportSource>(index);
        }
    }
    return std::nullopt;
}

std::string ImportSourceDescription(const ImportSourceInfo& info) {
    switch (info.id) {
        case ImportSource::kArc:
            return "Spaces, their colors, and pinned tabs";
        case ImportSource::kFirefox:
            return "Bookmarks from the latest Firefox backup";
        case ImportSource::kSafari:
            return "Bookmarks";
        default:
            return "Bookmarks";
    }
}

}  // namespace island
