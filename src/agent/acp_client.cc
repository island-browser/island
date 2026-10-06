#include "acp_client.h"

#include "jsonrpc.h"

namespace island::agent {

namespace {

using json::Value;

// ACP reports "authentication required" with this JSON-RPC error code.
constexpr std::int64_t kAuthRequired = -32000;

std::string ContentText(const Value* content) {
    if (content == nullptr || !content->IsObject()) return {};
    const std::string_view type = content->StringOr("type", "");
    if (type == "text") return std::string(content->StringOr("text", ""));
    if (type == "image") return "[image]";
    if (type == "audio") return "[audio]";
    if (type == "resource_link") {
        return "[" + std::string(content->StringOr("name", content->StringOr("uri", "link"))) + "]";
    }
    if (type == "resource") return "[resource]";
    return {};
}

}  // namespace

std::string_view AcpStateName(AcpState state) {
    switch (state) {
        case AcpState::kIdle:
            return "idle";
        case AcpState::kInitializing:
            return "initializing";
        case AcpState::kCreatingSession:
            return "creating_session";
        case AcpState::kReady:
            return "ready";
        case AcpState::kPrompting:
            return "prompting";
        case AcpState::kFailed:
            return "failed";
    }
    return "unknown";
}

AcpClient::AcpClient(Send send, EventSink sink) : send_(std::move(send)), sink_(std::move(sink)) {}

void AcpClient::Start(std::string cwd, std::vector<AcpMcpServer> mcp_servers) {
    if (state_ != AcpState::kIdle && state_ != AcpState::kFailed) return;
    cwd_ = std::move(cwd);
    mcp_servers_ = std::move(mcp_servers);
    session_id_.clear();
    pending_.clear();
    permission_requests_.clear();
    SetState(AcpState::kInitializing);
    Value capabilities = Value::MakeObject()
                             .Set("fs", Value::MakeObject()
                                            .Set("readTextFile", Value::Bool(false))
                                            .Set("writeTextFile", Value::Bool(false)))
                             .Set("terminal", Value::Bool(false));
    SendRequest("initialize",
                Value::MakeObject()
                    .Set("protocolVersion", Value::Int(kProtocolVersion))
                    .Set("clientCapabilities", std::move(capabilities))
                    .Set("clientInfo", Value::MakeObject()
                                           .Set("name", Value::String("island-browser"))
                                           .Set("title", Value::String("Island"))
                                           .Set("version", Value::String("0.4.0"))));
}

bool AcpClient::Prompt(std::string_view text, std::string_view context) {
    if (state_ != AcpState::kReady || text.empty()) return false;
    Value prompt = Value::MakeArray();
    if (!context.empty()) {
        prompt.Push(Value::MakeObject()
                        .Set("type", Value::String("text"))
                        .Set("text", Value::String(std::string(context))));
    }
    prompt.Push(Value::MakeObject()
                    .Set("type", Value::String("text"))
                    .Set("text", Value::String(std::string(text))));
    SetState(AcpState::kPrompting);
    SendRequest("session/prompt", Value::MakeObject()
                                      .Set("sessionId", Value::String(session_id_))
                                      .Set("prompt", std::move(prompt)));
    return true;
}

void AcpClient::Cancel() {
    if (state_ != AcpState::kPrompting) return;
    for (const auto& [local_id, agent_id] : permission_requests_) {
        send_(jsonrpc::MakeResult(
            agent_id,
            Value::MakeObject().Set(
                "outcome", Value::MakeObject().Set("outcome", Value::String("cancelled")))));
    }
    permission_requests_.clear();
    send_(jsonrpc::MakeNotification(
        "session/cancel", Value::MakeObject().Set("sessionId", Value::String(session_id_))));
}

void AcpClient::ResolvePermission(std::int64_t request_id, std::optional<std::string> option_id) {
    auto it = permission_requests_.find(request_id);
    if (it == permission_requests_.end()) return;
    Value outcome = Value::MakeObject();
    if (option_id) {
        outcome.Set("outcome", Value::String("selected"))
            .Set("optionId", Value::String(std::move(*option_id)));
    } else {
        outcome.Set("outcome", Value::String("cancelled"));
    }
    send_(jsonrpc::MakeResult(it->second, Value::MakeObject().Set("outcome", std::move(outcome))));
    permission_requests_.erase(it);
}

void AcpClient::HandleLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    if (line.empty()) return;
    jsonrpc::Message message = jsonrpc::Classify(line);
    switch (message.kind) {
        case jsonrpc::MessageKind::kInvalid:
            // Agents occasionally log to stdout; ignore anything that is not
            // JSON-RPC rather than failing the session.
            return;
        case jsonrpc::MessageKind::kResponse:
            if (message.id.IsInt()) HandleResponse(message.id.int_val, true, message.result, {});
            return;
        case jsonrpc::MessageKind::kErrorResponse:
            if (message.id.IsInt()) {
                if (message.error_code == kAuthRequired) auth_required_ = true;
                HandleResponse(message.id.int_val, false, {}, message.error_message);
            }
            return;
        case jsonrpc::MessageKind::kRequest:
            HandleAgentRequest(message.id, message.method, message.params);
            return;
        case jsonrpc::MessageKind::kNotification:
            if (message.method == "session/update") HandleSessionUpdate(message.params);
            return;
    }
}

