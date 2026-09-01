#include "search/memtable.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "search/types.h"

namespace island {
namespace search {
namespace {

using island::search::DocId;
using island::search::DocumentInput;
using island::search::MemTable;
using island::search::StoredDocument;

StoredDocument MakeDoc(DocId id, std::string url, std::string title, std::uint64_t visited_ms) {
    StoredDocument doc;
    doc.id = id;
    doc.url = std::move(url);
    doc.title = std::move(title);
    doc.visited_at_ms = visited_ms;
    return doc;
}

TEST(MemTable, StartsAtTheConfiguredFirstIdAndNeverAtZero) {
    const MemTable from_one;
    EXPECT_EQ(from_one.next_doc_id(), 1u);

    const MemTable resumed(/*first_doc_id=*/42);
    EXPECT_EQ(resumed.next_doc_id(), 42u);
    EXPECT_TRUE(resumed.empty());
    EXPECT_EQ(resumed.doc_count(), 0u);
}

TEST(MemTable, AddDocumentMintsMonotonicIds) {
    MemTable table;
    std::vector<std::string> tokens{"alpha", "beta"};

    const DocId first = table.AddDocument(MakeDoc(1, "https://a.test", "Alpha", 100), tokens, {});
    const DocId second =
        table.AddDocument(MakeDoc(2, "https://b.test", "Beta", 200), {}, tokens);

    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
    EXPECT_EQ(table.next_doc_id(), 3u);
    EXPECT_EQ(table.doc_count(), 2u);
}

TEST(MemTable, IgnoresDuplicateTokensWithinAFieldButCountsTotalTokens) {
    MemTable table;
    std::vector<std::string> title{"alpha", "alpha", "beta"};
    std::vector<std::string> url{"alpha"};

    table.AddDocument(MakeDoc(1, "https://a.test/alpha", "Alpha Alpha Beta", 0), title, url);

    EXPECT_EQ(table.total_token_count(), 4u);
    EXPECT_DOUBLE_EQ(table.avg_doc_length(), 4.0);

    const auto record = table.RecordAt(1);
    ASSERT_TRUE(record.has_value());
    ASSERT_EQ(record->term_frequencies.size(), 2u);  // "alpha" and "beta".
}

TEST(MemTable, PostingListsStayIdAscendingByConstruction) {
    MemTable table;
    std::vector<std::string> shared{"common"};
    std::vector<std::string> other{"rare"};

    for (DocId id = 1; id <= 100; ++id) {
        const std::vector<std::string>& tokens = (id % 5 == 0) ? shared : other;
        table.AddDocument(MakeDoc(id, "https://x.test", "t", id), tokens, {});
    }

    const std::span<const std::uint64_t> common = table.PostingsFor("common");
    ASSERT_EQ(common.size(), 20u);
    for (std::size_t i = 0; i < common.size(); ++i) {
        EXPECT_EQ(common[i], (i + 1) * 5);
    }
    EXPECT_TRUE(table.PostingsFor("absent").empty());
}

TEST(MemTable, TracksPerFieldTermFrequencies) {
    MemTable table;
    std::vector<std::string> title{"island", "island", "browser"};
    std::vector<std::string> url{"island"};

    table.AddDocument(MakeDoc(1, "https://island.test/", "Island Island Browser", 0), title, url);

    const auto record = table.RecordAt(1);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->title_token_count, 3u);
    EXPECT_EQ(record->url_token_count, 1u);

    bool saw_island = false;
    for (const TermFrequencyEntry& entry : record->term_frequencies) {
        if (entry.title_tf == 2 && entry.url_tf == 1) {
            saw_island = true;
        }
    }
    EXPECT_TRUE(saw_island);
}

TEST(MemTable, RecordAtBoundsCheckMissesForUnknownIds) {
    MemTable table;
    EXPECT_FALSE(table.RecordAt(1).has_value());
    EXPECT_FALSE(table.RecordAt(999).has_value());
}

TEST(MemTable, SortedTermsAreByteLexicographic) {
    MemTable table;
    std::vector<std::string> tokens{"zeta", "alpha", "mid"};
    table.AddDocument(MakeDoc(1, "https://a.test", "Zeta Alpha Mid", 0), tokens, {});

    const std::vector<MemTable::SortedTerm> terms = table.SortedTerms();
    ASSERT_EQ(terms.size(), 3u);
    EXPECT_EQ(std::string(terms[0].term.data(), terms[0].term.size()), "alpha");
    EXPECT_EQ(std::string(terms[1].term.data(), terms[1].term.size()), "mid");
    EXPECT_EQ(std::string(terms[2].term.data(), terms[2].term.size()), "zeta");
    EXPECT_EQ(terms[0].postings.size(), 1u);
}

TEST(MemTable, AverageDocLengthReflectsBothFields) {
    MemTable table;
    std::vector<std::string> two{"a", "b"};
    std::vector<std::string> four{"c", "d", "e", "f"};
    table.AddDocument(MakeDoc(1, "u", "t", 0), two, two);     // 4 tokens
    table.AddDocument(MakeDoc(2, "u", "t", 0), four, {});     // 4 tokens
    table.AddDocument(MakeDoc(3, "u", "t", 0), {}, {});       // 0 tokens

    EXPECT_EQ(table.total_token_count(), 8u);
    EXPECT_DOUBLE_EQ(table.avg_doc_length(), 8.0 / 3.0);
}

}  // namespace
}  // namespace search
}  // namespace island
