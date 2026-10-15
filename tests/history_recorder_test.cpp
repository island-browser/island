#include "history_recorder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

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

}  // namespace
}  // namespace island
