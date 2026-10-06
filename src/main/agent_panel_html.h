#ifndef ISLAND_AGENT_PANEL_HTML_H_
#define ISLAND_AGENT_PANEL_HTML_H_

#include <string_view>

namespace island {

// The agent panel page (src/main/agent_panel.html), embedded at configure time
// by the root CMakeLists.txt into a generated agent_panel_html.cc.
[[nodiscard]] std::string_view AgentPanelHtml() noexcept;

}  // namespace island

#endif  // ISLAND_AGENT_PANEL_HTML_H_
