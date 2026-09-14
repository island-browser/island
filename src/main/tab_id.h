#ifndef ISLAND_TAB_ID_H_
#define ISLAND_TAB_ID_H_

#include <atomic>
#include <compare>
#include <cstdint>

namespace island {

namespace detail {

inline std::atomic<std::uint64_t>& tab_id_counter() {
    static std::atomic<std::uint64_t> next{1};
    return next;
}

inline std::atomic<std::uint64_t>& space_id_counter() {
    static std::atomic<std::uint64_t> next{1};
    return next;
}

}  // namespace detail

// Process-lifetime-unique tab identity. Never derived from a vector position, which
// changes on reorder/close, and never reused once a tab is closed.
struct TabId {
    std::uint64_t value = 0;

    auto operator<=>(const TabId&) const noexcept = default;
};

// Allocates the next unused TabId for the process lifetime.
[[nodiscard]] inline TabId NextTabId() {
    return TabId{detail::tab_id_counter().fetch_add(1, std::memory_order_relaxed)};
}

// Session restore re-reserves persisted identities: after this call the next
// allocated TabId is strictly greater than |value|.
inline void ReserveTabIdsUpTo(std::uint64_t value) {
    std::uint64_t current = detail::tab_id_counter().load(std::memory_order_relaxed);
    while (current <= value && !detail::tab_id_counter().compare_exchange_weak(
                                   current, value + 1, std::memory_order_relaxed)) {
    }
}

// Process-lifetime-unique space identity, with the same guarantees as TabId.
struct SpaceId {
    std::uint64_t value = 0;

    auto operator<=>(const SpaceId&) const noexcept = default;
};

// Allocates the next unused SpaceId for the process lifetime.
[[nodiscard]] inline SpaceId NextSpaceId() {
    return SpaceId{detail::space_id_counter().fetch_add(1, std::memory_order_relaxed)};
}

// Session restore counterpart of ReserveTabIdsUpTo for space identities.
inline void ReserveSpaceIdsUpTo(std::uint64_t value) {
    std::uint64_t current = detail::space_id_counter().load(std::memory_order_relaxed);
    while (current <= value && !detail::space_id_counter().compare_exchange_weak(
                                   current, value + 1, std::memory_order_relaxed)) {
    }
}

}  // namespace island

#endif  // ISLAND_TAB_ID_H_
