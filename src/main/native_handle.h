#ifndef ISLAND_NATIVE_HANDLE_H_
#define ISLAND_NATIVE_HANDLE_H_

#include <cstdint>
#include <type_traits>

namespace island {

// CEF's cef_window_handle_t is a pointer on macOS (NSView*) and Windows (HWND) but an integer on
// Linux (an X11 window id). Native helpers that take `void*` go through this conversion so the
// same call compiles everywhere; the macOS-only helpers are no-ops on the other platforms.
template <typename Handle>
[[nodiscard]] void* NativeHandlePointer(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) {
        return handle;
    } else {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle));
    }
}

}  // namespace island

#endif  // ISLAND_NATIVE_HANDLE_H_
