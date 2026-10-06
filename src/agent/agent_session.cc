#include "agent_session.h"

#include <cstdlib>
#include <utility>

#include "json_util.h"

namespace island::agent {

namespace {

bool IsBlank(std::string_view text) {
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

// The last few non-empty stderr lines, which is where agents explain why
// they could not start ("command not found", "please log in", ...).
std::string StderrSummary(const std::string& tail) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < tail.size()) {
        std::size_t end = tail.find('\n', start);
        if (end == std::string::npos) end = tail.size();
        std::string line = tail.substr(start, end - start);
        if (!IsBlank(line)) lines.push_back(std::move(line));
        start = end + 1;
    }
    std::string summary;
    const std::size_t first = lines.size() > 4 ? lines.size() - 4 : 0;
    for (std::size_t i = first; i < lines.size(); ++i) {
        if (!summary.empty()) summary += '\n';
        summary += lines[i];
    }
    return summary;
}

}  // namespace

AgentSession::AgentSession(Dispatcher dispatcher, ChangeCallback on_change)
    : dispatcher_(std::move(dispatcher)),
      on_change_(std::move(on_change)),
      alive_(std::make_shared<Alive>()) {
    alive_->session = this;
}

AgentSession::~AgentSession() {
    alive_->session = nullptr;
    on_change_ = nullptr;
    Stop();
}

std::vector<std::string> AgentSession::LaunchArgv(const AgentSessionConfig& config) {
    std::vector<std::string> argv = SplitCommandLine(config.command);
#if !defined(_WIN32)
    // A command the splitter rejects (unbalanced quotes) would fail in the
    // shell too; refusing it here keeps the clear parse error and starts no
    // process.
    if (config.use_login_shell && !argv.empty()) {
        const char* shell = std::getenv("SHELL");
        const std::string shell_path = shell != nullptr && *shell != '\0' ? shell : "/bin/sh";
        return {shell_path, "-lc", "exec " + config.command};
    }
#endif
    return argv;
}

bool AgentSession::Start(AgentSessionConfig config) {
    Stop();
    config_ = std::move(config);
    error_.clear();
    if (IsBlank(config_.command)) {
        error_ = "No agent command is set. Choose an agent, or enter a custom command in Settings.";
        Changed();
        return false;
    }
    const std::vector<std::string> argv = LaunchArgv(config_);
    if (argv.empty()) {
        error_ = "The agent command could not be parsed (check its quotes).";
        Changed();
        return false;
    }

    const std::uint64_t generation = ++generation_;
    std::weak_ptr<Alive> weak = alive_;
    process_ = std::make_unique<AgentProcess>();
    client_ = std::make_unique<AcpClient>(
        [this](std::string line) {
            if (process_ != nullptr) process_->WriteLine(line);
        },
        [this](const AcpEvent& event) { OnEvent(event); });

    std::string start_error;
    const bool started = process_->Start(
        argv, config_.cwd,
        [dispatcher = dispatcher_, weak, generation](std::string line) {
            dispatcher([weak, generation, line = std::move(line)]() mutable {
                std::shared_ptr<Alive> alive = weak.lock();
                if (alive && alive->session && alive->session->generation_ == generation) {
                    alive->session->OnLine(std::move(line));
                }
            });
        },
        [dispatcher = dispatcher_, weak, generation](int code, std::string tail) {
            dispatcher([weak, generation, code, tail = std::move(tail)]() mutable {
                std::shared_ptr<Alive> alive = weak.lock();
                if (alive && alive->session && alive->session->generation_ == generation) {
                    alive->session->OnExit(code, std::move(tail));
                }
            });
        },
        &start_error);
    if (!started) {
        client_.reset();
        process_.reset();
        error_ = start_error.empty() ? "The agent could not be started." : start_error;
        Changed();
        return false;
    }
    client_->Start(config_.cwd.string(), config_.mcp_servers);
    Changed();
    return true;
}

void AgentSession::Stop() {
    ++generation_;
    queued_prompt_.reset();
    client_.reset();
    if (process_ != nullptr) {
        process_->Terminate();
        process_.reset();
    }
}

bool AgentSession::Send(std::string text, std::string context) {
    if (IsBlank(text) || busy()) return false;
    if (client_ == nullptr || client_->state() == AcpState::kFailed) {
        if (!Start(config_)) return false;
    }
    transcript_.AddUserPrompt(text);
    if (client_->state() == AcpState::kReady) {
        client_->Prompt(text, context);
    } else {
        queued_prompt_ = std::make_pair(std::move(text), std::move(context));
    }
    Changed();
    return true;
}

