// The mutable, in-RAM index: a dense DocId -> document store, a per-term
// ascending posting list, and the corpus statistics the ranker needs.
// Postings hold DocIds only; the ranker derives per-field term frequencies by
// re-tokenizing the stored url and title (see SearchIndex::BuildStat), so no
// per-document frequency entries are retained here -- that is a deliberate
// memory-budget decision, not an oversight.
//
// Document bytes are arena-backed rather than one std::string heap allocation
// per field: url/title/tag bytes are appended into one growing byte arena and
// each record keeps offsets into it. This keeps the per-document footprint
// close to the raw bytes, which is what the S0 memory gate
// (docs/search-phase-s0-membench.md) measures.

#ifndef ISLAND_SEARCH_MEMTABLE_H_
#define ISLAND_SEARCH_MEMTABLE_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "search/types.h"

namespace island {
namespace search {

// Append-only byte store backing document text. A growing std::vector<char>
// would reallocate by doubling, and the transient old+new coexistence plus the
// up-to-2x capacity slack would dominate the RSS high-water the memory gate
// measures; fixed 1 MiB chunks keep both at a per-chunk constant. Strings never
// straddle chunks: an append that does not fit the current tail starts a new
// chunk, wasting at most one max-string-size tail per chunk.
//
// Offsets are global: (chunk_index << kChunkBits) | within_chunk_offset.
class DocumentArena {
  public:
    static constexpr std::uint64_t kChunkBits = 20;  // 1 MiB chunks.
    static constexpr std::uint64_t kChunkBytes = std::uint64_t{1} << kChunkBits;

    // Returns the global offset of the appended bytes, or std::nullopt when
    // |bytes| exceeds kChunkBytes: strings never straddle chunks, so nothing
    // larger than one chunk is storable. Callers reject oversized fields before
    // any document state changes; the arena never partial-fails.
    std::optional<std::uint64_t> Append(std::string_view bytes);

    [[nodiscard]] std::string_view View(std::uint64_t offset, std::uint32_t size) const;

  private:
    std::vector<std::unique_ptr<char[]>> chunks_;
    std::size_t chunk_fill_ = 0;  // Bytes used in the last chunk.
};

// Read view of one stored document. The string_views point into the MemTable's
// arena and stay valid for the lifetime of the MemTable (chunks are never
// reallocated or freed); queries never ingest, so query-time views are stable.
struct MemTableDocumentView {
    std::string_view url;
    std::string_view title;
    std::uint64_t visited_at_ms = 0;
    std::optional<std::string_view> partition_tag;
};

// Read view of one stored record.
struct MemTableRecord {
    MemTableDocumentView document;
    std::uint32_t title_token_count = 0;
    std::uint32_t url_token_count = 0;
};

class MemTable {
  public:
    // `first_doc_id` is the id assigned to the first ingested document; the
    // facade resumes it from a segment header high-water mark. Must be >= 1 so
    // DocId 0 stays the reserved null.
    explicit MemTable(std::uint64_t first_doc_id = 1);

    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;
    MemTable(MemTable&&) noexcept = default;
    MemTable& operator=(MemTable&&) noexcept = default;

    // Adds one document. `document.id` must equal next_doc_id(). Tokenizes no
    // text itself: the caller passes the already-tokenized fields so ingest
    // and query share exactly one tokenizer path. Posting lists stay
    // id-ascending because documents are added in id order. Returns the
    // assigned DocId, or std::nullopt when any stored field exceeds one arena
    // chunk (kChunkBytes) — rejected with no partial state applied.
    std::optional<DocId> AddDocument(StoredDocument document,
                                     std::span<const std::string> title_tokens,
                                     std::span<const std::string> url_tokens);

    [[nodiscard]] std::uint64_t next_doc_id() const noexcept { return next_doc_id_; }
    [[nodiscard]] std::size_t doc_count() const noexcept { return records_.size(); }
    [[nodiscard]] std::uint64_t total_token_count() const noexcept { return total_tokens_; }
    [[nodiscard]] double avg_doc_length() const noexcept {
        return records_.empty()
                   ? 0.0
                   : static_cast<double>(total_tokens_) / static_cast<double>(records_.size());
    }
    [[nodiscard]] bool empty() const noexcept { return records_.empty(); }
    [[nodiscard]] std::size_t term_count() const noexcept { return term_by_index_.size(); }

    // Ascending posting list for `term`, empty when absent.
    [[nodiscard]] std::span<const std::uint64_t> PostingsFor(std::string_view term) const;

    // Record view for `id`, or std::nullopt when the id is not in this table.
    [[nodiscard]] std::optional<MemTableRecord> RecordAt(DocId id) const;

    // All terms with their posting lists in ascending (byte-lexicographic)
    // term order. Built on demand; used by the segment writer.
    struct SortedTerm {
        std::span<const char> term;
        std::span<const std::uint64_t> postings;
    };
    [[nodiscard]] std::vector<SortedTerm> SortedTerms() const;

  private:
    // Offsets into arena_ for one document's bytes. Sizes are uint32 because a
    // single url/title/tag is far below 4 GB; offsets stay uint64 so the arena
    // itself may grow past 4 GB.
    struct StoredRecord {
        std::uint64_t url_offset = 0;
        std::uint32_t url_size = 0;
        std::uint64_t title_offset = 0;
        std::uint32_t title_size = 0;
        std::uint64_t tag_offset = 0;
        std::uint32_t tag_size = 0;
        std::uint64_t visited_at_ms = 0;
        std::uint32_t title_token_count = 0;
        std::uint32_t url_token_count = 0;
    };

    std::uint32_t TermIndexFor(std::string_view term);

    // records_ is dense from first_doc_id_, so RecordAt maps id -> id - first_doc_id_.
    // std::deque keeps the dense access pattern without the doubling
    // reallocation (and its transient RSS peak) a vector would pay.
    std::uint64_t first_doc_id_;
    std::uint64_t next_doc_id_;
    std::uint64_t total_tokens_ = 0;
    std::deque<StoredRecord> records_;                                // dense by id.
    DocumentArena arena_;                                             // url/title/tag bytes.
    std::unordered_map<std::string, std::uint32_t> term_index_;       // term -> index.
    std::vector<std::string> term_by_index_;                          // index -> term.
    std::vector<std::vector<std::uint64_t>> postings_by_term_index_;  // ascending ids.
    // Per-AddDocument scratch: the distinct terms of the document being added.
    // Cleared and reused for every document instead of being stored per doc.
    struct FrequencyScratchEntry {
        std::uint32_t term_index;
        std::uint16_t title_tf;
        std::uint16_t url_tf;
    };
    std::vector<FrequencyScratchEntry> frequency_scratch_;
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_MEMTABLE_H_
