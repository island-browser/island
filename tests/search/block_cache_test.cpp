// The cache must never exceed its byte ceiling, must evict in strict LRU
// order, and must leave results unchanged whatever it happens to hold.

#include "search/cache/block_cache.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace island {
namespace search {
namespace {

std::vector<std::uint64_t> Ids(std::size_t count) {
    std::vector<std::uint64_t> ids;
    ids.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ids.push_back(i + 1);
    }
    return ids;
}

TEST(BlockCache, StartsEmptyAndReportsItsCeiling) {
    const BlockCache cache(1024);
    EXPECT_EQ(cache.capacity(), 1024u);
    EXPECT_EQ(cache.current_bytes(), 0u);
    EXPECT_EQ(cache.size(), 0u);
}

TEST(BlockCache, StoresAndReturnsAPostingList) {
    BlockCache cache(1024);
    ASSERT_TRUE(cache.Put("alpha", Ids(3)));

    const std::vector<std::uint64_t>* got = cache.Get("alpha");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(*got, (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(cache.hit_count(), 1u);
}

TEST(BlockCache, ReportsAMissWithoutInventingAList) {
    BlockCache cache(1024);
    EXPECT_EQ(cache.Get("absent"), nullptr);
    EXPECT_EQ(cache.miss_count(), 1u);
}

TEST(BlockCache, NeverExceedsItsByteCeiling) {
    // Each entry is 5 ids (40 bytes) plus a 4-byte key = 44 bytes.
    BlockCache cache(100);
    for (int i = 0; i < 20; ++i) {
        cache.Put("k" + std::to_string(i) + "x", Ids(5));
        EXPECT_LE(cache.current_bytes(), cache.capacity());
    }
    EXPECT_GT(cache.eviction_count(), 0u);
}

TEST(BlockCache, EvictsInStrictLeastRecentlyUsedOrder) {
    const std::size_t entry = BlockCache::EntryBytes("aaaa", 4);
    BlockCache cache(entry * 2);

    ASSERT_TRUE(cache.Put("aaaa", Ids(4)));
    ASSERT_TRUE(cache.Put("bbbb", Ids(4)));
    // Touch "aaaa" so "bbbb" becomes the least recently used.
    ASSERT_NE(cache.Get("aaaa"), nullptr);
    ASSERT_TRUE(cache.Put("cccc", Ids(4)));

    EXPECT_NE(cache.Get("aaaa"), nullptr);
    EXPECT_NE(cache.Get("cccc"), nullptr);
    EXPECT_EQ(cache.Get("bbbb"), nullptr);
    EXPECT_EQ(cache.eviction_count(), 1u);
}

TEST(BlockCache, BypassesAnEntryLargerThanTheWholeCeiling) {
    BlockCache cache(64);
    ASSERT_TRUE(cache.Put("small", Ids(3)));
    const std::size_t before = cache.current_bytes();

    EXPECT_FALSE(cache.Put("huge", Ids(1000)));
    // The oversized entry neither landed nor flushed what was already there.
    EXPECT_EQ(cache.current_bytes(), before);
    EXPECT_NE(cache.Get("small"), nullptr);
    EXPECT_EQ(cache.Get("huge"), nullptr);
}

TEST(BlockCache, ReplacingAKeyDoesNotDoubleCountItsBytes) {
    BlockCache cache(4096);
    ASSERT_TRUE(cache.Put("term", Ids(10)));
    const std::size_t after_first = cache.current_bytes();

    ASSERT_TRUE(cache.Put("term", Ids(10)));
    EXPECT_EQ(cache.current_bytes(), after_first);
    EXPECT_EQ(cache.size(), 1u);
}

TEST(BlockCache, ReplacingAKeyWithADifferentSizeAdjustsTheTotal) {
    BlockCache cache(4096);
    ASSERT_TRUE(cache.Put("term", Ids(10)));
    ASSERT_TRUE(cache.Put("term", Ids(2)));

    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.current_bytes(), BlockCache::EntryBytes("term", 2));
    const std::vector<std::uint64_t>* got = cache.Get("term");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->size(), 2u);
}

TEST(BlockCache, ClearReleasesEverything) {
    BlockCache cache(4096);
    ASSERT_TRUE(cache.Put("a", Ids(4)));
    ASSERT_TRUE(cache.Put("b", Ids(4)));

    cache.Clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.current_bytes(), 0u);
    EXPECT_EQ(cache.Get("a"), nullptr);
}

