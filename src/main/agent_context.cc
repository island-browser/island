#include "agent_context.h"

namespace island {

namespace {

bool IsBlank(std::string_view text) {
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

}  // namespace

std::string BuildAgentContext(std::string_view url, std::string_view title,
                              std::string_view selection) {
    std::string context = "Page: ";
    context += title;
    context += "\nURL: ";
    context += url;
    if (IsBlank(selection)) {
        return context;
    }
    bool clipped = false;
    if (selection.size() > kAgentSelectionLimit) {
        std::size_t end = kAgentSelectionLimit;
        // A continuation byte (10xxxxxx) would start mid-code-point: step back to its lead byte.
        while (end > 0 && (static_cast<unsigned char>(selection[end]) & 0xC0U) == 0x80U) {
            --end;
        }
        selection = selection.substr(0, end);
        clipped = true;
    }
    context += "\nSelection:\n";
    context += selection;
    if (clipped) {
        context += "\xE2\x80\xA6";  // …
    }
    return context;
}

std::vector<AgentMenuItem> AgentContextMenuItems(bool has_agent, bool has_selection,
                                                 bool has_link) {
    std::vector<AgentMenuItem> items;
    if (!has_agent) {
        return items;
    }
    if (has_selection) {
        items.push_back({kAgentMenuAsk, "Ask agent about selection"});
        items.push_back({kAgentMenuExplain, "Explain"});
    }
    if (has_link) {
        items.push_back({kAgentMenuSummarizeLink, "Summarize linked page"});
    }
    return items;
}

}  // namespace island
