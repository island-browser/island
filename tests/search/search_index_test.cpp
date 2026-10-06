// Facade acceptance: Ingest -> Query correctness, Flush-then-Query
// equivalence (the test that catches double-counting the retained MemTable),
// graceful degradation on a corrupt segment, and the hard privacy refusals.

#include "search/index/search_index.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
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

TEST(SearchIndexPrivacy, NormalizesTheUrlTheWayABrowserParsesIt) {
    // Leading/trailing C0 controls and spaces are stripped, and tabs and
    // newlines anywhere are removed, before the scheme is read.
    EXPECT_TRUE(IsPrivacyRefusedUrl(" data:text/html,<h1>hi</h1>"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("\x01\x1f data:text/plain,x \n"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("da\tta:text/plain,x"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("d\na\rta:text/plain,x"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("\thttps://user:pass@host.test/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("https://us\ner@host.test/"));
}

TEST(SearchIndexPrivacy, FindsUserinfoAfterAnyRunOfSlashesForSpecialSchemes) {
    // A special scheme's authority follows the colon after any run of '/' or
    // '\', including none at all.
    EXPECT_TRUE(IsPrivacyRefusedUrl("https:user:pass@host.test/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("https:\\\\user:pass@host.test/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("http:/user@host.test/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("HTTPS:u@h"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("ftp:///\\u@h/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("wss://u@h/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("file://u@h/"));
    // '\' ends a special authority, so an '@' after it is in the path.
    EXPECT_FALSE(IsPrivacyRefusedUrl("https://host.test\\a@b"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("https:host.test/a@b"));
}

TEST(SearchIndexPrivacy, OtherSchemesHaveAnAuthorityOnlyAfterTwoSlashes) {
    EXPECT_FALSE(IsPrivacyRefusedUrl("about:blank#x=http://a@b"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("mailto:someone@example.test"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("about:blank"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("foo://u@h/"));
    EXPECT_TRUE(IsPrivacyRefusedUrl("chrome-extension://u:p@h/"));
    // '\' is not an authority delimiter outside the special schemes.
    EXPECT_TRUE(IsPrivacyRefusedUrl("foo://h\\x@y/"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("foo:/u@h/"));
}

TEST(SearchIndexPrivacy, InputWithoutASchemeIsNotRefused) {
    EXPECT_FALSE(IsPrivacyRefusedUrl(""));
    EXPECT_FALSE(IsPrivacyRefusedUrl("   "));
    EXPECT_FALSE(IsPrivacyRefusedUrl("host.test/a@b"));
    EXPECT_FALSE(IsPrivacyRefusedUrl("1data:text/plain,x"));
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
    // A cold load is a miss and only a miss: counting it as a hit as well
    // would let this test pass against a cache that never retains anything.
    static_cast<void>(index->Query(query, kNowMs));
    EXPECT_EQ(index->cache().hit_count(), 0u);
    EXPECT_EQ(index->cache().miss_count(), 1u);
    EXPECT_EQ(index->cache().size(), 1u);

    static_cast<void>(index->Query(query, kNowMs));
    EXPECT_EQ(index->cache().hit_count(), 1u);
    EXPECT_EQ(index->cache().miss_count(), 1u);
}

TEST(SearchIndex, IngestKeepsTheSegmentPostingCache) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());

    Query query;
    query.text = "notes";
    static_cast<void>(index->Query(query, kNowMs));
    ASSERT_EQ(index->cache().size(), 1u);
    const std::size_t misses = index->cache().miss_count();

    // Segment posting lists are immutable, so a MemTable ingest cannot make a
    // cached one stale; the next query must still be served from the cache.
    ASSERT_TRUE(index->Ingest(MakeInput("https://post.test/notes", "Post Flush Notes", kNowMs))
                    .has_value());
    EXPECT_EQ(index->cache().size(), 1u);

    const SearchResult result = index->Query(query, kNowMs);
    EXPECT_EQ(index->cache().miss_count(), misses);
    EXPECT_EQ(index->cache().hit_count(), 1u);
    // And the new document is found, from the MemTable, alongside the cached
    // segment hits.
    EXPECT_EQ(result.hits.size(), 3u);
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------

TEST(SearchIndex, ConcurrentQueriesShareATinyCacheSafely) {
    TempDir tmp;
    // 64 bytes holds one or two short posting lists, so concurrent queries
    // evict each other's entries constantly; several lists are larger than
    // the whole ceiling and take the uncached path. Before the cache was
    // guarded this was a data race and a heap use-after-free under the
    // sanitizers.
    auto index = OpenIndex(tmp, 64);
    const std::vector<std::string> words = {"alpha", "bravo", "charlie", "delta",
                                            "echo",  "fox",   "golf",    "hotel"};
    for (std::size_t i = 0; i < 48; ++i) {
        std::string title;
        for (std::size_t w = 0; w < words.size(); ++w) {
            if ((i + 1) % (w + 1) == 0) {
                title += words[w] + " ";
            }
        }
        ASSERT_TRUE(index
                        ->Ingest(MakeInput("https://site" + std::to_string(i) + ".test/", title,
                                           kNowMs - i * 1000))
                        .has_value());
    }
    ASSERT_TRUE(index->Flush().has_value());

    std::vector<Query> queries;
    std::vector<SearchResult> expected;
    for (const std::string& text : {"alpha", "bravo charlie", "delta echo fox", "golf hotel",
                                    "alpha hotel", "charlie golf", "fox", "bravo delta"}) {
        Query query;
        query.text = text;
        query.max_results = 50;
        queries.push_back(query);
        expected.push_back(index->Query(query, kNowMs));
        ASSERT_FALSE(expected.back().hits.empty()) << text;
    }

    constexpr int kThreads = 4;
    constexpr int kIterations = 300;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kIterations; ++i) {
                const std::size_t q = static_cast<std::size_t>(t + i * (t + 1)) % queries.size();
                const SearchResult got = index->Query(queries[q], kNowMs);
                if (HitIds(got) != HitIds(expected[q])) {
                    ++mismatches;
                    continue;
                }
                for (std::size_t h = 0; h < got.hits.size(); ++h) {
                    if (got.hits[h].score != expected[q].hits[h].score) {
                        ++mismatches;
                        break;
                    }
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(mismatches.load(), 0);
    EXPECT_LE(index->cache().current_bytes(), index->cache().capacity());
}

// ---------------------------------------------------------------------------
// Ranking statistics
// ---------------------------------------------------------------------------

double ScoreOf(const SearchResult& result, const std::string& url) {
    for (const SearchHit& hit : result.hits) {
        if (hit.url == url) {
            return hit.score;
        }
    }
    ADD_FAILURE() << url << " not among the hits";
    return 0.0;
}

// The zebra document's score for "zebra" must not depend on which other
// documents an unrelated query term happens to match: BM25's avgdl is a
// corpus statistic, not an average over the candidates.
void ExpectUnrelatedTermLeavesScoreUnchanged(const SearchIndex& index) {
    Query zebra;
    zebra.text = "zebra";
    Query zebra_notes;
    zebra_notes.text = "zebra notes";
    const SearchResult alone = index.Query(zebra, kNowMs);
    const SearchResult combined = index.Query(zebra_notes, kNowMs);
    ASSERT_EQ(alone.hits.size(), 1u);
    ASSERT_GT(combined.hits.size(), 1u);
    EXPECT_DOUBLE_EQ(ScoreOf(alone, "https://zebra.test/"),
                     ScoreOf(combined, "https://zebra.test/"));
}

TEST(SearchIndex, AnUnrelatedQueryTermDoesNotChangeAScore) {
    TempDir tmp;
    {
        auto index = OpenIndex(tmp);
        IngestCorpus(*index);
        ExpectUnrelatedTermLeavesScoreUnchanged(*index);
        ASSERT_TRUE(index->Flush().has_value());
        ExpectUnrelatedTermLeavesScoreUnchanged(*index);
    }

    // Reopened: the segment predates the MemTable, so the corpus average
    // combines the segment's with the newly ingested documents'.
    auto reopened = OpenIndex(tmp);
    ASSERT_TRUE(reopened->has_segment());
    ExpectUnrelatedTermLeavesScoreUnchanged(*reopened);
    ASSERT_TRUE(reopened
                    ->Ingest(MakeInput("https://more.test/notes/long/path/here",
                                       "More Notes With A Much Longer Title", kNowMs))
                    .has_value());
    ExpectUnrelatedTermLeavesScoreUnchanged(*reopened);
}

TEST(SearchIndex, AReopenedIndexAveragesLengthOverSegmentAndMemTable) {
    TempDir tmp;
    const DocumentInput extra = MakeInput("https://more.test/notes/long/path/here",
                                          "More Notes With A Much Longer Title", kNowMs);

    // In-session: the retained MemTable holds all four documents.
    auto session = OpenIndex(tmp);
    IngestCorpus(*session);
    ASSERT_TRUE(session->Flush().has_value());
    ASSERT_TRUE(session->Ingest(extra).has_value());

    // Reopened on the same three-document segment, plus the same fourth
    // document in its MemTable: the same corpus, so the same scores up to the
    // segment's Q16 rounding of its average.
    auto reopened = OpenIndex(tmp);
    ASSERT_TRUE(reopened->has_segment());
    ASSERT_TRUE(reopened->Ingest(extra).has_value());

    Query query;
    query.text = "zebra";
    const double in_session = ScoreOf(session->Query(query, kNowMs), "https://zebra.test/");
    const double after_reopen = ScoreOf(reopened->Query(query, kNowMs), "https://zebra.test/");
    EXPECT_NEAR(in_session, after_reopen, in_session * 1e-4);
}

// ---------------------------------------------------------------------------
// Flush replacing a mapped segment
// ---------------------------------------------------------------------------

TEST(SearchIndex, ASecondFlushReplacesTheMappedSegmentFile) {
    // On Windows the second Flush renames over a file the index has mapped,
    // which fails unless the mapping is released first. POSIX allows that
    // rename either way, so off Windows this pins the outcome -- the file on
    // disk is the second flush's -- rather than reproducing the failure.
    TempDir tmp;
    {
        auto index = OpenIndex(tmp);
        IngestCorpus(*index);
        ASSERT_TRUE(index->Flush().has_value());
        ASSERT_TRUE(
            index->Ingest(MakeInput("https://x.test/notes", "Extra Notes", kNowMs)).has_value());
        ASSERT_TRUE(index->Flush().has_value());
        EXPECT_TRUE(index->has_segment());
    }

    auto reopened = OpenIndex(tmp);
    ASSERT_TRUE(reopened->has_segment());
    EXPECT_EQ(reopened->next_doc_id(), 5u);
    Query query;
    query.text = "notes";
    EXPECT_EQ(reopened->Query(query, kNowMs).hits.size(), 3u);
}

TEST(SearchIndex, AFailedFlushKeepsThePreviousSegmentAndEveryDocument) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());
    ASSERT_TRUE(
        index->Ingest(MakeInput("https://x.test/notes", "Extra Notes", kNowMs)).has_value());

    Query query;
    query.text = "notes";
    const SearchResult before = index->Query(query, kNowMs);
    ASSERT_EQ(before.hits.size(), 3u);

    // A directory squatting on the writer's temporary path makes the write
    // fail after Flush has already released the mapped segment.
    std::filesystem::path tmp_path = tmp.segment();
    tmp_path += ".tmp";
    ASSERT_TRUE(std::filesystem::create_directory(tmp_path));

    const auto flushed = index->Flush();
    ASSERT_FALSE(flushed.has_value());
    EXPECT_EQ(flushed.error().kind, SearchErrorKind::kIoError);
    // The untouched previous segment is mapped again...
    EXPECT_TRUE(index->has_segment());
    EXPECT_FALSE(index->segment_error().has_value());
    // ...and every document still answers, with unchanged scores.
    const SearchResult after = index->Query(query, kNowMs);
    EXPECT_EQ(HitIds(before), HitIds(after));
    for (std::size_t i = 0; i < before.hits.size() && i < after.hits.size(); ++i) {
        EXPECT_DOUBLE_EQ(before.hits[i].score, after.hits[i].score);
    }

    std::filesystem::remove(tmp_path);
    ASSERT_TRUE(index->Flush().has_value());
    EXPECT_EQ(HitIds(index->Query(query, kNowMs)), HitIds(before));
}

TEST(SearchIndex, AFailedFlushWithNothingToRemapStillAnswersEveryDocument) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);
    ASSERT_TRUE(index->Flush().has_value());

    Query query;
    query.text = "notes";
    const SearchResult before = index->Query(query, kNowMs);
    ASSERT_EQ(before.hits.size(), 2u);

    // The previous segment file is gone and the write fails, so Flush ends
    // with no segment at all. The MemTable must then answer for every id,
    // including the ones the released segment used to own.
    std::error_code removed;
    std::filesystem::remove(tmp.segment(), removed);
    if (removed) {
        GTEST_SKIP() << "this platform cannot delete a mapped file: " << removed.message();
    }
    std::filesystem::path tmp_path = tmp.segment();
    tmp_path += ".tmp";
    ASSERT_TRUE(std::filesystem::create_directory(tmp_path));

    ASSERT_FALSE(index->Flush().has_value());
    EXPECT_FALSE(index->has_segment());
    EXPECT_TRUE(index->segment_error().has_value());
    EXPECT_EQ(HitIds(index->Query(query, kNowMs)), HitIds(before));
}

TEST(SearchIndex, AFailedFirstFlushLeavesTheIndexAnsweringFromTheMemTable) {
    TempDir tmp;
    auto index = OpenIndex(tmp);
    IngestCorpus(*index);

    std::filesystem::path tmp_path = tmp.segment();
    tmp_path += ".tmp";
    ASSERT_TRUE(std::filesystem::create_directory(tmp_path));

    const auto flushed = index->Flush();
    ASSERT_FALSE(flushed.has_value());
    EXPECT_FALSE(index->has_segment());
    ASSERT_TRUE(index->segment_error().has_value());
    EXPECT_EQ(index->segment_error()->kind, SearchErrorKind::kIoError);

    Query query;
    query.text = "notes";
    EXPECT_EQ(index->Query(query, kNowMs).hits.size(), 2u);
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
