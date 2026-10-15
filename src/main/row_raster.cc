#include "row_raster.h"

#include <algorithm>
#include <cmath>

namespace island {
namespace {

// Whether the sample point (sx, sy in px, relative to the rect's top-left) is inside the rounded
// rectangle of size w x h with the given corner radii.
bool Inside(double sx, double sy, double w, double h, double tl, double tr, double br, double bl) {
    if (sx < 0 || sy < 0 || sx > w || sy > h) {
        return false;
    }
    const auto corner = [&](double cx, double cy, double radius) {
        const double dx = sx - cx;
        const double dy = sy - cy;
        return dx * dx + dy * dy <= radius * radius;
    };
    if (tl > 0 && sx < tl && sy < tl) {
        return corner(tl, tl, tl);
    }
    if (tr > 0 && sx > w - tr && sy < tr) {
        return corner(w - tr, tr, tr);
    }
    if (br > 0 && sx > w - br && sy > h - br) {
        return corner(w - br, h - br, br);
    }
    if (bl > 0 && sx < bl && sy > h - bl) {
        return corner(bl, h - bl, bl);
    }
    return true;
}

}  // namespace

double RoundedRectCoverage(int px, int py, int w_px, int h_px, int tl_px, int tr_px, int br_px,
                           int bl_px) {
    constexpr int kSamples = 4;
    int hits = 0;
    for (int j = 0; j < kSamples; ++j) {
        for (int i = 0; i < kSamples; ++i) {
            const double sx = px + (i + 0.5) / kSamples;
            const double sy = py + (j + 0.5) / kSamples;
            if (Inside(sx, sy, w_px, h_px, tl_px, tr_px, br_px, bl_px)) {
                ++hits;
            }
        }
    }
    return static_cast<double>(hits) / (kSamples * kSamples);
}

RasterBitmap NewRasterBitmap(int width_dip, int height_dip, int scale) {
    RasterBitmap bitmap;
    bitmap.scale = std::max(1, scale);
    bitmap.width_px = std::max(0, width_dip) * bitmap.scale;
    bitmap.height_px = std::max(0, height_dip) * bitmap.scale;
    bitmap.bgra.assign(static_cast<std::size_t>(bitmap.width_px) * bitmap.height_px * 4, 0);
    return bitmap;
}

void FillRoundedRect(RasterBitmap& bitmap, int x_dip, int y_dip, int w_dip, int h_dip,
                     const CornerRadii& radii, ArgbColor color, double opacity) {
    const int s = bitmap.scale;
    const int x0 = x_dip * s;
    const int y0 = y_dip * s;
    const int w = w_dip * s;
    const int h = h_dip * s;
    const double r = ((color.argb >> 16) & 0xFF) / 255.0;
    const double g = ((color.argb >> 8) & 0xFF) / 255.0;
    const double b = (color.argb & 0xFF) / 255.0;
    for (int py = std::max(0, y0); py < std::min(bitmap.height_px, y0 + h); ++py) {
        for (int px = std::max(0, x0); px < std::min(bitmap.width_px, x0 + w); ++px) {
            const double coverage =
                RoundedRectCoverage(px - x0, py - y0, w, h, radii.top_left * s, radii.top_right * s,
                                    radii.bottom_right * s, radii.bottom_left * s) *
                std::clamp(opacity, 0.0, 1.0);
            if (coverage <= 0.0) {
                continue;
            }
            std::uint8_t* dst =
                &bitmap.bgra[(static_cast<std::size_t>(py) * bitmap.width_px + px) * 4];
            const double inv = 1.0 - coverage;
            dst[0] = static_cast<std::uint8_t>(std::lround(255.0 * b * coverage + dst[0] * inv));
            dst[1] = static_cast<std::uint8_t>(std::lround(255.0 * g * coverage + dst[1] * inv));
            dst[2] = static_cast<std::uint8_t>(std::lround(255.0 * r * coverage + dst[2] * inv));
            dst[3] = static_cast<std::uint8_t>(std::lround(255.0 * coverage + dst[3] * inv));
        }
    }
}

void CompositePremultiplied(RasterBitmap& bitmap, const std::uint8_t* src, int src_w, int src_h,
                            int dx_px, int dy_px) {
    for (int sy = 0; sy < src_h; ++sy) {
        const int py = dy_px + sy;
        if (py < 0 || py >= bitmap.height_px) {
            continue;
        }
        for (int sx = 0; sx < src_w; ++sx) {
            const int px = dx_px + sx;
            if (px < 0 || px >= bitmap.width_px) {
                continue;
            }
            const std::uint8_t* from = src + (static_cast<std::size_t>(sy) * src_w + sx) * 4;
            std::uint8_t* dst =
                &bitmap.bgra[(static_cast<std::size_t>(py) * bitmap.width_px + px) * 4];
            const int inv = 255 - from[3];
            for (int c = 0; c < 4; ++c) {
                dst[c] =
                    static_cast<std::uint8_t>(std::min(255, from[c] + (dst[c] * inv + 127) / 255));
            }
        }
    }
}

}  // namespace island
