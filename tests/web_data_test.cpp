#include "web_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace island {
namespace {

// A unique temporary root, removed when the test ends. No CEF and no environment access.
class TempRoot {
  public:
    TempRoot() {
        std::random_device device;
        std::string suffix;
        for (int i = 0; i < 8; ++i) suffix += "0123456789abcdef"[device() % 16];
        path_ = std::filesystem::temp_directory_path() / ("island-web-data-test-" + suffix);
        std::filesystem::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

constexpr const char* kKeyA = "0123456789abcdef0123456789abcdef";
constexpr const char* kKeyB = "fedcba9876543210fedcba9876543210";
constexpr const char* kKeyC = "00000000000000000000000000000001";

// Creates <root>/Spaces/<key>, with the tombstone marker when |tombstoned|.
void MakeSpaceDir(const std::filesystem::path& root, const std::string& key, bool tombstoned) {
    const std::filesystem::path dir = root / "Spaces" / key;
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "Cookies").put('x');
    if (tombstoned) std::ofstream(dir / ".island-delete").put('\n');
}

TEST(WebDataSweepTest, TombstonedDirectoryRemovedAtStartup) {
    TempRoot root;
    MakeSpaceDir(root.path(), kKeyA, /*tombstoned=*/true);
    MakeSpaceDir(root.path(), kKeyB, /*tombstoned=*/false);

    const WebDataSweepResult result =
        SweepWebData(root.path(), {kKeyB}, /*session_loaded_cleanly=*/true);

    EXPECT_EQ(result.removed, std::vector<std::string>{kKeyA});
    EXPECT_TRUE(result.orphans.empty());
    EXPECT_FALSE(std::filesystem::exists(root.path() / "Spaces" / kKeyA));
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyB));
}

TEST(WebDataSweepTest, TombstonedButStillLiveDirectoryIsKept) {
    // The session still names the space, so the deletion was not saved yet. Removing the
    // directory now would hand the space an empty profile under a live key.
    TempRoot root;
    MakeSpaceDir(root.path(), kKeyA, /*tombstoned=*/true);

    const WebDataSweepResult result =
        SweepWebData(root.path(), {kKeyA}, /*session_loaded_cleanly=*/true);

    EXPECT_TRUE(result.removed.empty());
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyA));
}

TEST(WebDataSweepTest, OrphanKeptAndLogged) {
    TempRoot root;
    MakeSpaceDir(root.path(), kKeyB, /*tombstoned=*/false);
    MakeSpaceDir(root.path(), kKeyC, /*tombstoned=*/false);

    const WebDataSweepResult result =
        SweepWebData(root.path(), {kKeyB}, /*session_loaded_cleanly=*/true);

    EXPECT_TRUE(result.removed.empty());
    EXPECT_EQ(result.orphans, std::vector<std::string>{kKeyC});
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyC));
}

TEST(WebDataSweepTest, NothingSweptAfterSessionLoadError) {
    // Review-relevant: one corrupt session.json must not erase every sign-in.
    TempRoot root;
    MakeSpaceDir(root.path(), kKeyA, /*tombstoned=*/true);
    MakeSpaceDir(root.path(), kKeyC, /*tombstoned=*/false);

    const WebDataSweepResult result =
        SweepWebData(root.path(), {}, /*session_loaded_cleanly=*/false);

    EXPECT_TRUE(result.removed.empty());
    EXPECT_TRUE(result.orphans.empty());
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyA));
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyC));
}

TEST(WebDataSweepTest, DirectoriesWithInvalidNamesAreNeverTouched) {
    TempRoot root;
    MakeSpaceDir(root.path(), "NOT-A-KEY", /*tombstoned=*/true);
    MakeSpaceDir(root.path(), "short", /*tombstoned=*/false);

    const WebDataSweepResult result =
        SweepWebData(root.path(), {}, /*session_loaded_cleanly=*/true);

    EXPECT_TRUE(result.removed.empty());
    EXPECT_TRUE(result.orphans.empty());
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / "NOT-A-KEY"));
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / "short"));
}

TEST(WebDataTest, NewKeysAreValidAndDistinct) {
    std::set<std::string> keys;
    for (int i = 0; i < 1000; ++i) {
        const std::string key = NewWebDataKey();
        EXPECT_TRUE(IsValidWebDataKey(key)) << key;
        keys.insert(key);
    }
    EXPECT_EQ(keys.size(), 1000u);
}

TEST(WebDataTest, RejectsKeyThatIsNotLowercaseHex32) {
    // Review Focus 1: a hand-edited or foreign session must never reach outside <root>/Spaces.
    EXPECT_TRUE(IsValidWebDataKey(kKeyA));
    EXPECT_FALSE(IsValidWebDataKey(""));
    EXPECT_FALSE(IsValidWebDataKey("../x"));
    EXPECT_FALSE(IsValidWebDataKey("0123456789ABCDEF0123456789ABCDEF"));
    EXPECT_FALSE(IsValidWebDataKey("0123456789abcdef0123456789abcde"));    // 31 characters
    EXPECT_FALSE(IsValidWebDataKey("0123456789abcdef0123456789abcdef0"));  // 33 characters
    EXPECT_FALSE(IsValidWebDataKey("0123456789abcdef0123456789abcde/"));
    EXPECT_FALSE(IsValidWebDataKey("0123456789abcdef0123456789abcdeg"));
}

TEST(WebDataTest, SpacePathIsUnderSpacesAndEmptyForBadKeys) {
    const std::filesystem::path root = "/some/root";
    EXPECT_EQ(SpaceWebDataPath(root, kKeyA), root / "Spaces" / kKeyA);
    EXPECT_TRUE(SpaceWebDataPath(root, "../x").empty());
}

TEST(WebDataTest, TombstoneFileIsWritten) {
    TempRoot root;
    ASSERT_TRUE(WriteWebDataTombstone(root.path(), kKeyA));
    EXPECT_TRUE(std::filesystem::exists(root.path() / "Spaces" / kKeyA / ".island-delete"));
    EXPECT_FALSE(WriteWebDataTombstone(root.path(), "../x"));
}

TEST(WebDataTest, PrivateDirectoryIsOwnerOnly) {
    TempRoot root;
    const std::filesystem::path dir = root.path() / "Spaces";
    std::filesystem::create_directories(dir);
    // Start readable by group and others, so the test shows that the call tightens the mode
    // and does not depend on the runner's umask.
    std::filesystem::permissions(
        dir,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace);
#if !defined(_WIN32)
    struct stat before{};
    ASSERT_EQ(stat(dir.c_str(), &before), 0);
    ASSERT_NE(before.st_mode & 077, 0u);
#endif

    EXPECT_TRUE(EnsurePrivateDirectory(dir));

    ASSERT_TRUE(std::filesystem::is_directory(dir));
#if !defined(_WIN32)
    struct stat info{};
    ASSERT_EQ(stat(dir.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 077, 0u);
#endif
}

TEST(WebDataTest, PrivateDirectoryReportsFailure) {
    TempRoot root;
    // A regular file where the parent should be: the directory cannot be created, and the
    // caller must learn that instead of getting a silent success.
    const std::filesystem::path blocker = root.path() / "blocker";
    std::ofstream(blocker) << "not a directory";
    EXPECT_FALSE(EnsurePrivateDirectory(blocker / "Spaces"));
    EXPECT_FALSE(std::filesystem::exists(blocker / "Spaces"));
}

}  // namespace
}  // namespace island
