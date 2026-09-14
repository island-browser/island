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

    const DocId first =
        table.AddDocument(MakeDoc(1, "https://a.test", "Alpha", 100), tokens, {}).value();
    const DocId second =
        table.AddDocument(MakeDoc(2, "https://b.test", "Beta", 200), {}, tokens).value();

    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
    EXPECT_EQ(table.next_doc_id(), 3u);
    EXPECT_EQ(table.doc_count(), 2u);
}

TEST(MemTable, RejectsFieldsLargerThanOneArenaChunkWithNoPartialState) {
    MemTable table;
    std::vector<std::string> tokens{"alpha"};

    // A field that cannot fit a single 1 MiB chunk is unstorable: the document
    // is rejected whole, consuming no id and touching no posting list.
    const std::string oversized(DocumentArena::kChunkBytes + 1, 'x');
    StoredDocument unstorable = MakeDoc(1, "https://a.test", "Alpha", 100);
    unstorable.title = oversized;
    EXPECT_FALSE(table.AddDocument(std::move(unstorable), tokens, {}).has_value());
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.next_doc_id(), 1u);
    EXPECT_TRUE(table.PostingsFor("alpha").empty());

    // An oversized url and an oversized partition tag are rejected the same
    // way, and the table keeps accepting well-formed documents afterwards.
    StoredDocument oversized_url = MakeDoc(1, "https://a.test", "Alpha", 100);
    oversized_url.url = oversized;
    EXPECT_FALSE(table.AddDocument(std::move(oversized_url), tokens, {}).has_value());
    StoredDocument oversized_tag = MakeDoc(1, "https://a.test", "Alpha", 100);
    oversized_tag.partition_tag = oversized;
    EXPECT_FALSE(table.AddDocument(std::move(oversized_tag), tokens, {}).has_value());

    const DocId accepted =
        table.AddDocument(MakeDoc(1, "https://a.test", "Alpha", 100), tokens, {}).value();
    EXPECT_EQ(accepted, 1u);
    EXPECT_EQ(table.doc_count(), 1u);

    // Exactly-chunk-sized fields sit at the cap and are storable.
    MemTable at_cap;
    StoredDocument chunk_sized = MakeDoc(1, "https://a.test", "Alpha", 100);
    chunk_sized.title = std::string(DocumentArena::kChunkBytes, 'y');
    EXPECT_TRUE(at_cap.AddDocument(std::move(chunk_sized), {}, {}).has_value());
}

TEST(MemTable, IgnoresDuplicateTokensWithinAFieldButCountsTotalTokens) {
    MemTable table;
    std::vector<std::string> title{"alpha", "alpha", "beta"};
    std::vector<std::string> url{"alpha"};

    table.AddDocument(MakeDoc(1, "https://a.test/alpha", "Alpha Alpha Beta", 0), title, url);

    EXPECT_EQ(table.total_token_count(), 4u);
    EXPECT_DOUBLE_EQ(table.avg_doc_length(), 4.0);

    // Duplicates collapse to one posting per distinct term.
    EXPECT_EQ(table.term_count(), 2u);  // "alpha" and "beta".
    EXPECT_EQ(table.PostingsFor("alpha").size(), 1u);
    EXPECT_EQ(table.PostingsFor("beta").size(), 1u);
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

TEST(MemTable, TracksPerFieldTokenCountsAndDistinctTermPostings) {
    MemTable table;
    std::vector<std::string> title{"island", "island", "browser"};
    std::vector<std::string> url{"island"};

    table.AddDocument(MakeDoc(1, "https://island.test/", "Island Island Browser", 0), title, url);

    const auto record = table.RecordAt(1);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->title_token_count, 3u);
    EXPECT_EQ(record->url_token_count, 1u);

    // Per-field frequencies are re-derived by the ranker at query time, so the
    // record keeps only field lengths; each distinct term still holds exactly
    // one posting for this document.
    EXPECT_EQ(table.PostingsFor("island").size(), 1u);
    EXPECT_EQ(table.PostingsFor("browser").size(), 1u);
    EXPECT_EQ(table.PostingsFor("island").front(), 1u);
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
    table.AddDocument(MakeDoc(1, "u", "t", 0), two, two);  // 4 tokens
    table.AddDocument(MakeDoc(2, "u", "t", 0), four, {});  // 4 tokens
    table.AddDocument(MakeDoc(3, "u", "t", 0), {}, {});    // 0 tokens

    EXPECT_EQ(table.total_token_count(), 8u);
    EXPECT_DOUBLE_EQ(table.avg_doc_length(), 8.0 / 3.0);
}

}  // namespace
}  // namespace search
}  // namespace island
