// The memory gate's verdict: a failed RSS sample must be reported as an
// error, never as a pass.

#include "search/bench/membench_checkpoints.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace island {
namespace search {
namespace bench {
namespace {

constexpr std::uint64_t kCeiling = 32ull * 1024ull * 1024ull;

MembenchCheckpoints Sampled(std::uint64_t baseline, std::uint64_t ingest, std::uint64_t flush,
                            std::uint64_t query) {
    MembenchCheckpoints checkpoints;
    checkpoints.baseline = baseline;
    checkpoints.after_ingest = ingest;
    checkpoints.after_flush = flush;
    checkpoints.after_query = query;
    return checkpoints;
}

TEST(MembenchCheckpoints, PassesWithinTheCeiling) {
    const MembenchCheckpoints checkpoints = Sampled(1000, 2000, 5000, 3000);
    EXPECT_EQ(checkpoints.peak(), 5000u);
    EXPECT_EQ(checkpoints.delta(), 4000u);
    EXPECT_EQ(Judge(checkpoints, kCeiling), MembenchResult::kPass);
    EXPECT_EQ(std::string(ResultName(MembenchResult::kPass)), "pass");
}

TEST(MembenchCheckpoints, FailsOverTheCeiling) {
    const MembenchCheckpoints checkpoints = Sampled(1000, 1000 + kCeiling + 1, 2000, 2000);
    EXPECT_EQ(Judge(checkpoints, kCeiling), MembenchResult::kFail);
    EXPECT_EQ(std::string(ResultName(MembenchResult::kFail)), "fail");
}

TEST(MembenchCheckpoints, AnyFailedSampleIsAnErrorNotAPass) {
    // Every zero here used to read as "no growth" and print result=pass.
    EXPECT_EQ(Judge(Sampled(0, 0, 0, 0), kCeiling), MembenchResult::kError);
    EXPECT_EQ(Judge(Sampled(0, 2000, 2000, 2000), kCeiling), MembenchResult::kError);
    EXPECT_EQ(Judge(Sampled(1000, 0, 2000, 2000), kCeiling), MembenchResult::kError);
    EXPECT_EQ(Judge(Sampled(1000, 2000, 0, 2000), kCeiling), MembenchResult::kError);
    EXPECT_EQ(Judge(Sampled(1000, 2000, 2000, 0), kCeiling), MembenchResult::kError);
    EXPECT_EQ(std::string(ResultName(MembenchResult::kError)), "error");
}

TEST(MembenchCheckpoints, ADipBelowBaselineIsZeroGrowth) {
    const MembenchCheckpoints checkpoints = Sampled(5000, 4000, 4500, 4800);
    EXPECT_EQ(checkpoints.delta(), 0u);
    EXPECT_EQ(Judge(checkpoints, kCeiling), MembenchResult::kPass);
}

}  // namespace
}  // namespace bench
}  // namespace search
}  // namespace island
