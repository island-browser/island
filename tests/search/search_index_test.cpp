// Facade acceptance: Ingest -> Query correctness, Flush-then-Query
// equivalence (the test that catches double-counting the retained MemTable),
// graceful degradation on a corrupt segment, and the hard privacy refusals.

#include "search/index/search_index.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "search/types.h"

namespace island {
namespace search {
namespace {

constexpr std::uint64_t kNowMs = 1'000'000'000;

class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("island_index_test_" + std::to_string(++counter) + "_" +
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

DocumentInput MakeInput(std::string url, std::string title, std::uint64_t visited_ms) {
    DocumentInput input;
    input.url = std::move(url);
    input.title = std::move(title);
    input.visited_at_ms = visited_ms;
    return input;
}

std::unique_ptr<SearchIndex> OpenIndex(const TempDir& tmp,
                                       std::size_t cache_bytes = kBlockCacheBytes) {
    SearchIndex::Options options;
    options.segment_path = tmp.segment();
    options.block_cache_bytes = cache_bytes;
    auto opened = SearchIndex::Open(std::move(options));
    EXPECT_TRUE(opened.has_value());
    return std::move(opened.value());
}

void IngestCorpus(SearchIndex& index) {
    ASSERT_TRUE(
        index
            .Ingest(MakeInput("https://island.test/browser", "Island Browser Notes", kNowMs - 1000))
            .has_value());
    ASSERT_TRUE(index.Ingest(MakeInput("https://notes.test/daily", "Daily Notes", kNowMs - 2000))
                    .has_value());
    ASSERT_TRUE(
        index.Ingest(MakeInput("https://zebra.test/", "Zebra Facts", kNowMs - 3000)).has_value());
}

std::vector<DocId> HitIds(const SearchResult& result) {
    std::vector<DocId> ids;
    for (const SearchHit& hit : result.hits) {
        ids.push_back(hit.doc);
    }
    return ids;
}

// ---------------------------------------------------------------------------
// Privacy refusals
// ---------------------------------------------------------------------------

TEST(SearchIndexPrivacy, RefusesDataUrlsRegardlessOfCase) {
    EXPECT_TRUE(IsPrivacyRefusedUrl("data:text/html,<h1>hi</h1>"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("DATA:text/plain,secret"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("https://data.example.com/page"));
}

TEST(SearchIndexPrivacy, RefusesCredentialedUrlsOnly) {
    EXPECT_TRUE(IsPrivacyRefusedUrl("https://user:pass@host.test/path"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("https://user@host.test/"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("https://host.test/"));
    // An '@' outside the authority is not userinfo and must not be refused.
    EXPECT_FALSE(IsPrivacyRefusedUrl("https://host.test/path?mail=a@b.test"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("https://host.test/a@b"));
}

TEST(SearchIndex, IngestRefusesDataAndCredentialedUrls) {
    TempDir tmp;
    auto index = OpenIndex(tmp);

    const auto data_url = index->Ingest(MakeInput("data:text/html,<b>x</b>", "Inline", kNowMs));
    ASSERT_FALSE(data_url.has_value());
    EXPECT_EQ(data_url.error().kind, SearchErrorKind::kRefusedForPrivacy);

    const auto credentialed =
        index->Ingest(MakeInput("https://u:p@host.test/", "Credentialed", kNowMs));
    ASSERT_FALSE(credentialed.has_value());
    EXPECT_EQ(credentialed.error().kind, SearchErrorKind::kRefusedForPrivacy);

    // Neither refusal minted an id.
    EXPECT_EQ(index->next_doc_id(), 1u);
}

TEST(SearchIndex, IngestRejectsAnEmptyUrlAsInvalidInput) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    const auto empty = index->Ingest(MakeInput("", "No URL", kNowMs));
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().kind, SearchErrorKind::kInvalidInput);
}

// ---------------------------------------------------------------------------
// Ingest and Query
// ---------------------------------------------------------------------------

TEST(SearchIndex, OpensWithoutASegmentAndReportsWhy) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    EXPECT_FALSE(index->has_segment());
    ASSERT_TRUE(index->segment_error().has_value());
    EXPECT_EQ(index->segment_error()->kind, SearchErrorKind::kIoError);
}

TEST(SearchIndex, IngestMintsMonotonicIds) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    const auto first = index->Ingest(MakeInput("https://a.test/", "A", kNowMs));
    const auto second = index->Ingest(MakeInput("https://b.test/", "B", kNowMs));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first.value(), 1u);
    EXPECT_EQ(second.value(), 2u);
}

TEST(SearchIndex, QueryFindsOnlyMatchingDocuments) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);

