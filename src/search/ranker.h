#ifndef ISLAND_SEARCH_RANKER_H_
#define ISLAND_SEARCH_RANKER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace island {
namespace search {

// Stable, arbitrary document identifier used to break score ties.
using DocId = std::uint64_t;

// Tuning constants for BM25-lite. All values are stored as doubles; using the
// default-constructed struct yields the standard BM25 defaults.
struct RankingWeights {
    double k1 = 1.2;            // BM25 term-frequency saturation.
    double b = 0.75;            // BM25 length-normalization exponent.
    double title_weight = 1.0;  // Multiplier on title term frequency.
    double url_weight = 0.5;    // Multiplier on url term frequency.
    // Recency: boost = recency_max_boost when age == 0, halving every
    // recency_half_life seconds. Always bounded by recency_max_boost.
    double recency_max_boost = 0.3;
    double recency_half_life = 30.0 * 24.0 * 3600.0;  // 30 days, in seconds.
};

// Corpus-level statistics used by the length-normalization term.
struct CorpusStats {
    std::size_t num_docs = 0;
    double avg_doc_length = 0.0;
};

// Per-field term frequency for one document.
using FieldFrequencies = std::unordered_map<std::string, std::size_t>;

// Lightweight per-document input to ranking. `title_tf`/`url_tf` map a
// normalized term to its count in the corresponding field. `age_seconds` is
// the document's age (0 == newest); it drives the bounded recency boost.
struct DocumentStat {
    DocId doc_id = 0;
    FieldFrequencies title_tf;
    FieldFrequencies url_tf;
    std::size_t title_length = 0;
    std::size_t url_length = 0;
    double age_seconds = 0.0;
};

// Result of scoring/ranking a single document.
struct ScoredDoc {
    DocId doc_id = 0;
    double score = 0.0;
};

// Computes the BM25-lite score of one document against a single term with the
// given document frequency, using only the supplied stats and weights. Never
// returns NaN or infinity for any input; semantically absent terms score 0.
double ScoreTerm(const DocumentStat& doc, std::string_view term, std::size_t document_frequency,
                 const CorpusStats& stats, const RankingWeights& weights);

// Counts how many documents in `corpus` contain `term` in either field.
// Used to derive document frequency for idf. Never throws.
std::size_t ComputeDocumentFrequency(const std::vector<DocumentStat>& corpus,
                                     std::string_view term);

// Scores every document in `corpus` against `terms` (already-normalized query
// terms) and returns them sorted by descending score, ties broken by ascending
// DocId. Deterministic; no hidden state. Never returns NaN/inf scores.
std::vector<ScoredDoc> RankDocuments(const std::vector<DocumentStat>& corpus,
                                     const std::vector<std::string>& terms,
                                     const CorpusStats& stats, const RankingWeights& weights);

// Same scoring as RankDocuments, but returns at most `k` highest-scoring docs
// without sorting the full corpus when `k` is small (bounded binary heap). For
// k == 0 returns an empty vector; for k >= corpus size returns the full corpus
// ranked. Ties broken by ascending DocId.
std::vector<ScoredDoc> TopKDocuments(const std::vector<DocumentStat>& corpus,
                                     const std::vector<std::string>& terms,
                                     const CorpusStats& stats, const RankingWeights& weights,
                                     std::size_t k);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_RANKER_H_