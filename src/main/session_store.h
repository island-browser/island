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

    bool operator==(const SpaceState&) const = default;
};

struct SessionState {
    static constexpr int kCurrentSchemaVersion = 1;

    int version = kCurrentSchemaVersion;
    std::optional<SpaceId> active_space_id;
    std::vector<SpaceState> spaces;

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
