// Byte-bounded LRU cache of decoded posting lists.
//
// Why this does not reuse ByteLruCache: that template stores byte *sizes*, not
// values, and evicts internally with no callback naming what it dropped. A
// cache built on it would have to keep the decoded blocks in a side map that
// the ceiling never bounds -- the accounting would stay under budget while the
// actual memory grew without limit. BlockCache therefore owns its own recency
// list, its own key map, and its own running byte total.
//
// Deviation from the S0 design, recorded deliberately: the design keys entries
// on (term_dict_index, block_index), but the committed Segment reader exposes
// only PostingsFor(term), which decodes a term's entire posting body -- there
// is no per-block accessor, and adding one is a change to store/, which is a
// different unit's file. Entries are therefore whole decoded posting lists
// keyed by term. Every property the plan requires is unaffected: a hard byte
// ceiling, strict LRU eviction, oversized-entry bypass, and results that do
// not depend on cache contents. A later unit that adds a per-block reader can
// narrow the key without changing this class's contract.

#ifndef ISLAND_SEARCH_CACHE_BLOCK_CACHE_H_
#define ISLAND_SEARCH_CACHE_BLOCK_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace island {
namespace search {

// Default ceiling for the decoded-posting cache, sized within the overall S0
// memory budget.
inline constexpr std::size_t kBlockCacheBytes = 4u * 1024u * 1024u;

class BlockCache {
  public:
    explicit BlockCache(std::size_t capacity_bytes = kBlockCacheBytes);

    // Written by hand: a defaulted move would copy current_bytes_ while leaving
    // the source's list empty, so a reused moved-from cache would believe it
    // held bytes it does not and overrun its ceiling. The source is left empty,
    // with its capacity intact and its counters reset.
    BlockCache(BlockCache&& other) noexcept;
    BlockCache& operator=(BlockCache&& other) noexcept;
    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    // Returns the cached posting list for `term`, or nullptr on a miss. A hit
    // promotes the entry to most-recently-used, so this mutates recency even
    // though it reads. The pointer is invalidated by the next Put or Clear.
    //
    // Not internally synchronized: the owner must serialize every call, Get
    // included, since a hit relinks the recency list.
    [[nodiscard]] const std::vector<std::uint64_t>* Get(const std::string& term);

    // Inserts or replaces `term`'s posting list, evicting least-recently-used
    // entries until it fits, and returns the stored list so a cold load need
    // not call Get (which would count it as a hit). The pointer is invalidated
    // by the next Put or Clear.
    //
    // An entry larger than the whole ceiling is rejected outright (bypass)
    // rather than emptying the cache to hold it; Put returns nullptr and
    // leaves every other entry in place. Any existing entry for `term` is
    // dropped even then, so a later Get cannot serve the superseded list.
    const std::vector<std::uint64_t>* Put(const std::string& term,
                                          std::vector<std::uint64_t> postings);

    void Clear();

    // True when an entry of this size can be cached at all: non-empty and no
    // larger than the whole ceiling. Put bypasses exactly the entries for which
    // this is false, so a caller can keep such a list itself instead of moving
    // it into a Put that will discard it.
    [[nodiscard]] bool Admits(const std::string& term, std::size_t posting_count) const noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t current_bytes() const noexcept { return current_bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }
    [[nodiscard]] std::size_t hit_count() const noexcept { return hit_count_; }
    [[nodiscard]] std::size_t miss_count() const noexcept { return miss_count_; }
    [[nodiscard]] std::size_t eviction_count() const noexcept { return eviction_count_; }

    // The byte cost an entry would charge against the ceiling: the decoded ids
    // plus the key itself, so a cache of many short lists is bounded by its
    // real footprint rather than by the ids alone.
    [[nodiscard]] static std::size_t EntryBytes(const std::string& term,
                                                std::size_t posting_count) noexcept;

  private:
    struct Entry {
        std::string term;
        std::vector<std::uint64_t> postings;
        std::size_t byte_size = 0;
    };

    void EvictUntilFits(std::size_t byte_size);

    // Empties the cache and zeroes its byte total and counters.
    void Reset() noexcept;

    std::size_t capacity_;
    std::size_t current_bytes_ = 0;
    std::size_t hit_count_ = 0;
    std::size_t miss_count_ = 0;
    std::size_t eviction_count_ = 0;
    std::list<Entry> order_;  // front = most recently used.
    std::unordered_map<std::string, std::list<Entry>::iterator> by_term_;
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_CACHE_BLOCK_CACHE_H_
