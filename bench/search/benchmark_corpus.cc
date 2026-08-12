#include "search/benchmark_corpus.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace island {
namespace search {
namespace bench {
namespace {

// Fixed delta between consecutive records' timestamps (milliseconds). Keeps
// timestamps deterministic and strictly increasing with the record index.
constexpr std::uint64_t kTimestampStepMs = 1000;
constexpr std::uint64_t kTimestampEpochMs = 1760000000ULL * 1000ULL;

// Fixed host suffix. The numeric label is derived from the seed so different
// seeds exercise different domains without ever exceeding the configured
// bounds.
constexpr std::string_view kHostSuffix = ".test";

// Lowercase hex digits. Using these exclusively keeps every generated word a
// pure-lowercase ASCII string: no credentials, no control characters.
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::size_t kWordLength = 16;

// splitmix64 finalizer: a well-known bijective avalanche that maps a counter
// to a deterministic, well-distributed pseudo-random value.
std::uint64_t SplitMix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

}  // namespace

bool ValidateConfig(const CorpusConfig& config) {
    if (config.document_count < 1) {
        return false;
    }
    if (config.vocabulary_size < 1) {
        return false;
    }
    const bool title_ok = config.min_title_tokens >= 1 &&
                          config.max_title_tokens >= config.min_title_tokens &&
                          config.max_title_tokens <= config.vocabulary_size &&
                          config.max_title_tokens <= kMaxTokensPerRecord;
    const bool path_ok = config.min_url_path_tokens >= 1 &&
                         config.max_url_path_tokens >= config.min_url_path_tokens &&
                         config.max_url_path_tokens <= config.vocabulary_size &&
                         config.max_url_path_tokens <= kMaxTokensPerRecord;
    return title_ok && path_ok;
}

CorpusGenerator::CorpusGenerator(const CorpusConfig& config) : config_(config) {
    if (!ValidateConfig(config)) {
        throw std::invalid_argument("invalid CorpusConfig");
    }
    state_ = SplitMix64(config_.seed);
}

std::uint64_t CorpusGenerator::Rand() {
    state_ = SplitMix64(state_);
    return state_;
}

std::string CorpusGenerator::Word(std::uint64_t index) const {
    std::string word;
    word.reserve(kWordLength);
    std::uint64_t x = SplitMix64(index + 1);
    for (std::size_t i = 0; i < kWordLength; ++i) {
        word.push_back(kHexDigits[x & 0xF]);
        x = SplitMix64(x);
    }
    return word;
}

std::vector<std::uint64_t> CorpusGenerator::SampleTokens(std::uint32_t count) {
    std::vector<std::uint64_t> tokens;
    tokens.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        tokens.push_back(Rand() % config_.vocabulary_size);
    }
    return tokens;
}

std::string CorpusGenerator::Host() const {
    const std::uint64_t label =
        1 + (SplitMix64(config_.seed ^ 0xDEADBEEFCAFEBABEULL) % config_.vocabulary_size);
    return "https://example-" + std::to_string(label) + std::string(kHostSuffix);
}

std::string CorpusGenerator::Title(std::uint32_t token_count) {
    std::string title;
    const std::vector<std::uint64_t> tokens = SampleTokens(token_count);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            title.push_back(' ');
        }
        title += Word(tokens[i]);
    }
    return title;
}

std::string CorpusGenerator::UrlPath(std::uint32_t token_count) {
    std::string path;
    const std::vector<std::uint64_t> tokens = SampleTokens(token_count);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        path.push_back('/');
        path += Word(tokens[i]);
    }
    return path;
}

std::uint64_t CorpusGenerator::Timestamp() const {
    return kTimestampEpochMs + index_ * kTimestampStepMs;
}

CorpusRecord CorpusGenerator::MakeRecord() {
    CorpusRecord record;
    record.doc_id = index_;
    record.timestamp = Timestamp();
    const std::uint32_t title_count =
        config_.min_title_tokens +
        Rand() % (config_.max_title_tokens - config_.min_title_tokens + 1);
    const std::uint32_t path_count =
        config_.min_url_path_tokens +
        Rand() % (config_.max_url_path_tokens - config_.min_url_path_tokens + 1);
    record.title = Title(title_count);
    record.url = Host() + UrlPath(path_count);
    return record;
}

bool CorpusGenerator::Next(CorpusRecord* out) {
    if (out == nullptr || index_ >= config_.document_count) {
        return false;
    }
    *out = MakeRecord();
    ++index_;
    return true;
}

std::vector<CorpusRecord> CorpusGenerator::GenerateAll() {
    std::vector<CorpusRecord> records;
    records.reserve(config_.document_count);
    CorpusRecord record;
    while (Next(&record)) {
        records.push_back(std::move(record));
    }
    return records;
}

std::size_t CorpusGenerator::Count() const {
    return static_cast<std::size_t>(config_.document_count);
}

std::size_t CorpusGenerator::Remaining() const {
    const std::uint64_t remaining =
        config_.document_count > index_ ? config_.document_count - index_ : 0;
    return static_cast<std::size_t>(remaining);
}

}  // namespace bench
}  // namespace search
}  // namespace island