#ifndef ISLAND_AGENT_CONTEXT_H_
#define ISLAND_AGENT_CONTEXT_H_

// What "ask the agent about this" sends along: the page, and optionally the text the person
// selected. CEF-free. Page title, URL and selection are web content: they go into the prompt
// inside tags and are labelled as data, never as instructions.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island {

// The selection is clipped to this many bytes (on a UTF-8 boundary) before it is sent.
inline constexpr std::size_t kAgentSelectionLimit = 4096;
// Titles are clipped to this many bytes too.
inline constexpr std::size_t kAgentTitleLimit = 200;

// The longest prefix of `text` that fits in `limit` bytes and ends on a code point boundary.
// Invalid UTF-8 with no lead byte within three bytes of the cut is cut at `limit` as it is.
[[nodiscard]] std::string_view ClipUtf8(std::string_view text, std::size_t limit);

// The page context for a page the person is looking at: a tool hint, then the title (when there
// is one), the URL and the selection (when it is not blank), each inside its own tag. A selection
// over kAgentSelectionLimit bytes is cut and ends in "…".
[[nodiscard]] std::string BuildAgentContext(std::string_view url, std::string_view title,
                                            std::string_view selection);

// The page context for a link the person has not opened: the agent reads it in a background tab
// and closes that tab afterwards.
[[nodiscard]] std::string BuildLinkAgentContext(std::string_view url);

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
