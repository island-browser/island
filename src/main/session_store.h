#ifndef ISLAND_SESSION_STORE_H_
#define ISLAND_SESSION_STORE_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "tab_id.h"

namespace island {

// ---------------------------------------------------------------------------
// Error taxonomy
// ---------------------------------------------------------------------------

enum class SessionError : std::uint8_t {
    kNone = 0,
    kFileReadError,
    kFileWriteError,
    kParseError,
    kSchemaError,
    kVersionMismatch,
};

// ---------------------------------------------------------------------------
// Serializable sub-structures
// ---------------------------------------------------------------------------

struct TabState {
    TabId id;
    std::string url;
    // Optional in the file (absent = false), so older sessions still load.
    bool pinned = false;
    // Research grouping is optional so older sessions remain valid.
    std::optional<TabId> group_parent;
    std::string group_label;
    // Optional in the file (absent = false): the agent opened this tab itself.
    bool opened_by_agent = false;

    bool operator==(const TabState&) const = default;
};

struct SplitState {
    TabId first_tab_id;
    TabId second_tab_id;

    bool operator==(const SplitState&) const = default;
};

struct SpaceState {
    SpaceId id;
    std::string name;
    std::uint32_t color_argb = 0;
    std::vector<TabState> tabs;
    std::optional<TabId> active_tab_id;
    std::optional<SplitState> split;
    // Optional in the file; empty and unknown values both mean no icon.
    std::string icon_id;
    // 0.10 (R-D3, R-R1, R-D4). All optional in the file; an absent or wrong-typed member
    // loads as empty. |web_data_key| names the space's cookie directory (R-D3); the store does
    // not validate it, the web-data layer does. |import_key| marks a space created by an Arc
    // import; |routes| holds host patterns that open in this space (R-R1).
    std::string web_data_key;
    std::string import_key;
    std::vector<std::string> routes;

    bool operator==(const SpaceState&) const = default;
};

// The saved record of one favorite's virtual tab (see favorite_tabs.h). Absent |space_id| means
// the global favorite strip. Optional in the file: a session without the key has none.
struct FavoriteTabState {
    std::optional<SpaceId> space_id;
    std::uint64_t bookmark_id = 0;
    std::string reset_url;
    std::string last_url;

    bool operator==(const FavoriteTabState&) const = default;
};

struct SessionState {
    static constexpr int kCurrentSchemaVersion = 1;

    int version = kCurrentSchemaVersion;
    std::optional<SpaceId> active_space_id;
    std::vector<SpaceState> spaces;
    // Optional in the file (absent = none), so the schema version does not change for it.
    std::vector<FavoriteTabState> favorite_tabs;

    bool operator==(const SessionState&) const = default;

    // Returns a default session that represents a fresh-install state (zero
    // spaces, zero tabs, no active selection).  Callers use this as the
    // fallback for every non-recoverable Load outcome.
    static SessionState Default() noexcept;
};

// ---------------------------------------------------------------------------
// Load result
// ---------------------------------------------------------------------------

// Every Load call returns a LoadResult.  When `error` is kNone the `state`
// field holds the validated deserialized session.  For every other error
// (missing file, I/O failure, parse failure, schema/version mismatch) the
// `state` field is a fresh Default() session and `error` records the cause.
// Callers MUST treat every non‑kNone outcome identically: fall back to a
// fresh session.  No partial-recovery path exists.
struct LoadResult {
    SessionState state;
    SessionError error = SessionError::kNone;
};

// ---------------------------------------------------------------------------
// SessionStore — the single public seam U2/U11 call
// ---------------------------------------------------------------------------

class SessionStore {
  public:
    SessionStore() = delete;

    // The platform app-data session file path: macOS
    // ~/Library/Application Support/Island/session.json; Windows
    // %APPDATA%\Island\session.json; Linux $XDG_DATA_HOME/Island/session.json
    // with ~/.local/share/Island/session.json as the fallback. Callers that
    // need isolation (tests) pass explicit paths instead.
    [[nodiscard]] static std::filesystem::path DefaultSessionFilePath();

    // Load and validate the JSON session file at `path`.
    //
    // - File not found → returns Default() + kFileReadError.
    // - Unreadable file → returns Default() + kFileReadError.
    // - Malformed JSON  → returns Default() + kParseError.
    // - Valid JSON that fails schema/version checks → returns Default() +
    //   kSchemaError or kVersionMismatch.
    // - Valid session    → returns the deserialized state + kNone.
    [[nodiscard]] static LoadResult Load(const std::filesystem::path& path);

    // Serialise `state` to the JSON session file at `path`.  The file is
    // written atomically (write‑to‑temp + rename) so a crash during save
    // never produces a truncated session file.
    //
    // Returns kNone on success or the specific I/O error otherwise.
    [[nodiscard]] static SessionError Save(const std::filesystem::path& path,
                                           const SessionState& state);
};

}  // namespace island

#endif  // ISLAND_SESSION_STORE_H_
