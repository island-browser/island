// search_membench: the S0 memory gate.
//
// Builds a deterministic synthetic corpus, drives SearchIndex through
// ingest -> flush -> a representative query mix, samples process RSS at each
// checkpoint, and fails when the growth over baseline exceeds the ceiling.
//
// The gate is on the DELTA from a baseline taken with SearchIndex constructed
// but empty, not on absolute RSS: absolute RSS includes the executable, the
// C++ runtime and the allocator's own arenas, none of which are attributable
// to search. The segment on disk is not counted; its resident mapped pages
// are, which is why the budget targets RSS rather than allocated bytes.
//
// Links island_search only -- no CEF, no GoogleTest.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "search/benchmark_corpus.h"
#include "search/index/search_index.h"
#include "search/mem_sampler.h"
#include "search/types.h"

namespace {

using island::search::MemSampler;
using island::search::SearchIndex;

constexpr std::uint64_t kDefaultCeilingBytes = 32ull * 1024ull * 1024ull;
constexpr std::uint64_t kDefaultDocuments = 100000;
constexpr std::uint64_t kDefaultSeed = 20260810;

struct Options {
    std::uint64_t ceiling_bytes = kDefaultCeilingBytes;
    std::uint64_t documents = kDefaultDocuments;
    std::uint64_t seed = kDefaultSeed;
    std::filesystem::path segment_path;
};

// Returns the highest RSS seen so far, so a transient peak between checkpoints
// is not lost by the final reading alone.
struct Checkpoints {
    std::uint64_t baseline = 0;
    std::uint64_t after_ingest = 0;
    std::uint64_t after_flush = 0;
    std::uint64_t after_query = 0;

    [[nodiscard]] std::uint64_t peak() const {
        std::uint64_t peak = after_ingest;
        if (after_flush > peak) {
            peak = after_flush;
        }
        if (after_query > peak) {
            peak = after_query;
        }
        return peak;
    }

    // Growth attributable to search. Saturates at zero: RSS can dip below the
    // baseline when the allocator returns pages, and negative growth is not a
    // meaningful measurement.
    [[nodiscard]] std::uint64_t delta() const {
        const std::uint64_t high = peak();
        return high > baseline ? high - baseline : 0;
    }
};

bool ParseU64(std::string_view text, std::uint64_t* out) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

void PrintUsage() {
    std::fprintf(stderr,
                 "usage: search_membench [--ceiling-bytes N] [--documents N] [--seed N]\n"
                 "                       [--segment-path PATH]\n");
}

bool ParseArgs(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        const auto needs_value = [&](std::uint64_t* target) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "search_membench: %.*s needs a value\n",
                             static_cast<int>(flag.size()), flag.data());
                return false;
            }
            if (!ParseU64(argv[++i], target)) {
                std::fprintf(stderr, "search_membench: %.*s needs a non-negative integer\n",
                             static_cast<int>(flag.size()), flag.data());
                return false;
            }
            return true;
        };

        if (flag == "--ceiling-bytes") {
            if (!needs_value(&options->ceiling_bytes)) {
                return false;
            }
        } else if (flag == "--documents") {
            if (!needs_value(&options->documents)) {
                return false;
            }
        } else if (flag == "--seed") {
            if (!needs_value(&options->seed)) {
                return false;
            }
        } else if (flag == "--segment-path") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "search_membench: --segment-path needs a value\n");
                return false;
            }
            options->segment_path = argv[++i];
        } else if (flag == "--help" || flag == "-h") {
            PrintUsage();
            return false;
        } else {
            std::fprintf(stderr, "search_membench: unknown argument %.*s\n",
                         static_cast<int>(flag.size()), flag.data());
            PrintUsage();
            return false;
        }
    }
    if (options->documents == 0) {
        std::fprintf(stderr, "search_membench: --documents must be at least 1\n");
        return false;
    }
    return true;
}

