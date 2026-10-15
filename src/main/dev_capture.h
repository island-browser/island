#ifndef ISLAND_MAIN_DEV_CAPTURE_H_
#define ISLAND_MAIN_DEV_CAPTURE_H_

// Developer-only startup flags that drive the window into a state and save a
// PNG of it, so the native chrome can be reviewed without screen-recording
// permission (a process may always capture its own windows):
//
//   --island-dev-actions=welcome,theme:dark,command_palette
//       Runs each action in order, a short pause apart, once the window is up.
//       Besides the menu-backed names, `navigate:<address>` loads an address in the active
//       tab and `sidebar:expanded|collapsed` sets the persistent sidebar state;
//       `research:<question>` starts a Research task and `open_report` opens the
//       newest stored report in a new tab.
//   --island-dev-capture=/tmp/shot.png
//       Saves the window (with any overlays) after the actions, then quits.
//   --island-dev-delay=4
//       Seconds to wait after the last action before capturing (default 3).
//   --island-dev-keep-open
//       Do not quit after capturing.
//
// Run captures with a scratch HOME so theme actions never touch real prefs.

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace island {

struct DevCaptureOptions {
    std::vector<std::string> actions;
    std::string capture_path;
    double delay_seconds = 3.0;
    bool keep_open = false;

    [[nodiscard]] bool enabled() const { return !actions.empty() || !capture_path.empty(); }

    // Unknown or malformed --island-dev-* values are ignored (with defaults
    // kept), never fatal: these flags must not be able to break a normal run.
    [[nodiscard]] static DevCaptureOptions Parse(std::span<const std::string_view> arguments);
};

}  // namespace island

#endif  // ISLAND_MAIN_DEV_CAPTURE_H_
