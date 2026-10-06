#include "mcp_server.h"

#include <array>
#include <utility>

#include "jsonrpc.h"

namespace island::agent {

namespace {

using json::Value;

constexpr std::array<std::string_view, 3> kSupportedProtocolVersions = {"2025-06-18", "2025-03-26",
                                                                        "2024-11-05"};

constexpr std::string_view kInstructions =
    "Island browser tools. Start with browser_list_tabs or page_read to see where you are. "
    "To interact with a page, call page_snapshot to get numbered element refs, then page_click "
    "or page_type with a ref. Snapshot again after navigation, since refs go stale. "
    "page_screenshot shows what the user sees.";

Value ToolsListResult(const BrowserToolbox& toolbox) {
    Value tools = Value::MakeArray();
    for (const ToolDefinition& def : toolbox.definitions()) {
        Value annotations = Value::MakeObject()
                                .Set("title", Value::String(def.title))
                                .Set("readOnlyHint", Value::Bool(def.read_only))
                                .Set("openWorldHint", Value::Bool(true));
        tools.Push(Value::MakeObject()
                       .Set("name", Value::String(def.name))
                       .Set("title", Value::String(def.title))
                       .Set("description", Value::String(def.description))
                       .Set("inputSchema", def.input_schema)
                       .Set("annotations", std::move(annotations)));
    }
    return Value::MakeObject().Set("tools", std::move(tools));
}

std::string NegotiateVersion(std::string_view requested) {
    for (std::string_view supported : kSupportedProtocolVersions) {
        if (supported == requested) return std::string(requested);
    }
    return std::string(McpServer::kLatestProtocolVersion);
}

}  // namespace

McpServer::McpServer(BrowserToolbox& toolbox, Dispatcher dispatcher)
    : toolbox_(toolbox), dispatcher_(std::move(dispatcher)) {}

void McpServer::HandleMessage(std::string_view text, Reply reply) {
    jsonrpc::Message message = jsonrpc::Classify(text);
    switch (message.kind) {
        case jsonrpc::MessageKind::kInvalid:
            reply(
                jsonrpc::MakeError(Value{}, jsonrpc::kInvalidRequest, "Invalid JSON-RPC message"));
            return;
        case jsonrpc::MessageKind::kNotification:
        case jsonrpc::MessageKind::kResponse:
        case jsonrpc::MessageKind::kErrorResponse:
            // notifications/initialized, notifications/cancelled, and stray
            // responses need no reply.
            reply(std::nullopt);
            return;
        case jsonrpc::MessageKind::kRequest:
            break;
    }

    const Value& id = message.id;
    if (message.method == "initialize") {
        const std::string version =
            NegotiateVersion(message.params.StringOr("protocolVersion", kLatestProtocolVersion));
        Value result =
            Value::MakeObject()
                .Set("protocolVersion", Value::String(version))
                .Set("capabilities",
                     Value::MakeObject().Set(
                         "tools", Value::MakeObject().Set("listChanged", Value::Bool(false))))
                .Set("serverInfo", Value::MakeObject()
                                       .Set("name", Value::String(std::string(kServerName)))
                                       .Set("title", Value::String("Island Browser"))
                                       .Set("version", Value::String("0.4.0")))
                .Set("instructions", Value::String(std::string(kInstructions)));
        reply(jsonrpc::MakeResult(id, std::move(result)));
        return;
    }
    if (message.method == "ping") {
        reply(jsonrpc::MakeResult(id, Value::MakeObject()));
        return;
    }
    if (message.method == "tools/list") {
        reply(jsonrpc::MakeResult(id, ToolsListResult(toolbox_)));
        return;
    }
    if (message.method == "tools/call") {
        const std::string_view name = message.params.StringOr("name", "");
        if (name.empty()) {
            reply(jsonrpc::MakeError(id, jsonrpc::kInvalidParams, "tools/call needs a tool name"));
            return;
        }
        if (!toolbox_.HasTool(name)) {
            reply(jsonrpc::MakeError(id, jsonrpc::kInvalidParams,
                                     "Unknown tool: " + std::string(name)));
            return;
        }
        Value arguments = Value::MakeObject();
        if (const Value* args = message.params.FindMember("arguments"); args && args->IsObject()) {
            arguments = *args;
        }
        dispatcher_([this, id, tool = std::string(name), arguments = std::move(arguments),
                     reply = std::move(reply)]() mutable {
            toolbox_.Call(tool, arguments, [id, reply = std::move(reply)](ToolResult result) {
                reply(jsonrpc::MakeResult(id, result.ToJson()));
            });
        });
        return;
    }
    reply(jsonrpc::MakeError(id, jsonrpc::kMethodNotFound, "Method not found: " + message.method));
}

}  // namespace island::agent