    Query query;
    query.text = "zebra";
    const SearchResult result = index->Query(query, kNowMs);
    ASSERT_EQ(result.hits.size(), 1u);
    EXPECT_EQ(result.hits[0].url, "https://zebra.test/");
}

TEST(SearchIndex, QueryMatchesTitleAndUrlFields) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);

    Query query;
    query.text = "notes";
    const SearchResult result = index->Query(query, kNowMs);
    // "Notes" appears in both documents' titles, one also in its url.
    EXPECT_EQ(result.hits.size(), 2u);
}

TEST(SearchIndex, QueryRespectsMaxResults) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);

    Query query;
    query.text = "notes";
    query.max_results = 1;
    EXPECT_EQ(index->Query(query, kNowMs).hits.size(), 1u);

    query.max_results = 0;
    EXPECT_TRUE(index->Query(query, kNowMs).hits.empty());
}

TEST(SearchIndex, QueryOnAnEmptyIndexIsTotalAndEmpty) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    Query query;
    query.text = "anything";
    const SearchResult result = index->Query(query, kNowMs);
    EXPECT_TRUE(result.hits.empty());
}

TEST(SearchIndex, AnEmptyQueryScansNoTerms) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    Query query;
    query.text = "   ";
    const SearchResult result = index->Query(query, kNowMs);
    EXPECT_TRUE(result.hits.empty());
    EXPECT_EQ(result.scanned_terms, 0u);
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------

TEST(SearchIndex, FlushThenQueryReturnsExactlyTheSameHits) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);

    Query query;
    query.text = "notes";
    const SearchResult before = index->Query(query, kNowMs);

    ASSERT_TRUE(index->Flush().has_value());
    ASSERT_TRUE(index->has_segment());

    const SearchResult after = index->Query(query, kNowMs);
    EXPECT_EQ(HitIds(before), HitIds(after));
    ASSERT_EQ(before.hits.size(), after.hits.size());
    for (std::size_t i = 0; i < before.hits.size(); ++i) {
        EXPECT_EQ(before.hits[i].url, after.hits[i].url);
        EXPECT_DOUBLE_EQ(before.hits[i].score, after.hits[i].score);
    }
}

TEST(SearchIndex, FlushDoesNotCountRetainedMemTableDocumentsTwice) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());

    Query query;
    query.text = "notes";
    const SearchResult result = index->Query(query, kNowMs);

    // Each document must appear exactly once even though it now lives in both
    // the segment and the retained MemTable. Note this property is upheld by
    // the candidate set, not by the id-range split; the split's own
    // correctness role is the corpus size, which
    // FlushThenQueryReturnsExactlyTheSameHits pins via the scores.
    std::vector<DocId> ids = HitIds(result);
    const std::size_t total = ids.size();
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    EXPECT_EQ(ids.size(), total);
}

TEST(SearchIndex, DocumentsIngestedAfterFlushAreQueryableAlongsideTheSegment) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());

    ASSERT_TRUE(index->Ingest(MakeInput("https://post.test/notes", "Post Flush Notes", kNowMs))
                    .has_value());

    Query query;
    query.text = "notes";
    const SearchResult result = index->Query(query, kNowMs);
    EXPECT_EQ(result.hits.size(), 3u);
}

TEST(SearchIndex, FlushingTwiceInOneSessionKeepsEveryDocument) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());
    ASSERT_TRUE(
        index->Ingest(MakeInput("https://x.test/notes", "Extra Notes", kNowMs)).has_value());
    ASSERT_TRUE(index->Flush().has_value());

    Query query;
    query.text = "notes";
    EXPECT_EQ(index->Query(query, kNowMs).hits.size(), 3u);
}

TEST(SearchIndex, ReopeningReadsTheFlushedSegment) {
    TempDir tmp;
    {
        auto index = OpenIndex(tmp);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
    }

    auto reopened = OpenIndex(tmp);
    ASSERT_TRUE(reopened->has_segment());
    EXPECT_FALSE(reopened->segment_error().has_value());

    Query query;
    query.text = "zebra";
    ASSERT_EQ(reopened->Query(query, kNowMs).hits.size(), 1u);
}

