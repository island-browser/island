// The immutable, memory-mapped segment reader.
//
// Segment::Open runs the full validation ladder before any query can touch a
// byte, and quarantines the file on ANY failure instead of reporting a
// partially-readable index. Every decode path is bounds-checked and total, so a
// corrupt segment can never cause an out-of-bounds read: the worst case is a
// quarantined file and an index that answers from the MemTable alone.

#ifndef ISLAND_SEARCH_STORE_SEGMENT_H_
#define ISLAND_SEARCH_STORE_SEGMENT_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "search/expected.h"
#include "search/store/segment_header.h"
#include "search/types.h"

namespace island {
namespace search {

// One document as stored in the segment. The two token counts travel with the
// record because BM25-lite scores against per-field lengths, and recomputing
// them would require re-tokenizing at query time -- the one thing the design
// forbids, since ingest and query must share a single tokenizer pass.
struct SegmentDocument {
    StoredDocument document;
    std::uint32_t title_token_count = 0;
    std::uint32_t url_token_count = 0;
};

class Segment {
  public:
    // Maps `path` read-only and validates it. On ANY validation failure the
    // mapping is released, the file is moved beside itself to
    // "<name>.corrupt-<unix_millis>" -- never deleted, so it stays available
    // for post-hoc inspection -- and a SearchError is returned.
    //
    // A missing file is reported as kIoError without quarantining anything.
    [[nodiscard]] static Expected<std::unique_ptr<Segment>, SearchError> Open(
        const std::filesystem::path& path);

    ~Segment();

    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;

    [[nodiscard]] std::uint64_t doc_count() const noexcept { return header_.doc_count; }
    [[nodiscard]] std::uint64_t next_doc_id() const noexcept { return header_.next_doc_id; }
    [[nodiscard]] std::uint64_t first_doc_id() const noexcept { return first_doc_id_; }

    // The corpus average document length, reconstructed from the header's Q16
    // fixed-point form.
    [[nodiscard]] double avg_doc_len() const noexcept;

    [[nodiscard]] std::size_t term_count() const noexcept { return terms_.size(); }

    // Decoded ascending posting list for `term`, or std::nullopt when the term
    // is absent or its posting bytes fail to decode.
    [[nodiscard]] std::optional<std::vector<std::uint64_t>> PostingsFor(
        std::string_view term) const;

    // The record for `id`, or std::nullopt when the id is outside this
    // segment's range or its bytes fail to decode.
    [[nodiscard]] std::optional<SegmentDocument> DocumentAt(DocId id) const;

    // Terms in ascending byte-lexicographic order, as written.
    [[nodiscard]] std::vector<std::string_view> Terms() const;

  private:
    class Mapping;

    struct TermEntry {
        std::string_view term;
        std::uint64_t posting_off = 0;  // Absolute, from file start.
        std::uint64_t posting_len = 0;
    };

    Segment();

    std::unique_ptr<Mapping> mapping_;
    SegmentHeader header_;
    std::uint64_t first_doc_id_ = 1;
    std::vector<TermEntry> terms_;       // Sorted by term.
    std::vector<std::uint64_t> doc_off_;  // Absolute record offsets, dense by id.
};

// Moves `path` to "<name>.corrupt-<unix_millis>" beside itself. Exposed so the
// facade can quarantine a segment it rejects for its own reasons. Returns the
// quarantine path on success, std::nullopt when the move fails.
std::optional<std::filesystem::path> QuarantineSegment(const std::filesystem::path& path);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_STORE_SEGMENT_H_