TEST(BlockCache, AZeroCapacityCacheStoresNothingButStaysUsable) {
    BlockCache cache(0);
    EXPECT_FALSE(cache.Put("a", Ids(1)));
    EXPECT_EQ(cache.Get("a"), nullptr);
    EXPECT_EQ(cache.current_bytes(), 0u);
}

TEST(BlockCache, PutReturnsTheStoredListWithoutCountingAHit) {
    BlockCache cache(1024);
    const std::vector<std::uint64_t>* stored = cache.Put("alpha", Ids(3));
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(*stored, (std::vector<std::uint64_t>{1, 2, 3}));
    // A cold load is a miss for the caller, never a hit.
    EXPECT_EQ(cache.hit_count(), 0u);
    // The pointer is the cached entry itself, not a copy.
    EXPECT_EQ(cache.Get("alpha"), stored);
    EXPECT_EQ(cache.hit_count(), 1u);
}

TEST(BlockCache, ARefusedOversizedReplacementDropsTheSupersededList) {
    BlockCache cache(64);
    ASSERT_NE(cache.Put("term", Ids(3)), nullptr);

    EXPECT_EQ(cache.Put("term", Ids(1000)), nullptr);
    // Serving the old list after its replacement was refused would answer
    // from data the caller has already superseded.
    EXPECT_EQ(cache.Get("term"), nullptr);
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.current_bytes(), 0u);
}

TEST(BlockCache, AMovedFromCacheIsEmptyAndStillHonoursItsCeiling) {
    const std::size_t entry = BlockCache::EntryBytes("aaaa", 4);
    BlockCache source(entry * 2);
    ASSERT_NE(source.Put("aaaa", Ids(4)), nullptr);
    ASSERT_NE(source.Put("bbbb", Ids(4)), nullptr);

    BlockCache moved(std::move(source));
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.current_bytes(), entry * 2);
    EXPECT_NE(moved.Get("aaaa"), nullptr);
    EXPECT_NE(moved.Get("bbbb"), nullptr);

    // NOLINTBEGIN(bugprone-use-after-move): reuse after move is the point.
    EXPECT_EQ(source.size(), 0u);
    EXPECT_EQ(source.current_bytes(), 0u);
    EXPECT_EQ(source.capacity(), entry * 2);
    for (int i = 0; i < 8; ++i) {
        source.Put("k" + std::to_string(i) + "xx", Ids(4));
        EXPECT_LE(source.current_bytes(), source.capacity());
    }
    EXPECT_EQ(source.size(), 2u);
    // NOLINTEND(bugprone-use-after-move)
}

TEST(BlockCache, MoveAssignmentTransfersEntriesAndResetsTheSource) {
    const std::size_t entry = BlockCache::EntryBytes("aaaa", 4);
    BlockCache source(entry * 2);
    ASSERT_NE(source.Put("aaaa", Ids(4)), nullptr);

    BlockCache target(entry);
    ASSERT_NE(target.Put("zzzz", Ids(4)), nullptr);
    target = std::move(source);

    EXPECT_EQ(target.capacity(), entry * 2);
    EXPECT_EQ(target.size(), 1u);
    EXPECT_EQ(target.Get("zzzz"), nullptr);
    const std::vector<std::uint64_t>* got = target.Get("aaaa");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->size(), 4u);

    // NOLINTBEGIN(bugprone-use-after-move): reuse after move is the point.
    EXPECT_EQ(source.size(), 0u);
    EXPECT_EQ(source.current_bytes(), 0u);
    EXPECT_EQ(source.hit_count(), 0u);
    for (int i = 0; i < 8; ++i) {
        source.Put("k" + std::to_string(i) + "xx", Ids(4));
        EXPECT_LE(source.current_bytes(), source.capacity());
    }
    // NOLINTEND(bugprone-use-after-move)
}

}  // namespace
}  // namespace search
}  // namespace island
