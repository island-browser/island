#include "history_recorder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "start_tiles.h"

namespace island {
namespace {

std::filesystem::path HistoryPath(std::string_view name) {
    static std::atomic<std::uint64_t> sequence{0};
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("island_history_" + std::string(name) + "_" + std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(directory);
    return directory / "history" / "index.seg";
}

HistoryRecorder::Options TestOptions(const std::filesystem::path& path,
                                     std::size_t max_entries = 50'000) {
    return {.segment_path = path,
            .max_entries = max_entries,
            .flush_every_ingests = 1,
            .flush_interval = std::chrono::hours(1)};
}

TEST(HistoryRecorderTest, GivenNavigationUrlsWhenRecordedThenOnlyPrivateSafeWebPagesEnterHistory) {
    HistoryRecorder recorder(TestOptions(HistoryPath("privacy")));

    EXPECT_TRUE(recorder.RecordNavigation("https://example.test/path#fragment", "Example", 100));
    EXPECT_TRUE(recorder.RecordNavigation("HTTP://example.test/other", "Other", 101));
    EXPECT_FALSE(recorder.RecordNavigation("data:text/html,secret", "Secret", 102));
    EXPECT_FALSE(recorder.RecordNavigation("file:///tmp/a", "File", 103));
    EXPECT_FALSE(recorder.RecordNavigation("about:blank", "Blank", 104));
    EXPECT_FALSE(recorder.RecordNavigation("https://user:pass@example.test/", "Private", 105));
    EXPECT_FALSE(recorder.RecordNavigation("https://private.test/", "Private mode", 106, true));

    recorder.WaitForIdleForTest();
    EXPECT_EQ(recorder.EntryCountForTest(), 2U);
    const std::vector<HistoryResult> results = recorder.QueryForTest("example", 200);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].url.find('#'), std::string::npos);
    EXPECT_EQ(results[1].url.find('#'), std::string::npos);
}

TEST(HistoryRecorderTest, GivenAPartialLastWordThenItMatchesAsAPrefixAndSyntaxIsInert) {
    HistoryRecorder recorder(TestOptions(HistoryPath("prefix")));
    ASSERT_TRUE(recorder.RecordNavigation("https://quokka.test/", "Quokka Habitat Guide", 100));

    EXPECT_EQ(recorder.QueryForTest("quok", 200).size(), 1U);
    EXPECT_EQ(recorder.QueryForTest("habitat qu", 200).size(), 1U);
    EXPECT_EQ(recorder.QueryForTest("\"quok", 200).size(), 1U);
    EXPECT_TRUE(recorder.QueryForTest("\"*-", 200).empty());
    EXPECT_TRUE(recorder.QueryForTest("zeppelin", 200).empty());
}

TEST(HistoryRecorderTest, GivenRepeatedVisitsAndALateTitleThenResultsDeduplicateAndCountVisits) {
    HistoryRecorder recorder(TestOptions(HistoryPath("dedupe")));
    ASSERT_TRUE(recorder.RecordNavigation("https://repeat.test/docs#one", "Old title", 100));
    ASSERT_TRUE(recorder.UpdateTitle("https://repeat.test/docs#two", "New Docs title"));
    ASSERT_TRUE(recorder.RecordNavigation("https://other.test/docs", "Other Docs", 150));
    ASSERT_TRUE(recorder.RecordNavigation("https://repeat.test/docs", "New Docs title", 200));

    const std::vector<HistoryResult> results = recorder.QueryForTest("docs", 300);

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].url, "https://repeat.test/docs");
    EXPECT_EQ(results[0].title, "New Docs title");
    EXPECT_EQ(results[0].visited_at_ms, 200U);
    EXPECT_EQ(results[0].visit_count, 2U);
    EXPECT_EQ(results[1].visit_count, 1U);
}

