#include "mouse_down_monitor.h"

#import <Cocoa/Cocoa.h>

namespace island {

MouseDownMonitor::MouseDownMonitor(void* view, Handler handler) : handler_(std::move(handler)) {
    NSView* root = (__bridge NSView*)view;
    NSWindow* window = root.window;
    id token = [NSEvent
        addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                                              NSEventMaskOtherMouseDown)
                                     handler:^NSEvent*(NSEvent* event) {
                                       // Overlays are hosted in child windows of the root window,
                                       // so a press on the sheet arrives with that child as its
                                       // window.
                                       NSWindow* eventWindow = event.window;
                                       const BOOL ours =
                                           eventWindow == window ||
                                           [window.childWindows containsObject:eventWindow];
                                       if (!ours) {
                                           handler_(/*in_view=*/false, CefPoint());
                                           return event;
                                       }
                                       // Go through screen space, then into the root view's DIP
                                       // space (top-left origin).
                                       NSPoint screen = [eventWindow
                                           convertPointToScreen:event.locationInWindow];
                                       NSPoint point =
                                           [root convertPoint:[window convertPointFromScreen:screen]
                                                     fromView:nil];
                                       if (!root.isFlipped) {
                                           point.y = root.bounds.size.height - point.y;
                                       }
                                       handler_(/*in_view=*/true,
                                                CefPoint(static_cast<int>(point.x),
                                                         static_cast<int>(point.y)));
                                       return event;
                                     }];
    token_ = (__bridge_retained void*)token;
}

MouseDownMonitor::~MouseDownMonitor() {
    if (token_ != nullptr) {
        id token = (__bridge_transfer id)token_;
        token_ = nullptr;
        [NSEvent removeMonitor:token];
    }
}

}  // namespace island
