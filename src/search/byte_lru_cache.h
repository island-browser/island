#ifndef ISLAND_SEARCH_BYTE_LRU_CACHE_H_
#define ISLAND_SEARCH_BYTE_LRU_CACHE_H_

#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>

namespace island {
namespace search {

// Byte-bounded least-recently-used cache.
//
// Every entry contributes a caller-provided byte size. An insert that would
// push current_bytes() above capacity() evicts least-recently-used entries
// until the new entry fits, so the cache never exceeds its ceiling. Get() and
// Put() are O(1) via a list (recency order) plus a map (key -> list node).
//
// Zero-sized entries and entries larger than the ceiling are rejected and
// leave the cache untouched. Re-inserting an existing key updates its byte
// size (adjusting the running total) and promotes it to most-recently-used
// without double-counting.
template <typename Key>
class ByteLruCache {
  public:
    ByteLruCache() = delete;
    explicit ByteLruCache(std::size_t capacity_bytes);
    ~ByteLruCache() = default;

    ByteLruCache(ByteLruCache&& other) noexcept;
    ByteLruCache& operator=(ByteLruCache&& other) noexcept;
    ByteLruCache(const ByteLruCache&) = delete;
    ByteLruCache& operator=(const ByteLruCache&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t current_bytes() const noexcept { return current_bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }
    [[nodiscard]] std::size_t hit_count() const noexcept { return hit_count_; }
    [[nodiscard]] std::size_t miss_count() const noexcept { return miss_count_; }
    [[nodiscard]] std::size_t eviction_count() const noexcept { return eviction_count_; }

    // Inserts (or replaces) `key` with `byte_size`. Returns false without
    // modifying the cache when `byte_size` is zero or exceeds capacity.
    bool Put(const Key& key, std::size_t byte_size);

    // Returns the stored byte size, or nullopt on a miss. Hits promote the key
    // to most-recently-used.
    std::optional<std::size_t> Get(const Key& key);

    bool Contains(const Key& key) const;
    void Clear();

  private:
    struct Entry {
        Key key;
        std::size_t byte_size;
    };

    // Evicts least-recently-used entries until `byte_size` fits within the
    // remaining budget. Called only when `byte_size` <= capacity_.
    void EvictUntilFits(std::size_t byte_size);

    std::size_t capacity_;
    std::size_t current_bytes_ = 0;
    std::size_t hit_count_ = 0;
    std::size_t miss_count_ = 0;
    std::size_t eviction_count_ = 0;
    std::list<Entry> order_;  // front = most recently used.
    std::unordered_map<Key, typename std::list<Entry>::iterator> by_key_;
};

template <typename Key>
ByteLruCache<Key>::ByteLruCache(std::size_t capacity_bytes) : capacity_(capacity_bytes) {}

template <typename Key>
ByteLruCache<Key>::ByteLruCache(ByteLruCache&& other) noexcept
    : capacity_(other.capacity_),
      current_bytes_(other.current_bytes_),
      hit_count_(other.hit_count_),
      miss_count_(other.miss_count_),
      eviction_count_(other.eviction_count_),
      order_(std::move(other.order_)),
      by_key_(std::move(other.by_key_)) {
    other.capacity_ = 0;
    other.current_bytes_ = 0;
    other.hit_count_ = 0;
    other.miss_count_ = 0;
    other.eviction_count_ = 0;
}

template <typename Key>
ByteLruCache<Key>& ByteLruCache<Key>::operator=(ByteLruCache&& other) noexcept {
    if (this != &other) {
        capacity_ = other.capacity_;
        current_bytes_ = other.current_bytes_;
        hit_count_ = other.hit_count_;
        miss_count_ = other.miss_count_;
        eviction_count_ = other.eviction_count_;
        order_ = std::move(other.order_);
        by_key_ = std::move(other.by_key_);
        other.capacity_ = 0;
        other.current_bytes_ = 0;
        other.hit_count_ = 0;
        other.miss_count_ = 0;
        other.eviction_count_ = 0;
    }
    return *this;
}

template <typename Key>
void ByteLruCache<Key>::EvictUntilFits(std::size_t byte_size) {
    while (current_bytes_ + byte_size > capacity_) {
        // The front is the most-recently-used entry; the back is least-recently-used.
        const Entry& lru = order_.back();
        current_bytes_ -= lru.byte_size;
        by_key_.erase(lru.key);
        order_.pop_back();
        ++eviction_count_;
    }
}

template <typename Key>
bool ByteLruCache<Key>::Put(const Key& key, std::size_t byte_size) {
    if (byte_size == 0 || byte_size > capacity_) {
        return false;
    }

    const auto existing = by_key_.find(key);
    if (existing != by_key_.end()) {
        // Re-insertion: adjust the running total and promote without evicting.
        current_bytes_ -= existing->second->byte_size;
        existing->second->byte_size = byte_size;
        order_.splice(order_.begin(), order_, existing->second);
        current_bytes_ += byte_size;
        return true;
    }

    EvictUntilFits(byte_size);

    order_.push_front(Entry{key, byte_size});
    by_key_.emplace(key, order_.begin());
    current_bytes_ += byte_size;
    return true;
}

template <typename Key>
std::optional<std::size_t> ByteLruCache<Key>::Get(const Key& key) {
    const auto it = by_key_.find(key);
    if (it == by_key_.end()) {
        ++miss_count_;
        return std::nullopt;
    }
    ++hit_count_;
    order_.splice(order_.begin(), order_, it->second);
    return it->second->byte_size;
}

template <typename Key>
bool ByteLruCache<Key>::Contains(const Key& key) const {
    return by_key_.find(key) != by_key_.end();
}

template <typename Key>
void ByteLruCache<Key>::Clear() {
    order_.clear();
    by_key_.clear();
    current_bytes_ = 0;
}

}  // namespace search
}  // namespace island

#endif
