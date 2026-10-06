#ifndef ISLAND_BROWSER_COMMAND_H_
#define ISLAND_BROWSER_COMMAND_H_

#include <cstdint>

namespace island {

// Window-level browser commands. The Phase 1/2 navigation values (kBack,
// kForward, kReload) are never renumbered; Phase 3 tab/space commands append
// after them. Commands that need an argument (direct-index tab switch, space
// switch/rename/reorder) are explicit BrowserWindow methods instead so
// dispatch stays unit-testable without a CEF runtime.
enum class BrowserCommand : std::uint8_t {
    kBack,
    kForward,
    kReload,
    kNewTab,
    kCloseTab,
    kNextTab,
    kPreviousTab,
    kNewSpace,
    kCloseSpace,
    // U6 split view. kToggleSplit pairs the active tab with its adjacent tab
    // or tears the pair down; the divider commands nudge the flex ratio. Split
    // view is scoped to one space, so the commands never name tabs from
    // different spaces.
    kToggleSplit,
    kMoveDividerLeft,
    kMoveDividerRight,
    // Arc-style pinned tabs: pins or unpins the active tab.
    kTogglePinTab,
};

}  // namespace island

#endif
