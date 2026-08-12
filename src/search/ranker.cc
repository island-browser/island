#include "search/ranker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace island {
namespace search {
namespace {

// Returns idf for a term seen in `document_frequency` of `num_docs`
// documents. Clamped into [0, kMaxIdf] and never NaN/inf.
double Idf(std::size_t num_docs, std::size_t document_frequency) {
    if (num_docs == 0) {
        return 0.0;
    }
    const double df = static_cast<double>(document_frequency);
    const double n = static_cast<double>(num_docs);
    const double ratio = (n - df + 0.5) / (df + 0.5);
    const double raw = std::log(1.0 + std::max(0.0, ratio));
    constexpr double kMaxIdf = 5.0;
    return std::min(raw, kMaxIdf);
}

// Returns the bounded recency boost for `age_seconds`. 0 when age is negative
// (treated as newest), decaying from recency_max_boost by half-life, and
// clamped to recency_max_boost. Never NaN/inf.
double RecencyBoost(double age_seconds, const RankingWeights& weights) {
    const double age = std::max(0.0, age_seconds);
    const double half_life = weights.recency_half_life;
    if (!(half_life > 0.0)) {
        return 0.0;
    }
    const double boost = weights.recency_max_boost * std::pow(0.5, age / half_life);
    return std::min(std::max(0.0, boost), std::max(0.0, weights.recency_max_boost));
}

// Sums the raw term-frequency contribution of `term` across both fields,
// weighted by field. `FieldFrequencies` has no transparent hash, so the key is
// a std::string (callers pass already-normalized terms).
double FieldWeightedTf(const DocumentStat& doc, const std::string& term,
                       const RankingWeights& weights) {
    double tf = 0.0;
    const auto title_it = doc.title_tf.find(term);
    if (title_it != doc.title_tf.end()) {
        tf += static_cast<double>(title_it->second) * weights.title_weight;
    }
    const auto url_it = doc.url_tf.find(term);
    if (url_it != doc.url_tf.end()) {
        tf += static_cast<double>(url_it->second) * weights.url_weight;
    }
    return tf;
}

// BM25 fractional term-frequency saturation term.
double TfSaturation(double tf, double k1, double b, double doc_length, double avg_doc_length) {
    const double length_ratio = avg_doc_length > 0.0 ? doc_length / avg_doc_length : 1.0;
    const double denominator = tf + k1 * (1.0 - b + b * length_ratio);
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    return tf / denominator;
}

struct ScoredDocBetter {
    bool operator()(const ScoredDoc& a, const ScoredDoc& b) const {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.doc_id < b.doc_id;
    }
};

std::unordered_map<std::string, std::size_t> MakeDocumentFrequencies(
    const std::vector<DocumentStat>& corpus) {
    std::unordered_map<std::string, std::size_t> document_frequencies;

    for (const DocumentStat& doc : corpus) {
        // A term present in both fields of the same doc must count that doc
        // exactly once.
        std::unordered_set<std::string> seen;
        auto record = [&document_frequencies, &seen](const FieldFrequencies& field) {
            for (const auto& [term, count] : field) {
                if (count == 0) {
                    continue;
                }
                if (seen.count(term) != 0) {
                    continue;
                }
                seen.insert(term);
                const auto it = document_frequencies.find(term);
                if (it == document_frequencies.end()) {
                    document_frequencies.emplace(term, 1);
                } else {
                    ++it->second;
                }
            }
        };
        record(doc.title_tf);
        record(doc.url_tf);
    }

    return document_frequencies;
}

double ScoreTermImpl(const DocumentStat& doc, const std::string& term,
                     std::size_t document_frequency, const CorpusStats& stats,
                     const RankingWeights& weights) {
    const double tf = FieldWeightedTf(doc, term, weights);
    if (tf <= 0.0) {
        return 0.0;
    }
    const double doc_length = static_cast<double>(doc.title_length + doc.url_length);
    const double idf = Idf(stats.num_docs, document_frequency);
    const double saturation =
        TfSaturation(tf, weights.k1, weights.b, doc_length, stats.avg_doc_length);
    return idf * saturation;
}

}  // namespace

double ScoreTerm(const DocumentStat& doc, std::string_view term, std::size_t document_frequency,
                 const CorpusStats& stats, const RankingWeights& weights) {
    return ScoreTermImpl(doc, std::string(term), document_frequency, stats, weights);
}

std::size_t ComputeDocumentFrequency(const std::vector<DocumentStat>& corpus,
                                     std::string_view term) {
    const std::string term_key(term);
    std::size_t count = 0;
    for (const DocumentStat& doc : corpus) {
        const auto title_it = doc.title_tf.find(term_key);
        const auto url_it = doc.url_tf.find(term_key);
        const bool in_title = title_it != doc.title_tf.end() && title_it->second > 0;
        const bool in_url = url_it != doc.url_tf.end() && url_it->second > 0;
        if (in_title || in_url) {
            ++count;
        }
    }
    return count;
}

std::vector<ScoredDoc> RankDocuments(const std::vector<DocumentStat>& corpus,
                                     const std::vector<std::string>& terms,
                                     const CorpusStats& stats, const RankingWeights& weights) {
    const std::unordered_map<std::string, std::size_t> document_frequencies =
        MakeDocumentFrequencies(corpus);

    std::vector<ScoredDoc> results;
    results.reserve(corpus.size());
    for (const DocumentStat& doc : corpus) {
        double score = 0.0;
        for (const std::string& term : terms) {
            const auto df_it = document_frequencies.find(term);
            const std::size_t df = df_it != document_frequencies.end() ? df_it->second : 0;
            score += ScoreTermImpl(doc, term, df, stats, weights);
        }
        score += RecencyBoost(doc.age_seconds, weights);
        results.push_back(ScoredDoc{doc.doc_id, score});
    }

    std::stable_sort(results.begin(), results.end(), ScoredDocBetter{});
    return results;
}

std::vector<ScoredDoc> TopKDocuments(const std::vector<DocumentStat>& corpus,
                                     const std::vector<std::string>& terms,
                                     const CorpusStats& stats, const RankingWeights& weights,
                                     std::size_t k) {
    if (k == 0 || corpus.empty()) {
        return {};
    }
    if (k >= corpus.size()) {
        return RankDocuments(corpus, terms, stats, weights);
    }

    const std::unordered_map<std::string, std::size_t> document_frequencies =
        MakeDocumentFrequencies(corpus);

    // Min-heap: the root is the worst of the current best-K, so a better candidate
    // evicts it. Sorted by descending score at the end.
    std::vector<ScoredDoc> heap;
    heap.reserve(k + 1);

    for (const DocumentStat& doc : corpus) {
        double score = 0.0;
        for (const std::string& term : terms) {
            const auto df_it = document_frequencies.find(term);
            const std::size_t df = df_it != document_frequencies.end() ? df_it->second : 0;
            score += ScoreTermImpl(doc, term, df, stats, weights);
        }
        score += RecencyBoost(doc.age_seconds, weights);

        const ScoredDoc candidate{doc.doc_id, score};
        if (heap.size() < k) {
            heap.push_back(candidate);
            std::push_heap(heap.begin(), heap.end(), ScoredDocBetter{});
        } else if (ScoredDocBetter{}(candidate, heap.front())) {
            std::pop_heap(heap.begin(), heap.end(), ScoredDocBetter{});
            heap.back() = candidate;
            std::push_heap(heap.begin(), heap.end(), ScoredDocBetter{});
        }
    }

    std::sort(heap.begin(), heap.end(), ScoredDocBetter{});
    return heap;
}

}  // namespace search
}  // namespace island