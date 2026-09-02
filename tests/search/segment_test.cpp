// W4 acceptance: round-trip fidelity plus the corruption ladder. Every
// corruption case must quarantine the file to ".corrupt-*" and report an
// error, never crash and never return a partially-readable segment.

#include "search/store/segment.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "search/crc32c.h"
#include "search/memtable.h"
#include "search/store/segment_header.h"
#include "search/store/segment_writer.h"
#include "search/types.h"

namespace island {
namespace search {
namespace {

// A unique directory per test, removed on destruction.
class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("island_segment_test_" + std::to_string(++counter) + "_" +
                 std::to_string(static_cast<long long>(
                     std::chrono::steady_clock::now().time_since_epoch().count())));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] std::filesystem::path segment() const { return path_ / "index.seg"; }
    [[nodiscard]] const std::filesystem::path& dir() const { return path_; }

  private:
    std::filesystem::path path_;
};

StoredDocument MakeDoc(std::string url, std::string title, std::uint64_t visited_ms) {
    StoredDocument doc;
    doc.url = std::move(url);
    doc.title = std::move(title);
    doc.visited_at_ms = visited_ms;
    return doc;
}

MemTable BuildTable() {
    MemTable table;
    std::vector<std::string> a_title{"island", "island", "browser"};
    std::vector<std::string> a_url{"island"};
    table.AddDocument(MakeDoc("https://island.test/", "Island Island Browser", 1000), a_title,
                      a_url);

    std::vector<std::string> b_title{"browser", "notes"};
    std::vector<std::string> b_url{"notes"};
    table.AddDocument(MakeDoc("https://notes.test/x", "Browser Notes", 2000), b_title, b_url);

    std::vector<std::string> c_title{"zebra"};
    table.AddDocument(MakeDoc("https://zebra.test/", "Zebra", 3000), c_title, {});
    return table;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// True when exactly one ".corrupt-*" sibling exists and the original is gone.
bool QuarantinedExactlyOnce(const std::filesystem::path& dir,
                            const std::filesystem::path& original) {
    int quarantined = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.find(".corrupt-") != std::string::npos) {
            ++quarantined;
        }
    }
    return quarantined == 1 && !std::filesystem::exists(original);
}

// Writes a valid segment, applies `mutate` to its bytes, and expects Open to
// reject and quarantine it.
template <typename Mutate>
void ExpectRejectedAndQuarantined(Mutate mutate) {
    TempDir tmp;
    const MemTable table = BuildTable();
    ASSERT_TRUE(WriteSegment(table, tmp.segment()).has_value());

    std::vector<std::uint8_t> bytes = ReadFile(tmp.segment());
    mutate(bytes);
    WriteFile(tmp.segment(), bytes);

    auto opened = Segment::Open(tmp.segment());
    EXPECT_FALSE(opened.has_value());
    EXPECT_TRUE(QuarantinedExactlyOnce(tmp.dir(), tmp.segment()));
}

TEST(Segment, RoundTripsDocumentsTermsAndCorpusStatistics) {
    TempDir tmp;
    const MemTable table = BuildTable();
    ASSERT_TRUE(WriteSegment(table, tmp.segment()).has_value());

    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());
    const Segment& segment = *opened.value();

    EXPECT_EQ(segment.doc_count(), 3u);
    EXPECT_EQ(segment.next_doc_id(), table.next_doc_id());
    EXPECT_EQ(segment.first_doc_id(), 1u);
    EXPECT_NEAR(segment.avg_doc_len(), table.avg_doc_length(), 1e-4);

    const auto first = segment.DocumentAt(1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->document.url, "https://island.test/");
    EXPECT_EQ(first->document.title, "Island Island Browser");
    EXPECT_EQ(first->document.visited_at_ms, 1000u);
    EXPECT_EQ(first->title_token_count, 3u);
    EXPECT_EQ(first->url_token_count, 1u);

    const auto third = segment.DocumentAt(3);
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->document.title, "Zebra");
}

TEST(Segment, PostingListsMatchTheMemTableAndStayAscending) {
    TempDir tmp;
    const MemTable table = BuildTable();
    ASSERT_TRUE(WriteSegment(table, tmp.segment()).has_value());
    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());

    const auto browser = opened.value()->PostingsFor("browser");
    ASSERT_TRUE(browser.has_value());
    EXPECT_EQ(*browser, (std::vector<std::uint64_t>{1, 2}));

    const auto island = opened.value()->PostingsFor("island");
    ASSERT_TRUE(island.has_value());
    EXPECT_EQ(*island, (std::vector<std::uint64_t>{1}));

    EXPECT_FALSE(opened.value()->PostingsFor("absent").has_value());
}

TEST(Segment, TermsAreByteLexicographic) {
    TempDir tmp;
    ASSERT_TRUE(WriteSegment(BuildTable(), tmp.segment()).has_value());
    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());

    const std::vector<std::string_view> terms = opened.value()->Terms();
    ASSERT_FALSE(terms.empty());
    for (std::size_t i = 1; i < terms.size(); ++i) {
        EXPECT_LT(terms[i - 1], terms[i]);
    }
}

TEST(Segment, OpeningAMissingFileFailsWithoutQuarantining) {
    TempDir tmp;
    auto opened = Segment::Open(tmp.segment());
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().kind, SearchErrorKind::kIoError);
    int entries = 0;
    for (const auto& unused : std::filesystem::directory_iterator(tmp.dir())) {
        (void)unused;
        ++entries;
    }
    EXPECT_EQ(entries, 0);
}

