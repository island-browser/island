#ifndef ISLAND_WEB_DATA_H_
#define ISLAND_WEB_DATA_H_

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace island {

// Per-space web data on disk (R-D3, R-D5). Each space's cookies and storage live in
// <root>/Spaces/<web_data_key>. The key is a random 128-bit value (32 lowercase hex digits),
// never the SpaceId, and is never reused. CEF-free: the root is passed in by the caller
// (IslandWebDataRoot() in data_paths.h), so these functions never read the environment.

// A new key from std::random_device. Keys are never reused.
[[nodiscard]] std::string NewWebDataKey();

// True for exactly 32 lowercase hex digits. Anything else (path separators, uppercase, other
// lengths) is rejected, so a hand-edited session cannot point a context outside <root>/Spaces.
[[nodiscard]] bool IsValidWebDataKey(std::string_view key);

// <root>/Spaces/<key>, or an empty path when |key| is not a valid key.
[[nodiscard]] std::filesystem::path SpaceWebDataPath(const std::filesystem::path& root,
                                                     std::string_view key);

// Marks <root>/Spaces/<key> for deletion at the next launch (R-D5) by writing
// .island-delete into it. Returns false when the marker could not be written.
bool WriteWebDataTombstone(const std::filesystem::path& root, std::string_view key);

struct WebDataSweepResult {
    // Tombstoned directories that were removed.
    std::vector<std::string> removed;
    // Directories that are neither live nor tombstoned. They are kept and logged, never
    // deleted. Reported only after a cleanly loaded session.
    std::vector<std::string> orphans;
    // Live directories whose stale .island-delete marker was removed (M1). The directory stays.
    std::vector<std::string> tombstones_cleared;
};

// The startup sweep (R-D5). |live_keys| are the keys of the spaces in the session.
//   - A tombstoned directory whose key is not live is removed.
//   - A tombstoned directory whose key is still live is kept and its stale marker is removed
//     (reported in tombstones_cleared): the session still names the space.
//   - When |session_loaded_cleanly| is false nothing is removed, cleared or reported,
//     because the live set is unknown and a corrupt session must not erase sign-ins.
//   - Directory names that are not valid keys are skipped and never deleted.
WebDataSweepResult SweepWebData(const std::filesystem::path& root,
                                const std::set<std::string>& live_keys,
                                bool session_loaded_cleanly);

// Creates |path| (and its parents) and sets it to owner-only (0700) on POSIX, so the
// permissions do not depend on the umask. The permission part is a no-op on Windows.
// Returns false, and the caller must not write cookies there, when the directory cannot be
// created or is not a directory, or when the mode cannot be set. A silent failure would leave
// a context's cookies readable by other users.
[[nodiscard]] bool EnsurePrivateDirectory(const std::filesystem::path& path);

}  // namespace island

#endif  // ISLAND_WEB_DATA_H_
