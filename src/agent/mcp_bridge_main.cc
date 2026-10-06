// island_mcp_bridge: exposes a running Island browser's MCP endpoint over
// stdio, for agents that only launch stdio MCP servers (Claude Desktop, the
// ACP agents without HTTP MCP support, ...).
//
// Endpoint resolution, first match wins:
//   1. ISLAND_MCP_URL + ISLAND_MCP_TOKEN environment variables
//   2. --endpoint-file <path>
//   3. the default discovery file the browser writes at startup
//
// Each stdin line is one JSON-RPC message; each response is one stdout line.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "agent_endpoint.h"
#include "http_client.h"
#include "island_version.h"
#include "json_util.h"
#include "jsonrpc.h"

namespace {

struct Endpoint {
    std::string url;
    std::string token;
};

std::optional<Endpoint> ReadEndpointFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::optional<island::json::Value> parsed = island::json::Parse(buffer.str());
    if (!parsed) {
        // Tolerate the trailing newline the browser writes.
        std::string text = buffer.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        parsed = island::json::Parse(text);
    }
    if (!parsed || !parsed->IsObject()) return std::nullopt;
    Endpoint endpoint{std::string(parsed->StringOr("url", "")),
                      std::string(parsed->StringOr("token", ""))};
    if (endpoint.url.empty()) return std::nullopt;
    return endpoint;
}

std::optional<Endpoint> ResolveEndpoint(int argc, char** argv) {
    const char* url = std::getenv("ISLAND_MCP_URL");
    const char* token = std::getenv("ISLAND_MCP_TOKEN");
    if (url != nullptr && *url != '\0') {
        return Endpoint{url, token != nullptr ? token : ""};
    }
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--endpoint-file") return ReadEndpointFile(argv[i + 1]);
    }
    return ReadEndpointFile(island::agent::DefaultDiscoveryFilePath());
}

void WriteLine(const std::string& line) {
    std::cout << line << '\n';
    std::cout.flush();
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version") {
            std::cout << "island_mcp_bridge " << ISLAND_VERSION_STRING << "\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "usage: island_mcp_bridge [--endpoint-file PATH] [--version]\n"
                         "Bridges stdio MCP to a running Island browser.\n";
            return 0;
        }
    }
    const std::optional<Endpoint> endpoint = ResolveEndpoint(argc, argv);
    if (!endpoint) {
        std::cerr << "island_mcp_bridge: no running Island browser found (start Island, or set "
                     "ISLAND_MCP_URL and ISLAND_MCP_TOKEN)\n";
        return 2;
    }
    const std::optional<island::agent::LoopbackUrl> target =
        island::agent::ParseLoopbackUrl(endpoint->url);
    if (!target) {
        std::cerr << "island_mcp_bridge: endpoint must be a loopback http URL\n";
        return 2;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const island::agent::jsonrpc::Message message = island::agent::jsonrpc::Classify(line);
        // Malformed lines still go through so the server can answer with an error.
        const bool expects_reply = message.kind == island::agent::jsonrpc::MessageKind::kRequest ||
                                   message.kind == island::agent::jsonrpc::MessageKind::kInvalid;
        std::string error;
        const std::optional<island::agent::HttpResponse> response =
            island::agent::LoopbackHttpRequest(target->port, "POST", target->path,
                                               {{"Content-Type", "application/json"},
                                                {"Accept", "application/json, text/event-stream"},
                                                {"Authorization", "Bearer " + endpoint->token}},
                                               line, &error);
        if (!expects_reply) continue;
        if (!response) {
            WriteLine(island::agent::jsonrpc::MakeError(message.id,
                                                        island::agent::jsonrpc::kInternalError,
                                                        "Island browser unreachable: " + error));
            continue;
        }
        if (response->status != 200 || response->body.empty()) {
            WriteLine(island::agent::jsonrpc::MakeError(
                message.id, island::agent::jsonrpc::kInternalError,
                "Island browser answered HTTP " + std::to_string(response->status) + ": " +
                    response->body));
            continue;
        }
        WriteLine(response->body);
    }
    return 0;
}