TEST(HistoryRecorderTest, GivenMoreThanTheCapWhenRecordedThenTheOldestVisitsAreDropped) {
    HistoryRecorder recorder(TestOptions(HistoryPath("cap"), 3));
    const std::vector<std::string> names{"alpha", "bravo", "charlie", "delta", "echo"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        ASSERT_TRUE(recorder.RecordNavigation("https://" + names[index] + ".test/", names[index],
                                              (index + 1) * 100));
    }

    recorder.WaitForIdleForTest();
    EXPECT_EQ(recorder.EntryCountForTest(), 3U);
    EXPECT_TRUE(recorder.QueryForTest("alpha", 1'000).empty());
    EXPECT_TRUE(recorder.QueryForTest("bravo", 1'000).empty());
    EXPECT_EQ(recorder.QueryForTest("echo", 1'000).size(), 1U);
}

TEST(HistoryRecorderTest, GivenAFlushedSegmentWhenReopenedThenItRebuildsAndCanFlushAgain) {
    const std::filesystem::path path = HistoryPath("reopen");
    {
        HistoryRecorder recorder(TestOptions(path));
        ASSERT_TRUE(recorder.RecordNavigation("https://first.test/docs", "First Docs", 100));
        ASSERT_TRUE(
            recorder.UpdateTitle("https://first.test/docs#section", "First Docs (renamed)"));
        recorder.WaitForIdleForTest();
    }

    {
        HistoryRecorder recorder(TestOptions(path));
        EXPECT_EQ(recorder.EntryCountForTest(), 1U);
        const std::vector<HistoryResult> rebuilt = recorder.QueryForTest("renamed", 150);
        ASSERT_EQ(rebuilt.size(), 1U);
        EXPECT_EQ(rebuilt[0].title, "First Docs (renamed)");
        ASSERT_TRUE(recorder.RecordNavigation("https://second.test/docs", "Second Docs", 200));
        recorder.WaitForIdleForTest();
        EXPECT_EQ(recorder.QueryForTest("docs", 300).size(), 2U);
    }

    HistoryRecorder reopened(TestOptions(path));
    EXPECT_EQ(reopened.EntryCountForTest(), 2U);
    EXPECT_EQ(reopened.QueryForTest("docs", 300).size(), 2U);
}

TEST(HistoryRecorderTest, GivenStoredHistoryWhenClearedThenMemoryAndSegmentAreReset) {
    const std::filesystem::path path = HistoryPath("clear");
    HistoryRecorder recorder(TestOptions(path));
    ASSERT_TRUE(recorder.RecordNavigation("https://example.test/docs", "Docs", 100));
    recorder.WaitForIdleForTest();
    ASSERT_TRUE(std::filesystem::exists(path));
    std::filesystem::path quarantined = path;
    quarantined += ".corrupt-123";
    std::filesystem::copy_file(path, quarantined);

    bool callback_result = false;
    recorder.ClearAsync([&callback_result](bool cleared) { callback_result = cleared; });
    recorder.WaitForIdleForTest();

    EXPECT_TRUE(callback_result);
    EXPECT_EQ(recorder.EntryCountForTest(), 0U);
    EXPECT_TRUE(recorder.QueryForTest("docs", 200).empty());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(quarantined));
}

TEST(HistoryRecorderTest, GivenRepeatedVisitsWhenMostVisitedThenTheFrequentHostsComeFirst) {
    HistoryRecorder recorder(TestOptions(HistoryPath("most_visited")));
    ASSERT_TRUE(recorder.RecordNavigation("https://quiet.test/", "Quiet", 500));
    ASSERT_TRUE(recorder.RecordNavigation("https://busy.test/a", "Busy A", 100));
    ASSERT_TRUE(recorder.RecordNavigation("https://busy.test/b", "Busy B", 200));
    ASSERT_TRUE(recorder.RecordNavigation("https://busy.test/a", "Busy A again", 300));
    ASSERT_TRUE(recorder.RecordNavigation("https://mid.test/", "Mid", 400));
    ASSERT_TRUE(recorder.RecordNavigation("https://mid.test/", "Mid", 450));

    const std::vector<HistoryResult> all = recorder.MostVisitedForTest(10);
    // Visits are counted per host: busy.test has three visits and is represented by its most
    // recent page. Visit count first, then the latest visit; one visit each for quiet.test.
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].url, "https://busy.test/a");
    EXPECT_EQ(all[0].title, "Busy A again");
    EXPECT_EQ(all[0].visit_count, 3U);
    EXPECT_EQ(all[1].url, "https://mid.test/");
    EXPECT_EQ(all[1].visit_count, 2U);
    EXPECT_EQ(all[2].url, "https://quiet.test/");

    const std::vector<HistoryResult> top_two = recorder.MostVisitedForTest(2);
    ASSERT_EQ(top_two.size(), 2U);
    EXPECT_EQ(top_two[1].url, "https://mid.test/");
}

TEST(HistoryRecorderTest, GivenManyPagesOfOneHostThenTheTileLimitStillReachesOtherSites) {
    HistoryRecorder recorder(TestOptions(HistoryPath("host_limit")));
    std::uint64_t when = 1;
    // Sixty pages of one host, each visited twice: at page level they would fill any top list.
    for (int page = 0; page < 60; ++page) {
        const std::string url = "https://docs.test/page" + std::to_string(page);
        ASSERT_TRUE(recorder.RecordNavigation(url, "Docs", when++));
        ASSERT_TRUE(recorder.RecordNavigation(url, "Docs", when++));
    }
    for (int site = 0; site < 6; ++site) {
        ASSERT_TRUE(recorder.RecordNavigation("https://site" + std::to_string(site) + ".test/",
                                              "Site", when++));
    }

    const std::vector<StartTile> tiles = BuildStartTiles(recorder.MostVisitedForTest(6));

    ASSERT_EQ(tiles.size(), 6U);
    EXPECT_EQ(tiles[0].host, "docs.test");
    EXPECT_EQ(tiles[1].host, "site5.test");
    EXPECT_EQ(tiles[5].host, "site1.test");
}

}  // namespace
}  // namespace island
