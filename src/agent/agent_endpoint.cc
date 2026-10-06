#include "agent_endpoint.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

#include "json_util.h"

namespace island::agent {

namespace {

bool ConstantTimeEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Strips an optional ":port" suffix from a host[:port] authority.
std::string_view HostWithoutPort(std::string_view authority) {
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return {};
        return authority.substr(0, close + 1);
    }
    const std::size_t colon = authority.find(':');
    return colon == std::string_view::npos ? authority : authority.substr(0, colon);
}

bool IsLoopbackName(std::string_view host) {
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

HttpResponse Plain(int status, std::string body) {
    HttpResponse response;
    response.status = status;
    response.content_type = "text/plain; charset=utf-8";
    response.body = std::move(body);
    return response;
}

}  // namespace

std::filesystem::path DefaultDiscoveryFilePath() {
    constexpr std::string_view kFileName = "agent-endpoint.json";
#if defined(_WIN32)
    const char* const app_data = std::getenv("APPDATA");
    if (app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "Island" / kFileName;
    }
    const char* const profile = std::getenv("USERPROFILE");
    return std::filesystem::path(profile != nullptr ? profile : ".") / "AppData" / "Roaming" /
           "Island" / kFileName;
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return std::filesystem::path(kFileName);
    return std::filesystem::path(home) / "Library" / "Application Support" / "Island" / kFileName;
#else
    const char* const xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path(xdg_data_home) / "Island" / kFileName;
    }
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return std::filesystem::path(kFileName);
    return std::filesystem::path(home) / ".local" / "share" / "Island" / kFileName;
#endif
}

std::string GenerateToken() {
    std::random_device device;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 16; ++i) {
        const std::uint32_t word = device();
        for (int nibble = 0; nibble < 4; ++nibble) {
            token += kHex[(word >> (nibble * 4)) & 0xFU];
        }
    }
    return token;
}

bool IsLoopbackHost(std::string_view host) {
    if (host.empty()) return false;
    return IsLoopbackName(HostWithoutPort(host));
}

bool IsLoopbackOrigin(std::string_view origin) {
    std::string_view rest;
    if (origin.substr(0, 7) == "http://") {
        rest = origin.substr(7);
    } else if (origin.substr(0, 8) == "https://") {
        rest = origin.substr(8);
    } else {
        return false;
    }
    if (rest.find('/') != std::string_view::npos) return false;
    return IsLoopbackName(HostWithoutPort(rest));
}

AgentEndpoint::AgentEndpoint(BrowserToolbox& toolbox, McpServer::Dispatcher dispatcher)
    : mcp_(toolbox, std::move(dispatcher)), token_(GenerateToken()) {}

AgentEndpoint::~AgentEndpoint() { Stop(); }

bool AgentEndpoint::Start(int port, std::string* error) {
    return http_.Start(
        port,
        [this](const HttpRequest& request, LoopbackHttpServer::Respond respond) {
            Handle(request, std::move(respond));
        },
        error);
}

void AgentEndpoint::Stop() {
    http_.Stop();
    if (!discovery_file_.empty()) {
        // Another Island instance may have replaced the file since; only
        // remove it while it still describes this endpoint.
        std::ifstream in(discovery_file_, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const std::optional<json::Value> info = json::Parse(text.substr(0, text.find('\n')));
        if (info.has_value() && ConstantTimeEquals(info->StringOr("token", ""), token_)) {
            std::error_code ec;
            std::filesystem::remove(discovery_file_, ec);
        }
        discovery_file_.clear();
    }
}

std::string AgentEndpoint::url() const {
    return "http://127.0.0.1:" + std::to_string(http_.port()) + std::string(kPath);
}

bool AgentEndpoint::WriteDiscoveryFile(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = path.string() + ".tmp";
    json::Value info = json::Value::MakeObject()
                           .Set("url", json::Value::String(url()))
                           .Set("token", json::Value::String(token_))
                           .Set("transport", json::Value::String("streamable-http"));
#if !defined(_WIN32)
    info.Set("pid", json::Value::Int(static_cast<std::int64_t>(::getpid())));
#endif
    const std::string contents = json::Serialize(info) + "\n";
#if defined(_WIN32)
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << contents;
        if (!out) return false;
    }
#else
    // The file holds the bearer token: create it owner-only from the start
    // (never widen-then-narrow), and refuse to write if that did not stick.
    std::filesystem::remove(temp, ec);
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    struct stat info_stat {};
    bool ok = ::fstat(fd, &info_stat) == 0 && (info_stat.st_mode & 077) == 0;
    std::string_view pending = contents;
    while (ok && !pending.empty()) {
        const ssize_t written = ::write(fd, pending.data(), pending.size());
        if (written < 0 && errno == EINTR) continue;
        ok = written > 0;
        if (ok) pending.remove_prefix(static_cast<std::size_t>(written));
    }
    ok = ::close(fd) == 0 && ok;
    if (!ok) {
        std::filesystem::remove(temp, ec);
        return false;
    }
#endif
    std::filesystem::rename(temp, path, ec);
    if (ec) return false;
    discovery_file_ = path;
    return true;
}

HttpResponse AgentEndpoint::CheckRequest(const HttpRequest& request) const {
    // DNS-rebinding defence: only loopback authorities and origins.
    if (!IsLoopbackHost(request.Header("Host"))) return Plain(403, "Host must be loopback");
    const std::string_view origin = request.Header("Origin");
    if (!origin.empty() && !IsLoopbackOrigin(origin)) {
        return Plain(403, "Cross-origin requests are not allowed");
    }
    std::string_view path = request.target;
    if (const std::size_t query = path.find('?'); query != std::string_view::npos) {
        path = path.substr(0, query);
    }
    if (path != kPath) return Plain(404, "Not found; the MCP endpoint is /mcp");
    const std::string_view authorization = request.Header("Authorization");
    constexpr std::string_view kBearer = "Bearer ";
    if (authorization.substr(0, kBearer.size()) != kBearer ||
        !ConstantTimeEquals(authorization.substr(kBearer.size()), token_)) {
        HttpResponse response = Plain(401, "Missing or invalid bearer token");
        response.headers.push_back({"WWW-Authenticate", "Bearer"});
        return response;
    }
    if (request.method != "POST") {
        // No server-initiated SSE stream; MCP allows answering GET with 405.
        HttpResponse response = Plain(405, "Use POST");
        response.headers.push_back({"Allow", "POST"});
        return response;
    }
    return HttpResponse{.status = 0};
}

void AgentEndpoint::Handle(const HttpRequest& request, LoopbackHttpServer::Respond respond) {
    HttpResponse rejected = CheckRequest(request);
    if (rejected.status != 0) {
        respond(std::move(rejected));
        return;
    }
    mcp_.HandleMessage(request.body,
                       [respond = std::move(respond)](std::optional<std::string> reply) {
                           if (!reply) {
                               respond(HttpResponse{.status = 202});
                               return;
                           }
                           HttpResponse response;
                           response.status = 200;
                           response.content_type = "application/json";
                           response.body = std::move(*reply);
                           respond(std::move(response));
                       });
}

}  // namespace island::agent
