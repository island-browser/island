#ifndef ISLAND_TAB_ID_H_
#define ISLAND_TAB_ID_H_

#include <atomic>
#include <compare>
#include <cstdint>

namespace island {

// Process-lifetime-unique tab identity. Never derived from a vector position, which
// changes on reorder/close, and never reused once a tab is closed.
struct TabId {
    std::uint64_t value = 0;

    auto operator<=>(const TabId&) const noexcept = default;
};

// Allocates the next unused TabId for the process lifetime.
[[nodiscard]] inline TabId NextTabId() {
    static std::atomic<std::uint64_t> next_id{1};
    return TabId{next_id.fetch_add(1, std::memory_order_relaxed)};
}

// Process-lifetime-unique space identity, with the same guarantees as TabId.
struct SpaceId {
    std::uint64_t value = 0;

    auto operator<=>(const SpaceId&) const noexcept = default;
};

// Allocates the next unused SpaceId for the process lifetime.
[[nodiscard]] inline SpaceId NextSpaceId() {
    static std::atomic<std::uint64_t> next_id{1};
    return SpaceId{next_id.fetch_add(1, std::memory_order_relaxed)};
}

}  // namespace island

#endif  // ISLAND_TAB_ID_H_
