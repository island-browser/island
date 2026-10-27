#include "prefs_store.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "action_gate.h"
#include "agent_providers.h"
#include "data_paths.h"
#include "json_util.h"
#include "tab_sleep_policy.h"

namespace island {
namespace {

static_assert(agent::kDefaultAgentProviderId == "claude",
              "PrefsState::agent_provider defaults to the default provider");

PrefsLoadResult Fail(PrefsError error) { return {error, PrefsState{}}; }

// A bare host ("Example.com") or a URL, with surrounding spaces ignored. HostOf needs a scheme,
// so one is added to a bare host. A host without a dot ("com", "localhost") yields nothing: it
// would otherwise trust every host under that label.
std::string TrustedHostOf(const std::string& entry) {
    constexpr std::string_view kSpaces = " \t\r\n";
    const std::size_t first = entry.find_first_not_of(kSpaces);
    if (first == std::string::npos) {
        return {};
    }
    const std::string text = entry.substr(first, entry.find_last_not_of(kSpaces) - first + 1);
    const std::string host =
        agent::HostOf(text.find("://") == std::string::npos ? "https://" + text : text);
    return host.find('.') == std::string::npos ? std::string() : host;
}

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
    if (const json::Value* provider = root.FindMember("agent_provider")) {
        if (!provider->IsString()) {
            return Fail(PrefsError::kSchemaError);
        }
        state.agent_provider = provider->string_val;
    } else if (state.agent_command == agent::kLegacyClaudeAgentCommand) {
        // Files from before providers existed: the old built-in default
        // becomes the Claude Code provider (whose adapter was renamed) ...
        state.agent_provider = std::string(agent::kDefaultAgentProviderId);
        state.agent_command.clear();
    } else if (!state.agent_command.empty()) {
        // ... and any other saved command becomes the custom provider.
        state.agent_provider = std::string(agent::kCustomAgentProviderId);
    }
    if (const json::Value* bindings = root.FindMember("keybindings")) {
        if (!bindings->IsObject()) {
            return Fail(PrefsError::kSchemaError);
        }
        for (const auto& [action, binding] : bindings->object_val) {
            if (!binding.IsString()) {
                return Fail(PrefsError::kSchemaError);
            }
            state.keybindings.emplace_back(action, binding.string_val);
        }
    }
    if (const json::Value* open = root.FindMember("agent_panel_open")) {
        if (open->type != json::Type::kBool) {
            return Fail(PrefsError::kSchemaError);
        }
        state.agent_panel_open = open->bool_val;
    }
    for (const auto& [key, field] : {std::pair{"auto_check_updates", &state.auto_check_updates},
                                     std::pair{"include_prereleases", &state.include_prereleases},
                                     std::pair{"sidebar_collapsed", &state.sidebar_collapsed},
                                     std::pair{"remember_history", &state.remember_history},
                                     std::pair{"block_ads", &state.block_ads},
                                     std::pair{"block_trackers", &state.block_trackers}}) {
        if (const json::Value* value = root.FindMember(key)) {
            if (!value->IsBool()) {
                return Fail(PrefsError::kSchemaError);
            }
            *field = value->bool_val;
        }
    }
    for (const auto& [key, field] : {std::pair{"search_engine", &state.search_engine},
                                     std::pair{"start_mode", &state.start_mode},
                                     std::pair{"reader_width", &state.reader_width},
                                     std::pair{"reader_theme", &state.reader_theme}}) {
        if (const json::Value* value = root.FindMember(key)) {
            if (!value->IsString()) {
                return Fail(PrefsError::kSchemaError);
            }
            *field = value->string_val;
        }
    }
    if (const json::Value* sleep_minutes = root.FindMember("tab_sleep_minutes")) {
        if (!sleep_minutes->IsInt()) {
            return Fail(PrefsError::kSchemaError);
        }
        state.tab_sleep_minutes =
            NormalizeTabSleepMinutes(static_cast<int>(sleep_minutes->int_val));
    }
    if (const json::Value* reader_size = root.FindMember("reader_size")) {
        if (!reader_size->IsInt()) {
            return Fail(PrefsError::kSchemaError);
        }
        state.reader_size =
            static_cast<int>(std::clamp<std::int64_t>(reader_size->int_val, 1, 1000));
    }
    if (const json::Value* appearance = root.FindMember("appearance")) {
        if (!appearance->IsObject()) {
            return Fail(PrefsError::kSchemaError);
        }
        // Arc-era development profiles wrote appearance knobs before the Look key existed.
        // Released 0.6.x profiles have no appearance object and keep the Native default.
        state.appearance = ApplyLookPreset(LookPreset::kArc);
        AppearanceSettings& a = state.appearance;
        if (const json::Value* look = appearance->FindMember("look")) {
            if (!look->IsString()) {
                return Fail(PrefsError::kSchemaError);
            }
            if (const std::optional<LookPreset> preset = ParseLook(look->string_val)) {
                a = ApplyLookPreset(*preset);
            }
            // Unknown names mean Custom. Keep the Arc-compatible base and load the stored knobs.
        }
        for (const auto& [key, value] : appearance->object_val) {
            if (key == "sidebar_tint_percent" && value.IsInt()) {
                a.sidebar_tint_percent = static_cast<int>(value.int_val);
            } else if (key == "card_radius" && value.IsInt()) {
                a.card_radius_dip = static_cast<int>(value.int_val);
            } else if (key == "sidebar_side" && value.IsString()) {
                a.sidebar_side =
                    value.string_val == "right" ? SidebarSide::kRight : SidebarSide::kLeft;
            } else if (key == "density" && value.IsString()) {
                a.density =
                    value.string_val == "compact" ? Density::kCompact : Density::kComfortable;
            } else if (key == "font" && value.IsString()) {
                a.font_family = value.string_val;
            } else if (key == "card_shadow" && value.IsBool()) {
                a.card_shadow = value.bool_val;
            } else if (key == "full_bleed" && value.IsBool()) {
                a.full_bleed = value.bool_val;
            }
        }
        a = NormalizeAppearance(a);
    }
    if (const json::Value* last_check = root.FindMember("last_update_check")) {
        if (!last_check->IsInt()) {
            return Fail(PrefsError::kSchemaError);
        }
        state.last_update_check = last_check->int_val;
    }
    if (const json::Value* allowlist = root.FindMember("content_blocker_allowlist")) {
        if (!allowlist->IsArray()) {
            return Fail(PrefsError::kSchemaError);
        }
        for (const json::Value& site : allowlist->array_val) {
            if (!site.IsString()) {
                return Fail(PrefsError::kSchemaError);
            }
            state.content_blocker_allowlist.push_back(site.string_val);
        }
    }
    // Lenient, unlike the lists above: a value that is not an array is ignored and non-string
    // entries are skipped. Fewer trusted sites only means more confirmations, and a damaged list
    // must not cost the person the rest of their preferences.
    if (const json::Value* trusted = root.FindMember("agent_trusted_sites");
        trusted != nullptr && trusted->IsArray()) {
        std::vector<std::string> sites;
        for (const json::Value& site : trusted->array_val) {
            if (site.IsString()) {
                sites.push_back(site.string_val);
            }
        }
        state.agent_trusted_sites = NormalizeTrustedSites(sites);
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
    w.Key("agent_provider");
    w.StringValue(state.agent_provider);
    if (!state.agent_command.empty()) {
        w.Key("agent_command");
        w.StringValue(state.agent_command);
    }
    w.Key("agent_panel_open");
    w.BoolValue(state.agent_panel_open);
    if (!state.keybindings.empty()) {
        w.Key("keybindings");
        w.StartObject();
        for (const auto& [action, binding] : state.keybindings) {
            w.Key(action);
            w.StringValue(binding);
        }
        w.EndObject();
    }
    w.Key("sidebar_collapsed");
    w.BoolValue(state.sidebar_collapsed);
    w.Key("auto_check_updates");
    w.BoolValue(state.auto_check_updates);
    w.Key("include_prereleases");
    w.BoolValue(state.include_prereleases);
    w.Key("search_engine");
    w.StringValue(state.search_engine);
    w.Key("tab_sleep_minutes");
    w.IntValue(state.tab_sleep_minutes);
    w.Key("start_mode");
    w.StringValue(state.start_mode);
    w.Key("remember_history");
    w.BoolValue(state.remember_history);
    w.Key("block_ads");
    w.BoolValue(state.block_ads);
    w.Key("block_trackers");
    w.BoolValue(state.block_trackers);
    if (!state.content_blocker_allowlist.empty()) {
        w.Key("content_blocker_allowlist");
        w.StartArray();
        for (const std::string& site : state.content_blocker_allowlist) {
            w.ArrayElement();
            w.StringValue(site);
        }
        w.EndArray();
    }
    w.Key("reader_size");
    w.IntValue(state.reader_size);
    w.Key("reader_width");
    w.StringValue(state.reader_width);
    w.Key("reader_theme");
    w.StringValue(state.reader_theme);
    const AppearanceSettings& a = state.appearance;
    w.Key("appearance");
    w.StartObject();
    w.Key("look");
    w.StringValue(LookName(a.look));
    w.Key("sidebar_tint_percent");
    w.IntValue(a.sidebar_tint_percent);
    w.Key("sidebar_side");
    w.StringValue(a.sidebar_side == SidebarSide::kRight ? "right" : "left");
    w.Key("card_radius");
    w.IntValue(a.card_radius_dip);
    w.Key("density");
    w.StringValue(a.density == Density::kCompact ? "compact" : "comfortable");
    w.Key("font");
    w.StringValue(a.font_family);
    w.Key("card_shadow");
    w.BoolValue(a.card_shadow);
    w.Key("full_bleed");
    w.BoolValue(a.full_bleed);
    w.EndObject();
    if (!state.agent_trusted_sites.empty()) {
        w.Key("agent_trusted_sites");
        w.StartArray();
        for (const std::string& site : state.agent_trusted_sites) {
            w.ArrayElement();
            w.StringValue(site);
        }
        w.EndArray();
    }
    if (state.last_update_check != 0) {
        w.Key("last_update_check");
        w.IntValue(state.last_update_check);
    }
    w.EndObject();
    oss << '\n';
    return oss.str();
}

}  // namespace

std::vector<std::string> NormalizeTrustedSites(const std::vector<std::string>& sites) {
    std::vector<std::string> normalized;
    for (const std::string& site : sites) {
        std::string host = TrustedHostOf(site);
        if (!host.empty() &&
            std::find(normalized.begin(), normalized.end(), host) == normalized.end()) {
            normalized.push_back(std::move(host));
        }
    }
    return normalized;
}

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
    return IslandDataDirectory() / "prefs.json";
}

}  // namespace island
