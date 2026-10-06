#ifndef ISLAND_AGENT_MCP_SERVER_H_
#define ISLAND_AGENT_MCP_SERVER_H_

// Model Context Protocol server core: handles one JSON-RPC message at a time
// (initialize, ping, tools/list, tools/call) against a BrowserToolbox. The
// transport (HTTP on loopback, or the stdio bridge) lives elsewhere; this
// class only maps request text to response text.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "browser_tools.h"

namespace island::agent {

class McpServer {
  public:
    // Runs `task` on the thread that owns the toolbox (the CEF UI thread in
    // the browser, inline in tests).
    using Dispatcher = std::function<void(std::function<void()> task)>;
    // Receives the response text, or std::nullopt for notifications (which
    // have no response). Called exactly once per HandleMessage.
    using Reply = std::function<void(std::optional<std::string>)>;

    McpServer(BrowserToolbox& toolbox, Dispatcher dispatcher);

    void HandleMessage(std::string_view text, Reply reply);

    static constexpr std::string_view kServerName = "island-browser";
    static constexpr std::string_view kLatestProtocolVersion = "2025-06-18";

  private:
    BrowserToolbox& toolbox_;
    Dispatcher dispatcher_;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_MCP_SERVER_H_