TEST(SearchIndex, FlushRefusesRatherThanDiscardingAPreExistingSegment) {
    TempDir tmp;
    {
        auto index = OpenIndex(tmp);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
    }

    // S0 has no segment merge, so this MemTable does not hold the segment's
    // documents; flushing it would drop them.
    auto reopened = OpenIndex(tmp);
    ASSERT_TRUE(reopened->Ingest(MakeInput("https://new.test/", "New", kNowMs)).has_value());
    const auto flushed = reopened->Flush();
    ASSERT_FALSE(flushed.has_value());
    EXPECT_EQ(flushed.error().kind, SearchErrorKind::kInvalidInput);

    // The persisted segment survived the refusal.
    auto again = OpenIndex(tmp);
    Query query;
    query.text = "zebra";
    EXPECT_EQ(again->Query(query, kNowMs).hits.size(), 1u);
}

// ---------------------------------------------------------------------------
// Degradation
// ---------------------------------------------------------------------------

TEST(SearchIndex, ACorruptSegmentIsQuarantinedAndQueriesStillAnswer) {
    TempDir tmp;
    {
        auto index = OpenIndex(tmp);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
    }

    // Corrupt the persisted segment's magic.
    {
        std::fstream file(tmp.segment(), std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        file.seekp(0);
        const char bogus = 0x00;
        file.write(&bogus, 1);
    }

    auto index = OpenIndex(tmp);
    EXPECT_FALSE(index->has_segment());
    ASSERT_TRUE(index->segment_error().has_value());

    int quarantined = 0;
    for (const auto& entry : std::filesystem::directory_iterator(tmp.dir())) {
        if (entry.path().filename().string().find(".corrupt-") != std::string::npos) {
            ++quarantined;
        }
    }
    EXPECT_EQ(quarantined, 1);

    // Queries still answer -- from the MemTable, which here is empty, but the
    // call is total and does not throw.
    Query query;
    query.text = "zebra";
    EXPECT_TRUE(index->Query(query, kNowMs).hits.empty());

    // And the index remains usable for new documents.
    ASSERT_TRUE(index->Ingest(MakeInput("https://fresh.test/zebra", "Zebra", kNowMs)).has_value());
    EXPECT_EQ(index->Query(query, kNowMs).hits.size(), 1u);
}

// ---------------------------------------------------------------------------
// Cache independence
// ---------------------------------------------------------------------------

TEST(SearchIndex, ResultsAreIdenticalWithATinyCacheAndAGenerousOne) {
    TempDir tmp;
    Query query;
    query.text = "notes";

    std::vector<DocId> generous;
    {
        auto index = OpenIndex(tmp, kBlockCacheBytes);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
        generous = HitIds(index->Query(query, kNowMs));
    }

    TempDir tiny_dir;
    std::vector<DocId> tiny;
    {
        // A one-byte ceiling rejects every entry, forcing the uncached path.
        auto index = OpenIndex(tiny_dir, 1);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
        tiny = HitIds(index->Query(query, kNowMs));
    }

    EXPECT_EQ(generous, tiny);
    EXPECT_FALSE(generous.empty());
}

TEST(SearchIndex, RepeatedQueriesHitTheCache) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());

    Query query;
    query.text = "notes";
    static_cast<void>(index->Query(query, kNowMs));
    const std::size_t hits_before = index->cache().hit_count();
    static_cast<void>(index->Query(query, kNowMs));
    EXPECT_GT(index->cache().hit_count(), hits_before);
}

TEST(SearchIndex, LoadDocumentCarriesTheStoredId) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    const Expected<DocId, SearchError> ingested =
        index->Ingest(MakeInput("https://island.test/entry", "Island Notes", 5000));
    ASSERT_TRUE(ingested.has_value()) << ingested.error().detail;
    const DocId id = ingested.value();

    const std::optional<StoredDocument> from_memtable = index->LoadDocument(id);
    ASSERT_TRUE(from_memtable.has_value());
    EXPECT_EQ(from_memtable->id, id);
    EXPECT_EQ(from_memtable->url, "https://island.test/entry");
    EXPECT_EQ(from_memtable->title, "Island Notes");

    // The same identity survives a flush into the on-disk segment.
    ASSERT_TRUE(index->Flush().has_value());
    const std::optional<StoredDocument> from_segment = index->LoadDocument(id);
    ASSERT_TRUE(from_segment.has_value());
    EXPECT_EQ(from_segment->id, id);
    EXPECT_EQ(from_segment->url, "https://island.test/entry");
}

}  // namespace
}  // namespace search
}  // namespace island
