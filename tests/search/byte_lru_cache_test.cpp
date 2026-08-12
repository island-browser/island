#include "search/byte_lru_cache.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace island {
namespace search {

using Cache = ByteLruCache<std::string>;

TEST(ByteLruCache, ReportsCapacityAndInitializesZeroed) {
    const Cache cache(/*capacity_bytes=*/100);

    EXPECT_EQ(cache.capacity(), 100U);
    EXPECT_EQ(cache.current_bytes(), 0U);
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.hit_count(), 0U);
    EXPECT_EQ(cache.miss_count(), 0U);
    EXPECT_EQ(cache.eviction_count(), 0U);
}

TEST(ByteLruCache, ServesBackInsertedEntryAndCountsSingleHit) {
    Cache cache(/*capacity_bytes=*/100);
    ASSERT_TRUE(cache.Put("a", 10));

    EXPECT_EQ(cache.Get("a"), 10);
    EXPECT_EQ(cache.hit_count(), 1U);
    EXPECT_EQ(cache.miss_count(), 0U);
}

TEST(ByteLruCache, CountsMissForAbsentEntry) {
    Cache cache(/*capacity_bytes=*/100);

    EXPECT_EQ(cache.Get("missing"), std::nullopt);
    EXPECT_EQ(cache.hit_count(), 0U);
    EXPECT_EQ(cache.miss_count(), 1U);
}

TEST(ByteLruCache, NeverExceedsTheByteCeiling) {
    Cache cache(/*capacity_bytes=*/40);
    cache.Put("a", 10);
    cache.Put("b", 10);
    cache.Put("c", 10);
    // d(30) does not fit alongside all three (30 + 30 = 60 > 40). Evict the
    // least-recently-used entries (a, then b) until it fits; over-evicting is
    // not required, so c (10) survives alongside d (30) at exactly 40 bytes.
    cache.Put("d", 30);

    EXPECT_LE(cache.current_bytes(), 40U);
    EXPECT_EQ(cache.current_bytes(), 40U);
    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_FALSE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
    EXPECT_EQ(cache.eviction_count(), 2U);
}

TEST(ByteLruCache, EvictsLeastRecentlyUsedEntry) {
    Cache cache(/*capacity_bytes=*/130);
    cache.Put("a", 50);
    cache.Put("b", 50);
    cache.Put("c", 50);  // 150 > 130: evicts "a" (LRU).

    EXPECT_FALSE(cache.Contains("a"));
    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));

    // Touching "b" makes it most-recently-used; "c" becomes LRU.
    EXPECT_EQ(cache.Get("b"), 50);
    cache.Put("d", 50);  // Evicts "c".

    EXPECT_TRUE(cache.Contains("b"));
    EXPECT_FALSE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
    EXPECT_LE(cache.current_bytes(), 130U);
}

TEST(ByteLruCache, TouchedEntryIsPromotedToMostRecentlyUsed) {
    Cache cache(/*capacity_bytes=*/150);
    cache.Put("a", 50);
    cache.Put("b", 50);
    cache.Put("c", 50);
    EXPECT_EQ(cache.Get("a"), 50);  // "a" now most recent.

    cache.Put("d", 50);  // Evicts "b" (LRU), keeps "a".

    EXPECT_TRUE(cache.Contains("a"));
    EXPECT_FALSE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(ByteLruCache, RejectsZeroSizedEntry) {
    Cache cache(/*capacity_bytes=*/100);

    EXPECT_FALSE(cache.Put("zero", 0));
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.current_bytes(), 0U);
    EXPECT_FALSE(cache.Contains("zero"));
}

TEST(ByteLruCache, RejectsEntryLargerThanCapacity) {
    Cache cache(/*capacity_bytes=*/100);

    EXPECT_FALSE(cache.Put("huge", 101));
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.current_bytes(), 0U);
    EXPECT_FALSE(cache.Contains("huge"));
    EXPECT_EQ(cache.eviction_count(), 0U);
}

TEST(ByteLruCache, AllowsSingleEntryExactlyAtCapacity) {
    Cache cache(/*capacity_bytes=*/100);

    EXPECT_TRUE(cache.Put("exact", 100));
    EXPECT_EQ(cache.current_bytes(), 100U);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_TRUE(cache.Contains("exact"));
}

TEST(ByteLruCache, ReplacementUpdatesBytesWithoutDoublingCount) {
    Cache cache(/*capacity_bytes=*/100);
    ASSERT_TRUE(cache.Put("key", 10));
    ASSERT_TRUE(cache.Put("key", 30));

    EXPECT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.current_bytes(), 30U);
    EXPECT_EQ(cache.Get("key"), 30);
    EXPECT_EQ(cache.eviction_count(), 0U);
}

TEST(ByteLruCache, ReplacementPromotesKeyToMostRecentlyUsed) {
    Cache cache(/*capacity_bytes=*/150);
    cache.Put("a", 50);
    cache.Put("b", 50);
    cache.Put("c", 50);
    cache.Put("a", 50);  // Re-touch "a" -> makes it most recent.

    cache.Put("d", 50);  // Evicts "b" (now LRU).

    EXPECT_TRUE(cache.Contains("a"));
    EXPECT_FALSE(cache.Contains("b"));
    EXPECT_TRUE(cache.Contains("c"));
    EXPECT_TRUE(cache.Contains("d"));
}

TEST(ByteLruCache, ClearResetsAllState) {
    Cache cache(/*capacity_bytes=*/100);
    cache.Put("a", 10);
    cache.Put("b", 20);
    EXPECT_EQ(cache.Get("a"), 10);
    EXPECT_EQ(cache.Get("zz"), std::nullopt);

    cache.Clear();

    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.current_bytes(), 0U);
    EXPECT_EQ(cache.capacity(), 100U);
}

TEST(ByteLruCache, WorksWithNonStringKeyTypes) {
    ByteLruCache<std::uint64_t> cache(/*capacity_bytes=*/100);
    cache.Put(1, 10);
    cache.Put(2, 20);

    EXPECT_EQ(cache.Get(1), 10);
    EXPECT_EQ(cache.Get(3), std::nullopt);
    EXPECT_EQ(cache.size(), 2U);
}

TEST(ByteLruCache, IsMoveConstructibleAndMovablyAssignable) {
    static_assert(std::is_move_constructible_v<Cache>);
    static_assert(std::is_move_assignable_v<Cache>);
}

}  // namespace search
}  // namespace island