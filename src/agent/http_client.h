#ifndef ISLAND_AGENT_HTTP_CLIENT_H_
#define ISLAND_AGENT_HTTP_CLIENT_H_

// Blocking HTTP/1.1 POST to a loopback endpoint, used by island_mcp_bridge
// and the end-to-end tests. POSIX only.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "http_server.h"

namespace island::agent {

struct LoopbackUrl {
    int port = 0;
    std::string path;
};

// Parses http://127.0.0.1:<port>/<path> (or localhost). Anything else,
// including non-loopback hosts, is rejected.
[[nodiscard]] std::optional<LoopbackUrl> ParseLoopbackUrl(std::string_view url);

// Sends one request and reads the whole response. std::nullopt on transport
// failure.
[[nodiscard]] std::optional<HttpResponse> LoopbackHttpRequest(
    int port, std::string_view method, std::string_view path,
    const std::vector<HttpHeader>& headers, std::string_view body, std::string* error = nullptr);

}  // namespace island::agent

#endif  // ISLAND_AGENT_HTTP_CLIENT_H_