// The corpus vocabulary is generated from a hash, so query terms cannot be
// hard-coded: they are sampled from documents the run actually ingested. A mix
// that matched nothing would leave the post-query checkpoint measuring an
// untouched index, which is precisely the memory the gate exists to observe.
std::vector<std::string> BuildQueryMix(const std::vector<std::string>& sampled_titles) {
    std::vector<std::string> mix;
    for (const std::string& title : sampled_titles) {
        const std::size_t space = title.find(' ');
        if (space == std::string::npos) {
            mix.push_back(title);
            continue;
        }
        // One single-term query and one two-term conjunction per sample.
        mix.push_back(title.substr(0, space));
        const std::size_t second = title.find(' ', space + 1);
        mix.push_back(second == std::string::npos ? title : title.substr(0, second));
    }
    // A term the vocabulary cannot contain: the generator emits hex digits only.
    mix.emplace_back("zzz_absent_term");
    return mix;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, &options)) {
        return 2;
    }

    std::error_code ec;
    if (options.segment_path.empty()) {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path(ec) /
            ("island_search_membench_" + std::to_string(static_cast<unsigned long long>(
                                             options.seed)));
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::fprintf(stderr, "search_membench: cannot create %s: %s\n", dir.string().c_str(),
                         ec.message().c_str());
            return 2;
        }
        options.segment_path = dir / "membench.seg";
    }
    // A stale segment from a previous run would be mapped at Open and make the
    // baseline non-empty, so start from a clean slate.
    std::filesystem::remove(options.segment_path, ec);

    SearchIndex::Options index_options;
    index_options.segment_path = options.segment_path;
    auto opened = SearchIndex::Open(index_options);
    if (!opened.has_value()) {
        std::fprintf(stderr, "search_membench: SearchIndex::Open failed: %s\n",
                     opened.error().detail.c_str());
        return 2;
    }
    SearchIndex& index = *opened.value();

    Checkpoints checkpoints;
    checkpoints.baseline = MemSampler::ResidentBytes();

    island::search::bench::CorpusConfig corpus;
    corpus.seed = options.seed;
    corpus.document_count = options.documents;
    corpus.vocabulary_size = 4096;
    corpus.min_title_tokens = 3;
    corpus.max_title_tokens = 12;
    corpus.min_url_path_tokens = 1;
    corpus.max_url_path_tokens = 5;
    if (!island::search::bench::ValidateConfig(corpus)) {
        std::fprintf(stderr, "search_membench: internal corpus configuration is invalid\n");
        return 2;
    }

    std::uint64_t ingested = 0;
    std::uint64_t refused = 0;
    std::vector<std::string> sampled_titles;
    island::search::bench::CorpusGenerator generator(corpus);
    // Streamed rather than materialized: holding 100k records alongside the
    // index would charge the generator's own memory to the search budget.
    generator.ForEach([&](const island::search::bench::CorpusRecord& record) {
        island::search::DocumentInput input;
        input.url = record.url;
        input.title = record.title;
        input.visited_at_ms = record.timestamp;
        if (index.Ingest(input).has_value()) {
            ++ingested;
        } else {
            ++refused;
        }
        // A handful of titles, spread across the corpus, seed the query mix.
        if (sampled_titles.size() < 4 && (record.doc_id % 17389) == 1) {
            sampled_titles.push_back(record.title);
        }
    });
    checkpoints.after_ingest = MemSampler::ResidentBytes();

    const auto flushed = index.Flush();
    if (!flushed.has_value()) {
        std::fprintf(stderr, "search_membench: Flush failed: %s\n",
                     flushed.error().detail.c_str());
        return 2;
    }
    checkpoints.after_flush = MemSampler::ResidentBytes();

    std::uint64_t hits = 0;
    for (const std::string& text : BuildQueryMix(sampled_titles)) {
        island::search::Query query;
        query.text = text;
        query.max_results = 20;
        hits += index.Query(query, /*query_now_ms=*/options.documents + 1).hits.size();
    }
    checkpoints.after_query = MemSampler::ResidentBytes();

    const std::uint64_t delta = checkpoints.delta();
    const bool within_ceiling = delta <= options.ceiling_bytes;

    // One machine-readable line, stable field order, so CI can assert on it
    // without parsing prose.
    std::printf(
        "membench documents=%llu ingested=%llu refused=%llu hits=%llu baseline_bytes=%llu "
        "ingest_bytes=%llu flush_bytes=%llu query_bytes=%llu peak_bytes=%llu delta_bytes=%llu "
        "ceiling_bytes=%llu result=%s\n",
        static_cast<unsigned long long>(options.documents),
        static_cast<unsigned long long>(ingested), static_cast<unsigned long long>(refused),
        static_cast<unsigned long long>(hits),
        static_cast<unsigned long long>(checkpoints.baseline),
        static_cast<unsigned long long>(checkpoints.after_ingest),
        static_cast<unsigned long long>(checkpoints.after_flush),
        static_cast<unsigned long long>(checkpoints.after_query),
        static_cast<unsigned long long>(checkpoints.peak()),
        static_cast<unsigned long long>(delta),
        static_cast<unsigned long long>(options.ceiling_bytes), within_ceiling ? "pass" : "fail");
    std::fflush(stdout);

    std::filesystem::remove(options.segment_path, ec);

    if (MemSampler::ResidentBytes() == 0) {
        // No sampler for this platform: reporting "pass" would assert a budget
        // nothing measured.
        std::fprintf(stderr,
                     "search_membench: no RSS sampler on this platform; refusing to report a "
                     "budget result\n");
        return 3;
    }
    return within_ceiling ? 0 : 1;
}
