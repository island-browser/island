#include "bookmark_import.h"

#include <algorithm>
#include <array>
#include <cctype>
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

std::vector<std::string> ChromeProfilesFromLocalState(const std::filesystem::path& user_data) {
    std::ifstream in(user_data / "Local State", std::ios::binary);
    if (!in.is_open()) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::optional<json::Value> root = json::Parser(text).Parse();
    if (!root || !root->IsObject()) return {};
    const json::Value* profile = root->FindMember("profile");
    const json::Value* cache = profile == nullptr ? nullptr : profile->FindMember("info_cache");
    if (cache == nullptr || !cache->IsObject()) return {};
    std::vector<std::string> names;
    for (const auto& [name, value] : cache->object_val) {
        static_cast<void>(value);
        // Profile names originate in a local JSON file, but must not turn this
        // probe into a path traversal when the file is malformed or hostile.
        if (!name.empty() && name.find('/') == std::string::npos &&
            name.find('\\') == std::string::npos && name.find("..") == std::string::npos) {
            names.push_back(name);
        }
    }
    return names;
}

std::vector<std::filesystem::path> SourceBookmarksPaths(const SourcePaths& source,
                                                        const std::filesystem::path& base_home) {
#if defined(_WIN32)
    const std::filesystem::path primary = base_home / source.windows_path;
#elif defined(__APPLE__)
    const std::filesystem::path primary = base_home / source.macos_path;
#else
    const std::filesystem::path primary = base_home / source.linux_path;
#endif
    std::vector<std::filesystem::path> paths;
    const auto add_if_file = [&paths](const std::filesystem::path& candidate) {
        std::error_code candidate_ec;
        if (std::filesystem::is_regular_file(candidate, candidate_ec) &&
            std::find(paths.begin(), paths.end(), candidate) == paths.end()) {
            paths.push_back(candidate);
        }
    };
    add_if_file(primary);
    // One Welcome row represents the union of all named Chrome-family
    // profiles. Default remains first, then Local State's names, then the
    // conventional profile directories not mentioned by Local State.
    const std::filesystem::path user_data = primary.parent_path().parent_path();
    std::error_code ec;
    if (!std::filesystem::is_directory(user_data, ec)) {
        return paths;
    }
    for (const std::string& name : ChromeProfilesFromLocalState(user_data)) {
        add_if_file(user_data / name / "Bookmarks");
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(user_data, ec)) {
        if (ec) {
            break;
        }
        const std::string name = entry.path().filename().string();
        const bool profile = name == "Default" ||
                             (name.rfind("Profile ", 0) == 0 && name.size() > 8 &&
                              std::all_of(name.begin() + 8, name.end(),
                                          [](unsigned char c) { return std::isdigit(c) != 0; }));
        if (!profile) continue;
        add_if_file(entry.path() / "Bookmarks");
    }
    return paths;
}

bool CanOpenForRead(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in.is_open();
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
        const std::vector<std::filesystem::path> paths = SourceBookmarksPaths(entry, base_home);
        const bool detected = !paths.empty();
        const bool available = std::any_of(paths.begin(), paths.end(), CanOpenForRead);
        sources.push_back(ImportSourceInfo{
            .id = static_cast<ImportSource>(chromium_index),
            .display_name = entry.display_name,
            .detected = detected,
            .available = available,
            .bookmarks_path = detected ? paths.front() : std::filesystem::path{},
            .unavailable_reason = detected && !available ? "bookmarks file cannot be opened" : ""});
        ++chromium_index;
    }

    std::error_code ec;
    const std::filesystem::path safari_path = base_home / "Library/Safari/Bookmarks.plist";
    const bool safari_available =
        PlatformCanReadSafari() && std::filesystem::is_regular_file(safari_path, ec);
    sources.push_back(ImportSourceInfo{
        .id = ImportSource::kSafari,
        .display_name = "Safari",
        .detected = safari_available,
        .available = safari_available,
        .bookmarks_path = PlatformCanReadSafari() ? safari_path : std::filesystem::path{}});
    const std::filesystem::path firefox = FindFirefoxPlacesDatabase(base_home);
    const std::filesystem::path firefox_backup = FindFirefoxBookmarksBackup(base_home);
    const std::filesystem::path firefox_path = !firefox.empty() ? firefox : firefox_backup;
    const bool firefox_detected = !firefox_path.empty();
    const bool firefox_available = firefox_detected && CanOpenForRead(firefox_path);
    sources.push_back(ImportSourceInfo{
        .id = ImportSource::kFirefox,
        .display_name = "Firefox",
        .detected = firefox_detected,
        .available = firefox_available,
        .bookmarks_path = firefox_path,
        .unavailable_reason =
            firefox_detected && !firefox_available ? "profile database cannot be opened" : ""});
    const std::filesystem::path arc = FindArcSidebarFile(base_home);
    sources.push_back(ImportSourceInfo{.id = ImportSource::kArc,
                                       .display_name = "Arc",
                                       .detected = !arc.empty(),
                                       .available = !arc.empty() && CanOpenForRead(arc),
                                       .bookmarks_path = arc,
                                       .unavailable_reason = !arc.empty() && !CanOpenForRead(arc)
                                                                 ? "sidebar data cannot be opened"
                                                                 : ""});
    return sources;
}

std::optional<std::vector<BookmarkItem>> ImportFromSource(
    ImportSource source, const std::filesystem::path& base_home,
    std::size_t* unreadable_chromium_profiles) {
    if (unreadable_chromium_profiles != nullptr) {
        *unreadable_chromium_profiles = 0;
    }
    if (source == ImportSource::kFirefox) {
        return ImportFirefoxBookmarks(base_home);
    }
    if (source == ImportSource::kArc) {
        // As plain bookmarks, Arc contributes every space's pinned tabs, then the
        // items of its pinned folders, in Arc order. Favorites are not included here.
        const std::optional<std::vector<ImportedSpace>> spaces = ImportArcSpaces(base_home);
        if (!spaces.has_value()) {
            return std::nullopt;
        }
        std::vector<BookmarkItem> items;
        for (const ImportedSpace& space : *spaces) {
            items.insert(items.end(), space.pinned_tabs.begin(), space.pinned_tabs.end());
            for (const ImportedFolder& folder : space.folders) {
                items.insert(items.end(), folder.items.begin(), folder.items.end());
            }
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
    const std::vector<std::filesystem::path> paths =
        SourceBookmarksPaths(kChromiumSources[index], base_home);
    std::vector<BookmarkItem> items;
    bool parsed_any = false;
    std::size_t unreadable_profiles = 0;
    for (const std::filesystem::path& path : paths) {
        const std::optional<std::vector<BookmarkItem>> profile_items = ReadAndParse(path);
        if (!profile_items.has_value()) {
            ++unreadable_profiles;
            continue;
        }
        parsed_any = true;
        const std::size_t remaining = kMaxImportedBookmarks - items.size();
        const std::size_t count = std::min(remaining, profile_items->size());
        items.insert(items.end(), profile_items->begin(), profile_items->begin() + count);
        if (items.size() == kMaxImportedBookmarks) break;
    }
    if (unreadable_chromium_profiles != nullptr) {
        *unreadable_chromium_profiles = unreadable_profiles;
    }
    return parsed_any ? std::optional<std::vector<BookmarkItem>>(std::move(items)) : std::nullopt;
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
