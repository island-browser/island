#ifndef ISLAND_MAIN_BROWSER_VIEW_PARK_H_
#define ISLAND_MAIN_BROWSER_VIEW_PARK_H_

namespace island {

// How long a superseded tab view stays parked (hidden, still in the content slot) before it
// leaves the view tree.
//
// Removing a CefBrowserView from the hierarchy soon after it was created crashed CEF on the UI
// thread a few seconds later (EXC_BAD_ACCESS inside the framework, no Island frame). The browser
// was already created at that point, so the crash comes from CEF finishing the view's setup
// after it was taken out. Hiding the view is safe, and removing it after this delay was safe in
// every run. Parked views are a handful per session (one per tab the person has shown), so
// keeping them this long costs nothing.
inline constexpr int kParkedBrowserViewRemovalMs = 3000;

// Whether a parked view's timer should remove it. A view shown again is the attached view and
// stays; a view already gone from the content slot has nothing left to remove.
[[nodiscard]] inline bool ShouldRemoveParkedView(bool visible, bool in_content) {
    return in_content && !visible;
}

}  // namespace island

#endif  // ISLAND_MAIN_BROWSER_VIEW_PARK_H_
