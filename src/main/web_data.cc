#include "web_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <random>
#include <string_view>
#include <system_error>

namespace island {
namespace {

constexpr std::size_t kKeyLength = 32;
constexpr std::string_view kSpacesDirName = "Spaces";
constexpr std::string_view kTombstoneName = ".island-delete";

std::filesystem::path SpacesDir(const std::filesystem::path& root) { return root / kSpacesDirName; }

bool IsLowerHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

}  // namespace

std::string NewWebDataKey() {
    static constexpr std::array<char, 16> kHexDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::random_device device;
    std::string key;
    key.reserve(kKeyLength);
    // 32 digits of 4 bits each: 128 random bits in total.
    for (std::size_t i = 0; i < kKeyLength; ++i) {
        key.push_back(kHexDigits[device() % kHexDigits.size()]);
    }
    return key;
}

bool IsValidWebDataKey(std::string_view key) {
    return key.size() == kKeyLength && std::all_of(key.begin(), key.end(), IsLowerHexDigit);
}

std::filesystem::path SpaceWebDataPath(const std::filesystem::path& root, std::string_view key) {
    if (!IsValidWebDataKey(key)) return {};
    return SpacesDir(root) / std::string(key);
}

bool WriteWebDataTombstone(const std::filesystem::path& root, std::string_view key) {
    const std::filesystem::path dir = SpaceWebDataPath(root, key);
    if (dir.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return false;
    std::ofstream marker(dir / kTombstoneName, std::ios::binary | std::ios::trunc);
    marker << '\n';
    marker.flush();
    return marker.good();
}

WebDataSweepResult SweepWebData(const std::filesystem::path& root,
                                const std::set<std::string>& live_keys,
                                bool session_loaded_cleanly) {
    WebDataSweepResult result;
    const std::filesystem::path spaces = SpacesDir(root);
    std::error_code error;
    if (!std::filesystem::is_directory(spaces, error)) return result;

    // Decide first and remove afterwards, so the iteration never changes the directory it reads.
    std::vector<std::string> to_remove;
    std::filesystem::directory_iterator it(spaces, error);
    if (error) return result;
    for (const std::filesystem::directory_iterator end; it != end; it.increment(error)) {
        if (error) break;
        const std::string name = it->path().filename().string();
        // Names that are not keys were not made by Island: skip them, never delete them.
        if (!IsValidWebDataKey(name)) continue;
        const bool tombstoned = std::filesystem::exists(it->path() / kTombstoneName, error);
        const bool live = live_keys.contains(name);
        if (tombstoned) {
            // A live key means the session still names the space, so keep the directory.
            // With an unreadable session the live set is unknown, so nothing is removed.
            if (!live && session_loaded_cleanly) to_remove.push_back(name);
            continue;
        }
        if (!live && session_loaded_cleanly) result.orphans.push_back(name);
    }

    for (const std::string& name : to_remove) {
        std::error_code remove_error;
        std::filesystem::remove_all(spaces / name, remove_error);
        if (!remove_error) result.removed.push_back(name);
    }
    std::sort(result.removed.begin(), result.removed.end());
    std::sort(result.orphans.begin(), result.orphans.end());
    return result;
}

void EnsurePrivateDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
#if !defined(_WIN32)
    // Set the mode explicitly: create_directories() takes it from the umask.
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
#endif
}

}  // namespace island
