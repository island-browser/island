#include "data_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>

#include "agent/agent_endpoint.h"
#include "bookmark_store.h"
#include "prefs_store.h"
#include "report_store.h"
#include "session_store.h"
#include "support/scoped_data_home.h"

namespace island {
namespace {

using island::testing::ScopedDataHome;

}  // namespace

TEST(DataPathsTest, StoresKeepTheirCurrentPaths) {
    ScopedDataHome home;
    const std::filesystem::path dir = IslandDataDirectory();

    EXPECT_EQ(SessionStore::DefaultSessionFilePath(), dir / "session.json");
    EXPECT_EQ(PrefsStore::DefaultPrefsFilePath(), dir / "prefs.json");
    EXPECT_EQ(BookmarkStore::DefaultBookmarksFilePath(), dir / "bookmarks.json");
    EXPECT_EQ(ReportStore::DefaultReportsFilePath(), dir / "reports.json");

#if defined(__APPLE__)
    EXPECT_EQ(dir, home.home() / "Library/Application Support/Island");
#elif defined(_WIN32)
    EXPECT_EQ(dir, home.home() / "AppData/Roaming/Island");
#else
    EXPECT_EQ(dir, home.home() / ".local/share/Island");
#endif
}

TEST(DataPathsTest, WebDataRootFollowsDevCacheDir) {
    ScopedDataHome home;

    // ScopedDataHome points ISLAND_DEV_CACHE_DIR at <home>/web.
    EXPECT_EQ(IslandWebDataRoot(), home.home() / "web");

    // With the variable unset, the root sits beside the other stores.
    // ScopedDataHome restores the variable when the test ends.
    ScopedDataHome::SetEnv("ISLAND_DEV_CACHE_DIR", std::nullopt);
    const std::filesystem::path root = IslandWebDataRoot();

#if defined(_WIN32)
    // The Windows root comes from LOCALAPPDATA or USERPROFILE, which ScopedDataHome also sets.
    EXPECT_FALSE(root.empty());
#else
    // macOS and Linux both use <data directory>/Web Data (data_paths.cc).
    EXPECT_EQ(root, IslandDataDirectory() / "Web Data");
#endif
}

TEST(DataPathsTest, AgentEndpointPathMatches) {
    ScopedDataHome home;
    EXPECT_EQ(agent::DefaultDiscoveryFilePath(), IslandDataDirectory() / "agent-endpoint.json");
}

}  // namespace island
