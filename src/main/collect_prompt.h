#ifndef ISLAND_COLLECT_PROMPT_H_
#define ISLAND_COLLECT_PROMPT_H_

// The CEF-free half of "Collect from tabs": the prompt that asks the agent to read the chosen
// tabs and answer with one Markdown table. The table is taken from the turn with the Research
// report markers (research_prompt.h), so the same extraction serves both.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island {

// The tab the person picked: its index in the Island window's tab list, and its title and URL
// (web content, quoted into the prompt as data).
struct CollectTab {
    std::size_t index = 0;
    std::string title;
    std::string url;

    bool operator==(const CollectTab&) const = default;
};

// What to collect is clipped to this many bytes (on a UTF-8 boundary).
inline constexpr std::size_t kMaxCollectWhatBytes = 1000;
// A collection reads at most this many tabs.
inline constexpr std::size_t kMaxCollectTabs = 30;

// The instruction for `what` over `tabs`: the tabs inside tags labelled as web content, then the
// table format the report is read from.
[[nodiscard]] std::string BuildCollectPrompt(std::string_view what,
                                             const std::vector<CollectTab>& tabs);

}  // namespace island

#endif  // ISLAND_COLLECT_PROMPT_H_
