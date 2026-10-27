#include "bookmark_store.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "data_paths.h"
#include "json_util.h"

namespace island {
namespace {

constexpr int kSchemaVersionOne = 1;

BookmarkResult Ok(std::uint64_t id = 0) {
    BookmarkResult result;
    result.id = id;
    return result;
}

std::string_view DefaultMessage(BookmarkError error) {
    switch (error) {
        case BookmarkError::kNone:
            return {};
        case BookmarkError::kFileReadError:
            return "Could not read bookmarks.";
        case BookmarkError::kFileWriteError:
            return "Could not save bookmarks. The change was not applied.";
        case BookmarkError::kParseError:
        case BookmarkError::kSchemaError:
            return "The bookmarks file is damaged. Its original copy is kept before the next save.";
        case BookmarkError::kNotFound:
            return "That bookmark or folder no longer exists.";
        case BookmarkError::kDuplicate:
            return "That bookmark is already here.";
        case BookmarkError::kInvalidInput:
            return "That name or address is not valid.";
        case BookmarkError::kNotEmpty:
            return "Move or delete its bookmarks first.";
        case BookmarkError::kReadOnly:
            return "Bookmarks are read-only for this run.";
    }
    return {};
}

BookmarkResult Fail(BookmarkError error, std::string_view message = {}) {
    BookmarkResult result;
    result.error = error;
    result.message = message.empty() ? std::string(DefaultMessage(error)) : std::string(message);
    return result;
}

bool IsBlank(std::string_view text) {
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

// Case-insensitive URL identity: www.Example.com and www.example.com are the same bookmark.
std::string NormalizeUrlForDedup(std::string_view url) {
    std::string lowered;
    lowered.reserve(url.size());
    for (char c : url) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered;
}

bool IsValidFavoriteScope(FavoriteScope scope) { return !scope.has_value() || *scope != 0; }

std::uint64_t Allocate(BookmarkState& state) { return state.next_id++; }

Bookmark* FindBookmarkIn(BookmarkState& state, std::uint64_t bookmark_id) {
    for (Bookmark& bookmark : state.bookmarks) {
        if (bookmark.id == bookmark_id) {
            return &bookmark;
        }
    }
    return nullptr;
}

const Bookmark* FindBookmarkIn(const BookmarkState& state, std::uint64_t bookmark_id) {
    for (const Bookmark& bookmark : state.bookmarks) {
        if (bookmark.id == bookmark_id) {
            return &bookmark;
        }
    }
    return nullptr;
}

BookmarkFolder* FindFolderIn(BookmarkState& state, std::uint64_t folder_id) {
    for (BookmarkFolder& folder : state.folders) {
        if (folder.id == folder_id) {
            return &folder;
        }
    }
    return nullptr;
}

std::optional<std::uint64_t> FindIdByUrl(const BookmarkState& state, std::string_view url) {
    const std::string key = NormalizeUrlForDedup(url);
    for (const Bookmark& bookmark : state.bookmarks) {
        if (NormalizeUrlForDedup(bookmark.url) == key) {
            return bookmark.id;
        }
    }
    return std::nullopt;
}

// The favorite list for |scope|, or nullptr when a space has no list yet.
std::vector<std::uint64_t>* FavoriteListIn(BookmarkState& state, FavoriteScope scope) {
    if (!scope.has_value()) {
        return &state.global_favorite_ids;
    }
    const auto found = state.space_favorite_ids.find(*scope);
    return found == state.space_favorite_ids.end() ? nullptr : &found->second;
}

// Adds a new bookmark to the library (not to any folder) and returns its ID.
std::uint64_t AppendBookmark(BookmarkState& state, std::string_view title, std::string_view url) {
    const std::uint64_t id = Allocate(state);
    state.bookmarks.push_back(Bookmark{id, std::string(title), std::string(url)});
    return id;
}

// Returns the folder named |name|, creating it at the end of folder order when missing.
// The returned index stays valid until the next folder is added.
std::size_t FindOrCreateFolderIndex(BookmarkState& state, std::string_view name) {
    for (std::size_t index = 0; index < state.folders.size(); ++index) {
        if (state.folders[index].name == name) {
            return index;
        }
    }
    state.folders.push_back(BookmarkFolder{Allocate(state), std::string(name), {}});
    return state.folders.size() - 1;
}

BookmarkResult AddFavoriteIn(BookmarkState& state, FavoriteScope scope, std::uint64_t bookmark_id) {
    if (!IsValidFavoriteScope(scope)) {
        return Fail(BookmarkError::kInvalidInput, "That space is not valid.");
    }
    if (FindBookmarkIn(state, bookmark_id) == nullptr) {
        return Fail(BookmarkError::kNotFound);
    }
    std::vector<std::uint64_t>& list =
        scope.has_value() ? state.space_favorite_ids[*scope] : state.global_favorite_ids;
    if (std::find(list.begin(), list.end(), bookmark_id) != list.end()) {
        return Fail(BookmarkError::kDuplicate, "Already in favorites.");
    }
    list.push_back(bookmark_id);
    return Ok(bookmark_id);
}

// Removes a bookmark and every reference to it. Folder deletion and direct deletion must have
// exactly the same favorite-cleanup behavior.
bool EraseBookmarkIn(BookmarkState& state, std::uint64_t bookmark_id) {
    const auto found =
        std::find_if(state.bookmarks.begin(), state.bookmarks.end(),
                     [&](const Bookmark& bookmark) { return bookmark.id == bookmark_id; });
    if (found == state.bookmarks.end()) {
        return false;
    }
    state.bookmarks.erase(found);
    for (BookmarkFolder& folder : state.folders) {
        std::erase(folder.bookmark_ids, bookmark_id);
    }
    std::erase(state.global_favorite_ids, bookmark_id);
    for (auto it = state.space_favorite_ids.begin(); it != state.space_favorite_ids.end();) {
        std::erase(it->second, bookmark_id);
        it = it->second.empty() ? state.space_favorite_ids.erase(it) : std::next(it);
    }
    return true;
}

// Reads a whole file; nullopt when it cannot be opened or read.
std::optional<std::string> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string content(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    if (in.bad()) {
        return std::nullopt;
    }
    return content;
}

// Writes |content| to a temporary sibling and renames it over |path|, so a reader sees either the
// old bytes or the new ones. Returns false on any failure and leaves |path| untouched.
bool WriteAtomically(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << content;
        out.close();
        if (out.fail()) {
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }

    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

// Space keys are the decimal SpaceId value: no sign, no leading zero, and not zero.
bool ParseSpaceKey(std::string_view text, std::uint64_t& key) {
    if (text.empty() || text.front() == '0' || !std::all_of(text.begin(), text.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        return false;
    }
    const char* const last = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), last, key);
    return parsed.ec == std::errc{} && parsed.ptr == last && key > 0;
}

bool ParseId(const json::Value& value, std::uint64_t& id) {
    if (!value.IsInt() || value.int_val <= 0) {
        return false;
    }
    id = static_cast<std::uint64_t>(value.int_val);
    return true;
}

bool ParseIdArray(const json::Value* node, std::vector<std::uint64_t>& ids) {
    if (node == nullptr || !node->IsArray()) {
        return false;
    }
    for (const json::Value& element : node->array_val) {
        std::uint64_t id = 0;
        if (!ParseId(element, id)) {
            return false;
        }
        ids.push_back(id);
    }
    return true;
}

// Every folder and bookmark ID is unique, one bookmark sits in exactly one folder, every favorite
// refers to an existing bookmark once per list, and next_id is above every ID in use.
bool IsConsistent(const BookmarkState& state) {
    std::set<std::uint64_t> ids;
    std::uint64_t largest = 0;
    const auto add_id = [&ids, &largest](std::uint64_t id) {
        largest = std::max(largest, id);
        return ids.insert(id).second;
    };

    std::set<std::uint64_t> bookmark_ids;
    for (const Bookmark& bookmark : state.bookmarks) {
        if (!add_id(bookmark.id)) {
            return false;
        }
        bookmark_ids.insert(bookmark.id);
    }
    std::set<std::uint64_t> members;
    for (const BookmarkFolder& folder : state.folders) {
        if (!add_id(folder.id)) {
            return false;
        }
        for (std::uint64_t id : folder.bookmark_ids) {
            if (bookmark_ids.count(id) == 0 || !members.insert(id).second) {
                return false;
            }
        }
    }
    if (members.size() != bookmark_ids.size()) {
        return false;
    }

    const auto list_is_valid = [&bookmark_ids](const std::vector<std::uint64_t>& list) {
        std::set<std::uint64_t> seen;
        return std::all_of(list.begin(), list.end(), [&](std::uint64_t id) {
            return bookmark_ids.count(id) != 0 && seen.insert(id).second;
        });
    };
    if (!list_is_valid(state.global_favorite_ids)) {
        return false;
    }
    for (const auto& entry : state.space_favorite_ids) {
        if (!list_is_valid(entry.second)) {
            return false;
        }
    }
    return state.next_id > largest;
}

std::optional<BookmarkState> ParseSchemaTwo(const json::Value& root) {
    BookmarkState state;
    const json::Value* next_id = root.FindMember("next_id");
    if (next_id == nullptr || !ParseId(*next_id, state.next_id)) {
        return std::nullopt;
    }
    if (!ParseIdArray(root.FindMember("global_favorite_ids"), state.global_favorite_ids)) {
        return std::nullopt;
    }

    const json::Value* spaces = root.FindMember("space_favorite_ids");
    if (spaces == nullptr || !spaces->IsObject()) {
        return std::nullopt;
    }
    // Empty lists are not stored in the map, so duplicates are tracked here, not by the map.
    std::set<std::uint64_t> seen_space_keys;
    for (const auto& [key, list_value] : spaces->object_val) {
        std::uint64_t space_key = 0;
        if (!ParseSpaceKey(key, space_key) || !seen_space_keys.insert(space_key).second) {
            return std::nullopt;
        }
        std::vector<std::uint64_t> ids;
        if (!ParseIdArray(&list_value, ids)) {
            return std::nullopt;
        }
        // An empty list is never kept, so the in-memory form matches what is written.
        if (!ids.empty()) {
            state.space_favorite_ids.emplace(space_key, std::move(ids));
        }
    }

    const json::Value* folders = root.FindMember("folders");
    if (folders == nullptr || !folders->IsArray()) {
        return std::nullopt;
    }
    for (const json::Value& folder_value : folders->array_val) {
        const json::Value* id = folder_value.FindMember("id");
        const json::Value* name = folder_value.FindMember("name");
        BookmarkFolder folder;
        if (id == nullptr || !ParseId(*id, folder.id) || name == nullptr || !name->IsString() ||
            !ParseIdArray(folder_value.FindMember("bookmark_ids"), folder.bookmark_ids)) {
            return std::nullopt;
        }
        folder.name = name->string_val;
        state.folders.push_back(std::move(folder));
    }

    const json::Value* bookmarks = root.FindMember("bookmarks");
    if (bookmarks == nullptr || !bookmarks->IsArray()) {
        return std::nullopt;
    }
    for (const json::Value& bookmark_value : bookmarks->array_val) {
        const json::Value* id = bookmark_value.FindMember("id");
        const json::Value* title = bookmark_value.FindMember("title");
        const json::Value* url = bookmark_value.FindMember("url");
        Bookmark bookmark;
        if (id == nullptr || !ParseId(*id, bookmark.id) || title == nullptr || !title->IsString() ||
            url == nullptr || !url->IsString()) {
            return std::nullopt;
        }
        bookmark.title = title->string_val;
        bookmark.url = url->string_val;
        state.bookmarks.push_back(std::move(bookmark));
    }

    if (!IsConsistent(state)) {
        return std::nullopt;
    }
    return state;
}

// Schema 1 keeps the items inside each folder. IDs follow the old traversal order: a folder,
// then its items. A URL that repeats an earlier one is one record, so its later copy is dropped.
std::optional<BookmarkState> MigrateSchemaOne(const json::Value& root) {
    const json::Value* folders = root.FindMember("folders");
    if (folders == nullptr || !folders->IsArray()) {
        return std::nullopt;
    }
    BookmarkState state;
    for (const json::Value& folder_value : folders->array_val) {
        const json::Value* name = folder_value.FindMember("name");
        const json::Value* items = folder_value.FindMember("items");
        if (name == nullptr || !name->IsString() || items == nullptr || !items->IsArray()) {
            return std::nullopt;
        }
        BookmarkFolder folder{Allocate(state), name->string_val, {}};
        for (const json::Value& item_value : items->array_val) {
            const json::Value* title = item_value.FindMember("title");
            const json::Value* url = item_value.FindMember("url");
            if (title == nullptr || !title->IsString() || url == nullptr || !url->IsString()) {
                return std::nullopt;
            }
            if (FindIdByUrl(state, url->string_val).has_value()) {
                continue;
            }
            const std::uint64_t id = AppendBookmark(state, title->string_val, url->string_val);
            folder.bookmark_ids.push_back(id);
        }
        state.folders.push_back(std::move(folder));
    }
    return state;
}

std::string SerializeToJson(const BookmarkState& state) {
    std::ostringstream oss;
    json::Writer w(oss);

    const auto write_ids = [&w](const std::vector<std::uint64_t>& ids) {
        w.StartArray();
        for (std::uint64_t id : ids) {
            w.ArrayElement();
            w.Uint64Value(id);
        }
        w.EndArray();
    };

    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(BookmarkState::kCurrentSchemaVersion));
    w.Key("next_id");
    w.Uint64Value(state.next_id);
    w.Key("global_favorite_ids");
    write_ids(state.global_favorite_ids);
    w.Key("space_favorite_ids");
    w.StartObject();
    for (const auto& [space_key, ids] : state.space_favorite_ids) {
        if (ids.empty()) {
            continue;
        }
        w.Key(std::to_string(space_key));
        write_ids(ids);
    }
    w.EndObject();
    w.Key("folders");
    w.StartArray();
    for (const BookmarkFolder& folder : state.folders) {
        w.ArrayElement();
        w.StartObject();
        w.Key("id");
        w.Uint64Value(folder.id);
        w.Key("name");
        w.StringValue(folder.name);
        w.Key("bookmark_ids");
        write_ids(folder.bookmark_ids);
        w.EndObject();
    }
    w.EndArray();
    w.Key("bookmarks");
    w.StartArray();
    for (const Bookmark& bookmark : state.bookmarks) {
        w.ArrayElement();
        w.StartObject();
        w.Key("id");
        w.Uint64Value(bookmark.id);
        w.Key("title");
        w.StringValue(bookmark.title);
        w.Key("url");
        w.StringValue(bookmark.url);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    oss << '\n';
    return oss.str();
}

}  // namespace

BookmarkStore::BookmarkStore(std::filesystem::path path) : path_(std::move(path)) {}

BookmarkError BookmarkStore::Load() {
    state_ = BookmarkState{};
    read_only_ = false;
    read_only_message_.clear();
    invalid_original_.reset();
    if (path_.empty()) {
        return BookmarkError::kNone;
    }

    std::optional<std::string> content = ReadWholeFile(path_);
    if (!content.has_value()) {
        // An existing file that cannot be read has no bytes to back up, and a rename over it
        // would destroy it, so it stays untouched and the store is read-only. Only a missing
        // file starts writable.
        std::error_code ec;
        const bool exists = std::filesystem::exists(path_, ec);
        // A failed lookup is not proof the file is missing, so it counts as present.
        read_only_ = exists || static_cast<bool>(ec);
        if (read_only_) {
            read_only_message_ =
                "The bookmarks file exists but could not be read, so it was left as it is. "
                "Bookmarks are read-only for this run.";
        }
        return BookmarkError::kFileReadError;
    }
    std::optional<json::Value> root = json::Parser(*content).Parse();
    if (!root.has_value()) {
        invalid_original_ = std::move(*content);
        return BookmarkError::kParseError;
    }

    const json::Value* version = root->FindMember("version");
    if (version != nullptr && version->IsInt() && version->int_val == kSchemaVersionOne) {
        std::optional<BookmarkState> migrated = MigrateSchemaOne(*root);
        if (!migrated.has_value()) {
            invalid_original_ = std::move(*content);
            return BookmarkError::kSchemaError;
        }
        // Keep the migrated data in memory whatever happens to the disk. The original bytes are
        // saved first, then the schema-2 file replaces the v1 file.
        state_ = std::move(*migrated);
        if (!WriteAtomically(std::filesystem::path(path_.string() + ".v1.bak"), *content)) {
            read_only_ = true;
            read_only_message_ =
                "The old bookmarks file could not be backed up, so it was kept as it is. "
                "Bookmarks are read-only for this run.";
            return BookmarkError::kFileWriteError;
        }
        if (WriteState(state_) != BookmarkError::kNone) {
            read_only_ = true;
            read_only_message_ =
                "The upgraded bookmarks file could not be saved, so the old file was kept. "
                "Bookmarks are read-only for this run.";
            return BookmarkError::kFileWriteError;
        }
        return BookmarkError::kNone;
    }

    if (version != nullptr && version->IsInt() &&
        version->int_val == BookmarkState::kCurrentSchemaVersion) {
        std::optional<BookmarkState> parsed = ParseSchemaTwo(*root);
        if (parsed.has_value()) {
            state_ = std::move(*parsed);
            return BookmarkError::kNone;
        }
    }
    invalid_original_ = std::move(*content);
    return BookmarkError::kSchemaError;
}

const Bookmark* BookmarkStore::FindBookmark(std::uint64_t bookmark_id) const {
    return FindBookmarkIn(state_, bookmark_id);
}

const BookmarkFolder* BookmarkStore::FindFolder(std::uint64_t folder_id) const {
    for (const BookmarkFolder& folder : state_.folders) {
        if (folder.id == folder_id) {
            return &folder;
        }
    }
    return nullptr;
}

std::optional<std::uint64_t> BookmarkStore::FindBookmarkByUrl(std::string_view url) const {
    return FindIdByUrl(state_, url);
}

std::vector<FlatBookmark> BookmarkStore::Flattened() const {
    std::vector<FlatBookmark> flat;
    for (const BookmarkFolder& folder : state_.folders) {
        for (std::uint64_t id : folder.bookmark_ids) {
            if (const Bookmark* bookmark = FindBookmarkIn(state_, id)) {
                flat.push_back(FlatBookmark{folder.name, bookmark->title, bookmark->url});
            }
        }
    }
    return flat;
}

std::uint64_t BookmarkStore::MaxSpaceKey() const {
    return state_.space_favorite_ids.empty() ? 0 : state_.space_favorite_ids.rbegin()->first;
}

template <typename Apply>
BookmarkResult BookmarkStore::Commit(Apply&& apply) {
    // The edit runs on a copy, so a rejected or unsaved edit leaves state_ untouched. A read-only
    // store still answers edits that change nothing (for example, adding a URL already saved).
    BookmarkState next = state_;
    BookmarkResult result = apply(next);
    if (!result.ok() || next == state_) {
        return result;
    }
    if (read_only_) {
        return Fail(BookmarkError::kReadOnly, read_only_message_);
    }
    const BookmarkError error = WriteState(next);
    if (error != BookmarkError::kNone) {
        return Fail(error);
    }
    state_ = std::move(next);
    return result;
}

BookmarkError BookmarkStore::WriteState(const BookmarkState& next) {
    if (path_.empty()) {
        return BookmarkError::kNone;
    }
    if (invalid_original_.has_value() &&
        !WriteAtomically(std::filesystem::path(path_.string() + ".invalid.bak"),
                         *invalid_original_)) {
        return BookmarkError::kFileWriteError;
    }
    if (!WriteAtomically(path_, SerializeToJson(next))) {
        return BookmarkError::kFileWriteError;
    }
    invalid_original_.reset();
    return BookmarkError::kNone;
}

BookmarkResult BookmarkStore::AddOrFindByUrl(std::string_view title, std::string_view url,
                                             std::optional<std::uint64_t> folder_id) {
    if (IsBlank(url)) {
        return Fail(BookmarkError::kInvalidInput, "Enter an address first.");
    }
    return Commit([&](BookmarkState& next) {
        if (const std::optional<std::uint64_t> existing = FindIdByUrl(next, url)) {
            return Ok(*existing);
        }
        std::size_t folder_index = 0;
        if (folder_id.has_value()) {
            const auto found =
                std::find_if(next.folders.begin(), next.folders.end(),
                             [&](const BookmarkFolder& folder) { return folder.id == *folder_id; });
            if (found == next.folders.end()) {
                return Fail(BookmarkError::kNotFound);
            }
            folder_index = static_cast<std::size_t>(found - next.folders.begin());
        } else {
            folder_index = FindOrCreateFolderIndex(next, kUnfiledFolderName);
        }
        const std::uint64_t id = AppendBookmark(next, title, url);
        next.folders[folder_index].bookmark_ids.push_back(id);
        return Ok(id);
    });
}

BookmarkResult BookmarkStore::CreateFolder(std::string_view name) {
    if (IsBlank(name)) {
        return Fail(BookmarkError::kInvalidInput, "Folder name cannot be empty.");
    }
    return Commit([&](BookmarkState& next) {
        const std::uint64_t id = Allocate(next);
        next.folders.push_back(BookmarkFolder{id, std::string(name), {}});
        return Ok(id);
    });
}

BookmarkResult BookmarkStore::RenameFolder(std::uint64_t folder_id, std::string_view name) {
    if (IsBlank(name)) {
        return Fail(BookmarkError::kInvalidInput, "Folder name cannot be empty.");
    }
    return Commit([&](BookmarkState& next) {
        BookmarkFolder* folder = FindFolderIn(next, folder_id);
        if (folder == nullptr) {
            return Fail(BookmarkError::kNotFound);
        }
        folder->name = std::string(name);
        return Ok(folder_id);
    });
}

BookmarkResult BookmarkStore::MoveFolder(std::uint64_t folder_id, std::size_t index) {
    return Commit([&](BookmarkState& next) {
        const auto found =
            std::find_if(next.folders.begin(), next.folders.end(),
                         [&](const BookmarkFolder& folder) { return folder.id == folder_id; });
        if (found == next.folders.end()) {
            return Fail(BookmarkError::kNotFound);
        }
        if (index >= next.folders.size()) {
            return Fail(BookmarkError::kInvalidInput);
        }
        BookmarkFolder moved = std::move(*found);
        next.folders.erase(found);
        next.folders.insert(next.folders.begin() + static_cast<std::ptrdiff_t>(index),
                            std::move(moved));
        return Ok(folder_id);
    });
}

BookmarkResult BookmarkStore::DeleteFolder(std::uint64_t folder_id) {
    return Commit([&](BookmarkState& next) {
        const auto found =
            std::find_if(next.folders.begin(), next.folders.end(),
                         [&](const BookmarkFolder& folder) { return folder.id == folder_id; });
        if (found == next.folders.end()) {
            return Fail(BookmarkError::kNotFound);
        }
        if (!found->bookmark_ids.empty()) {
            return Fail(BookmarkError::kNotEmpty);
        }
        next.folders.erase(found);
        return Ok(folder_id);
    });
}

BookmarkResult BookmarkStore::DeleteFolderWithContents(std::uint64_t folder_id,
                                                       bool move_to_unfiled) {
    return Commit([&](BookmarkState& next) {
        const auto found =
            std::find_if(next.folders.begin(), next.folders.end(),
                         [&](const BookmarkFolder& folder) { return folder.id == folder_id; });
        if (found == next.folders.end()) {
            return Fail(BookmarkError::kNotFound);
        }
        const std::size_t folder_index =
            static_cast<std::size_t>(std::distance(next.folders.begin(), found));
        if (move_to_unfiled && found->name == kUnfiledFolderName && !found->bookmark_ids.empty()) {
            return Fail(BookmarkError::kInvalidInput,
                        "Unfiled bookmarks cannot be moved to Unfiled.");
        }
        const std::vector<std::uint64_t> ids = found->bookmark_ids;
        if (move_to_unfiled && !ids.empty()) {
            auto unfiled = std::find_if(next.folders.begin(), next.folders.end(),
                                        [](const BookmarkFolder& folder) {
                                            return folder.name == BookmarkStore::kUnfiledFolderName;
                                        });
            if (unfiled == next.folders.end()) {
                next.folders.push_back({.id = next.next_id++,
                                        .name = std::string(kUnfiledFolderName),
                                        .bookmark_ids = {}});
                unfiled = std::prev(next.folders.end());
            }
            unfiled->bookmark_ids.insert(unfiled->bookmark_ids.end(), ids.begin(), ids.end());
        } else {
            for (const std::uint64_t bookmark_id : ids) {
                static_cast<void>(EraseBookmarkIn(next, bookmark_id));
            }
        }
        next.folders.erase(next.folders.begin() + static_cast<std::ptrdiff_t>(folder_index));
        return Ok(folder_id);
    });
}

BookmarkResult BookmarkStore::MoveBookmark(std::uint64_t bookmark_id, std::uint64_t folder_id) {
    return Commit([&](BookmarkState& next) {
        if (FindBookmarkIn(next, bookmark_id) == nullptr) {
            return Fail(BookmarkError::kNotFound);
        }
        BookmarkFolder* target = FindFolderIn(next, folder_id);
        if (target == nullptr) {
            return Fail(BookmarkError::kNotFound);
        }
        // Already in the target folder: nothing to move, and the order stays as it is.
        if (std::find(target->bookmark_ids.begin(), target->bookmark_ids.end(), bookmark_id) !=
            target->bookmark_ids.end()) {
            return Ok(bookmark_id);
        }
        for (BookmarkFolder& folder : next.folders) {
            std::erase(folder.bookmark_ids, bookmark_id);
        }
        FindFolderIn(next, folder_id)->bookmark_ids.push_back(bookmark_id);
        return Ok(bookmark_id);
    });
}

BookmarkResult BookmarkStore::EditBookmark(std::uint64_t bookmark_id, std::string_view title,
                                           std::string_view url) {
    if (IsBlank(url)) {
        return Fail(BookmarkError::kInvalidInput, "Enter an address first.");
    }
    return Commit([&](BookmarkState& next) {
        Bookmark* bookmark = FindBookmarkIn(next, bookmark_id);
        if (bookmark == nullptr) {
            return Fail(BookmarkError::kNotFound);
        }
        const std::optional<std::uint64_t> other = FindIdByUrl(next, url);
        if (other.has_value() && *other != bookmark_id) {
            return Fail(BookmarkError::kDuplicate, "Another bookmark already has this address.");
        }
        bookmark->title = std::string(title);
        bookmark->url = std::string(url);
        return Ok(bookmark_id);
    });
}

BookmarkResult BookmarkStore::DeleteBookmark(std::uint64_t bookmark_id) {
    return Commit([&](BookmarkState& next) {
        if (!EraseBookmarkIn(next, bookmark_id)) {
            return Fail(BookmarkError::kNotFound);
        }
        return Ok(bookmark_id);
    });
}

BookmarkResult BookmarkStore::AddFavorite(FavoriteScope scope, std::uint64_t bookmark_id) {
    return Commit([&](BookmarkState& next) { return AddFavoriteIn(next, scope, bookmark_id); });
}

BookmarkResult BookmarkStore::AddFavoriteByUrl(FavoriteScope scope, std::string_view title,
                                               std::string_view url) {
    if (IsBlank(url)) {
        return Fail(BookmarkError::kInvalidInput, "Enter an address first.");
    }
    return Commit([&](BookmarkState& next) {
        if (!IsValidFavoriteScope(scope)) {
            return Fail(BookmarkError::kInvalidInput, "That space is not valid.");
        }
        // Find or create the bookmark, then favorite it; both land in one save.
        std::uint64_t bookmark_id = 0;
        if (const std::optional<std::uint64_t> existing = FindIdByUrl(next, url)) {
            bookmark_id = *existing;
        } else {
            const std::size_t folder_index = FindOrCreateFolderIndex(next, kUnfiledFolderName);
            bookmark_id = AppendBookmark(next, title, url);
            next.folders[folder_index].bookmark_ids.push_back(bookmark_id);
        }
        return AddFavoriteIn(next, scope, bookmark_id);
    });
}

BookmarkResult BookmarkStore::RemoveFavorite(FavoriteScope scope, std::uint64_t bookmark_id) {
    return Commit([&](BookmarkState& next) {
        std::vector<std::uint64_t>* list = FavoriteListIn(next, scope);
        if (list == nullptr || std::find(list->begin(), list->end(), bookmark_id) == list->end()) {
            return Fail(BookmarkError::kNotFound);
        }
        std::erase(*list, bookmark_id);
        if (scope.has_value() && list->empty()) {
            next.space_favorite_ids.erase(*scope);
        }
        return Ok(bookmark_id);
    });
}

BookmarkResult BookmarkStore::ReorderFavorite(FavoriteScope scope, std::uint64_t bookmark_id,
                                              std::size_t index) {
    return Commit([&](BookmarkState& next) {
        std::vector<std::uint64_t>* list = FavoriteListIn(next, scope);
        const auto found = list == nullptr ? std::vector<std::uint64_t>::iterator{}
                                           : std::find(list->begin(), list->end(), bookmark_id);
        if (list == nullptr || found == list->end()) {
            return Fail(BookmarkError::kNotFound);
        }
        if (index >= list->size()) {
            return Fail(BookmarkError::kInvalidInput);
        }
        list->erase(found);
        list->insert(list->begin() + static_cast<std::ptrdiff_t>(index), bookmark_id);
        return Ok(bookmark_id);
    });
}

BookmarkResult BookmarkStore::RemoveSpaceFavorites(std::uint64_t space_key) {
    if (space_key == 0) {
        return Fail(BookmarkError::kInvalidInput, "That space is not valid.");
    }
    return Commit([&](BookmarkState& next) {
        next.space_favorite_ids.erase(space_key);
        return Ok(space_key);
    });
}

BookmarkResult BookmarkStore::Import(std::string_view folder_name,
                                     const std::vector<BookmarkItem>& items) {
    if (IsBlank(folder_name)) {
        return Fail(BookmarkError::kInvalidInput, "Folder name cannot be empty.");
    }
    return Commit([&](BookmarkState& next) {
        // Library-wide dedupe, case-insensitive, including duplicates inside |items|. Nothing is
        // created unless at least one bookmark will be added.
        std::set<std::string> seen;
        for (const Bookmark& bookmark : next.bookmarks) {
            seen.insert(NormalizeUrlForDedup(bookmark.url));
        }
        std::vector<const BookmarkItem*> fresh;
        for (const BookmarkItem& item : items) {
            if (seen.insert(NormalizeUrlForDedup(item.url)).second) {
                fresh.push_back(&item);
            }
        }
        if (fresh.empty()) {
            return Ok();
        }
        const std::size_t folder_index = FindOrCreateFolderIndex(next, folder_name);
        const std::uint64_t folder_id = next.folders[folder_index].id;
        for (const BookmarkItem* item : fresh) {
            const std::uint64_t id = AppendBookmark(next, item->title, item->url);
            next.folders[folder_index].bookmark_ids.push_back(id);
        }
        BookmarkResult result = Ok(folder_id);
        result.added = fresh.size();
        return result;
    });
}

std::filesystem::path BookmarkStore::DefaultBookmarksFilePath() {
    return IslandDataDirectory() / "bookmarks.json";
}

}  // namespace island
