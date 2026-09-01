// The mutable, in-RAM index: a dense DocId -> document store, a per-term
// ascending posting list, the per-doc field term frequencies, and the corpus
// statistics the ranker needs. Postings hold DocIds only; term frequencies are
// stored in the document record because the committed posting codec is
// id-only and the committed ranker consumes per-field frequencies.

#ifndef ISLAND_SEARCH_MEMTABLE_H_
#define ISLAND_SEARCH_MEMTABLE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "search/types.h"

namespace island {
namespace search {

// Per-doc, per-term frequencies for both fields.
struct TermFrequencyEntry {
    std::uint32_t term_index = 0;
    std::uint16_t title_tf = 0;
    std::uint16_t url_tf = 0;
};

// Read view of one stored record.
struct MemTableRecord {
    const StoredDocument* document = nullptr;
    std::uint32_t title_token_count = 0;
    std::uint32_t url_token_count = 0;
    std::span<const TermFrequencyEntry> term_frequencies;
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
    // assigned DocId.
    DocId AddDocument(StoredDocument document, std::span<const std::string> title_tokens,
                      std::span<const std::string> url_tokens);

    [[nodiscard]] std::uint64_t next_doc_id() const noexcept { return next_doc_id_; }
    [[nodiscard]] std::size_t doc_count() const noexcept { return records_.size(); }
    [[nodiscard]] std::uint64_t total_token_count() const noexcept { return total_tokens_; }
    [[nodiscard]] double avg_doc_length() const noexcept {
        return records_.empty() ? 0.0
                                  : static_cast<double>(total_tokens_) /
                                        static_cast<double>(records_.size());
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
    struct StoredRecord {
        StoredDocument document;
        std::uint32_t title_token_count = 0;
        std::uint32_t url_token_count = 0;
        std::vector<TermFrequencyEntry> term_frequencies;
    };

    std::uint32_t TermIndexFor(std::string_view term);

    // records_ is dense from first_doc_id_, so RecordAt maps id -> id - first_doc_id_.
    std::uint64_t first_doc_id_;
    std::uint64_t next_doc_id_;
    std::uint64_t total_tokens_ = 0;
    std::vector<StoredRecord> records_;                                   // dense by id.
    std::unordered_map<std::string, std::uint32_t> term_index_;           // term -> index.
    std::vector<std::string> term_by_index_;                              // index -> term.
    std::vector<std::vector<std::uint64_t>> postings_by_term_index_;      // ascending ids.
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_MEMTABLE_H_
