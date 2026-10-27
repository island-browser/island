#include "browser_import.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <system_error>

#if defined(ISLAND_HAS_SQLITE3)
#include <sqlite3.h>
#endif

#include "bookmark_import.h"
#include "json_util.h"

namespace island {

namespace {

constexpr std::string_view kMozLz4Magic{"mozLz40\0", 8};
constexpr int kMaxTreeDepth = 64;

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// --- Firefox ------------------------------------------------------------------

void CollectFirefoxPlaces(const json::Value& node, std::vector<BookmarkItem>& items, int depth) {
    if (depth > kMaxTreeDepth || !node.IsObject() || items.size() >= kMaxImportedBookmarks) {
        return;
    }
    const std::string_view type = node.StringOr("type", "");
    if (type == "text/x-moz-place") {
        const std::string_view uri = node.StringOr("uri", "");
        if (uri.empty() || StartsWith(uri, "place:") || StartsWith(uri, "javascript:")) {
            return;
        }
        std::string title(node.StringOr("title", ""));
        items.push_back({.title = title.empty() ? std::string(uri) : std::move(title),
                         .url = std::string(uri)});
        return;
    }
    if (const json::Value* children = node.FindMember("children");
        children != nullptr && children->IsArray()) {
        for (const json::Value& child : children->array_val) {
            CollectFirefoxPlaces(child, items, depth + 1);
            if (items.size() >= kMaxImportedBookmarks) return;
        }
    }
}

std::vector<std::filesystem::path> FirefoxDataRoots(const std::filesystem::path& home) {
#if defined(_WIN32)
    return {home / "AppData/Roaming/Mozilla/Firefox"};
#elif defined(__APPLE__)
    return {home / "Library/Application Support/Firefox"};
#else
    return {home / ".mozilla/firefox", home / "snap/firefox/common/.mozilla/firefox",
            home / ".var/app/org.mozilla.firefox/.mozilla/firefox"};
#endif
}

bool IsMacOrWindowsFirefox() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

struct FirefoxProfile {
    std::filesystem::path path;
    enum class Priority {
        kNone,
        kProfileDefault,
        kInstallDefault,
    } priority = Priority::kNone;
};

std::vector<FirefoxProfile> FirefoxProfileDirectories(const std::filesystem::path& home) {
    std::vector<FirefoxProfile> profiles;
    for (const std::filesystem::path& root : FirefoxDataRoots(home)) {
        const std::filesystem::path default_root =
            IsMacOrWindowsFirefox() ? root / "Profiles" : root;
        std::error_code ec;
        if (std::filesystem::is_directory(default_root, ec)) {
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(default_root, ec)) {
                if (ec) break;
                if (entry.is_directory(ec)) profiles.push_back({.path = entry.path()});
            }
        }

        // profiles.ini is authoritative when a profile uses a non-default path. Only its path
        // metadata is read here; bookmark data remains in places.sqlite.
        std::ifstream ini_file(root / "profiles.ini", std::ios::binary);
        if (!ini_file.is_open()) continue;
        const std::string ini((std::istreambuf_iterator<char>(ini_file)),
                              std::istreambuf_iterator<char>());
        std::string section;
        bool relative = true;
        FirefoxProfile::Priority priority = FirefoxProfile::Priority::kNone;
        std::string path;
        const auto add_profile = [&] {
            if (path.empty()) return;
            const std::filesystem::path candidate =
                relative ? root / path : std::filesystem::path(path);
            if (!std::filesystem::is_directory(candidate, ec)) return;
            const auto found = std::find_if(
                profiles.begin(), profiles.end(),
                [&candidate](const FirefoxProfile& profile) { return profile.path == candidate; });
            if (found == profiles.end()) {
                profiles.push_back({.path = candidate, .priority = priority});
            } else if (priority > found->priority) {
                found->priority = priority;
            }
        };
        std::size_t start = 0;
        while (start <= ini.size()) {
            const std::size_t end = ini.find('\n', start);
            const std::size_t count = end == std::string::npos ? std::string::npos : end - start;
            std::string_view line = std::string_view(ini).substr(start, count);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty() && line.front() == '[') {
                add_profile();
                section = std::string(line);
                path.clear();
                relative = true;
                priority = FirefoxProfile::Priority::kNone;
            } else if (line.rfind("Path=", 0) == 0) {
                path = std::string(line.substr(5));
            } else if (line.rfind("Default=", 0) == 0) {
                const std::string_view value = line.substr(8);
                if (section.rfind("[Install", 0) == 0) {
                    path = std::string(value);
                    relative = true;
                    priority = FirefoxProfile::Priority::kInstallDefault;
                } else if (value == "1") {
                    priority = FirefoxProfile::Priority::kProfileDefault;
                }
            } else if (line == "IsRelative=0") {
                relative = false;
            } else if (line == "IsRelative=1") {
                relative = true;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        add_profile();
    }
    return profiles;
}

std::string SqlitePath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::optional<std::vector<BookmarkItem>> ImportFirefoxPlaces(
    const std::filesystem::path& source_path) {
#if !defined(ISLAND_HAS_SQLITE3)
    static_cast<void>(source_path);
    return std::nullopt;
#else
    static std::atomic_uint64_t next_temp_id{0};
    std::error_code ec;
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path(ec) /
        ("island-firefox-places-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(next_temp_id++) + ".sqlite");
    const std::filesystem::path source_wal = source_path.string() + "-wal";
    const std::filesystem::path temp_wal = temp.string() + "-wal";
    const std::filesystem::path temp_shm = temp.string() + "-shm";
    const auto cleanup = [&] {
        std::filesystem::remove(temp, ec);
        std::filesystem::remove(temp_wal, ec);
        std::filesystem::remove(temp_shm, ec);
    };
    if (ec ||
        !std::filesystem::copy_file(source_path, temp, std::filesystem::copy_options::none, ec)) {
        cleanup();
        return std::nullopt;
    }
    std::error_code wal_ec;
    if (std::filesystem::is_regular_file(source_wal, wal_ec) &&
        !std::filesystem::copy_file(source_wal, temp_wal, std::filesystem::copy_options::none,
                                    wal_ec)) {
        cleanup();
        return std::nullopt;
    }
    sqlite3* database = nullptr;
    const std::string temp_sqlite_path = SqlitePath(temp);
    const int open_result =
        sqlite3_open_v2(temp_sqlite_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr);
    if (open_result != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        cleanup();
        return std::nullopt;
    }
    constexpr char kQuery[] =
        "WITH RECURSIVE tag_items(id) AS ("
        "SELECT id FROM moz_bookmarks WHERE guid = 'tags________' "
        "UNION ALL SELECT b.id FROM moz_bookmarks b JOIN tag_items t ON b.parent = t.id) "
        "SELECT COALESCE(b.title, p.url), p.url FROM moz_bookmarks b "
        "JOIN moz_places p ON b.fk = p.id WHERE b.type = 1 AND p.url IS NOT NULL "
        "AND p.url != '' AND substr(p.url, 1, 6) != 'place:' "
        "AND substr(p.url, 1, 11) != 'javascript:' "
        "AND b.id NOT IN (SELECT id FROM tag_items) ORDER BY b.parent, b.position, b.id";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, kQuery, -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_close(database);
        cleanup();
        return std::nullopt;
    }
    std::vector<BookmarkItem> items;
    int step = SQLITE_OK;
    while (items.size() < kMaxImportedBookmarks && (step = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* title = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const auto* url = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (url == nullptr || *url == '\0') continue;
        items.push_back({.title = title == nullptr ? std::string(url) : std::string(title),
                         .url = std::string(url)});
    }
    const bool complete = step == SQLITE_DONE || items.size() == kMaxImportedBookmarks;
    sqlite3_finalize(statement);
    sqlite3_close(database);
    cleanup();
    return complete ? std::optional<std::vector<BookmarkItem>>(std::move(items)) : std::nullopt;
#endif
}

// --- Arc ------------------------------------------------------------------------

// Arc stores collections as flat [id, object, id, object, ...] arrays (older
// files use plain object arrays); both shapes yield the objects in order.
std::vector<const json::Value*> ArcObjects(const json::Value* array) {
    std::vector<const json::Value*> objects;
    if (array == nullptr || !array->IsArray()) return objects;
    for (const json::Value& entry : array->array_val) {
        if (entry.IsObject()) objects.push_back(&entry);
    }
    return objects;
}

std::optional<std::uint32_t> FindRgbColor(const json::Value& node, int depth) {
    if (depth > 12) return std::nullopt;
    if (node.IsObject()) {
        const json::Value* red = node.FindMember("red");
        const json::Value* green = node.FindMember("green");
        const json::Value* blue = node.FindMember("blue");
        if (red && green && blue && red->IsNumber() && green->IsNumber() && blue->IsNumber()) {
            const auto channel = [](const json::Value* v) {
                const double value = std::clamp(v->AsDouble(), 0.0, 1.0);
                return static_cast<std::uint32_t>(std::lround(value * 255.0));
            };
            return 0xFF000000U | (channel(red) << 16) | (channel(green) << 8) | channel(blue);
        }
        for (const auto& [key, value] : node.object_val) {
            if (std::optional<std::uint32_t> found = FindRgbColor(value, depth + 1)) return found;
        }
    } else if (node.IsArray()) {
        for (const json::Value& value : node.array_val) {
            if (std::optional<std::uint32_t> found = FindRgbColor(value, depth + 1)) return found;
        }
    }
    return std::nullopt;
}

void CollectArcTabs(const std::map<std::string, const json::Value*>& items, const std::string& id,
                    std::vector<BookmarkItem>& tabs, int depth) {
    if (depth > kMaxTreeDepth || tabs.size() >= kMaxImportedBookmarks) return;
    const auto found = items.find(id);
    if (found == items.end()) return;
    const json::Value* children = found->second->FindMember("childrenIds");
    if (children == nullptr || !children->IsArray()) return;
    for (const json::Value& child_id : children->array_val) {
        if (!child_id.IsString()) continue;
        const auto child = items.find(child_id.string_val);
        if (child == items.end()) continue;
        const json::Value* data = child->second->FindMember("data");
        const json::Value* tab = data != nullptr ? data->FindMember("tab") : nullptr;
        if (tab != nullptr && tab->IsObject()) {
            const std::string_view url = tab->StringOr("savedURL", "");
            if (!url.empty()) {
                std::string title(child->second->StringOr("title", ""));
                if (title.empty()) title = std::string(tab->StringOr("savedTitle", url));
                tabs.push_back({.title = std::move(title), .url = std::string(url)});
            }
        } else {
            CollectArcTabs(items, child_id.string_val, tabs, depth + 1);
        }
        if (tabs.size() >= kMaxImportedBookmarks) return;
    }
}

}  // namespace

std::optional<std::string> ReadWholeFile(const std::filesystem::path& path, std::size_t max_bytes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > max_bytes) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return content;
}

std::optional<std::string> DecodeMozLz4(std::string_view bytes, std::size_t max_output_bytes) {
    if (bytes.size() < kMozLz4Magic.size() + 4 || !StartsWith(bytes, kMozLz4Magic)) {
        return std::nullopt;
    }
    std::uint32_t expected = 0;
    for (int i = 3; i >= 0; --i) {
        expected = (expected << 8) | static_cast<unsigned char>(bytes[kMozLz4Magic.size() + i]);
    }
    if (expected > max_output_bytes) return std::nullopt;
    std::string out;
    out.reserve(expected);
    const auto* in = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t ip = kMozLz4Magic.size() + 4;
    const std::size_t end = bytes.size();
    while (ip < end) {
        const unsigned token = in[ip++];
        std::size_t literal = token >> 4;
        if (literal == 15) {
            unsigned extra = 255;
            while (extra == 255) {
                if (ip >= end) return std::nullopt;
                extra = in[ip++];
                literal += extra;
            }
        }
        if (literal > end - ip || out.size() + literal > expected) return std::nullopt;
        out.append(reinterpret_cast<const char*>(in + ip), literal);
        ip += literal;
        if (ip == end) break;  // the last sequence carries literals only
        if (end - ip < 2) return std::nullopt;
        const std::size_t offset = in[ip] | (static_cast<std::size_t>(in[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > out.size()) return std::nullopt;
        std::size_t match = token & 0x0FU;
        if (match == 15) {
            unsigned extra = 255;
            while (extra == 255) {
                if (ip >= end) return std::nullopt;
                extra = in[ip++];
                match += extra;
            }
        }
        match += 4;
        if (out.size() + match > expected) return std::nullopt;
        // Byte-by-byte: matches may overlap the bytes they produce.
        const std::size_t from = out.size() - offset;
        for (std::size_t i = 0; i < match; ++i) out.push_back(out[from + i]);
    }
    if (out.size() != expected) return std::nullopt;
    return out;
}

std::optional<std::vector<BookmarkItem>> ParseFirefoxBookmarks(std::string_view text) {
    const std::optional<json::Value> root = json::Parse(text);
    if (!root || !root->IsObject() || root->StringOr("type", "") != "text/x-moz-place-container") {
        return std::nullopt;
    }
    std::vector<BookmarkItem> items;
    CollectFirefoxPlaces(*root, items, 0);
    return items;
}

std::filesystem::path FindFirefoxBookmarksBackup(const std::filesystem::path& base_home) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    for (const FirefoxProfile& profile : FirefoxProfileDirectories(base_home)) {
        const std::filesystem::path backups = profile.path / "bookmarkbackups";
        std::error_code inner;
        if (!std::filesystem::is_directory(backups, inner)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(backups, inner)) {
            if (inner) break;
            if (entry.path().extension() != ".jsonlz4" || !entry.is_regular_file(inner)) {
                continue;
            }
            const auto time = entry.last_write_time(inner);
            if (newest.empty() || time > newest_time) {
                newest = entry.path();
                newest_time = time;
            }
        }
    }
    return newest;
}

std::filesystem::path FindFirefoxPlacesDatabase(const std::filesystem::path& base_home) {
    FirefoxProfile::Priority selected_priority = FirefoxProfile::Priority::kNone;
    std::filesystem::path selected;
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    for (const FirefoxProfile& profile : FirefoxProfileDirectories(base_home)) {
        const std::filesystem::path database = profile.path / "places.sqlite";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(database, ec)) continue;
        if (profile.priority > selected_priority) {
            selected = database;
            selected_priority = profile.priority;
        }
        const auto time = std::filesystem::last_write_time(database, ec);
        if (newest.empty() || (!ec && time > newest_time)) {
            newest = database;
            newest_time = time;
        }
    }
    return selected_priority == FirefoxProfile::Priority::kNone ? newest : selected;
}

std::optional<std::vector<BookmarkItem>> ImportFirefoxBookmarks(
    const std::filesystem::path& base_home) {
    const std::filesystem::path places = FindFirefoxPlacesDatabase(base_home);
    if (!places.empty()) {
        if (const std::optional<std::vector<BookmarkItem>> items = ImportFirefoxPlaces(places)) {
            return items;
        }
    }
    const std::filesystem::path backup = FindFirefoxBookmarksBackup(base_home);
    if (backup.empty()) return std::nullopt;
    const std::optional<std::string> compressed = ReadWholeFile(backup);
    if (!compressed) return std::nullopt;
    const std::optional<std::string> text = DecodeMozLz4(*compressed);
    if (!text) return std::nullopt;
    return ParseFirefoxBookmarks(*text);
}

std::optional<std::vector<ImportedSpace>> ParseArcSidebar(std::string_view text) {
    const std::optional<json::Value> root = json::Parse(text);
    if (!root || !root->IsObject()) return std::nullopt;
    const json::Value* sidebar = root->FindMember("sidebar");
    if (sidebar == nullptr) return std::nullopt;
    const json::Value* containers = sidebar->FindMember("containers");
    const json::Value* container = nullptr;
    for (const json::Value* candidate : ArcObjects(containers)) {
        if (candidate->FindMember("spaces") != nullptr &&
            candidate->FindMember("items") != nullptr) {
            container = candidate;
            break;
        }
    }
    if (container == nullptr) return std::nullopt;

    std::map<std::string, const json::Value*> items;
    for (const json::Value* item : ArcObjects(container->FindMember("items"))) {
        const std::string_view id = item->StringOr("id", "");
        if (!id.empty()) items.emplace(std::string(id), item);
    }

    std::vector<ImportedSpace> spaces;
    for (const json::Value* space : ArcObjects(container->FindMember("spaces"))) {
        ImportedSpace imported;
        imported.name = std::string(space->StringOr("title", ""));
        if (imported.name.empty()) imported.name = "Space " + std::to_string(spaces.size() + 1);
        if (const json::Value* custom = space->FindMember("customInfo")) {
            imported.color_argb = FindRgbColor(*custom, 0);
        }
        const json::Value* ids = space->FindMember("containerIDs");
        if (ids != nullptr && ids->IsArray()) {
            for (std::size_t i = 0; i + 1 < ids->array_val.size(); ++i) {
                const json::Value& marker = ids->array_val[i];
                if (marker.IsString() && marker.string_val == "pinned" &&
                    ids->array_val[i + 1].IsString()) {
                    CollectArcTabs(items, ids->array_val[i + 1].string_val, imported.pinned_tabs,
                                   0);
                }
            }
        }
        spaces.push_back(std::move(imported));
    }
    return spaces;
}

std::filesystem::path FindArcSidebarFile(const std::filesystem::path& base_home) {
    std::error_code ec;
#if defined(__APPLE__)
    const std::filesystem::path path =
        base_home / "Library/Application Support/Arc/StorableSidebar.json";
    return std::filesystem::is_regular_file(path, ec) ? path : std::filesystem::path{};
#elif defined(_WIN32)
    const std::filesystem::path packages = base_home / "AppData/Local/Packages";
    if (!std::filesystem::is_directory(packages, ec)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(packages, ec)) {
        if (ec) break;
        if (!StartsWith(entry.path().filename().string(), "TheBrowserCompany.Arc")) continue;
        const std::filesystem::path path =
            entry.path() / "LocalCache/Local/Arc/StorableSidebar.json";
        std::error_code inner;
        if (std::filesystem::is_regular_file(path, inner)) return path;
    }
    return {};
#else
    static_cast<void>(base_home);
    static_cast<void>(ec);
    return {};
#endif
}

std::optional<std::vector<ImportedSpace>> ImportArcSpaces(const std::filesystem::path& base_home) {
    const std::filesystem::path path = FindArcSidebarFile(base_home);
    if (path.empty()) return std::nullopt;
    const std::optional<std::string> text = ReadWholeFile(path);
    if (!text) return std::nullopt;
    return ParseArcSidebar(*text);
}

}  // namespace island
