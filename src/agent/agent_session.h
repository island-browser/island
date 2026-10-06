#ifndef ISLAND_AGENT_AGENT_SESSION_H_
#define ISLAND_AGENT_AGENT_SESSION_H_

// One conversation with an ACP agent, as the sidebar panel sees it: owns the
// agent subprocess, the ACP client, and the transcript, queues a message sent
// while the agent is still starting, and reports every change through one
// callback. All public methods and callbacks run on the owner thread (the CEF
// UI thread in the browser); process I/O is hopped there by the dispatcher.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "acp_client.h"
#include "agent_process.h"
#include "agent_transcript.h"

namespace island::agent {

struct AgentSessionConfig {
    // Shell command line, e.g. "npx -y @agentclientprotocol/claude-agent-acp".
    std::string command;
    // The provider the command came from (agent_providers.h) and its display
    // name, which stands in for the agent's own name until it introduces
    // itself. Both optional.
    std::string provider_id;
    std::string provider_name;
    std::filesystem::path cwd;
    std::vector<AcpMcpServer> mcp_servers;
    // Run through the user's login shell ($SHELL -lc) so PATH matches a
    // terminal even when the app was launched from the Dock or Finder.
    bool use_login_shell = true;
};

class AgentSession {
  public:
    using Dispatcher = std::function<void(std::function<void()> task)>;
    using ChangeCallback = std::function<void()>;

    AgentSession(Dispatcher dispatcher, ChangeCallback on_change);
    ~AgentSession();

    AgentSession(const AgentSession&) = delete;
    AgentSession& operator=(const AgentSession&) = delete;

    // Starts (or restarts) the agent with `config`. False when the command is
    // empty or cannot be launched; error() says why.
    bool Start(AgentSessionConfig config);
    // Records the config Send() will start with, without launching anything.
    void Configure(AgentSessionConfig config) { config_ = std::move(config); }
    void Stop();
    // Sends a user turn, starting the agent first when it is not running.
    // While the agent is still connecting the turn is queued. False when the
    // text is blank or a turn is already running.
    bool Send(std::string text, std::string context = {});
    void Cancel();
    void ResolvePermission(std::int64_t request_id, std::optional<std::string> option_id);
    // Stops the agent, clears the conversation, and starts a fresh session.
    void NewChat();
    // Switches to another agent: stops the current one, clears the
    // conversation and any error, and adopts `config`. The new agent is
    // launched right away only when one was running; otherwise the next
    // Send() starts it. Returns whether it launched.
    bool SwitchAgent(AgentSessionConfig config);

    [[nodiscard]] AcpState state() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool running() const noexcept { return process_ != nullptr; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const AgentTranscript& transcript() const noexcept { return transcript_; }
    [[nodiscard]] const AgentSessionConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::string agent_name() const;
    // A short human status line: "Starting Claude Code...", "Ready", ...
    [[nodiscard]] std::string StatusText() const;
    // Everything the panel renders, as one JSON object.
    [[nodiscard]] std::string StateJson() const;

    // The argv that starts the agent: the command wrapped in the login shell
    // (POSIX, by default) or split into words. Empty when the command is
    // blank or its quotes do not balance.
    [[nodiscard]] static std::vector<std::string> LaunchArgv(const AgentSessionConfig& config);

  private:
    struct Alive {
        AgentSession* session = nullptr;
    };

    void OnLine(std::string line);
    void OnExit(int exit_code, std::string stderr_tail);
    void OnEvent(const AcpEvent& event);
    void FlushQueuedPrompt();
    void Changed();

    Dispatcher dispatcher_;
    ChangeCallback on_change_;
    std::shared_ptr<Alive> alive_;
    AgentSessionConfig config_;
    std::unique_ptr<AgentProcess> process_;
    std::unique_ptr<AcpClient> client_;
    AgentTranscript transcript_;
    std::string error_;
    std::optional<std::pair<std::string, std::string>> queued_prompt_;
    std::uint64_t generation_ = 0;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_SESSION_H_
