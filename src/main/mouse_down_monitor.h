#ifndef ISLAND_MOUSE_DOWN_MONITOR_H_
#define ISLAND_MOUSE_DOWN_MONITOR_H_

#include <functional>
#include <utility>

#include "include/internal/cef_types_wrappers.h"

namespace island {

// Reports every mouse press the app receives, so an overlay can close when the user clicks outside
// it. Focus cannot stand in for that press: showing an activatable overlay and switching apps both
// move focus without a click, so a sheet that closed on focus loss would close itself at once, or
// whenever the app was deactivated.
//
// On macOS this is an NSEvent local monitor. It sees each press before the window dispatches it,
// and it always passes the event on, so the click that closes an overlay still lands. Keep one
// alive only while its overlay is shown (see overlay_lifetime.h). Elsewhere it does nothing; the
// overlays using it then close through Escape, their close button or a palette.
class MouseDownMonitor final {
  public:
    // `in_view` is false when the press landed in another window. `point` is in the view's DIP
    // space (top-left origin) and only meaningful when `in_view` is true.
    using Handler = std::function<void(bool in_view, CefPoint point)>;

    // `view` is the window's native root view (CefWindow::GetWindowHandle).
#if defined(__APPLE__)
    MouseDownMonitor(void* view, Handler handler);
    ~MouseDownMonitor();
#else
    MouseDownMonitor(void*, Handler handler) : handler_(std::move(handler)) {}
    ~MouseDownMonitor() = default;
#endif

    MouseDownMonitor(const MouseDownMonitor&) = delete;
    MouseDownMonitor& operator=(const MouseDownMonitor&) = delete;

  private:
    Handler handler_;
    // The NSEvent monitor token, retained by the object until the monitor is removed.
    void* token_ = nullptr;
};

}  // namespace island

#endif  // ISLAND_MOUSE_DOWN_MONITOR_H_
