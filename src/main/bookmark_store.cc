#include "bookmark_store.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "json_util.h"

namespace island {
namespace {

BookmarkLoadResult Fail(BookmarkError error) { return {error, BookmarkState{}}; }

std::string NormalizeUrlForDedup(std::string_view url) {
    std::string lowered;
    lowered.reserve(url.size());
    for (char c : url) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered;
}

BookmarkLoadResult ParseBookmarkState(const json::Value& root) {
    if (!root.IsObject() || !root.HasRequiredInt("version") ||
        root.FindMember("version")->int_val != BookmarkState::kCurrentSchemaVersion ||
        !root.HasRequiredArray("folders")) {
        return Fail(BookmarkError::kSchemaError);
    }

    BookmarkState state;
    for (const json::Value& folder_val : root.FindMember("folders")->array_val) {
        if (!folder_val.IsObject() || !folder_val.HasRequiredString("name") ||
            !folder_val.HasRequiredArray("items")) {
            return Fail(BookmarkError::kSchemaError);
        }
        BookmarkFolder folder;
        folder.name = folder_val.FindMember("name")->string_val;
        for (const json::Value& item_val : folder_val.FindMember("items")->array_val) {
            if (!item_val.IsObject() || !item_val.HasRequiredString("title") ||
                !item_val.HasRequiredString("url")) {
                return Fail(BookmarkError::kSchemaError);
            }
            folder.items.push_back(
                {item_val.FindMember("title")->string_val, item_val.FindMember("url")->string_val});
        }
        state.folders.push_back(std::move(folder));
    }
    return {BookmarkError::kNone, std::move(state)};
}

std::string SerializeToJson(const BookmarkState& state) {
    std::ostringstream oss;
    json::Writer w(oss);

    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(state.version));
    w.Key("folders");
    w.StartArray();
    for (const BookmarkFolder& folder : state.folders) {
        w.ArrayElement();
        w.StartObject();
        w.Key("name");
        w.StringValue(folder.name);
        w.Key("items");
        w.StartArray();
        for (const BookmarkItem& item : folder.items) {
            w.ArrayElement();
            w.StartObject();
            w.Key("title");
            w.StringValue(item.title);
            w.Key("url");
            w.StringValue(item.url);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    oss << '\n';
    return oss.str();
}

}  // namespace

BookmarkLoadResult BookmarkStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return Fail(BookmarkError::kFileReadError);
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return Fail(BookmarkError::kFileReadError);
    }
    std::optional<json::Value> root = json::Parser(content).Parse();
    if (!root.has_value()) {
        return Fail(BookmarkError::kParseError);
    }
    return ParseBookmarkState(*root);
}

BookmarkError BookmarkStore::Save(const std::filesystem::path& path, const BookmarkState& state) {
    const std::string json_text = SerializeToJson(state);

    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return BookmarkError::kFileWriteError;
    }

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return BookmarkError::kFileWriteError;
        }
        out << json_text;
        if (!out) {
            return BookmarkError::kFileWriteError;
        }
    }

    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        return BookmarkError::kFileWriteError;
    }
    return BookmarkError::kNone;
}

std::size_t BookmarkStore::MergeFolder(BookmarkState& state, std::string_view folder_name,
                                       const std::vector<BookmarkItem>& items) {
    BookmarkFolder* folder = nullptr;
    for (BookmarkFolder& candidate : state.folders) {
        if (candidate.name == folder_name) {
            folder = &candidate;
            break;
        }
    }
    if (folder == nullptr) {
        state.folders.push_back(BookmarkFolder{std::string(folder_name), {}});
        folder = &state.folders.back();
    }

    // Case-insensitive URL identity: www.Example.com and www.example.com are
    // the same bookmark for de-duplication purposes.
    std::size_t added = 0;
    for (const BookmarkItem& item : items) {
        const std::string key = NormalizeUrlForDedup(item.url);
        const bool exists = std::any_of(
            state.folders.begin(), state.folders.end(), [&](const BookmarkFolder& candidate) {
                return std::any_of(candidate.items.begin(), candidate.items.end(),
                                   [&](const BookmarkItem& existing) {
                                       return NormalizeUrlForDedup(existing.url) == key;
                                   });
            });
        if (exists) {
            continue;
        }
        folder->items.push_back(item);
        ++added;
    }
    return added;
}

std::filesystem::path BookmarkStore::DefaultBookmarksFilePath() {
#if defined(_WIN32)
    const char* const app_data = std::getenv("APPDATA");
    std::filesystem::path base =
        app_data != nullptr && *app_data != '\0'
            ? std::filesystem::path(app_data)
            : std::filesystem::path(std::getenv("USERPROFILE")) / "AppData" / "Roaming";
    return base / "Island" / "bookmarks.json";
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("bookmarks.json");
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "Island" /
           "bookmarks.json";
#else
    const char* const xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path(xdg_data_home) / "Island" / "bookmarks.json";
    }
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("bookmarks.json");
    }
    return std::filesystem::path(home) / ".local" / "share" / "Island" / "bookmarks.json";
#endif
}

}  // namespace island
