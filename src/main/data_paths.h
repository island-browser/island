#ifndef ISLAND_DATA_PATHS_H_
#define ISLAND_DATA_PATHS_H_

#include <filesystem>

namespace island {

// The directory the session, preferences, bookmarks and reports stores keep
// their files in (R-D1). Platform rule:
//   Windows: %APPDATA%\Island (USERPROFILE\AppData\Roaming when APPDATA is unset)
//   macOS:   $HOME/Library/Application Support/Island
//   Linux:   $XDG_DATA_HOME/Island, or $HOME/.local/share/Island
// When HOME (or the Windows profile) is unset the result is an empty path, so
// `IslandDataDirectory() / "session.json"` stays the relative "session.json".
std::filesystem::path IslandDataDirectory();

// The root of the persistent per-space web data (R-D1, R-D2):
//   ISLAND_DEV_CACHE_DIR when it is set and not empty (developer and helper runs),
//   Windows: %LOCALAPPDATA%\Island\Web Data (caches do not roam with APPDATA),
//   macOS and Linux: IslandDataDirectory() / "Web Data".
std::filesystem::path IslandWebDataRoot();

}  // namespace island

#endif  // ISLAND_DATA_PATHS_H_
