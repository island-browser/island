#include "browser_import.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <system_error>

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

std::vector<std::filesystem::path> FirefoxProfileRoots(const std::filesystem::path& home) {
#if defined(_WIN32)
    return {home / "AppData/Roaming/Mozilla/Firefox/Profiles"};
#elif defined(__APPLE__)
    return {home / "Library/Application Support/Firefox/Profiles"};
#else
    return {home / ".mozilla/firefox", home / "snap/firefox/common/.mozilla/firefox",
            home / ".var/app/org.mozilla.firefox/.mozilla/firefox"};
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
    for (const std::filesystem::path& root : FirefoxProfileRoots(base_home)) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (const auto& profile : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            const std::filesystem::path backups = profile.path() / "bookmarkbackups";
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
    }
    return newest;
}

std::optional<std::vector<BookmarkItem>> ImportFirefoxBookmarks(
    const std::filesystem::path& base_home) {
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
