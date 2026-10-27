#include "data_paths.h"

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace island {
namespace {

// Returns the value of |name| when it is set and not empty.
std::optional<std::filesystem::path> NonEmptyEnv(const char* name) {
    const char* const value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

}  // namespace

std::filesystem::path IslandDataDirectory() {
#if defined(_WIN32)
    if (std::optional<std::filesystem::path> app_data = NonEmptyEnv("APPDATA")) {
        return *app_data / "Island";
    }
    std::optional<std::filesystem::path> profile = NonEmptyEnv("USERPROFILE");
    if (!profile) return {};
    return *profile / "AppData" / "Roaming" / "Island";
#elif defined(__APPLE__)
    std::optional<std::filesystem::path> home = NonEmptyEnv("HOME");
    if (!home) return {};
    return *home / "Library" / "Application Support" / "Island";
#else
    if (std::optional<std::filesystem::path> xdg = NonEmptyEnv("XDG_DATA_HOME")) {
        return *xdg / "Island";
    }
    std::optional<std::filesystem::path> home = NonEmptyEnv("HOME");
    if (!home) return {};
    return *home / ".local" / "share" / "Island";
#endif
}

std::filesystem::path IslandWebDataRoot() {
    if (std::optional<std::filesystem::path> dev_cache = NonEmptyEnv("ISLAND_DEV_CACHE_DIR")) {
        return *dev_cache;
    }
#if defined(_WIN32)
    if (std::optional<std::filesystem::path> local_app_data = NonEmptyEnv("LOCALAPPDATA")) {
        return *local_app_data / "Island" / "Web Data";
    }
    std::optional<std::filesystem::path> profile = NonEmptyEnv("USERPROFILE");
    if (!profile) return std::filesystem::path("Web Data");
    return *profile / "AppData" / "Local" / "Island" / "Web Data";
#else
    return IslandDataDirectory() / "Web Data";
#endif
}

}  // namespace island
