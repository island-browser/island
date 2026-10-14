#include "download_path.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace island {
namespace {

class DownloadPathTest : public ::testing::Test {
  protected:
    void SetUp() override {
        static std::atomic<int> counter{0};
        dir_ = std::filesystem::temp_directory_path() /
               ("island_download_path_test_" + std::to_string(counter++) + "_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    void Touch(const std::string& name) { std::ofstream(dir_ / name) << "x"; }

    std::filesystem::path dir_;
};

TEST_F(DownloadPathTest, UsesTheSuggestedNameInsideTheDownloadsDirectory) {
    EXPECT_EQ(ResolveDownloadPath(dir_, "report.pdf"), dir_ / "report.pdf");
}

TEST_F(DownloadPathTest, NumbersTheNameInsteadOfOverwritingAnExistingFile) {
    Touch("report.pdf");
    EXPECT_EQ(ResolveDownloadPath(dir_, "report.pdf"), dir_ / "report (1).pdf");
    Touch("report (1).pdf");
    EXPECT_EQ(ResolveDownloadPath(dir_, "report.pdf"), dir_ / "report (2).pdf");
}

TEST_F(DownloadPathTest, NumbersNamesWithoutAnExtension) {
    Touch("README");
    EXPECT_EQ(ResolveDownloadPath(dir_, "README"), dir_ / "README (1)");
}

TEST_F(DownloadPathTest, FillsTheFirstFreeNumberAfterAGap) {
    Touch("a.txt");
    Touch("a (2).txt");
    EXPECT_EQ(ResolveDownloadPath(dir_, "a.txt"), dir_ / "a (1).txt");
}

TEST_F(DownloadPathTest, StripsDirectoryComponentsSoTheFileStaysInsideTheDirectory) {
    EXPECT_EQ(ResolveDownloadPath(dir_, "../../etc/passwd"), dir_ / "passwd");
    EXPECT_EQ(ResolveDownloadPath(dir_, "/abs/olute.txt"), dir_ / "olute.txt");
    EXPECT_EQ(ResolveDownloadPath(dir_, "..\\..\\win\\evil.exe"), dir_ / "evil.exe");
}

TEST_F(DownloadPathTest, FallsBackToAFixedNameWhenNothingUsableIsSuggested) {
    EXPECT_EQ(SanitizeDownloadFileName(""), "download");
    EXPECT_EQ(SanitizeDownloadFileName("."), "download");
    EXPECT_EQ(SanitizeDownloadFileName(".."), "download");
    EXPECT_EQ(SanitizeDownloadFileName("a/b/"), "download");
    EXPECT_EQ(SanitizeDownloadFileName("  . "), "download");
    EXPECT_EQ(ResolveDownloadPath(dir_, ""), dir_ / "download");
}

TEST_F(DownloadPathTest, DropsControlCharactersAndTrailingDotsAndSpaces) {
    EXPECT_EQ(SanitizeDownloadFileName(std::string("a\x01"
                                                   "b\n.txt")),
              "ab.txt");
    EXPECT_EQ(SanitizeDownloadFileName("file.txt. . "), "file.txt");
    EXPECT_EQ(SanitizeDownloadFileName("  spaced.txt"), "spaced.txt");
}

TEST_F(DownloadPathTest, KeepsNonAsciiNames) {
    EXPECT_EQ(SanitizeDownloadFileName("отчёт.pdf"), "отчёт.pdf");
}

TEST_F(DownloadPathTest, NumbersNonAsciiNamesWithoutChangingTheirBytes) {
    // The path must carry the UTF-8 name unchanged on every platform, including
    // Windows, where a narrow std::string would be read in the ANSI code page.
    const std::filesystem::path first = ResolveDownloadPath(dir_, "отчёт.pdf");
    EXPECT_EQ(first.filename().u8string(), u8"отчёт.pdf");
    std::ofstream(first).put('x');
    const std::filesystem::path second = ResolveDownloadPath(dir_, "отчёт.pdf");
    EXPECT_EQ(second.filename().u8string(), u8"отчёт (1).pdf");
}

TEST(DownloadsDirectoryTest, EndsWithDownloadsWhenTheHomeDirectoryIsKnown) {
    const std::filesystem::path dir = DefaultDownloadsDirectory();
    if (!dir.empty()) {
        EXPECT_EQ(dir.filename(), "Downloads");
    }
}

}  // namespace
}  // namespace island
