#include "search/cache/block_cache.h"

#include <utility>

namespace island {
namespace search {

BlockCache::BlockCache(std::size_t capacity_bytes) : capacity_(capacity_bytes) {}

// Moving a std::list transfers its nodes, so the iterators stored in by_term_
// stay valid and now point into this object's list.
BlockCache::BlockCache(BlockCache&& other) noexcept
    : capacity_(other.capacity_),
      current_bytes_(other.current_bytes_),
      hit_count_(other.hit_count_),
      miss_count_(other.miss_count_),
      eviction_count_(other.eviction_count_),
      order_(std::move(other.order_)),
      by_term_(std::move(other.by_term_)) {
    other.Reset();
}

BlockCache& BlockCache::operator=(BlockCache&& other) noexcept {
    if (this != &other) {
        capacity_ = other.capacity_;
        current_bytes_ = other.current_bytes_;
        hit_count_ = other.hit_count_;
        miss_count_ = other.miss_count_;
        eviction_count_ = other.eviction_count_;
        order_ = std::move(other.order_);
        by_term_ = std::move(other.by_term_);
        other.Reset();
    }
    return *this;
}

void BlockCache::Reset() noexcept {
    order_.clear();
    by_term_.clear();
    current_bytes_ = 0;
    hit_count_ = 0;
    miss_count_ = 0;
    eviction_count_ = 0;
}

std::size_t BlockCache::EntryBytes(const std::string& term, std::size_t posting_count) noexcept {
    return term.size() + posting_count * sizeof(std::uint64_t);
}

bool BlockCache::Admits(const std::string& term, std::size_t posting_count) const noexcept {
    const std::size_t byte_size = EntryBytes(term, posting_count);
    return byte_size != 0 && byte_size <= capacity_;
}

const std::vector<std::uint64_t>* BlockCache::Get(const std::string& term) {
    const auto it = by_term_.find(term);
    if (it == by_term_.end()) {
        ++miss_count_;
        return nullptr;
    }
    ++hit_count_;
    order_.splice(order_.begin(), order_, it->second);
    return &it->second->postings;
}

void BlockCache::EvictUntilFits(std::size_t byte_size) {
    while (!order_.empty() && current_bytes_ + byte_size > capacity_) {
        const Entry& victim = order_.back();
        current_bytes_ -= victim.byte_size;
        by_term_.erase(victim.term);
        order_.pop_back();
        ++eviction_count_;
    }
}

const std::vector<std::uint64_t>* BlockCache::Put(const std::string& term,
                                                  std::vector<std::uint64_t> postings) {
    // The superseded entry goes first, whether or not the replacement fits:
    // keeping it after a refused replacement would serve the old list.
    const auto existing = by_term_.find(term);
    if (existing != by_term_.end()) {
        current_bytes_ -= existing->second->byte_size;
        order_.erase(existing->second);
        by_term_.erase(existing);
    }

    if (!Admits(term, postings.size())) {
        // Oversized-entry bypass: caching it would require evicting everything
        // and still overflow, so the caller simply goes uncached this time.
        return nullptr;
    }
    const std::size_t byte_size = EntryBytes(term, postings.size());

    EvictUntilFits(byte_size);

    Entry entry;
    entry.term = term;
    entry.postings = std::move(postings);
    entry.byte_size = byte_size;
    order_.push_front(std::move(entry));
    by_term_.emplace(term, order_.begin());
    current_bytes_ += byte_size;
    return &order_.front().postings;
}

void BlockCache::Clear() {
    order_.clear();
    by_term_.clear();
    current_bytes_ = 0;
}

}  // namespace search
}  // namespace island
