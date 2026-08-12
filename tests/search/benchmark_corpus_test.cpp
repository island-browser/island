#include "search/benchmark_corpus.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace island {
namespace search {
namespace bench {
namespace {

using island::search::bench::CorpusConfig;
using island::search::bench::CorpusGenerator;
using island::search::bench::CorpusRecord;
using island::search::bench::ValidateConfig;

CorpusConfig FullConfig(std::uint64_t seed = 42) {
    CorpusConfig config;
    config.seed = seed;
    config.document_count = 1000;
    config.vocabulary_size = 512;
    config.min_title_tokens = 3;
    config.max_title_tokens = 8;
    config.min_url_path_tokens = 2;
    config.max_url_path_tokens = 5;
    return config;
}

TEST(Corpus, SameSeedProducesIdenticalRecords) {
    CorpusGenerator a(FullConfig(7));
    CorpusGenerator b(FullConfig(7));
    const std::vector<CorpusRecord> from_a = a.GenerateAll();
    const std::vector<CorpusRecord> from_b = b.GenerateAll();
    ASSERT_EQ(from_a.size(), from_b.size());
    for (std::size_t i = 0; i < from_a.size(); ++i) {
        EXPECT_EQ(from_a[i].doc_id, from_b[i].doc_id);
        EXPECT_EQ(from_a[i].url, from_b[i].url);
        EXPECT_EQ(from_a[i].title, from_b[i].title);
        EXPECT_EQ(from_a[i].timestamp, from_b[i].timestamp);
    }
}

TEST(Corpus, DifferentSeedsProduceDifferentCorpora) {
    const std::vector<CorpusRecord> a = CorpusGenerator(FullConfig(1)).GenerateAll();
    const std::vector<CorpusRecord> b = CorpusGenerator(FullConfig(2)).GenerateAll();
    ASSERT_EQ(a.size(), b.size());
    bool any_different = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].url != b[i].url || a[i].title != b[i].title) {
            any_different = true;
            break;
        }
    }
    EXPECT_TRUE(any_different);
}

TEST(Corpus, ValidateConfigRejectsBadConfigs) {
    EXPECT_FALSE(ValidateConfig(CorpusConfig{}));
    CorpusConfig zero_docs = FullConfig();
    zero_docs.document_count = 0;
    EXPECT_FALSE(ValidateConfig(zero_docs));
    CorpusConfig zero_vocab = FullConfig();
    zero_vocab.vocabulary_size = 0;
    EXPECT_FALSE(ValidateConfig(zero_vocab));
    CorpusConfig min_zero = FullConfig();
    min_zero.min_title_tokens = 0;
    EXPECT_FALSE(ValidateConfig(min_zero));
    CorpusConfig min_gt_max = FullConfig();
    min_gt_max.min_title_tokens = 9;
    min_gt_max.max_title_tokens = 4;
    EXPECT_FALSE(ValidateConfig(min_gt_max));
    CorpusConfig max_gt_vocab = FullConfig();
    max_gt_vocab.max_title_tokens = 600;
    EXPECT_FALSE(ValidateConfig(max_gt_vocab));
    CorpusConfig path_min_gt_max = FullConfig();
    path_min_gt_max.min_url_path_tokens = 3;
    path_min_gt_max.max_url_path_tokens = 1;
    EXPECT_FALSE(ValidateConfig(path_min_gt_max));
}

TEST(Corpus, InvalidConfigThrowsInvalidArgument) {
    CorpusConfig config = FullConfig();
    config.document_count = 0;
    EXPECT_THROW(CorpusGenerator invalid_generator(config), std::invalid_argument);
}

TEST(Corpus, DocIdsAreMonotoneAndStartAtZero) {
    CorpusGenerator gen(FullConfig(3));
    CorpusRecord record;
    std::uint64_t previous = 0;
    bool first = true;
    std::size_t produced = 0;
    while (gen.Next(&record)) {
        if (first) {
            EXPECT_EQ(record.doc_id, 0u);
            first = false;
        } else {
            EXPECT_GT(record.doc_id, previous);
        }
        previous = record.doc_id;
        ++produced;
    }
    EXPECT_EQ(produced, 1000u);
}

TEST(Corpus, TimestampsAreStrictlyMonotone) {
    CorpusGenerator gen(FullConfig(5));
    CorpusRecord record;
    std::uint64_t previous = 0;
    bool first = true;
    while (gen.Next(&record)) {
        if (first) {
            first = false;
        } else {
            EXPECT_GT(record.timestamp, previous);
        }
        previous = record.timestamp;
    }
}

