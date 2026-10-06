#ifndef ISLAND_AGENT_ACP_CLIENT_H_
#define ISLAND_AGENT_ACP_CLIENT_H_

// Agent Client Protocol (ACP) client: Island is the "client" (editor role)
// and talks newline-delimited JSON-RPC to an agent subprocess such as Claude
// Code's ACP adapter or `gemini --experimental-acp`. The browser hands the
// agent its own MCP endpoint in session/new, so the agent drives the browser
// through the same tools external agents use.
//
// Transport-agnostic and CEF-free: lines come in through HandleLine(), go
// out through the Send callback, and everything the UI shows is reported as
// AcpEvents. Not thread-safe: call it from one thread (the UI thread).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json_util.h"

namespace island::agent {

enum class AcpState : std::uint8_t {
    kIdle,             // not started
    kInitializing,     // initialize sent
    kCreatingSession,  // session/new sent
    kReady,            // session open, no prompt running
    kPrompting,        // session/prompt in flight
    kFailed,           // handshake failed or the agent exited
};

[[nodiscard]] std::string_view AcpStateName(AcpState state);

struct AcpPermissionOption {
    std::string id;
    std::string name;
    std::string kind;  // allow_once | allow_always | reject_once | reject_always

    bool operator==(const AcpPermissionOption&) const = default;
};

struct AcpPlanEntry {
    std::string content;
    std::string status;  // pending | in_progress | completed

    bool operator==(const AcpPlanEntry&) const = default;
};

struct AcpEvent {
    enum class Type : std::uint8_t {
        kStateChanged,
        kAgentMessageChunk,
        kAgentThoughtChunk,
        kToolCall,
        kToolCallUpdate,
        kPlan,
        kPermissionRequest,
        kTurnEnded,
        kError,
    };
    Type type = Type::kStateChanged;
    AcpState state = AcpState::kIdle;
    // Chunk text, error message, or stop reason.
    std::string text;
    // Tool calls.
    std::string tool_call_id;
    std::string title;
    std::string kind;
    std::string status;
    // Plans.
    std::vector<AcpPlanEntry> plan;
    // Permission requests: echo `request_id` to ResolvePermission().
    std::int64_t request_id = 0;
    std::vector<AcpPermissionOption> options;
};

// An MCP server handed to the agent. When the agent supports MCP over HTTP
// the URL form is used; otherwise the stdio bridge command (if any) is
// launched by the agent with the URL and token in its environment.
struct AcpMcpServer {
    std::string name;
    std::string url;
    std::string bearer_token;
    std::string bridge_command;  // absolute path to island_mcp_bridge; optional
};

class AcpClient {
  public:
    using Send = std::function<void(std::string line)>;
    using EventSink = std::function<void(const AcpEvent&)>;

    AcpClient(Send send, EventSink sink);

    // Sends initialize, then session/new once the agent answers.
    void Start(std::string cwd, std::vector<AcpMcpServer> mcp_servers);
    // Sends one user turn. Returns false unless the session is ready.
    bool Prompt(std::string_view text);
    // Cancels the running turn (session/cancel) and answers every open
    // permission request as cancelled, as the protocol requires.
    void Cancel();
    // Answers a session/request_permission. std::nullopt answers "cancelled".
    void ResolvePermission(std::int64_t request_id, std::optional<std::string> option_id);
    // Feeds one line received from the agent's stdout.
    void HandleLine(std::string_view line);
    // The transport is gone (agent exited); fails the session.
    void OnTransportClosed(std::string_view reason);

    [[nodiscard]] AcpState state() const noexcept { return state_; }
    [[nodiscard]] const std::string& session_id() const noexcept { return session_id_; }
    [[nodiscard]] const std::string& agent_name() const noexcept { return agent_name_; }
    [[nodiscard]] bool agent_supports_http_mcp() const noexcept { return agent_http_mcp_; }
    [[nodiscard]] std::size_t pending_permission_count() const noexcept {
        return permission_requests_.size();
    }

    static constexpr std::int64_t kProtocolVersion = 1;

  private:
    std::int64_t SendRequest(std::string_view method, json::Value params);
    void SetState(AcpState state);
    void Emit(AcpEvent event);
    void Fail(std::string message);
    void HandleResponse(std::int64_t id, bool ok, const json::Value& result,
                        std::string_view error_message);
    void HandleAgentRequest(const json::Value& id, std::string_view method,
                            const json::Value& params);
    void HandleSessionUpdate(const json::Value& params);
    void CreateSession();
    json::Value McpServersJson() const;

    Send send_;
    EventSink sink_;
    AcpState state_ = AcpState::kIdle;
    std::int64_t next_id_ = 1;
    std::map<std::int64_t, std::string> pending_;  // request id -> method
    // Our local id -> the agent's original JSON-RPC id for open permission
    // requests.
    std::map<std::int64_t, json::Value> permission_requests_;
    std::int64_t next_permission_id_ = 1;
    std::string cwd_;
    std::vector<AcpMcpServer> mcp_servers_;
    std::string session_id_;
    std::string agent_name_;
    bool agent_http_mcp_ = false;
    bool auth_required_ = false;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_ACP_CLIENT_H_
