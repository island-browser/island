#ifndef ISLAND_AGENT_HTTP_SERVER_H_
#define ISLAND_AGENT_HTTP_SERVER_H_

// Minimal HTTP/1.1 server bound to 127.0.0.1, enough for the MCP Streamable
// HTTP transport (one JSON POST in, one JSON response out). One short-lived
// thread per connection; every response closes the connection.
//
// POSIX only (Linux and macOS). On Windows Start() reports failure and the
// agent endpoint stays off; that is a documented gap, not a silent one.

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace island::agent {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::vector<HttpHeader> headers;
    std::string body;

    // Case-insensitive header lookup; empty when absent.
    [[nodiscard]] std::string_view Header(std::string_view name) const;
};

struct HttpResponse {
    int status = 200;
    std::string content_type;
    std::vector<HttpHeader> headers;
    std::string body;
};

// Parses one request head (request line + headers, without the trailing blank
// line). Returns false on malformed input. Exposed for tests.
bool ParseHttpRequestHead(std::string_view head, HttpRequest& out);
[[nodiscard]] std::string SerializeHttpResponse(const HttpResponse& response);
[[nodiscard]] std::string_view HttpReasonPhrase(int status);

class LoopbackHttpServer {
  public:
    using Respond = std::function<void(HttpResponse)>;
    // Called on a connection thread. `respond` may be called from any thread,
    // exactly once.
    using Handler = std::function<void(const HttpRequest&, Respond)>;

    LoopbackHttpServer() = default;
    ~LoopbackHttpServer();

    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    // Binds 127.0.0.1:`port` (0 picks a free port) and starts accepting.
    bool Start(int port, Handler handler, std::string* error = nullptr);
    void Stop();
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    static constexpr std::size_t kMaxHeadBytes = 64 * 1024;
    static constexpr std::size_t kMaxBodyBytes = 16 * 1024 * 1024;
    // How long a connection waits for the handler before answering 504.
    static constexpr int kHandlerTimeoutSeconds = 120;

  private:
    struct Shared;

    void AcceptLoop();
    static void ServeConnection(std::shared_ptr<Shared> shared, int fd);

    std::shared_ptr<Shared> shared_;
    std::thread accept_thread_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_HTTP_SERVER_H_
