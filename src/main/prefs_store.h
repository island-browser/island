#ifndef ISLAND_PREFS_STORE_H_
#define ISLAND_PREFS_STORE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace island {

// How the window resolves its chrome theme. kSystem keeps the Phase 3
// behavior of classifying the OS theme from the window background; the other
// two force one theme regardless of the OS.
enum class ThemePreference : std::uint8_t { kSystem, kLight, kDark };

// Persisted user preferences. Kept separate from the session file so clearing
// session state never resets the user's chosen appearance or onboarding flag.
struct PrefsState {
    static constexpr int kCurrentSchemaVersion = 1;

    int version = kCurrentSchemaVersion;
    bool onboarding_completed = false;
    ThemePreference theme = ThemePreference::kSystem;
    // The ACP agent the sidebar panel runs: a provider id from
    // src/agent/agent_providers.h ("claude", "codex", ..., "custom"). Optional
    // in the file; added without a schema bump because older files simply
    // lack the keys. A file without it is migrated from agent_command on load.
    std::string agent_provider = "claude";
    // The custom provider's shell command line; kept while another provider
    // is selected so switching back restores it. Optional in the file.
    std::string agent_command;
    // Whether the agent panel was open at the last clean quit.
    bool agent_panel_open = false;
    // Keyboard shortcut overrides: {action id, binding} pairs that differ
    // from the built-in keymap (an empty binding unbinds). Optional key.
    std::vector<std::pair<std::string, std::string>> keybindings;
    // In-browser updater (optional keys). Check GitHub Releases at startup
    // at most once a day; the time of the last check in unix seconds (0 =
    // never); and whether SemVer pre-releases (0.5.0-beta.1) are offered.
    bool auto_check_updates = true;
    std::int64_t last_update_check = 0;
    bool include_prereleases = false;

    bool operator==(const PrefsState&) const = default;
};

enum class PrefsError : std::uint8_t {
    kNone = 0,
    kFileReadError,
    kFileWriteError,
    kParseError,
    kSchemaError,
};

struct PrefsLoadResult {
    PrefsError error = PrefsError::kNone;
    // Fresh-install defaults unless error == kNone.
    PrefsState state;
};

class PrefsStore {
  public:
    // A missing, unreadable, or schema-invalid file returns the fresh-install
    // defaults and a non-kNone error; callers log and continue. Identical
    // policy to SessionStore::Load.
    static PrefsLoadResult Load(const std::filesystem::path& path);
    static PrefsError Save(const std::filesystem::path& path, const PrefsState& state);
    static std::filesystem::path DefaultPrefsFilePath();
};

[[nodiscard]] std::string ThemePreferenceToString(ThemePreference preference);
// Strict parse: only the exact lowercase tokens; anything else returns false.
[[nodiscard]] bool ThemePreferenceFromString(std::string_view text, ThemePreference& out);

}  // namespace island

#endif  // ISLAND_PREFS_STORE_H_
