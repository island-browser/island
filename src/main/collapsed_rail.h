#ifndef ISLAND_COLLAPSED_RAIL_H_
#define ISLAND_COLLAPSED_RAIL_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "design_tokens.h"
#include "sidebar_state.h"

namespace island {

// CEF-free metrics and pixel work for the collapsed icon rail, so the geometry and the
// space-dot raster are unit-tested without a running CEF.
//
// Geometry: the rail is kCollapsedRailWidthDip wide. Every control is a square
// kCollapsedRailButtonDip button, centered in the column. The tab list is a CefScrollView
// whose content is the buttons only; when the list overflows, a classic vertical
// scrollbar takes kScrollbarAllowanceDip from the viewport, and the buttons must still
// fit what is left, otherwise a horizontal scrollbar appears.
inline constexpr int kCollapsedRailButtonDip = 32;
inline constexpr int kCollapsedRailButtonSpacingDip = 2;
// Space dots are shorter than tab buttons so a few spaces stay compact.
inline constexpr int kCollapsedRailSpaceButtonHeightDip = 28;
// The widest classic scrollbar on any platform CEF supports.
inline constexpr int kCollapsedRailScrollbarAllowanceDip = 15;
// Side gutter on each side of a centered button.
inline constexpr int kCollapsedRailGutterDip =
    (kCollapsedRailWidthDip - kCollapsedRailButtonDip) / 2;
// The hairline that separates the pinned group and the bottom group: a short line centered
// in a wrapper that adds breathing room above and below it.
inline constexpr int kCollapsedRailSeparatorWidthDip = kCollapsedRailButtonDip - 12;
inline constexpr int kCollapsedRailSeparatorHeightDip = 9;

static_assert(kCollapsedRailButtonDip + kCollapsedRailScrollbarAllowanceDip <=
                  kCollapsedRailWidthDip,
              "a tab button plus a classic scrollbar must fit the collapsed rail");

// Height of `count` stacked tab buttons including the gaps between them, plus the
// pinned-group separator when `with_separator` is set.
[[nodiscard]] constexpr int CollapsedRailTabListHeightDip(std::size_t count, bool with_separator) {
    const int n = static_cast<int>(count);
    const int children = n + (with_separator ? 1 : 0);
    if (children == 0) {
        return 0;
    }
    return n * kCollapsedRailButtonDip + (with_separator ? kCollapsedRailSeparatorHeightDip : 0) +
           (children - 1) * kCollapsedRailButtonSpacingDip;
}

// The pixel buffer of one space dot, premultiplied BGRA, row-major, `size_px` square.
struct SpaceDotBitmap {
    int size_px = 0;
    std::vector<std::uint8_t> bgra;
};

// Draws a space's dot on a transparent `dip_size` DIP square at `scale` pixels per DIP
// (anti-aliased by 4x4 supersampling). Every dot is a filled circle in the space color;
// the active space is additionally ringed by a thin outline in `ring`, so the state is a
// shape and never color alone.
[[nodiscard]] inline SpaceDotBitmap RenderSpaceDot(ArgbColor fill, ArgbColor ring, bool active,
                                                   int dip_size, int scale) {
    constexpr double kDotRadiusDip = 3.5;
    constexpr double kRingInnerRadiusDip = 5.5;
    constexpr double kRingOuterRadiusDip = 6.75;
    constexpr int kSamples = 4;
    SpaceDotBitmap bitmap;
    bitmap.size_px = dip_size * scale;
    bitmap.bgra.assign(static_cast<std::size_t>(bitmap.size_px) * bitmap.size_px * 4, 0);
    const double center = dip_size / 2.0;
    const auto channel = [](ArgbColor color, int shift) {
        return static_cast<double>((color.argb >> shift) & 0xFFU);
    };
    for (int y = 0; y < bitmap.size_px; ++y) {
        for (int x = 0; x < bitmap.size_px; ++x) {
            double dot = 0.0;
            double outline = 0.0;
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const double px = (x + (sx + 0.5) / kSamples) / scale - center;
                    const double py = (y + (sy + 0.5) / kSamples) / scale - center;
                    const double distance = std::sqrt(px * px + py * py);
                    if (distance <= kDotRadiusDip) {
                        dot += 1.0;
                    } else if (active && distance >= kRingInnerRadiusDip &&
                               distance <= kRingOuterRadiusDip) {
                        outline += 1.0;
                    }
                }
            }
            dot /= kSamples * kSamples;
            outline /= kSamples * kSamples;
            const double alpha = dot + outline;
            if (alpha <= 0.0) {
                continue;
            }
            const std::size_t at = (static_cast<std::size_t>(y) * bitmap.size_px + x) * 4;
            // Premultiplied: color * coverage; the dot and the ring never overlap.
            const auto premultiplied = [&](int shift) {
                return static_cast<std::uint8_t>(
                    std::lround(channel(fill, shift) * dot + channel(ring, shift) * outline));
            };
            bitmap.bgra[at + 0] = premultiplied(0);
            bitmap.bgra[at + 1] = premultiplied(8);
            bitmap.bgra[at + 2] = premultiplied(16);
            bitmap.bgra[at + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
        }
    }
    return bitmap;
}

}  // namespace island

#endif  // ISLAND_COLLAPSED_RAIL_H_
