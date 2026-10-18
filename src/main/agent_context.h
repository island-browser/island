#ifndef ISLAND_AGENT_CONTEXT_H_
#define ISLAND_AGENT_CONTEXT_H_

// What "ask the agent about this" sends along: the page, and optionally the text the person
// selected. CEF-free.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island {

// The selection is clipped to this many bytes (on a UTF-8 boundary) before it is sent.
inline constexpr std::size_t kAgentSelectionLimit = 4096;

// "Page: <title>\nURL: <url>", plus "\nSelection:\n<selection>" when the selection is not blank.
// A selection over kAgentSelectionLimit bytes is cut at a code point boundary and ends in "…".
[[nodiscard]] std::string BuildAgentContext(std::string_view url, std::string_view title,
                                            std::string_view selection);

// Offsets from MENU_ID_USER_FIRST of the page context menu's agent items.
inline constexpr int kAgentMenuAsk = 1;
inline constexpr int kAgentMenuExplain = 2;
inline constexpr int kAgentMenuSummarizeLink = 3;

struct AgentMenuItem {
    int offset = 0;
    std::string_view label;

    bool operator==(const AgentMenuItem&) const = default;
};

// The agent items for a context menu: none without an agent; selection items for a selection;
// "Summarize linked page" for a link.
[[nodiscard]] std::vector<AgentMenuItem> AgentContextMenuItems(bool has_agent, bool has_selection,
                                                               bool has_link);

}  // namespace island

#endif  // ISLAND_AGENT_CONTEXT_H_
