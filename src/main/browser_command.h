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
};

}

#endif
