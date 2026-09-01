// Shared value types for the island::search S0 kernel.
//
// DocId is the rank module's own monotonic std::uint64_t identity (declared by
// search/ranker.h); S0 mints its own ids, never derives them from a URL hash or
// any browser identity, and reserves DocId 0 as the null id.

#ifndef ISLAND_SEARCH_TYPES_H_
#define ISLAND_SEARCH_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "search/ranker.h"

namespace island {
namespace search {

// The invalid/null document id. Never assigned by any minter.
inline constexpr DocId kDocIdNull = 0;

// Input for one ingestion. `visited_at_ms` is caller-supplied epoch
// milliseconds (the recency signal); the index never reads a clock.
// `partition_tag` is reserved and opaque in S0.
struct DocumentInput {
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms = 0;
    std::optional<std::string> partition_tag;
};

// One stored document.
struct StoredDocument {
    DocId id = kDocIdNull;
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms = 0;
    std::optional<std::string> partition_tag;
};

// One posting: a document containing a term.
struct Posting {
    DocId doc = kDocIdNull;
    std::uint32_t term_frequency = 0;
};

// Query input. `text` is tokenized by the single shared tokenizer; `max_results`
// hard-caps the returned hits. `partition_tag` is reserved in S0.
struct Query {
    std::string text;
    std::size_t max_results = 20;
    std::optional<std::string> partition_tag;
};

// One ranked result row.
struct SearchHit {
    DocId doc = kDocIdNull;
    std::string url;
    std::string title;
    double score = 0.0;
};

// A ranked answer. `Query` is total, so this is returned by value, never an
// error. `scanned_terms` counts distinct query tokens after tokenization.
struct SearchResult {
    std::vector<SearchHit> hits;
    std::size_t scanned_terms = 0;
};

// Typed facade errors. No exceptions cross the facade; fallible operations
// return Expected<T, SearchError> (search/expected.h).
enum class SearchErrorKind : std::uint8_t {
    kIoError,
    kCorruptSegment,
    kVersionMismatch,
    kInvalidInput,
    kRefusedForPrivacy,
    kBudgetError,
};

struct SearchError {
    SearchErrorKind kind = SearchErrorKind::kIoError;
    std::string detail;
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_TYPES_H_