void AgentSession::Cancel() {
    if (queued_prompt_.has_value()) {
        queued_prompt_.reset();
        transcript_.AddNotice("Stopped.");
    }
    if (client_ != nullptr) client_->Cancel();
    transcript_.CancelOpenPermissions();
    Changed();
}

void AgentSession::ResolvePermission(std::int64_t request_id,
                                     std::optional<std::string> option_id) {
    std::string label = "Denied";
    for (const TranscriptItem& item : transcript_.items()) {
        if (item.kind != TranscriptItem::Kind::kPermission || item.request_id != request_id) {
            continue;
        }
        for (const AcpPermissionOption& option : item.options) {
            if (option_id && option.id == *option_id) label = option.name;
        }
    }
    if (client_ != nullptr) client_->ResolvePermission(request_id, std::move(option_id));
    transcript_.ResolvePermission(request_id, label);
    Changed();
}

void AgentSession::NewChat() {
    AgentSessionConfig config = config_;
    Stop();
    transcript_.Clear();
    error_.clear();
    if (!IsBlank(config.command)) {
        Start(std::move(config));
    } else {
        Changed();
    }
}

bool AgentSession::SwitchAgent(AgentSessionConfig config) {
    const bool was_running = running();
    Stop();
    transcript_.Clear();
    error_.clear();
    config_ = std::move(config);
    if (was_running && !IsBlank(config_.command)) {
        return Start(config_);
    }
    Changed();
    return false;
}

AcpState AgentSession::state() const noexcept {
    if (client_ != nullptr) return client_->state();
    return error_.empty() ? AcpState::kIdle : AcpState::kFailed;
}

bool AgentSession::busy() const noexcept {
    return queued_prompt_.has_value() || state() == AcpState::kPrompting;
}

std::string AgentSession::agent_name() const {
    if (client_ != nullptr && !client_->agent_name().empty()) return client_->agent_name();
    return config_.provider_name.empty() ? "Agent" : config_.provider_name;
}

std::string AgentSession::StatusText() const {
    switch (state()) {
        case AcpState::kIdle:
            return "Not running";
        case AcpState::kInitializing:
        case AcpState::kCreatingSession:
            return "Connecting...";
        case AcpState::kReady:
            return queued_prompt_.has_value() ? "Working..." : "Ready";
        case AcpState::kPrompting:
            return "Working...";
        case AcpState::kFailed:
            return "Stopped";
    }
    return "";
}

std::string AgentSession::StateJson() const {
    json::Value items = json::Parse(transcript_.ToJson()).value_or(json::Value::MakeArray());
    json::Value state =
        json::Value::MakeObject()
            .Set("state", json::Value::String(std::string(AcpStateName(this->state()))))
            .Set("status", json::Value::String(StatusText()))
            .Set("agent", json::Value::String(agent_name()))
            .Set("error", json::Value::String(error_))
            .Set("busy", json::Value::Bool(busy()))
            .Set("command", json::Value::String(config_.command))
            .Set("provider", json::Value::String(config_.provider_id))
            .Set("items", std::move(items));
    return json::Serialize(state);
}

void AgentSession::OnLine(std::string line) {
    if (client_ != nullptr) client_->HandleLine(line);
}

void AgentSession::OnExit(int exit_code, std::string stderr_tail) {
    std::string message = "The agent exited";
    if (exit_code >= 0) message += " (code " + std::to_string(exit_code) + ")";
    message += ".";
    const std::string summary = StderrSummary(stderr_tail);
    if (!summary.empty()) message += "\n" + summary;
    queued_prompt_.reset();
    if (client_ != nullptr) client_->OnTransportClosed(message);
    if (process_ != nullptr) {
        process_->Terminate();
        process_.reset();
    }
    Changed();
}

void AgentSession::OnEvent(const AcpEvent& event) {
    transcript_.Apply(event);
    if (event.type == AcpEvent::Type::kError) error_ = event.text;
    if (event.type == AcpEvent::Type::kStateChanged && event.state == AcpState::kReady) {
        if (!queued_prompt_.has_value()) error_.clear();
        FlushQueuedPrompt();
    }
    Changed();
}

void AgentSession::FlushQueuedPrompt() {
    if (!queued_prompt_.has_value() || client_ == nullptr || client_->state() != AcpState::kReady) {
        return;
    }
    auto [text, context] = std::move(*queued_prompt_);
    queued_prompt_.reset();
    client_->Prompt(text, context);
}

void AgentSession::Changed() {
    if (on_change_) on_change_();
}

}  // namespace island::agent
