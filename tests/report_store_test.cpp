#include "report_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace island {
namespace {

class ReportStoreTest : public ::testing::Test {
  protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("island_report_store_" + std::to_string(::testing::UnitTest::GetInstance()
                                                           ->random_seed()) +
                "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    ResearchReport Report(std::string title, std::int64_t at) {
        return {.title = std::move(title),
                .question = "q",
                .markdown = "# body\n\"quoted\" \xD0\x96",
                .created_ms = at};
    }

    std::filesystem::path dir_;
};

TEST_F(ReportStoreTest, AddPutsNewestFirstAndAssignsUniqueIds) {
    ReportState state;
    const std::string first = ReportStore::Add(state, Report("one", 1000));
    const std::string second = ReportStore::Add(state, Report("two", 1000));
    EXPECT_NE(first, second);
    EXPECT_TRUE(ReportStore::IsValidId(first));
    ASSERT_EQ(state.reports.size(), 2U);
    EXPECT_EQ(state.reports[0].title, "two");
    EXPECT_EQ(ReportStore::Find(state, first)->title, "one");
    EXPECT_EQ(ReportStore::Find(state, "r999"), nullptr);
}

TEST_F(ReportStoreTest, RoundTripsThroughDiskIncludingNonAscii) {
    ReportState state;
    ReportStore::Add(state, Report("Привет", 5));
    ReportStore::Add(state, Report("two", 6));
    const auto path = dir_ / "nested" / "reports.json";
    ASSERT_EQ(ReportStore::Save(path, state), ReportError::kNone);
    const ReportLoadResult loaded = ReportStore::Load(path);
    ASSERT_EQ(loaded.error, ReportError::kNone);
    EXPECT_EQ(loaded.state, state);
}

TEST_F(ReportStoreTest, MissingCorruptAndWrongSchemaFilesLoadEmpty) {
    EXPECT_EQ(ReportStore::Load(dir_ / "none.json").error, ReportError::kFileReadError);
    std::filesystem::create_directories(dir_);
    const auto path = dir_ / "reports.json";
    const auto write = [&](const std::string& text) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    };
    write("{not json");
    ReportLoadResult result = ReportStore::Load(path);
    EXPECT_EQ(result.error, ReportError::kParseError);
    EXPECT_TRUE(result.state.reports.empty());
    write("{\"version\":99,\"reports\":[]}");
    EXPECT_EQ(ReportStore::Load(path).error, ReportError::kSchemaError);
    write("{\"version\":1,\"reports\":[{\"id\":\"../x\",\"title\":\"t\",\"question\":\"q\","
          "\"markdown\":\"m\",\"created_ms\":1}]}");
    EXPECT_EQ(ReportStore::Load(path).error, ReportError::kSchemaError);
    write("{\"version\":1,\"reports\":[{\"id\":\"r1\"}]}");
    EXPECT_EQ(ReportStore::Load(path).error, ReportError::kSchemaError);
}

TEST_F(ReportStoreTest, CapsTheListAndClipsOversizedBodiesOnAUtf8Boundary) {
    ReportState state;
    for (std::size_t i = 0; i < ReportState::kMaxReports + 5; ++i) {
        ReportStore::Add(state, Report("r" + std::to_string(i), static_cast<std::int64_t>(i)));
    }
    EXPECT_EQ(state.reports.size(), ReportState::kMaxReports);
    EXPECT_EQ(state.reports.front().title, "r" + std::to_string(ReportState::kMaxReports + 4));

    ResearchReport big = Report("big", 1);
    big.markdown.clear();
    while (big.markdown.size() < ReportState::kMaxMarkdownBytes + 10) big.markdown += "\xD0\x96";
    const std::string id = ReportStore::Add(state, std::move(big));
    const std::string& clipped = ReportStore::Find(state, id)->markdown;
    EXPECT_LE(clipped.size(), ReportState::kMaxMarkdownBytes);
    EXPECT_EQ(clipped.size() % 2, 0U);
}

