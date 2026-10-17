#ifndef ISLAND_ROW_RASTER_H_
#define ISLAND_ROW_RASTER_H_

#include <cstdint>
#include <vector>

#include "design_tokens.h"

namespace island {

// CEF `cef_views` buttons have no corner radius, so Arc's soft rounded fills are baked into the
// images the edge buttons of a row show: a software raster of rounded rectangles composited with
// the real icon. CEF-free so the geometry is unit-tested.

// Premultiplied BGRA, `scale` pixels per DIP (the same layout CEF's AddBitmap takes).
struct RasterBitmap {
    int width_px = 0;
    int height_px = 0;
    int scale = 1;
    std::vector<std::uint8_t> bgra;
};

// Corner radii in DIP, clockwise from top-left.
struct CornerRadii {
    int top_left = 0;
    int top_right = 0;
    int bottom_right = 0;
    int bottom_left = 0;
};

struct RowColors {
    ArgbColor fill;
    ArgbColor title;
};

// Resolves the two colors shared by Native tab and palette rows. Other looks keep the existing
// soft selected surface and secondary idle title. Sleeping rows always use the disabled-text slot.
[[nodiscard]] RowColors ResolveRowColors(const ChromeTokens& tokens, bool selected, bool hovered,
                                         bool slept) noexcept;

// A transparent bitmap of `width_dip` x `height_dip`.
[[nodiscard]] RasterBitmap NewRasterBitmap(int width_dip, int height_dip, int scale);

// Fills the rounded rectangle (x, y, w, h in DIP, top-left origin) with `color` at `opacity`
// (0..1), anti-aliased, source-over onto the bitmap.
void FillRoundedRect(RasterBitmap& bitmap, int x_dip, int y_dip, int w_dip, int h_dip,
                     const CornerRadii& radii, ArgbColor color, double opacity = 1.0);

// Source-over composites premultiplied BGRA pixels (`src_w` x `src_h` px) at pixel offset
// (`dx_px`, `dy_px`); out-of-range pixels are clipped.
void CompositePremultiplied(RasterBitmap& bitmap, const std::uint8_t* src, int src_w, int src_h,
                            int dx_px, int dy_px);

// Coverage 0..1 of pixel (px, py) by the rounded rect (all in px); exposed for tests.
[[nodiscard]] double RoundedRectCoverage(int px, int py, int w_px, int h_px, int tl_px, int tr_px,
                                         int br_px, int bl_px);

}  // namespace island

#endif  // ISLAND_ROW_RASTER_H_
