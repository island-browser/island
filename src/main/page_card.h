#ifndef ISLAND_PAGE_CARD_H_
#define ISLAND_PAGE_CARD_H_

#include <vector>

#include "design_tokens.h"

namespace island {

// How the page card (the CefBrowserView) is dressed. CEF `cef_views` cannot round or shadow a
// view, and the page itself is drawn by the browser's compositor, so on macOS the platform helper
// (macos/page_card_mac.mm) lays a mouse-transparent native "frame" over the card: a ring in the
// canvas color whose hole has the rounded corners (it repaints the four corner wedges and the
// margin) plus the one soft shadow cast into that margin. Elsewhere the card stays a rectangle.
struct PageCardStyle {
    int radius_dip = 10;
    bool shadow = true;
    // The window-margin/sidebar color the corners are repainted with.
    ArgbColor canvas;
    // Gutter around the card that the frame covers (the card's inset).
    int inset_dip = 8;
    bool dark = false;

    bool operator==(const PageCardStyle&) const = default;
};

// Applies `style` to the visible page views of the windows in `handles` (the CefWindowHandle
// CEF reports for a browser) and removes the frame of any view no longer shown. UI thread only.
#if defined(__APPLE__)
void ApplyPageCardStyle(const std::vector<void*>& handles, const PageCardStyle& style);
#else
inline void ApplyPageCardStyle(const std::vector<void*>&, const PageCardStyle&) {}
#endif

// A rounded rectangle (window DIP, top-left origin) whose four corners are repainted in the
// canvas color over the square fill CEF draws, so selected/hovered rows and the address pill read
// as soft rounded fills (CEF buttons and panels have no corner radius).
struct CornerPatch {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int radius = 8;
    // Color immediately outside the rounded fill. Zero keeps the caller-provided canvas color.
    ArgbColor outside;

    bool operator==(const CornerPatch&) const = default;
};

#if defined(__APPLE__)
void ApplyCornerPatches(const std::vector<void*>& handles, const std::vector<CornerPatch>& patches,
                        ArgbColor canvas);
#else
inline void ApplyCornerPatches(const std::vector<void*>&, const std::vector<CornerPatch>&,
                               ArgbColor) {}
#endif

}  // namespace island

#endif  // ISLAND_PAGE_CARD_H_