void AcpClient::OnTransportClosed(std::string_view reason) {
    if (state_ == AcpState::kFailed) return;
    pending_.clear();
    permission_requests_.clear();
    Fail(reason.empty() ? std::string("The agent process exited.") : std::string(reason));
}

std::int64_t AcpClient::SendRequest(std::string_view method, json::Value params) {
    const std::int64_t id = next_id_++;
    pending_[id] = std::string(method);
    send_(jsonrpc::MakeRequest(Value::Int(id), method, std::move(params)));
    return id;
}

void AcpClient::SetState(AcpState state) {
    if (state_ == state) return;
    state_ = state;
    AcpEvent event;
    event.type = AcpEvent::Type::kStateChanged;
    event.state = state;
    Emit(std::move(event));
}

void AcpClient::Emit(AcpEvent event) {
    if (sink_) sink_(event);
}

void AcpClient::Fail(std::string message) {
    AcpEvent event;
    event.type = AcpEvent::Type::kError;
    event.text = std::move(message);
    Emit(std::move(event));
    SetState(AcpState::kFailed);
}

void AcpClient::HandleResponse(std::int64_t id, bool ok, const json::Value& result,
                               std::string_view error_message) {
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    const std::string method = it->second;
    pending_.erase(it);

    if (method == "initialize") {
        if (!ok) {
            Fail("The agent rejected initialize: " + std::string(error_message));
            return;
        }
        const std::int64_t version = result.IntOr("protocolVersion", 0);
        if (version != kProtocolVersion) {
            Fail("The agent speaks ACP version " + std::to_string(version) + "; Island speaks " +
                 std::to_string(kProtocolVersion) + ".");
            return;
        }
        if (const Value* caps = result.FindMember("agentCapabilities")) {
            if (const Value* mcp = caps->FindMember("mcpCapabilities")) {
                agent_http_mcp_ = mcp->BoolOr("http", false);
            }
        }
        if (const Value* info = result.FindMember("agentInfo")) {
            agent_name_ = std::string(info->StringOr("title", info->StringOr("name", "")));
        }
        CreateSession();
        return;
    }
    if (method == "session/new") {
        if (!ok) {
            Fail(auth_required_
                     ? "The agent needs you to sign in first. Run the agent once in a terminal "
                       "to log in, then restart it here."
                     : "The agent could not open a session: " + std::string(error_message));
            return;
        }
        session_id_ = std::string(result.StringOr("sessionId", ""));
        if (session_id_.empty()) {
            Fail("The agent opened a session without an id.");
            return;
        }
        SetState(AcpState::kReady);
        return;
    }
    if (method == "session/prompt") {
        // Late permission requests from a finished turn can no longer matter.
        permission_requests_.clear();
        AcpEvent event;
        if (ok) {
            event.type = AcpEvent::Type::kTurnEnded;
            event.text = std::string(result.StringOr("stopReason", "end_turn"));
        } else {
            event.type = AcpEvent::Type::kError;
            event.text = "The turn failed: " + std::string(error_message);
        }
        SetState(AcpState::kReady);
        Emit(std::move(event));
        return;
    }
}

void AcpClient::CreateSession() {
    SetState(AcpState::kCreatingSession);
    SendRequest(
        "session/new",
        Value::MakeObject().Set("cwd", Value::String(cwd_)).Set("mcpServers", McpServersJson()));
}