TEST(Segment, WriteIsAtomicAndLeavesNoTemporaryBehind) {
    TempDir tmp;
    ASSERT_TRUE(WriteSegment(BuildTable(), tmp.segment()).has_value());
    std::filesystem::path tmp_path = tmp.segment();
    tmp_path += ".tmp";
    EXPECT_FALSE(std::filesystem::exists(tmp_path));
    EXPECT_TRUE(std::filesystem::exists(tmp.segment()));
}

TEST(Segment, RewritingReplacesThePreviousSegment) {
    TempDir tmp;
    ASSERT_TRUE(WriteSegment(BuildTable(), tmp.segment()).has_value());

    MemTable bigger = BuildTable();
    std::vector<std::string> tokens{"extra"};
    bigger.AddDocument(MakeDoc("https://extra.test/", "Extra", 4000), tokens, {});
    ASSERT_TRUE(WriteSegment(bigger, tmp.segment()).has_value());

    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value()->doc_count(), 4u);
}

TEST(Segment, FlippedMagicQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) { bytes[0] ^= 0xFF; });
}

TEST(Segment, UnknownVersionQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) {
        // format_version sits at offset 4; 0x00FF is not a version this build knows.
        bytes[4] = 0xFF;
        bytes[5] = 0x00;
    });
}

TEST(Segment, TruncatedFileQuarantines) {
    ExpectRejectedAndQuarantined(
        [](std::vector<std::uint8_t>& bytes) { bytes.resize(bytes.size() / 2); });
}

TEST(Segment, TruncationBelowTheHeaderQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) { bytes.resize(16); });
}

TEST(Segment, BitFlippedBodyQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) {
        // Land inside the body, past the header and before the trailer.
        bytes[kSegmentHeaderBytes + 1] ^= 0x01;
    });
}

TEST(Segment, OverlappingSectionOffsetsQuarantine) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) {
        // Slide posting_off (header offset 60) back onto term_dict_off (header
        // offset 44) so those two sections overlap, and repair the header CRC.
        //
        // Only the overlap is wrong: the term dictionary bytes are untouched
        // and still parse, and every posting range still lies inside the
        // unchanged posting_len. So the overlap check is the only thing that
        // can reject this file -- remove it and Open succeeds.
        std::uint64_t term_dict_off = 0;
        for (int i = 7; i >= 0; --i) {
            term_dict_off = (term_dict_off << 8) | bytes[44 + i];
        }
        for (int i = 0; i < 8; ++i) {
            bytes[60 + i] = static_cast<std::uint8_t>(term_dict_off >> (8 * i));
        }
        const std::uint32_t crc = Crc32c(bytes.data(), kSegmentHeaderBytes - 4);
        for (int i = 0; i < 4; ++i) {
            bytes[kSegmentHeaderBytes - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
        }
    });
}

TEST(Segment, SectionRunningPastTheFileQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) {
        // The posting region is the last section, so growing posting_len
        // (header offset 68) past the end of the file overruns the mapping
        // without overlapping any earlier section. That isolates the
        // section-bounds check: nothing else can reject this file.
        std::uint64_t posting_len = 0;
        for (int i = 7; i >= 0; --i) {
            posting_len = (posting_len << 8) | bytes[68 + i];
        }
        posting_len += 64;
        for (int i = 0; i < 8; ++i) {
            bytes[68 + i] = static_cast<std::uint8_t>(posting_len >> (8 * i));
        }
        const std::uint32_t crc = Crc32c(bytes.data(), kSegmentHeaderBytes - 4);
        for (int i = 0; i < 4; ++i) {
            bytes[kSegmentHeaderBytes - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
        }
    });
}

TEST(Segment, CorruptedTrailerMagicQuarantines) {
    ExpectRejectedAndQuarantined(
        [](std::vector<std::uint8_t>& bytes) { bytes[bytes.size() - 1] ^= 0xFF; });
}

TEST(Segment, HeaderChecksumCorruptionQuarantines) {
    ExpectRejectedAndQuarantined([](std::vector<std::uint8_t>& bytes) {
        // Flip a byte of avg_doc_len_q16 (header offset 76) and leave the
        // header CRC stale. That field is a pure statistic: no bounds, range,
        // or consistency check reads it, so the header checksum is the only
        // thing that can reject this file.
        bytes[76] ^= 0xFF;
    });
}

TEST(Segment, AnEmptyMemTableRoundTrips) {
    TempDir tmp;
    const MemTable empty;
    ASSERT_TRUE(WriteSegment(empty, tmp.segment()).has_value());

    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value()->doc_count(), 0u);
    EXPECT_EQ(opened.value()->term_count(), 0u);
    EXPECT_FALSE(opened.value()->DocumentAt(1).has_value());
}

TEST(Segment, ResumedDocIdsSurviveTheRoundTrip) {
    TempDir tmp;
    MemTable table(/*first_doc_id=*/42);
    std::vector<std::string> tokens{"resumed"};
    table.AddDocument(MakeDoc("https://resume.test/", "Resumed", 10), tokens, {});
    ASSERT_TRUE(WriteSegment(table, tmp.segment()).has_value());

    auto opened = Segment::Open(tmp.segment());
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value()->first_doc_id(), 42u);
    EXPECT_EQ(opened.value()->next_doc_id(), 43u);
    ASSERT_TRUE(opened.value()->DocumentAt(42).has_value());
    EXPECT_FALSE(opened.value()->DocumentAt(41).has_value());
    EXPECT_EQ(*opened.value()->PostingsFor("resumed"), (std::vector<std::uint64_t>{42}));
}

}  // namespace
}  // namespace search
}  // namespace island
