#import <Cocoa/Cocoa.h>

#include "sidebar_state.h"

// The pinned CEF exposes no hover or mouse callbacks in cef_view_delegate.h,
// cef_window_delegate.h, or cef_browser_view_delegate.h, so hover enters the
// application through this one platform-native object. It only observes: it
// converts the pointer position to window-relative DIP and hands it to the
// caller's callback, which is responsible for doing all chrome mutation on the
// CEF UI thread.
namespace island {
namespace {

// Owns the local event monitor and the C callback it forwards to. Held by raw
// pointer so the seam never retains the BrowserWindow and cannot create a
// refcount cycle; the window uninstalls it in OnWindowDestroyed.
struct SidebarHoverSeam {
    id monitor = nil;
    SidebarHoverCallback callback = nullptr;
    void* context = nullptr;
};

}  // namespace

void* InstallSidebarHoverSeam(void* window_handle, SidebarHoverCallback callback, void* context) {
    if (window_handle == nullptr || callback == nullptr) {
        return nullptr;
    }

    NSView* content_view = (__bridge NSView*)window_handle;
    NSWindow* window = [content_view window];
    if (window == nil) {
        return nullptr;
    }

    auto* seam = new SidebarHoverSeam();
    seam->callback = callback;
    seam->context = context;
    // A local monitor sees mouse-moved events before any subview, so the 2-DIP
    // sliver overlay sitting in the same edge band cannot swallow them the way a
    // subview NSTrackingArea would.
    seam->monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                     handler:^NSEvent*(NSEvent* event) {
                                       if ([event window] == window) {
                                           const NSPoint point =
                                               [content_view convertPoint:[event locationInWindow]
                                                                 fromView:nil];
                                           seam->callback(seam->context, static_cast<int>(point.x));
                                       }
                                       return event;
                                     }];
    [window setAcceptsMouseMovedEvents:YES];
    return seam;
}

void RemoveSidebarHoverSeam(void* seam_handle) {
    if (seam_handle == nullptr) {
        return;
    }
    auto* seam = static_cast<SidebarHoverSeam*>(seam_handle);
    if (seam->monitor != nil) {
        [NSEvent removeMonitor:seam->monitor];
        seam->monitor = nil;
    }
    delete seam;
}

}  // namespace island
