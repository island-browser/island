// Self-contained standalone tests for the Search S0 posting-list codec.
// Compiled directly by the harness (see README/test script); not part of the
// gtest tree. Covers exact round-trip, property, and corruption handling.

#include "search/posting_codec.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace island {
namespace search {
namespace {

int g_failures = 0;
int g_checks = 0;

#define EXPECT_TRUE(cond)                                                              \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

#define EXPECT_FALSE(cond)                                                              \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (cond) {                                                                     \
            std::printf("FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                               \
        }                                                                               \
    } while (0)

#define EXPECT_EQ_LL(a, b)                                                                       \
    do {                                                                                         \
        ++g_checks;                                                                              \
        const auto va = (a);                                                                     \
        const auto vb = (b);                                                                     \
        if (!(va == vb)) {                                                                       \
            std::printf("FAIL %s:%d: %s == %s (got %lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
                        static_cast<long long>(va), static_cast<long long>(vb));                 \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

std::vector<std::uint8_t> EncodeOrDie(const std::vector<std::uint64_t>& ids) {
    std::vector<std::uint8_t> out;
    if (!PostingCodecEncode(ids, out)) {
        std::printf("FAIL: encode returned false for ascending input\n");
        ++g_failures;
    }
    return out;
}

void ExpectRoundTrip(const std::vector<std::uint64_t>& ids) {
    const std::vector<std::uint8_t> bytes = EncodeOrDie(ids);
    const PostingDecodeResult result = PostingCodecDecode(bytes);
    EXPECT_EQ_LL(static_cast<int>(result.error), static_cast<int>(PostingCodecError::kOk));
    if (result.error != PostingCodecError::kOk) {
        return;
    }
    EXPECT_EQ_LL(result.consumed, bytes.size());
    if (result.ids.size() != ids.size()) {
        std::printf("FAIL: size %zu vs %zu\n", result.ids.size(), ids.size());
        ++g_failures;
        ++g_checks;
        return;
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (result.ids[i] != ids[i]) {
            std::printf("FAIL: id[%zu] %llu vs %llu\n", i,
                        static_cast<unsigned long long>(result.ids[i]),
                        static_cast<unsigned long long>(ids[i]));
            ++g_failures;
            ++g_checks;
            return;
        }
    }
    ++g_checks;
}

// --- Round-trip: basic shapes ---------------------------------------------

void TestEmpty() { ExpectRoundTrip({}); }

void TestSingle() { ExpectRoundTrip({42}); }

void TestSingleZero() { ExpectRoundTrip({0}); }

void TestMax() { ExpectRoundTrip({UINT64_MAX}); }

void TestMaxOnlySecond() { ExpectRoundTrip({1, UINT64_MAX}); }

void TestMany() {
    std::vector<std::uint64_t> ids;
    constexpr std::uint64_t kCount = 10'000;
    ids.reserve(kCount);
    for (std::uint64_t i = 0; i < kCount; ++i) {
        ids.push_back(i * 3);
    }
    ExpectRoundTrip(ids);
}

void TestLargeGaps() {
    std::vector<std::uint64_t> ids;
    std::uint64_t next = 1;
    for (int i = 0; i < 1000; ++i) {
        ids.push_back(next);
        next += static_cast<std::uint64_t>(i) * 1'000'000'000ULL + 1;
    }
    ExpectRoundTrip(ids);
}

void TestAdversarialDense() { ExpectRoundTrip({4, 5, 6, 7, 8, 9, 10}); }

void TestAdversarialMultiBlockBoundary() {
    // Exactly one block boundary and a straddling list.
    std::vector<std::uint64_t> ids;
    for (std::uint64_t i = 0; i < kPostingBlockSize + 1; ++i) {
        ids.push_back(i);
    }
    ExpectRoundTrip(ids);
}

void TestMultipleParallelLists() {
    // Appending several lists to one buffer must work, and each list must be
    // decodable from its own, externally-known offset. The format is not
    // self-delimiting at the list level, so the caller supplies offsets.
    const std::vector<std::uint64_t> a = {1, 5, 9};
    const std::vector<std::uint64_t> b = {100, 200, 300, 400};
    const std::vector<std::uint64_t> c = {};

    std::vector<std::uint8_t> out;
    EXPECT_TRUE(PostingCodecEncode(a, out));
    EXPECT_TRUE(PostingCodecEncode(b, out));
    EXPECT_TRUE(PostingCodecEncode(c, out));

    // Learn each member's byte length from its own standalone encoding, then
    // verify the concatenation is exactly the sum (appendability).
    std::size_t sum = 0;
    for (const std::vector<std::uint64_t>& list : {a, b, c}) {
        sum += EncodeOrDie(list).size();
    }
    EXPECT_EQ_LL(out.size(), sum);

    // Decode each list from its own subspan in the concatenated buffer.
    std::size_t offset = 0;
    for (const std::vector<std::uint64_t>& expected : {a, b}) {
        const std::size_t own_len = EncodeOrDie(expected).size();
        const auto rest = std::span<const std::uint8_t>(out).subspan(offset, own_len);
        const PostingDecodeResult result = PostingCodecDecode(rest);
        EXPECT_EQ_LL(static_cast<int>(result.error), static_cast<int>(PostingCodecError::kOk));
        if (result.ids == expected) {
            ++g_checks;
        } else {
            std::printf("FAIL: parallel list mismatch\n");
            ++g_failures;
            ++g_checks;
        }
        offset += own_len;
    }
    EXPECT_EQ_LL(offset, out.size());
    ;
}

// --- Contract violations ----------------------------------------------------

void TestRejectsDescending() {
    std::vector<std::uint8_t> out;
    // Encode must reject and leave `out` unchanged.
    out.push_back(0xAB);
    const std::vector<std::uint64_t> descending = {5, 3};
    EXPECT_FALSE(PostingCodecEncode(descending, out));
    EXPECT_TRUE(out.size() == 1 && out[0] == 0xAB);
}

void TestRejectsDuplicate() {
    std::vector<std::uint8_t> out;
    const std::vector<std::uint64_t> duplicate = {7, 7};
    EXPECT_FALSE(PostingCodecEncode(duplicate, out));
}

// --- Corruption / bounds ----------------------------------------------------

void ExpectError(const std::vector<std::uint8_t>& bytes, PostingCodecError expected) {
    const PostingDecodeResult result = PostingCodecDecode(bytes);
    EXPECT_EQ_LL(static_cast<int>(result.error), static_cast<int>(expected));
    if (result.error != PostingCodecError::kOk) {
        EXPECT_TRUE(result.ids.empty());
    }
}

void TestEmptyInputIsEmptyList() {
    const PostingDecodeResult result = PostingCodecDecode({});
    EXPECT_EQ_LL(static_cast<int>(result.error), static_cast<int>(PostingCodecError::kOk));
    EXPECT_TRUE(result.ids.empty());
    EXPECT_EQ_LL(result.consumed, 0u);
}

void TestHeaderOnlyTruncated() {
    // Count byte present but no delta bytes follow.
    ExpectError({0x01}, PostingCodecError::kTruncated);
}

void TestPartialVarintTruncated() {
    // Count=1, then a varint whose continuation bit is set but the stream ends.
    ExpectError({0x01, 0x80}, PostingCodecError::kTruncated);
}

void TestCorruptZeroCount() { ExpectError({0x00}, PostingCodecError::kCorrupt); }

void TestCorruptOverflowingVarint() {
    // 10 continuation bytes forcing overflow beyond 64 bits.
    ExpectError({0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                PostingCodecError::kCorrupt);
}

void TestCorruptZeroDelta() {
    // Count=2, deltas 1 then 0 -> duplicate {1,1}; strictly ascending violated.
    ExpectError({0x02, 0x01, 0x00}, PostingCodecError::kCorrupt);
}

void TestCorruptAccumulatedOverflow() {
    // Count=2, first delta=1, second delta=UINT64_MAX -> overflow.
    std::vector<std::uint8_t> bytes = {0x02, 0x01};
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0xFF);
    bytes.push_back(0x01);  // 10th byte low bits -> UINT64_MAX
    ExpectError(bytes, PostingCodecError::kCorrupt);
}

void TestTruncatedSecondBlock() {
    // Encode two blocks worth, then clip mid stream.
    std::vector<std::uint64_t> ids;
    for (std::uint64_t i = 0; i < kPostingBlockSize + 10; ++i) {
        ids.push_back(i);
    }
    const std::vector<std::uint8_t> bytes = EncodeOrDie(ids);
    if (bytes.size() < 2) {
        return;
    }
    const auto prefix = std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + bytes.size() - 1);
    ExpectError(prefix, PostingCodecError::kTruncated);
}

void TestAllZeroBytes() { ExpectError({0x00, 0x00, 0x00}, PostingCodecError::kCorrupt); }

void TestGarbageByte() {
    // Count=2 but the second delta is zero -> duplicate, corrupt.
    ExpectError({0x02, 0x05, 0x00}, PostingCodecError::kCorrupt);
}

// --- Property / randomized -------------------------------------------------

std::uint64_t SplitMix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void TestPropertyRandomRoundTrips() {
    std::uint64_t seed = 0xDEADBEEFCAFEBABEULL;
    for (int trial = 0; trial < 200; ++trial) {
        const std::size_t count = static_cast<std::size_t>(SplitMix64(seed) % 300);
        std::vector<std::uint64_t> ids;
        ids.reserve(count);
        std::uint64_t next = 0;
        for (std::size_t i = 0; i < count; ++i) {
            // Wide random gaps, sometimes contiguous, sometimes huge.
            const std::uint64_t gap = 1 + SplitMix64(seed) % 100;
            next += gap;
            ids.push_back(next);
        }
        ExpectRoundTrip(ids);
    }
}

}  // namespace

int RunAll() {
    TestEmpty();
    TestSingle();
    TestSingleZero();
    TestMax();
    TestMaxOnlySecond();
    TestMany();
    TestLargeGaps();
    TestAdversarialDense();
    TestAdversarialMultiBlockBoundary();
    TestMultipleParallelLists();

    TestRejectsDescending();
    TestRejectsDuplicate();

    TestEmptyInputIsEmptyList();
    TestHeaderOnlyTruncated();
    TestPartialVarintTruncated();
    TestCorruptZeroCount();
    TestCorruptOverflowingVarint();
    TestCorruptZeroDelta();
    TestCorruptAccumulatedOverflow();
    TestTruncatedSecondBlock();
    TestAllZeroBytes();
    TestGarbageByte();

    TestPropertyRandomRoundTrips();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace search
}  // namespace island

int main() { return island::search::RunAll(); }