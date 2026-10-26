#ifndef ISLAND_MAIN_PEEK_LAYOUT_H_
#define ISLAND_MAIN_PEEK_LAYOUT_H_

#include <algorithm>

#include "chrome_snapshot.h"

namespace island {

// The peek card is sized from the window's content area, not a fixed size: 80% of its width and
// 85% of its height, centered. The minimums keep a small window usable, and the card never grows
// past the content area itself.
constexpr int kPeekWidthPercent = 80;
constexpr int kPeekHeightPercent = 85;
constexpr int kPeekMinWidthDip = 360;
constexpr int kPeekMinHeightDip = 280;

// Card bounds (window-relative DIP) for the content area |content|. CEF-free so the geometry is
// unit-tested; PeekOverlay::CardBounds forwards here.
[[nodiscard]] inline DipRect PeekCardBounds(const DipRect& content) noexcept {
    const int available_width = std::max(content.width, 0);
    const int available_height = std::max(content.height, 0);
    const int width = std::clamp(available_width * kPeekWidthPercent / 100,
                                 std::min(kPeekMinWidthDip, available_width), available_width);
    const int height = std::clamp(available_height * kPeekHeightPercent / 100,
                                  std::min(kPeekMinHeightDip, available_height), available_height);
    return {.x = content.x + (available_width - width) / 2,
            .y = content.y + (available_height - height) / 2,
            .width = width,
            .height = height};
}

}  // namespace island

#endif  // ISLAND_MAIN_PEEK_LAYOUT_H_
