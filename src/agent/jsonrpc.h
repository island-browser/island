#ifndef ISLAND_AGENT_JSONRPC_H_
#define ISLAND_AGENT_JSONRPC_H_

// JSON-RPC 2.0 framing shared by the MCP server and the ACP client. Pure and
// CEF-free: messages are json::Value trees in, compact JSON text out.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "json_util.h"

namespace island::agent::jsonrpc {

// Standard JSON-RPC 2.0 error codes.
inline constexpr std::int64_t kParseError = -32700;
inline constexpr std::int64_t kInvalidRequest = -32600;
inline constexpr std::int64_t kMethodNotFound = -32601;
inline constexpr std::int64_t kInvalidParams = -32602;
inline constexpr std::int64_t kInternalError = -32603;

enum class MessageKind : std::uint8_t {
    kRequest,        // method + id
    kNotification,   // method, no id
    kResponse,       // result + id
    kErrorResponse,  // error + id
    kInvalid,
};

// A classified incoming message. `id` keeps the original id value (int or
// string) so a reply echoes it verbatim.
struct Message {
    MessageKind kind = MessageKind::kInvalid;
    json::Value id;
    std::string method;
    json::Value params;
    json::Value result;
    std::int64_t error_code = 0;
    std::string error_message;
};

// Parses and classifies one JSON-RPC message. Unparseable text and non-object
// roots yield kInvalid.
[[nodiscard]] Message Classify(std::string_view text);

[[nodiscard]] std::string MakeRequest(const json::Value& id, std::string_view method,
                                      json::Value params);
[[nodiscard]] std::string MakeNotification(std::string_view method, json::Value params);
[[nodiscard]] std::string MakeResult(const json::Value& id, json::Value result);
[[nodiscard]] std::string MakeError(const json::Value& id, std::int64_t code,
                                    std::string_view message);

}  // namespace island::agent::jsonrpc

#endif  // ISLAND_AGENT_JSONRPC_H_
