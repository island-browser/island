#ifndef ISLAND_AGENT_AGENT_ENDPOINT_H_
#define ISLAND_AGENT_AGENT_ENDPOINT_H_

// The browser's MCP endpoint for AI agents: http://127.0.0.1:<port>/mcp,
// guarded by a per-launch bearer token and DNS-rebinding checks (Host and
// Origin must be loopback). The URL and token are published to a 0600
// discovery file so local agents (and island_mcp_bridge) can connect.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "http_server.h"
#include "mcp_server.h"

namespace island::agent {

class AgentEndpoint {
  public:
    AgentEndpoint(BrowserToolbox& toolbox, McpServer::Dispatcher dispatcher);
    ~AgentEndpoint();

    AgentEndpoint(const AgentEndpoint&) = delete;
    AgentEndpoint& operator=(const AgentEndpoint&) = delete;

    // Starts listening on 127.0.0.1:`port` (0 picks a free port).
    bool Start(int port, std::string* error = nullptr);
    void Stop();

    [[nodiscard]] bool running() const { return http_.running(); }
    [[nodiscard]] int port() const { return http_.port(); }
    [[nodiscard]] std::string url() const;
    [[nodiscard]] const std::string& token() const { return token_; }

    // Writes {"url": ..., "token": ..., "pid": ...} with owner-only
    // permissions. Removed again by Stop().
    bool WriteDiscoveryFile(const std::filesystem::path& path);

    // Request policy, exposed for tests. Returns an empty response status (0)
    // when the request may proceed to the MCP server.
    [[nodiscard]] HttpResponse CheckRequest(const HttpRequest& request) const;

    static constexpr int kDefaultPort = 9223;
    static constexpr std::string_view kPath = "/mcp";

  private:
    void Handle(const HttpRequest& request, LoopbackHttpServer::Respond respond);

    McpServer mcp_;
    LoopbackHttpServer http_;
    std::string token_;
    std::filesystem::path discovery_file_;
};

// Platform app-data location of the discovery file, next to session.json:
// ~/Library/Application Support/Island (macOS), $XDG_DATA_HOME/Island or
// ~/.local/share/Island (Linux), %APPDATA%/Island (Windows).
[[nodiscard]] std::filesystem::path DefaultDiscoveryFilePath();

// Generates a random 32-byte hex token.
[[nodiscard]] std::string GenerateToken();
// True for a Host header naming 127.0.0.1, localhost, or [::1] (any port).
[[nodiscard]] bool IsLoopbackHost(std::string_view host);
// True for an Origin of http(s)://127.0.0.1|localhost|[::1] (any port).
[[nodiscard]] bool IsLoopbackOrigin(std::string_view origin);

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_ENDPOINT_H_
