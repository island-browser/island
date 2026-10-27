#ifndef ISLAND_TESTS_SUPPORT_SCOPED_DATA_HOME_H_
#define ISLAND_TESTS_SUPPORT_SCOPED_DATA_HOME_H_

#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "data_paths.h"

namespace island::testing {

// Points every Island data location at a fresh temporary directory for the
// lifetime of one test: HOME, XDG_DATA_HOME, APPDATA, LOCALAPPDATA,
// USERPROFILE and ISLAND_DEV_CACHE_DIR (<home>/web). The destructor restores
// each variable exactly (a variable that was unset stays unset) and removes
// the directory.
//
// The environment is process-wide. Gtest runs tests serially by default, so a
// test that uses this must not run in parallel with another test in the same
// process.
class ScopedDataHome {
  public:
    ScopedDataHome() : home_(MakeUniqueHome()) {
        for (const char* name : kVariables) {
            saved_.emplace_back(name, GetEnv(name));
        }
        SetEnv("HOME", home_.string());
        SetEnv("XDG_DATA_HOME", std::nullopt);
        SetEnv("APPDATA", (home_ / "AppData" / "Roaming").string());
        SetEnv("LOCALAPPDATA", (home_ / "AppData" / "Local").string());
        SetEnv("USERPROFILE", home_.string());
        SetEnv("ISLAND_DEV_CACHE_DIR", (home_ / "web").string());
    }

    ~ScopedDataHome() {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
            SetEnv(it->first, it->second);
        }
        std::error_code ec;
        std::filesystem::remove_all(home_, ec);
    }

    ScopedDataHome(const ScopedDataHome&) = delete;
    ScopedDataHome& operator=(const ScopedDataHome&) = delete;

    [[nodiscard]] const std::filesystem::path& home() const { return home_; }

    // The Island data directory under this home (IslandDataDirectory()).
    [[nodiscard]] static std::filesystem::path data_dir() { return IslandDataDirectory(); }

  private:
    static constexpr std::array<const char*, 6> kVariables = {
        "HOME", "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA", "USERPROFILE", "ISLAND_DEV_CACHE_DIR"};

    static std::filesystem::path MakeUniqueHome() {
        std::random_device device;
        std::string suffix;
        for (int i = 0; i < 8; ++i) {
            suffix += "0123456789abcdef"[device() % 16];
        }
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("island-data-home-" + suffix);
        std::filesystem::create_directories(path);
        return path;
    }

    static std::optional<std::string> GetEnv(const char* name) {
        const char* const value = std::getenv(name);
        if (value == nullptr) return std::nullopt;
        return std::string(value);
    }

    static void SetEnv(const char* name, const std::optional<std::string>& value) {
#if defined(_WIN32)
        _putenv_s(name, value ? value->c_str() : "");
#else
        if (value) {
            setenv(name, value->c_str(), 1);
        } else {
            unsetenv(name);
        }
#endif
    }

    static void SetEnv(const char* name, const std::string& value) {
        SetEnv(name, std::optional<std::string>(value));
    }

    std::filesystem::path home_;
    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
};

}  // namespace island::testing

#endif  // ISLAND_TESTS_SUPPORT_SCOPED_DATA_HOME_H_
