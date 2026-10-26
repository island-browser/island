#ifndef ISLAND_MAIN_LINK_OPEN_POLICY_H_
#define ISLAND_MAIN_LINK_OPEN_POLICY_H_

namespace island {

// The window-open dispositions CEF reports for a link or script open, mirrored without CEF
// types so the decision is unit-tested. `kNewWindow` is what Chromium reports for Shift+click;
// `kOther` (window.open with window features, popups) is always rejected.
enum class LinkDisposition {
    kCurrentTab,
    kNewForegroundTab,
    kNewBackgroundTab,
    kNewWindow,
    kOther,
};

// What Island does with a new-tab open that a page requested.
enum class LinkOpenAction {
    kPeek,
    kForegroundTab,
    kReject,
};

// Decides what a page's new-tab open becomes. Only an explicit gesture opens anything. The table:
//   Cmd/Ctrl+click, middle-click  -> peek card       (kNewBackgroundTab + user gesture)
//   Shift+click                   -> foreground tab  (kNewWindow + user gesture)
//   plain target=_blank, and
//   window.open with a gesture    -> foreground tab  (kNewForegroundTab + user gesture)
// Anything without a user gesture (script-initiated popups and tabs), popups with window
// features, and current-tab or other dispositions are rejected. CEF's popup callbacks carry no
// modifier flags, so Chromium's disposition is the only signal: Cmd/Ctrl and middle-click both
// arrive as NEW_BACKGROUND_TAB and cannot be told apart, so both peek; Shift is NEW_WINDOW;
// plain links are NEW_FOREGROUND_TAB.
[[nodiscard]] LinkOpenAction DecideLinkOpen(LinkDisposition disposition,
                                            bool user_gesture) noexcept;

}  // namespace island

#endif  // ISLAND_MAIN_LINK_OPEN_POLICY_H_
