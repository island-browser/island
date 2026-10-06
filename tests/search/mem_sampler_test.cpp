#include "search/mem_sampler.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

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

// The memory budget charges the mmap'd segment's resident pages to search, so
// the sampler must see clean file-backed pages. macOS phys_footprint does not:
// it reported almost no growth here, which is what this test pins against.
TEST(MemSampler, CountsTheResidentPagesOfAMappedFile) {
#if defined(_WIN32)
    GTEST_SKIP() << "WorkingSetSize counts mapped pages by definition; the mapping half of this "
                    "test is POSIX-only";
#else
    constexpr std::size_t kFileBytes = 32U * 1024U * 1024U;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("island_mem_sampler_" +
         std::to_string(
             static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())) +
         ".bin");
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::vector<char> chunk(1U << 20, 'x');
        for (std::size_t written = 0; written < kFileBytes; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
        ASSERT_TRUE(file.good());
    }

    const int fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    void* const view = ::mmap(nullptr, kFileBytes, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    ASSERT_NE(view, MAP_FAILED);

    const std::uint64_t before = MemSampler::ResidentBytes();
    const auto* bytes = static_cast<const volatile char*>(view);
    std::uint64_t sum = 0;
    for (std::size_t offset = 0; offset < kFileBytes; offset += 4096) {
        sum += static_cast<unsigned char>(bytes[offset]);
    }
    const std::uint64_t after = MemSampler::ResidentBytes();

    ::munmap(view, kFileBytes);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    EXPECT_EQ(sum, static_cast<std::uint64_t>('x') * (kFileBytes / 4096));
    ASSERT_GT(after, before);
    EXPECT_GE(after - before, kFileBytes / 2) << "before=" << before << " after=" << after;
#endif
}

}  // namespace search
}  // namespace island
