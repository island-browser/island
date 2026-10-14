#ifndef ISLAND_MAIN_RUNTIME_STYLE_H_
#define ISLAND_MAIN_RUNTIME_STYLE_H_

#include "include/internal/cef_types.h"

namespace island {

// The CEF runtime style of every Island window and browser view.
//
// Island is one top-level CefWindow whose view tree holds several
// CefBrowserViews at once: the tab pages (two side by side in split view), the
// agent panel, and the local Settings / overview pages. CEF documents that a
// Chrome style Window "can host at most one Chrome style BrowserView but
// potentially multiple Alloy style BrowserViews" (cef_types_runtime.h), and
// the default style is Chrome. With the default, the extra Chrome style views
// leave Chromium's per-window Browser state inconsistent, and the first
// Widget notification that touches it dereferences null inside CEF: the
// NSWindow resign-key notification posted when the app is deactivated
// (reproduced in split view) and the will-close notification posted during
// quit. Island draws its own chrome and needs none of the Chrome UI, so the
// window and every browser view use the Alloy style.
inline constexpr cef_runtime_style_t kIslandRuntimeStyle = CEF_RUNTIME_STYLE_ALLOY;

}  // namespace island

#endif  // ISLAND_MAIN_RUNTIME_STYLE_H_
