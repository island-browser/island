#include "session_store.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json_util.h"

namespace island {

// =========================================================================
// SessionState
// =========================================================================

SessionState SessionState::Default() noexcept {
    SessionState state;
    state.version = kCurrentSchemaVersion;
    return state;
}

// =========================================================================
// Serialize
// =========================================================================

namespace {

void WriteTabState(json::Writer& w, const TabState& tab) {
    w.StartObject();
    w.Key("id");
    w.Uint64Value(tab.id.value);
    w.Key("url");
    w.StringValue(tab.url);
    if (tab.pinned) {
        w.Key("pinned");
        w.BoolValue(true);
    }
    w.EndObject();
}

void WriteSplitState(json::Writer& w, const SplitState& split) {
    w.StartObject();
    w.Key("first_tab_id");
    w.Uint64Value(split.first_tab_id.value);
    w.Key("second_tab_id");
    w.Uint64Value(split.second_tab_id.value);
    w.EndObject();
}

void WriteSpaceState(json::Writer& w, const SpaceState& space) {
    w.StartObject();
    w.Key("id");
    w.Uint64Value(space.id.value);
    w.Key("name");
    w.StringValue(space.name);
    w.Key("color_argb");
    w.UintValue(space.color_argb);

    w.Key("tabs");
    w.StartArray();
    for (const auto& tab : space.tabs) {
        w.ArrayElement();
        WriteTabState(w, tab);
    }
    w.EndArray();

    if (space.active_tab_id.has_value()) {
        w.Key("active_tab_id");
        w.Uint64Value(space.active_tab_id->value);
    }

    if (space.split.has_value()) {
        w.Key("split");
        WriteSplitState(w, *space.split);
    }

    w.EndObject();
}

std::string SerializeToJson(const SessionState& state) {
    std::ostringstream oss;
    json::Writer w(oss);

    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(state.version));

    if (state.active_space_id.has_value()) {
        w.Key("active_space_id");
        w.Uint64Value(state.active_space_id->value);
    }

    w.Key("spaces");
    w.StartArray();
    for (const auto& space : state.spaces) {
        w.ArrayElement();
        WriteSpaceState(w, space);
    }
    w.EndArray();
    w.EndObject();

    // Trailing newline for human-readability; also helps with the
    // bookmark bracket test that asserts a trailing newline.
    oss << '\n';
    return oss.str();
}

}  // namespace

// =========================================================================
// Deserialize
// =========================================================================

namespace {

LoadResult ParseSessionState(const json::Value& root) {
    // Schema check: root must be an object
    if (!root.IsObject()) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }

