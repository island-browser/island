#include "start_tiles.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "history_recorder.h"

namespace island {
namespace {

HistoryResult Visited(std::string url, std::string title, std::size_t visits = 1) {
    return {.url = std::move(url), .title = std::move(title), .visit_count = visits};
}

TEST(StartTilesTest, GivenWebUrlsWhenBuiltThenTheOrderOfTheHistoryIsKept) {
    const std::vector<StartTile> tiles = BuildStartTiles({
        Visited("https://one.test/a", "One"),
        Visited("http://two.test/", "Two"),
        Visited("https://three.test:8443/x?y=1#z", "Three"),
    });

    ASSERT_EQ(tiles.size(), 3U);
    EXPECT_EQ(tiles[0],
              (StartTile{.title = "One", .url = "https://one.test/a", .host = "one.test"}));
    EXPECT_EQ(tiles[1].host, "two.test");
    EXPECT_EQ(tiles[2].host, "three.test");
}

TEST(StartTilesTest, GivenSeveralPagesOfOneHostThenTheFirstRankedOneWins) {
    const std::vector<StartTile> tiles = BuildStartTiles({
        Visited("https://Docs.Test/first", "Most visited"),
        Visited("https://docs.test/second", "Second"),
        Visited("https://other.test/", "Other"),
    });

    ASSERT_EQ(tiles.size(), 2U);
    EXPECT_EQ(tiles[0].url, "https://Docs.Test/first");
    EXPECT_EQ(tiles[0].title, "Most visited");
    EXPECT_EQ(tiles[1].host, "other.test");
}

TEST(StartTilesTest, GivenMoreThanSixSitesThenOnlySixAreKept) {
    std::vector<HistoryResult> history;
    for (int index = 0; index < 9; ++index) {
        history.push_back(Visited("https://site" + std::to_string(index) + ".test/", "S"));
    }

    const std::vector<StartTile> tiles = BuildStartTiles(history);

    ASSERT_EQ(tiles.size(), 6U);
    EXPECT_EQ(tiles.back().host, "site5.test");
}

TEST(StartTilesTest, GivenAnEmptyTitleThenTheHostIsTheLabel) {
    const std::vector<StartTile> tiles = BuildStartTiles(
        {Visited("https://untitled.test/page", ""), Visited("https://spaces.test/", "   ")});

    ASSERT_EQ(tiles.size(), 2U);
    EXPECT_EQ(tiles[0].title, "untitled.test");
    EXPECT_EQ(tiles[1].title, "spaces.test");
}

TEST(StartTilesTest, GivenNonWebOrCredentialedUrlsThenNoTileIsBuilt) {
    const std::vector<StartTile> tiles = BuildStartTiles({
        Visited("javascript:alert(1)", "Script"),
        Visited("data:text/html,hi", "Data"),
        Visited("file:///etc/hosts", "File"),
        Visited("chrome://settings", "Chrome"),
        Visited("https://user:pass@secret.test/", "Credentials"),
        Visited("https:///no-host", "Empty host"),
        Visited("", "Nothing"),
        Visited("https://kept.test/", "Kept"),
    });

    ASSERT_EQ(tiles.size(), 1U);
    EXPECT_EQ(tiles[0].host, "kept.test");
}

TEST(StartTilesTest, OnlyHttpAndHttpsUrlsMayBeOpenedFromATile) {
    EXPECT_TRUE(IsWebTileUrl("https://a.test/"));
    EXPECT_TRUE(IsWebTileUrl("HTTP://a.test"));
    EXPECT_FALSE(IsWebTileUrl("javascript:alert(1)"));
    EXPECT_FALSE(IsWebTileUrl("data:text/html,x"));
    EXPECT_FALSE(IsWebTileUrl("file:///tmp/x"));
    EXPECT_FALSE(IsWebTileUrl("about:blank"));
    EXPECT_FALSE(IsWebTileUrl(""));
}

}  // namespace
}  // namespace island
