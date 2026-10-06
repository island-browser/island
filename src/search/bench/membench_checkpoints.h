// The checkpoint arithmetic and the pass/fail/error verdict of
// search_membench, header-only so the unit suite can pin them without running
// the benchmark.

#ifndef ISLAND_SEARCH_BENCH_MEMBENCH_CHECKPOINTS_H_
#define ISLAND_SEARCH_BENCH_MEMBENCH_CHECKPOINTS_H_

#include <cstdint>

namespace island {
namespace search {
namespace bench {

// RSS samples at the gate's four checkpoints. 0 is what MemSampler returns
// when it cannot sample, so it marks a failed reading, never a measurement.
struct MembenchCheckpoints {
    std::uint64_t baseline = 0;
    std::uint64_t after_ingest = 0;
    std::uint64_t after_flush = 0;
    std::uint64_t after_query = 0;

    // True only when every checkpoint produced a reading. A missing one would
    // otherwise read as "no growth" and let the gate pass on nothing.
    [[nodiscard]] bool AllSampled() const noexcept {
        return baseline != 0 && after_ingest != 0 && after_flush != 0 && after_query != 0;
    }

    // The highest post-baseline sample, so a transient peak between
    // checkpoints is not lost by the final reading alone.
    [[nodiscard]] std::uint64_t peak() const noexcept {
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
    [[nodiscard]] std::uint64_t delta() const noexcept {
        const std::uint64_t high = peak();
        return high > baseline ? high - baseline : 0;
    }
};

enum class MembenchResult {
    kPass,   // Every sample taken and the delta is within the ceiling.
    kFail,   // Every sample taken and the delta exceeds the ceiling.
    kError,  // A sample failed, so there is no measurement to judge.
};

[[nodiscard]] inline MembenchResult Judge(const MembenchCheckpoints& checkpoints,
                                          std::uint64_t ceiling_bytes) noexcept {
    if (!checkpoints.AllSampled()) {
        return MembenchResult::kError;
    }
    return checkpoints.delta() <= ceiling_bytes ? MembenchResult::kPass : MembenchResult::kFail;
}

[[nodiscard]] inline const char* ResultName(MembenchResult result) noexcept {
    switch (result) {
        case MembenchResult::kPass:
            return "pass";
        case MembenchResult::kFail:
            return "fail";
        case MembenchResult::kError:
            return "error";
    }
    return "error";
}

}  // namespace bench
}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_BENCH_MEMBENCH_CHECKPOINTS_H_