TEST_F(ReportStoreTest, RemoveDropsOnlyThatReportAndIdsAreStrict) {
    ReportState state;
    const std::string a = ReportStore::Add(state, Report("a", 1));
    const std::string b = ReportStore::Add(state, Report("b", 2));
    EXPECT_TRUE(ReportStore::Remove(state, a));
    EXPECT_FALSE(ReportStore::Remove(state, a));
    EXPECT_NE(ReportStore::Find(state, b), nullptr);
    for (const char* bad : {"", "r", "x1", "r1a", "r-1", "r1 ", "r12345678901234567890123"}) {
        EXPECT_FALSE(ReportStore::IsValidId(bad)) << bad;
    }
}


TEST_F(ReportStoreTest, KindRoundTripsAndV1LoadsAsResearch) {
    ReportState state;
    ResearchReport collected = Report("grinders", 2000);
    collected.kind = std::string(kReportKindCollection);
    ReportStore::Add(state, std::move(collected));
    ReportStore::Add(state, Report("essay", 1000));
    const std::filesystem::path path = dir_ / "reports.json";
    ASSERT_EQ(ReportStore::Save(path, state), ReportError::kNone);
    const ReportLoadResult loaded = ReportStore::Load(path);
    ASSERT_EQ(loaded.error, ReportError::kNone);
    ASSERT_EQ(loaded.state.reports.size(), 2U);
    EXPECT_EQ(loaded.state.reports[0].kind, "research");  // newest first: "essay"
    EXPECT_EQ(loaded.state.reports[1].kind, "collection");
    EXPECT_EQ(loaded.state.version, 1);
    EXPECT_EQ(ReportState::kCurrentSchemaVersion, 1);
}

TEST_F(ReportStoreTest, FileStaysVersionOneAndWritesKindOnlyForCollections) {
    ReportState state;
    ResearchReport collected = Report("grinders", 2000);
    collected.kind = std::string(kReportKindCollection);
    ReportStore::Add(state, std::move(collected));
    ReportStore::Add(state, Report("essay", 1000));
    const std::filesystem::path path = dir_ / "reports.json";
    ASSERT_EQ(ReportStore::Save(path, state), ReportError::kNone);
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"version\":1,"), std::string::npos);
    // One "kind" in the file: the collection. A research report looks exactly like version 1.
    EXPECT_EQ(text.find("\"kind\""), text.rfind("\"kind\""));
    EXPECT_NE(text.find("\"kind\":\"collection\""), std::string::npos);
    EXPECT_EQ(text.find("\"kind\":\"research\""), std::string::npos);
}

TEST_F(ReportStoreTest, VersionOneFileWithoutKindLoadsAsResearch) {
    std::filesystem::create_directories(dir_);
    const std::filesystem::path path = dir_ / "reports.json";
    std::ofstream(path) << R"({"version":1,"reports":[{"id":"r1","title":"Old","question":"q",)"
                        << R"("markdown":"# Old","created_ms":5}]})";
    const ReportLoadResult v1 = ReportStore::Load(path);
    ASSERT_EQ(v1.error, ReportError::kNone);
    ASSERT_EQ(v1.state.reports.size(), 1U);
    EXPECT_EQ(v1.state.reports[0].kind, "research");
}

TEST_F(ReportStoreTest, UnknownKindLoadsAsResearch) {
    std::filesystem::create_directories(dir_);
    const std::filesystem::path path = dir_ / "reports.json";
    std::ofstream(path) << R"({"version":1,"reports":[{"id":"r1","title":"T","question":"q",)"
                        << R"("markdown":"# T","created_ms":5,"kind":"plan"}]})";
    const ReportLoadResult loaded = ReportStore::Load(path);
    ASSERT_EQ(loaded.error, ReportError::kNone);
    ASSERT_EQ(loaded.state.reports.size(), 1U);
    EXPECT_EQ(loaded.state.reports[0].kind, "research");
}

}  // namespace
}  // namespace island
