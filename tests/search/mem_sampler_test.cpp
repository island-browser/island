#include "search/mem_sampler.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace island {
namespace search {

TEST(MemSampler, ReturnsAStrictlyPositiveResidentByteCount) {
    const std::uint64_t bytes = MemSampler::ResidentBytes();

    EXPECT_GT(bytes, 0U);
}

TEST(MemSampler, GrowsMonotonicallyUnderALargeAllocation) {
    const std::uint64_t before = MemSampler::ResidentBytes();

    std::vector<std::byte> allocation;
    allocation.resize(64U * 1024U * 1024U, std::byte{0});  // 64 MiB, touched.
    const std::uint64_t after = MemSampler::ResidentBytes();

    EXPECT_GE(after, before);
}

}  // namespace search
}  // namespace island