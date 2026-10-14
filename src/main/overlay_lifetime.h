#ifndef ISLAND_MAIN_OVERLAY_LIFETIME_H_
#define ISLAND_MAIN_OVERLAY_LIFETIME_H_

#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_overlay_controller.h"
#include "include/wrapper/cef_closure_task.h"

namespace island {

// Why overlays only exist while they are shown.
//
// On macOS a live CefWindow overlay widget (even a hidden one, activatable or not) makes the
// window's NSView stop delivering mouse presses to the views and to the page: sidebar buttons,
// tab rows, the address field and web content all go dead until the overlay is destroyed.
// Every Island overlay (palettes, space rename, welcome card) therefore creates its overlay in
// Show() and destroys it in Hide(), and the hover sliver is a plain panel in the chrome root.

// What a surface owning one overlay must do after a lifecycle event.
enum class OverlayStep { kNone, kCreate, kDestroy };

// The CEF-free decision state: the overlay exists exactly while the surface is shown and not
// detached. Each event returns the one step that keeps that invariant, so repeating an event
// is a no-op.
class OverlayLifetime {
  public:
    OverlayStep Show() {
        if (detached_ || shown_) {
            return OverlayStep::kNone;
        }
        shown_ = true;
        return OverlayStep::kCreate;
    }

    OverlayStep Hide() {
        if (!shown_) {
            return OverlayStep::kNone;
        }
        shown_ = false;
        return OverlayStep::kDestroy;
    }

    OverlayStep Detach() {
        detached_ = true;
        return Hide();
    }

    [[nodiscard]] bool overlay_expected() const noexcept { return shown_ && !detached_; }

  private:
    bool shown_ = false;
    bool detached_ = false;
};

// Owns the controller of one overlay. Release() hides the overlay at once but destroys it on
// the next UI task, because Hide() is routinely reached from the overlay's own key or button
// handler, and destroying a widget inside its own event dispatch is not safe. Create() flushes
// a pending destroy first so two overlays never host the same panel at once.
class OverlaySlot {
  public:
    OverlaySlot() = default;
    OverlaySlot(const OverlaySlot&) = delete;
    OverlaySlot& operator=(const OverlaySlot&) = delete;

    [[nodiscard]] const CefRefPtr<CefOverlayController>& get() const noexcept { return overlay_; }
    [[nodiscard]] bool has_overlay() const noexcept { return overlay_ != nullptr; }

    void Adopt(CefRefPtr<CefOverlayController> overlay) {
        Flush();
        overlay_ = std::move(overlay);
    }

    void Release() {
        if (overlay_ == nullptr) {
            return;
        }
        if (overlay_->IsValid()) {
            overlay_->SetVisible(false);
        }
        pending_ = std::move(overlay_);
        overlay_ = nullptr;
        CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                                [](CefRefPtr<CefOverlayController> doomed) {
                                    if (doomed != nullptr && doomed->IsValid()) {
                                        doomed->Destroy();
                                    }
                                },
                                pending_)));
    }

    // Destroys a released overlay that has not been destroyed yet.
    void Flush() {
        if (pending_ != nullptr && pending_->IsValid()) {
            pending_->Destroy();
        }
        pending_ = nullptr;
    }

    // Destroys everything now; used when the owning window is going away.
    void DestroyNow() {
        Flush();
        if (overlay_ != nullptr) {
            if (overlay_->IsValid()) {
                overlay_->SetVisible(false);
                overlay_->Destroy();
            }
            overlay_ = nullptr;
        }
    }

  private:
    CefRefPtr<CefOverlayController> overlay_;
    CefRefPtr<CefOverlayController> pending_;
};

}  // namespace island

#endif  // ISLAND_MAIN_OVERLAY_LIFETIME_H_
