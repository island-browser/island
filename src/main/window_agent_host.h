#ifndef ISLAND_WINDOW_AGENT_HOST_H_
#define ISLAND_WINDOW_AGENT_HOST_H_

// Connects the CEF-free agent kernel (src/agent) to the live window: it
// implements agent::AgentBrowserHost on top of BrowserWindow's public seams,
// owns the DevTools bridge, the browser toolbox, and the MCP endpoint, and
// hops tool calls from the endpoint's HTTP threads onto the CEF UI thread.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agent_endpoint.h"
#include "browser_tools.h"
#include "devtools_bridge.h"
#include "include/cef_browser.h"

namespace island {

class BrowserWindow;

class WindowAgentHost final : public agent::AgentBrowserHost {
  public:
    explicit WindowAgentHost(BrowserWindow& window);
    ~WindowAgentHost() override;

    WindowAgentHost(const WindowAgentHost&) = delete;
    WindowAgentHost& operator=(const WindowAgentHost&) = delete;

    // Starts the MCP endpoint on 127.0.0.1 (ISLAND_AGENT_PORT, default 9223,
    // falling back to a free port) and publishes the discovery file.
    // ISLAND_AGENT_ENDPOINT=off disables it. Returns whether it is serving.
    bool StartEndpoint(const std::filesystem::path& discovery_file);
    // Stops serving and fails every in-flight DevTools call. Idempotent; the
    // window calls it when it starts closing.
    void Shutdown();
    void OnBrowserClosed(CefRefPtr<CefBrowser> browser);

    [[nodiscard]] agent::BrowserToolbox& toolbox() { return toolbox_; }
    [[nodiscard]] const agent::AgentEndpoint* endpoint() const { return endpoint_.get(); }

    // agent::AgentBrowserHost
    std::vector<agent::AgentSpaceInfo> ListSpaces() override;
    std::vector<agent::AgentTabInfo> ListTabs() override;
    agent::HostStatus OpenTab(std::string_view url) override;
    agent::HostStatus Navigate(std::optional<std::size_t> tab, std::string_view url) override;
    agent::HostStatus ActivateTab(std::size_t tab) override;
    agent::HostStatus CloseTab(std::size_t tab) override;
    agent::HostStatus PinTab(std::size_t tab, bool pinned) override;
    agent::HostStatus SwitchSpace(std::size_t space) override;
    agent::HostStatus NewSpace(std::string_view name) override;
    agent::HostStatus RunAction(agent::AgentBrowserAction action) override;
    void DevToolsCall(std::optional<std::size_t> tab, std::string_view method, json::Value params,
                      agent::DevToolsCallback done) override;

    // Shared with posted tasks: cleared on Shutdown so a task that arrives
    // after the window started closing does nothing.
    struct Guard {
        WindowAgentHost* host = nullptr;
    };

  private:
    BrowserWindow* window_;
    DevToolsBridge devtools_;
    agent::BrowserToolbox toolbox_;
    std::shared_ptr<Guard> guard_;
    std::unique_ptr<agent::AgentEndpoint> endpoint_;
};

}  // namespace island

#endif  // ISLAND_WINDOW_AGENT_HOST_H_
