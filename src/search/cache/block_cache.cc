#include "search/cache/block_cache.h"

#include <utility>

namespace island {
namespace search {

BlockCache::BlockCache(std::size_t capacity_bytes) : capacity_(capacity_bytes) {}

std::size_t BlockCache::EntryBytes(const std::string& term, std::size_t posting_count) noexcept {
    return term.size() + posting_count * sizeof(std::uint64_t);
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

bool BlockCache::Put(const std::string& term, std::vector<std::uint64_t> postings) {
    const std::size_t byte_size = EntryBytes(term, postings.size());
    if (byte_size == 0 || byte_size > capacity_) {
        // Oversized-entry bypass: caching it would require evicting everything
        // and still overflow, so the caller simply goes uncached this time.
        return false;
    }

    const auto existing = by_term_.find(term);
    if (existing != by_term_.end()) {
        current_bytes_ -= existing->second->byte_size;
        order_.erase(existing->second);
        by_term_.erase(existing);
    }

    EvictUntilFits(byte_size);

    Entry entry;
    entry.term = term;
    entry.postings = std::move(postings);
    entry.byte_size = byte_size;
    order_.push_front(std::move(entry));
    by_term_.emplace(term, order_.begin());
    current_bytes_ += byte_size;
    return true;
}

void BlockCache::Clear() {
    order_.clear();
    by_term_.clear();
    current_bytes_ = 0;
}

}  // namespace search
}  // namespace island