TEST(Corpus, TimestampsAreDeterministicAcrossRuns) {
    std::vector<std::uint64_t> first;
    {
        CorpusGenerator gen(FullConfig(9));
        CorpusRecord record;
        while (gen.Next(&record)) {
            first.push_back(record.timestamp);
        }
    }
    std::vector<std::uint64_t> second;
    {
        CorpusGenerator gen(FullConfig(9));
        CorpusRecord record;
        while (gen.Next(&record)) {
            second.push_back(record.timestamp);
        }
    }
    EXPECT_EQ(first, second);
}

TEST(Corpus, UrlIsAbsoluteHttpsExampleDomain) {
    CorpusGenerator gen(FullConfig(11));
    CorpusRecord record;
    while (gen.Next(&record)) {
        EXPECT_EQ(record.url.compare(0, 8, "https://"), 0);
        EXPECT_EQ(record.url.find("example-"), 8u);
    }
}

TEST(Corpus, UrlContainsNoCredentialsOrControlCharacters) {
    CorpusGenerator gen(FullConfig(13));
    CorpusRecord record;
    while (gen.Next(&record)) {
        EXPECT_EQ(record.url.find('@'), std::string::npos);
        EXPECT_EQ(record.url.find(':'), record.url.find("://"));
        for (const char c : record.url) {
            EXPECT_GE(static_cast<unsigned char>(c), 0x20u);
        }
    }
}

TEST(Corpus, TitleIsNonEmptyLowercaseAscii) {
    CorpusGenerator gen(FullConfig(17));
    CorpusRecord record;
    while (gen.Next(&record)) {
        EXPECT_FALSE(record.title.empty());
        for (const char c : record.title) {
            const bool lower_alpha = c >= 'a' && c <= 'z';
            const bool space = c == ' ';
            const bool digit = c >= '0' && c <= '9';
            EXPECT_TRUE(lower_alpha || space || digit) << "unexpected char: " << c;
        }
    }
}

TEST(Corpus, TitleAndPathTokenCountsStayWithinBounds) {
    CorpusConfig config = FullConfig(19);
    config.vocabulary_size = 64;
    config.min_title_tokens = 1;
    config.max_title_tokens = 4;
    config.min_url_path_tokens = 1;
    config.max_url_path_tokens = 3;
    CorpusGenerator gen(config);
    CorpusRecord record;
    while (gen.Next(&record)) {
        std::size_t title_tokens = 0;
        for (const char c : record.title) {
            if (c == ' ') {
                ++title_tokens;
            }
        }
        std::size_t path_tokens = 0;
        for (const char c : record.url) {
            if (c == '/') {
                ++path_tokens;
            }
        }
        EXPECT_GE(title_tokens + 1, config.min_title_tokens);
        EXPECT_LE(title_tokens + 1, config.max_title_tokens);
        EXPECT_GE(path_tokens - 2, config.min_url_path_tokens);
        EXPECT_LE(path_tokens - 2, config.max_url_path_tokens);
    }
}

TEST(Corpus, StreamingDoesNotAccumulateDocuments) {
    // The streaming generator must not retain previously produced records.
    // Generate a large corpus via Next() and verify Count()/Remaining() track
    // progress without materializing anything.
    CorpusConfig config = FullConfig(23);
    config.document_count = 100000;
    CorpusGenerator gen(config);
    EXPECT_EQ(gen.Count(), 100000u);
    EXPECT_EQ(gen.Remaining(), 100000u);
    CorpusRecord record;
    std::size_t produced = 0;
    while (gen.Next(&record)) {
        ++produced;
    }
    EXPECT_EQ(produced, 100000u);
    EXPECT_EQ(gen.Remaining(), 0u);
}

TEST(Corpus, ForEachYieldsAllRecordsInOrder) {
    CorpusGenerator a(FullConfig(29));
    const std::vector<CorpusRecord> expected = a.GenerateAll();
    CorpusGenerator b(FullConfig(29));
    std::vector<CorpusRecord> actual;
    b.ForEach([&actual](const CorpusRecord& record) { actual.push_back(record); });
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].url, expected[i].url);
        EXPECT_EQ(actual[i].title, expected[i].title);
    }
}

TEST(Corpus, SingleDocumentCorpusProducesExactlyOneRecord) {
    CorpusConfig config = FullConfig(31);
    config.document_count = 1;
    CorpusGenerator gen(config);
    CorpusRecord record;
    EXPECT_TRUE(gen.Next(&record));
    EXPECT_EQ(record.doc_id, 0u);
    EXPECT_FALSE(gen.Next(&record));
}

TEST(Corpus, ManySeedCorporaRemainDeterministic) {
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const std::vector<CorpusRecord> a = CorpusGenerator(FullConfig(seed)).GenerateAll();
        const std::vector<CorpusRecord> b = CorpusGenerator(FullConfig(seed)).GenerateAll();
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].url, b[i].url);
        }
    }
}

}  // namespace
}  // namespace bench
}  // namespace search
}  // namespace island