json::Value AcpClient::McpServersJson() const {
    Value servers = Value::MakeArray();
    for (const AcpMcpServer& server : mcp_servers_) {
        if (agent_http_mcp_) {
            Value headers = Value::MakeArray();
            if (!server.bearer_token.empty()) {
                headers.Push(Value::MakeObject()
                                 .Set("name", Value::String("Authorization"))
                                 .Set("value", Value::String("Bearer " + server.bearer_token)));
            }
            servers.Push(Value::MakeObject()
                             .Set("type", Value::String("http"))
                             .Set("name", Value::String(server.name))
                             .Set("url", Value::String(server.url))
                             .Set("headers", std::move(headers)));
        } else if (!server.bridge_command.empty()) {
            // stdio is the baseline every ACP agent supports. The token
            // travels in the environment, not argv, so it stays out of `ps`.
            Value env = Value::MakeArray();
            env.Push(Value::MakeObject()
                         .Set("name", Value::String("ISLAND_MCP_URL"))
                         .Set("value", Value::String(server.url)));
            env.Push(Value::MakeObject()
                         .Set("name", Value::String("ISLAND_MCP_TOKEN"))
                         .Set("value", Value::String(server.bearer_token)));
            servers.Push(Value::MakeObject()
                             .Set("name", Value::String(server.name))
                             .Set("command", Value::String(server.bridge_command))
                             .Set("args", Value::MakeArray())
                             .Set("env", std::move(env)));
        }
    }
    return servers;
}

void AcpClient::HandleAgentRequest(const json::Value& id, std::string_view method,
                                   const json::Value& params) {
    if (method == "session/request_permission") {
        const std::int64_t local_id = next_permission_id_++;
        permission_requests_[local_id] = id;
        AcpEvent event;
        event.type = AcpEvent::Type::kPermissionRequest;
        event.request_id = local_id;
        if (const Value* tool_call = params.FindMember("toolCall")) {
            event.tool_call_id = std::string(tool_call->StringOr("toolCallId", ""));
            event.title = std::string(tool_call->StringOr("title", ""));
            event.kind = std::string(tool_call->StringOr("kind", ""));
        }
        if (const Value* options = params.FindMember("options"); options && options->IsArray()) {
            for (const Value& option : options->array_val) {
                event.options.push_back({std::string(option.StringOr("optionId", "")),
                                         std::string(option.StringOr("name", "")),
                                         std::string(option.StringOr("kind", ""))});
            }
        }
        Emit(std::move(event));
        return;
    }
    // Island advertises no fs/terminal capabilities, so any other client
    // method is unsupported.
    send_(jsonrpc::MakeError(id, jsonrpc::kMethodNotFound,
                             "Island does not implement " + std::string(method)));
}

void AcpClient::HandleSessionUpdate(const json::Value& params) {
    const Value* update = params.FindMember("update");
    if (update == nullptr || !update->IsObject()) return;
    const std::string_view kind = update->StringOr("sessionUpdate", "");
    AcpEvent event;
    if (kind == "agent_message_chunk" || kind == "agent_thought_chunk") {
        event.type = kind == "agent_message_chunk" ? AcpEvent::Type::kAgentMessageChunk
                                                   : AcpEvent::Type::kAgentThoughtChunk;
        event.text = ContentText(update->FindMember("content"));
        if (event.text.empty()) return;
        Emit(std::move(event));
        return;
    }
    if (kind == "tool_call" || kind == "tool_call_update") {
        event.type =
            kind == "tool_call" ? AcpEvent::Type::kToolCall : AcpEvent::Type::kToolCallUpdate;
        event.tool_call_id = std::string(update->StringOr("toolCallId", ""));
        event.title = std::string(update->StringOr("title", ""));
        event.kind = std::string(update->StringOr("kind", ""));
        event.status = std::string(
            update->StringOr("status", kind == "tool_call" ? std::string_view("pending") : ""));
        if (event.tool_call_id.empty()) return;
        Emit(std::move(event));
        return;
    }
    if (kind == "plan") {
        event.type = AcpEvent::Type::kPlan;
        if (const Value* entries = update->FindMember("entries"); entries && entries->IsArray()) {
            for (const Value& entry : entries->array_val) {
                event.plan.push_back({std::string(entry.StringOr("content", "")),
                                      std::string(entry.StringOr("status", "pending"))});
            }
        }
        Emit(std::move(event));
        return;
    }
    // user_message_chunk echoes our own prompt; mode and command updates are
    // not surfaced yet.
}

}  // namespace island::agent
