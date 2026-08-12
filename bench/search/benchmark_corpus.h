// Deterministic synthetic corpus generation for search memory/performance
// benchmarks.
//
// This is a pure C++20 generator: for a fixed seed and configuration it always
// produces the same sequence of records, so benchmark runs are reproducible.
// The streaming API (Next / ForEach) keeps O(1) generator state with respect
// to the document count, so a 100k-document corpus can be generated without
// ever holding all records in memory.
//
// Generated URLs are absolute "https://example-<bounded>.test/..." documents
// with no credentials and no control characters. Document ids and timestamps
// are deterministic and monotone increasing.

#ifndef ISLAND_BENCH_SEARCH_BENCHMARK_CORPUS_H_
#define ISLAND_BENCH_SEARCH_BENCHMARK_CORPUS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace island {
namespace search {
namespace bench {

// Hard cap on the number of tokens in a single generated title or URL path,
// independent of the configured vocabulary size. Keeps generated records
// bounded for any configuration.
constexpr std::uint32_t kMaxTokensPerRecord = 64;

struct CorpusConfig {
    std::uint64_t seed = 0;
    std::uint64_t document_count = 0;
    std::uint32_t vocabulary_size = 0;
    std::uint32_t min_title_tokens = 0;
    std::uint32_t max_title_tokens = 0;
    std::uint32_t min_url_path_tokens = 0;
    std::uint32_t max_url_path_tokens = 0;
};

struct CorpusRecord {
    std::uint64_t doc_id = 0;
    std::string url;
    std::string title;
    std::uint64_t timestamp = 0;
};

// Returns true when the configuration is usable:
//   * document_count >= 1
//   * vocabulary_size >= 1
//   * 1 <= min_title_tokens <= max_title_tokens <= vocabulary_size
//   * 1 <= min_url_path_tokens <= max_url_path_tokens <= vocabulary_size
//   * max_title_tokens and max_url_path_tokens <= kMaxTokensPerRecord
bool ValidateConfig(const CorpusConfig& config);

// Streaming, deterministic corpus generator. Construction fails with
// std::invalid_argument when the configuration is not valid
// (see ValidateConfig).
class CorpusGenerator {
  public:
    explicit CorpusGenerator(const CorpusConfig& config);

    // Produces the next record into *out and returns true, or returns false
    // once the configured document_count has been reached. *out is only
    // modified on success. This is the O(1)-memory streaming entry point.
    bool Next(CorpusRecord* out);

    // Invokes fn(record) for every remaining record, in order. The callback
    // may consume each record without the generator retaining it.
    template <typename Fn>
    void ForEach(Fn&& fn) {
        CorpusRecord record;
        while (Next(&record)) {
            fn(record);
        }
    }

    // Convenience that materializes every remaining record into a vector.
    // This intentionally accumulates memory and is provided for callers that
    // want the whole corpus at once; streaming callers should use Next/ForEach.
    std::vector<CorpusRecord> GenerateAll();

    // Total number of records this generator will produce.
    std::size_t Count() const;

    // Number of records not yet produced by Next/ForEach/GenerateAll.
    std::size_t Remaining() const;

  private:
    CorpusConfig config_;
    std::uint64_t index_ = 0;
    std::uint64_t state_ = 0;

    std::uint64_t Rand();
    // Deterministic pseudo-word for a vocabulary index in [0, vocabulary_size).
    std::string Word(std::uint64_t index) const;
    // Deterministic, distinct token indices for a given record/position.
    std::vector<std::uint64_t> SampleTokens(std::uint32_t count);
    std::string Host() const;
    std::string Title(std::uint32_t token_count);
    std::string UrlPath(std::uint32_t token_count);
    std::uint64_t Timestamp() const;
    CorpusRecord MakeRecord();
};

}  // namespace bench
}  // namespace search
}  // namespace island

#endif  // ISLAND_BENCH_SEARCH_BENCHMARK_CORPUS_H_