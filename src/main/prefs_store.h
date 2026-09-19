#ifndef ISLAND_PREFS_STORE_H_
#define ISLAND_PREFS_STORE_H_

#include <cstdint>
#include <filesystem>
#include <string>

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