    // version must be present and an integer
    if (!root.HasRequiredInt("version")) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }
    std::int64_t version_val = root.FindMember("version")->int_val;
    if (version_val != SessionState::kCurrentSchemaVersion) {
        return {SessionState::Default(), SessionError::kVersionMismatch};
    }

    // spaces must be present and an array
    if (!root.HasRequiredArray("spaces")) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }

    SessionState state;
    state.version = SessionState::kCurrentSchemaVersion;

    // Optional active_space_id
    const json::Value* active_id = root.FindMember("active_space_id");
    if (active_id != nullptr) {
        if (active_id->IsInt() && active_id->int_val > 0) {
            state.active_space_id = SpaceId{static_cast<std::uint64_t>(active_id->int_val)};
        }
        // Non-int or non-positive → ignore, don't fail
    }

    const json::Array& spaces_arr = root.FindMember("spaces")->array_val;
    for (const auto& space_val : spaces_arr) {
        if (!space_val.IsObject()) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }

        // Required fields per space: id, name, color_argb, tabs (array)
        if (!space_val.HasRequiredInt("id") || !space_val.HasRequiredString("name") ||
            !space_val.HasRequiredInt("color_argb") || !space_val.HasRequiredArray("tabs")) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }

        SpaceState space_state;
        space_state.id = SpaceId{static_cast<std::uint64_t>(space_val.FindMember("id")->int_val)};
        space_state.name = space_val.FindMember("name")->string_val;
        std::int64_t color = space_val.FindMember("color_argb")->int_val;
        if (color < 0 || color > 0xFFFFFFFFLL) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }
        space_state.color_argb = static_cast<std::uint32_t>(color);

        // Parse tabs
        const json::Array& tabs_arr = space_val.FindMember("tabs")->array_val;
        for (const auto& tab_val : tabs_arr) {
            if (!tab_val.IsObject() || !tab_val.HasRequiredInt("id") ||
                !tab_val.HasRequiredString("url")) {
                return {SessionState::Default(), SessionError::kSchemaError};
            }
            TabState tab_state;
            tab_state.id = TabId{static_cast<std::uint64_t>(tab_val.FindMember("id")->int_val)};
            tab_state.url = tab_val.FindMember("url")->string_val;
            if (const json::Value* pinned = tab_val.FindMember("pinned")) {
                if (pinned->type != json::Type::kBool) {
                    return {SessionState::Default(), SessionError::kSchemaError};
                }
                tab_state.pinned = pinned->bool_val;
            }
            space_state.tabs.push_back(std::move(tab_state));
        }

        // Optional active_tab_id
        const json::Value* atid = space_val.FindMember("active_tab_id");
        if (atid != nullptr) {
            if (atid->IsInt() && atid->int_val > 0) {
                space_state.active_tab_id = TabId{static_cast<std::uint64_t>(atid->int_val)};
            }
        }

        // Optional split
        const json::Value* split_val = space_val.FindMember("split");
        if (split_val != nullptr) {
            if (!split_val->IsObject() || !split_val->HasRequiredInt("first_tab_id") ||
                !split_val->HasRequiredInt("second_tab_id")) {
                return {SessionState::Default(), SessionError::kSchemaError};
            }
            SplitState split;
            split.first_tab_id =
                TabId{static_cast<std::uint64_t>(split_val->FindMember("first_tab_id")->int_val)};
            split.second_tab_id =
                TabId{static_cast<std::uint64_t>(split_val->FindMember("second_tab_id")->int_val)};
            space_state.split = std::move(split);
        }

        state.spaces.push_back(std::move(space_state));
    }

    return {std::move(state), SessionError::kNone};
}

}  // namespace

// =========================================================================
// SessionStore
// =========================================================================

LoadResult SessionStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {SessionState::Default(), SessionError::kFileReadError};
    }

    // Read the entire file into a string.
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return {SessionState::Default(), SessionError::kFileReadError};
    }

    // Parse JSON.
    std::optional<json::Value> root = json::Parser(content).Parse();
    if (!root.has_value()) {
        return {SessionState::Default(), SessionError::kParseError};
    }

    // Validate and convert.
    return ParseSessionState(*root);
}

SessionError SessionStore::Save(const std::filesystem::path& path, const SessionState& state) {
    const std::string json_text = SerializeToJson(state);

    // Atomic write: write to a temp file in the same directory, then rename.
    // This guarantees the target file is either intact or absent — never
    // truncated.
    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return SessionError::kFileWriteError;

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return SessionError::kFileWriteError;
        out << json_text;
        if (!out) return SessionError::kFileWriteError;
        out.close();
        if (!out) return SessionError::kFileWriteError;
    }

    // Rename atomically
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        return SessionError::kFileWriteError;
    }

    return SessionError::kNone;
}

std::filesystem::path SessionStore::DefaultSessionFilePath() {
#if defined(_WIN32)
    const char* const app_data = std::getenv("APPDATA");
    std::filesystem::path base =
        app_data != nullptr && *app_data != '\0'
            ? std::filesystem::path(app_data)
            : std::filesystem::path(std::getenv("USERPROFILE")) / "AppData" / "Roaming";
    return base / "Island" / "session.json";
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("session.json");
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "Island" /
           "session.json";
#else
    const char* const xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path(xdg_data_home) / "Island" / "session.json";
    }
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("session.json");
    }
    return std::filesystem::path(home) / ".local" / "share" / "Island" / "session.json";
#endif
}

}  // namespace island
