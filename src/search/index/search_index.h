// The Ingest / Query / Flush facade: the only type a caller needs.
//
// Owns the MemTable, the active read-only Segment (when one exists), and the
// decoded-posting cache. No exceptions cross this boundary; every fallible
// operation returns Expected<T, SearchError>. Query is total -- a broken
// segment degrades to MemTable-only results -- so it returns a plain
// SearchResult.
//
// Single-writer: Ingest and Flush must be called from one thread. Query is
// const and safe against other Query calls only while no Ingest or Flush is in
// flight; S0 promises no reader/writer concurrency, so nothing here is
// internally synchronized.

#ifndef ISLAND_SEARCH_INDEX_SEARCH_INDEX_H_
#define ISLAND_SEARCH_INDEX_SEARCH_INDEX_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "search/cache/block_cache.h"
#include "search/expected.h"
#include "search/memtable.h"
#include "search/ranker.h"
#include "search/store/segment.h"
#include "search/types.h"

namespace island {
namespace search {

// True when `url` must never enter a persisted index: a data: URL, which can
// embed arbitrary page content, or a URL carrying userinfo (user:pass@host).
// Exposed so the refusal is testable on its own and so a caller can pre-check.
//
// This duplicates judgment that island::ValidatedAddress also makes. That is
// deliberate defense in depth: the library performs the check on the raw
// string so it stays safe when called without pre-validation, and so it never
// links src/main. address_policy.h is not included here -- its
// ValidatedAddress::is_valid() is defined in address_policy.cc, which this
// CEF-free library must not link.
[[nodiscard]] bool IsPrivacyRefusedUrl(std::string_view url) noexcept;

class SearchIndex {
  public:
    struct Options {
        std::filesystem::path segment_path;
        std::size_t block_cache_bytes = kBlockCacheBytes;
        RankingWeights weights;
    };

    // Maps an existing segment when there is one, quarantining it if it fails
    // validation, and starts an empty MemTable. A missing or rejected segment
    // is not an error: the index opens MemTable-only, and segment_error()
    // reports why.
    [[nodiscard]] static Expected<std::unique_ptr<SearchIndex>, SearchError> Open(Options options);

    // Mints the next DocId, tokenizes url and title, and records the document.
    // Refuses data: and credentialed URLs with kRefusedForPrivacy, and an
    // empty url with kInvalidInput.
    [[nodiscard]] Expected<DocId, SearchError> Ingest(const DocumentInput& input);

    // Ranks the segment and the MemTable together and returns the top
    // query.max_results hits. `query_now_ms` feeds the recency term, so the
    // index never reads a clock. Total: never throws, never fails.
    [[nodiscard]] SearchResult Query(const struct Query& query,
                                     std::uint64_t query_now_ms) const;

    // Serializes the MemTable to `segment_path` via the atomic writer, then
    // re-opens it read-only as the active segment.
    //
    // S0 LIMITATION, enforced rather than hidden: a segment holds exactly the
    // MemTable's documents, and S0 has no segment merge. If this index was
    // opened on an existing segment, the MemTable does not contain that
    // segment's documents, so writing it would silently discard them. Flush
    // refuses with kInvalidInput in that case instead of destroying the
    // persisted index. Flushing repeatedly within one session is fine: the
    // MemTable is retained across Flush and still holds every document.
    [[nodiscard]] Expected<void, SearchError> Flush();

    [[nodiscard]] bool has_segment() const noexcept { return segment_ != nullptr; }

    // Why the segment is absent, when it is absent because Open rejected one.
    [[nodiscard]] const std::optional<SearchError>& segment_error() const noexcept {
        return segment_error_;
    }

    [[nodiscard]] std::uint64_t next_doc_id() const noexcept { return memtable_.next_doc_id(); }
    [[nodiscard]] const BlockCache& cache() const noexcept { return cache_; }

  private:
    SearchIndex(Options options, std::unique_ptr<Segment> segment,
                std::optional<SearchError> segment_error);

    // Ascending posting list for `term` from the segment, served through the
    // cache. Returns nullptr when the term is absent or there is no segment.
    [[nodiscard]] const std::vector<std::uint64_t>* SegmentPostings(const std::string& term) const;

    // Builds the ranker's view of one document, deriving per-field term
    // frequencies by re-tokenizing the stored url and title.
    [[nodiscard]] std::optional<DocumentStat> BuildStat(DocId id,
                                                        std::uint64_t query_now_ms) const;

    [[nodiscard]] std::optional<StoredDocument> LoadDocument(DocId id) const;

    Options options_;
    MemTable memtable_;
    std::unique_ptr<Segment> segment_;
    std::optional<SearchError> segment_error_;

    // Documents with an id below this live in the segment; ids at or above it
    // live only in the MemTable. The split bounds each posting scan and, more
    // importantly, gives Query the true corpus size: the MemTable is retained
    // across Flush, so adding its full doc_count() to the segment's would
    // double-count every flushed document and distort idf.
    std::uint64_t segment_boundary_ = 0;

    // True when the active segment's documents are absent from the MemTable,
    // which is the case exactly when Open mapped a pre-existing segment. See
    // the Flush comment above.
    bool segment_predates_memtable_ = false;

    // Query is const, but an LRU promotes on read.
    mutable BlockCache cache_;

    // Holds a posting list too large for the cache's ceiling, so the
    // oversized-entry bypass can still hand out a valid reference. Overwritten
    // by the next oversized term; only ever read within one Query.
    mutable std::vector<std::uint64_t> uncached_;
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_INDEX_SEARCH_INDEX_H_
