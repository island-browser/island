#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "search/crc32c.h"
#include "search/varint.h"

namespace island {
namespace search {
namespace {

using std::size_t;

TEST(Varint, RoundTripsBoundaryValues) {
    const std::uint64_t values[] = {0,
                                    1,
                                    127,
                                    128,
                                    255,
                                    256,
                                    16383,
                                    16384,
                                    0xFFFF,
                                    0xFFFFFFFFull,
                                    0xFFFFFFFFFFFFFFFFull,
                                    0x8000000000000000ull};
    for (const std::uint64_t value : values) {
        std::vector<std::uint8_t> bytes;
        AppendVarint(value, bytes);
        std::size_t cursor = 0;
        const std::optional<std::uint64_t> decoded = DecodeVarint(bytes, &cursor);
        ASSERT_TRUE(decoded.has_value()) << "value " << value;
        EXPECT_EQ(*decoded, value);
        EXPECT_EQ(cursor, bytes.size());
    }
}

TEST(Varint, EncodesSingleByteForZeroAndSmallValues) {
    std::vector<std::uint8_t> bytes;
    AppendVarint(0, bytes);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0);

    bytes.clear();
    AppendVarint(127, bytes);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 127);
}

TEST(Varint, DecodeReturnsNulloptOnEmptyInput) {
    const std::vector<std::uint8_t> empty;
    std::size_t cursor = 0;
    EXPECT_FALSE(DecodeVarint(empty, &cursor).has_value());
    EXPECT_EQ(cursor, 0u);
}

TEST(Varint, DecodeReturnsNulloptOnTruncatedInput) {
    std::vector<std::uint8_t> bytes;
    AppendVarint(300, bytes);  // Two-byte encoding.
    ASSERT_EQ(bytes.size(), 2u);
    bytes.pop_back();          // Truncate before the final byte.

    std::size_t cursor = 0;
    EXPECT_FALSE(DecodeVarint(bytes, &cursor).has_value());
}

TEST(Varint, DecodeReturnsNulloptOnOverflow) {
    std::vector<std::uint8_t> bytes(11, 0x80);  // 11 continuation bytes.
    std::size_t cursor = 0;
    EXPECT_FALSE(DecodeVarint(bytes, &cursor).has_value());
    EXPECT_EQ(cursor, 0u);
}

TEST(Varint, DecodeStopsAtCursorAndLeavesTrailingBytesUnconsumed) {
    std::vector<std::uint8_t> bytes;
    AppendVarint(42, bytes);
    AppendVarint(7, bytes);

    std::size_t cursor = 0;
    const std::optional<std::uint64_t> first = DecodeVarint(bytes, &cursor);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 42u);
    const std::optional<std::uint64_t> second = DecodeVarint(bytes, &cursor);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 7u);
    EXPECT_EQ(cursor, bytes.size());
}

TEST(Crc32c, MatchesKnownCheckValues) {
    // Standard CRC32C (Castagnoli) check values: empty == 0, "123456789" ==
    // 0xE3069283.
    EXPECT_EQ(Crc32c(""), 0u);
    EXPECT_EQ(Crc32c("123456789"), 0xE3069283u);
}

TEST(Crc32c, DiffersForDifferentInputs) {
    EXPECT_NE(Crc32c("island"), Crc32c("island "));
    EXPECT_NE(Crc32c("a"), Crc32c("b"));
}

TEST(Crc32c, ContinueCombinesLikeOneShotChaining) {
    std::uint32_t chained = Crc32c("");
    chained = Crc32cContinue(chained, "island");
    chained = Crc32cContinue(chained, " search");
    EXPECT_EQ(chained, Crc32c("island search"));
}

}  // namespace
}  // namespace search
}  // namespace island
