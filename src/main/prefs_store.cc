#include "prefs_store.h"

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

PrefsLoadResult Fail(PrefsError error) { return {error, PrefsState{}}; }

PrefsLoadResult ParsePrefsState(const json::Value& root) {
    if (!root.IsObject() || !root.HasRequiredInt("version") ||
        root.FindMember("version")->int_val != PrefsState::kCurrentSchemaVersion ||
        !root.HasRequiredBool("onboarding_completed") || !root.HasRequiredString("theme")) {
        return Fail(PrefsError::kSchemaError);
    }

    ThemePreference theme;
    if (!ThemePreferenceFromString(root.FindMember("theme")->string_val, theme)) {
        return Fail(PrefsError::kSchemaError);
    }

    PrefsState state;
    state.onboarding_completed = root.FindMember("onboarding_completed")->bool_val;
    state.theme = theme;
    // Optional keys: a wrong type is a schema error, absence keeps the default.
    if (const json::Value* command = root.FindMember("agent_command")) {
        if (!command->IsString()) {
            return Fail(PrefsError::kSchemaError);
        }
        state.agent_command = command->string_val;
    }
    if (const json::Value* open = root.FindMember("agent_panel_open")) {
        if (open->type != json::Type::kBool) {
            return Fail(PrefsError::kSchemaError);
        }
        state.agent_panel_open = open->bool_val;
    }
    return {PrefsError::kNone, state};
}

std::string SerializeToJson(const PrefsState& state) {
    std::ostringstream oss;
    json::Writer w(oss);

    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(state.version));
    w.Key("onboarding_completed");
    w.BoolValue(state.onboarding_completed);
    w.Key("theme");
    w.StringValue(ThemePreferenceToString(state.theme));
    if (!state.agent_command.empty()) {
        w.Key("agent_command");
        w.StringValue(state.agent_command);
    }
    w.Key("agent_panel_open");
    w.BoolValue(state.agent_panel_open);
    w.EndObject();
    oss << '\n';
    return oss.str();
}

}  // namespace

std::string ThemePreferenceToString(ThemePreference preference) {
    switch (preference) {
        case ThemePreference::kLight:
            return "light";
        case ThemePreference::kDark:
            return "dark";
        case ThemePreference::kSystem:
            return "system";
    }
    return "system";
}

bool ThemePreferenceFromString(std::string_view text, ThemePreference& out) {
    if (text == "system") {
        out = ThemePreference::kSystem;
        return true;
    }
    if (text == "light") {
        out = ThemePreference::kLight;
        return true;
    }
    if (text == "dark") {
        out = ThemePreference::kDark;
        return true;
    }
    return false;
}

PrefsLoadResult PrefsStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return Fail(PrefsError::kFileReadError);
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return Fail(PrefsError::kFileReadError);
    }
    std::optional<json::Value> root = json::Parser(content).Parse();
    if (!root.has_value()) {
        return Fail(PrefsError::kParseError);
    }
    return ParsePrefsState(*root);
}

PrefsError PrefsStore::Save(const std::filesystem::path& path, const PrefsState& state) {
    const std::string json_text = SerializeToJson(state);

    // Atomic write, same discipline as SessionStore::Save: the target file is
    // either intact or absent, never truncated.
    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return PrefsError::kFileWriteError;
    }

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return PrefsError::kFileWriteError;
        }
        out << json_text;
        if (!out) {
            return PrefsError::kFileWriteError;
        }
    }

    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        return PrefsError::kFileWriteError;
    }
    return PrefsError::kNone;
}

std::filesystem::path PrefsStore::DefaultPrefsFilePath() {
    // Lives beside session.json in the shared Island data directory.
#if defined(_WIN32)
    const char* const app_data = std::getenv("APPDATA");
    std::filesystem::path base =
        app_data != nullptr && *app_data != '\0'
            ? std::filesystem::path(app_data)
            : std::filesystem::path(std::getenv("USERPROFILE")) / "AppData" / "Roaming";
    return base / "Island" / "prefs.json";
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("prefs.json");
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "Island" /
           "prefs.json";
#else
    const char* const xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path(xdg_data_home) / "Island" / "prefs.json";
    }
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("prefs.json");
    }
    return std::filesystem::path(home) / ".local" / "share" / "Island" / "prefs.json";
#endif
}

}  // namespace island
