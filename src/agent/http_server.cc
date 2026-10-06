#include "http_server.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <optional>
#include <set>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace island::agent {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string_view HttpRequest::Header(std::string_view name) const {
    for (const HttpHeader& header : headers) {
        if (EqualsIgnoreCase(header.name, name)) return header.value;
    }
    return {};
}

bool ParseHttpRequestHead(std::string_view head, HttpRequest& out) {
    const std::size_t line_end = head.find("\r\n");
    const std::string_view request_line = head.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    if (first_space == std::string_view::npos || first_space == 0) return false;
    const std::size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) return false;
    out.method = std::string(request_line.substr(0, first_space));
    out.target = std::string(request_line.substr(first_space + 1, second_space - first_space - 1));
    const std::string_view version = request_line.substr(second_space + 1);
    if (out.target.empty() || version.substr(0, 5) != "HTTP/") return false;
    for (char c : out.method) {
        if (!std::isupper(static_cast<unsigned char>(c))) return false;
    }

    out.headers.clear();
    if (line_end == std::string_view::npos) return true;
    std::string_view rest = head.substr(line_end + 2);
    while (!rest.empty()) {
        const std::size_t end = rest.find("\r\n");
        const std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 2);
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return false;
        const std::string_view name = line.substr(0, colon);
        for (char c : name) {
            if (c == ' ' || c == '\t') return false;
        }
        out.headers.push_back({std::string(name), std::string(Trim(line.substr(colon + 1)))});
    }
    return true;
}

std::string_view HttpReasonPhrase(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 411:
            return "Length Required";
        case 413:
            return "Payload Too Large";
        case 415:
            return "Unsupported Media Type";
        case 431:
            return "Request Header Fields Too Large";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "Status";
    }
}

std::string SerializeHttpResponse(const HttpResponse& response) {
    std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " +
                      std::string(HttpReasonPhrase(response.status)) + "\r\n";
    if (!response.content_type.empty()) out += "Content-Type: " + response.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: close\r\n";
    for (const HttpHeader& header : response.headers) {
        out += header.name + ": " + header.value + "\r\n";
    }
    out += "\r\n";
    out += response.body;
    return out;
}

#if defined(_WIN32)

struct LoopbackHttpServer::Shared {};

LoopbackHttpServer::~LoopbackHttpServer() = default;

bool LoopbackHttpServer::Start(int, Handler, std::string* error) {
    if (error != nullptr) *error = "The agent HTTP endpoint is not implemented on Windows yet.";
    return false;
}

void LoopbackHttpServer::Stop() {}
void LoopbackHttpServer::AcceptLoop() {}
void LoopbackHttpServer::ServeConnection(std::shared_ptr<Shared>, int) {}

#else

struct LoopbackHttpServer::Shared {
    Handler handler;
    std::mutex mutex;
    std::condition_variable idle;
    std::set<int> open_fds;
    bool stopping = false;
};

namespace {

void SendAll(int fd, std::string_view data) {
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    while (!data.empty()) {
        const ssize_t sent = ::send(fd, data.data(), data.size(), flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void SendSimple(int fd, int status, std::string_view message) {
    HttpResponse response;
    response.status = status;
    response.content_type = "text/plain; charset=utf-8";
    response.body = std::string(message);
    SendAll(fd, SerializeHttpResponse(response));
}

// Per-request rendezvous between the handler's respond callback (any thread)
// and the connection thread waiting to write it.
struct PendingResponse {
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<HttpResponse> response;
};

}  // namespace

LoopbackHttpServer::~LoopbackHttpServer() { Stop(); }

bool LoopbackHttpServer::Start(int port, Handler handler, std::string* error) {
    if (running_.load()) return true;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (error != nullptr) *error = "socket() failed";
        return false;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (error != nullptr) *error = "bind(127.0.0.1:" + std::to_string(port) + ") failed";
        ::close(fd);
        return false;
    }
    if (::listen(fd, 16) != 0) {
        if (error != nullptr) *error = "listen() failed";
        ::close(fd);
        return false;
    }
    socklen_t length = sizeof(address);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);
    listen_fd_ = fd;
    shared_ = std::make_shared<Shared>();
    shared_->handler = std::move(handler);
    running_.store(true);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
    return true;
}

void LoopbackHttpServer::Stop() {
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->stopping = true;
        for (int fd : shared_->open_fds) ::shutdown(fd, SHUT_RDWR);
    }
    // shutdown() wakes the blocked accept(); the fd is closed and cleared only
    // after the accept thread has exited, so it never reads a stale value.
    ::shutdown(listen_fd_, SHUT_RDWR);
    // macOS does not wake accept() on shutdown(); a throwaway loopback
    // connection does, everywhere. The loop sees running_ == false and exits.
    const int waker = ::socket(AF_INET, SOCK_STREAM, 0);
    if (waker >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port_));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(waker, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::close(waker);
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    ::close(listen_fd_);
    listen_fd_ = -1;
    // Connection threads notice `stopping` within one poll interval.
    std::unique_lock<std::mutex> lock(shared_->mutex);
    shared_->idle.wait_for(lock, std::chrono::seconds(5),
                           [this] { return shared_->open_fds.empty(); });
}

void LoopbackHttpServer::AcceptLoop() {
    for (;;) {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &length);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return;  // listen socket closed by Stop()
        }
        if (!running_.load()) {
            ::close(fd);
            return;
        }
#if defined(SO_NOSIGPIPE)
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        timeval timeout{};
        timeout.tv_sec = 15;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->stopping) {
                ::close(fd);
                return;
            }
            shared_->open_fds.insert(fd);
        }
        std::thread(&LoopbackHttpServer::ServeConnection, shared_, fd).detach();
    }
}

void LoopbackHttpServer::ServeConnection(std::shared_ptr<Shared> shared, int fd) {
    // Forget the fd before closing it so Stop() can never shut down a reused
    // descriptor number that now belongs to someone else.
    auto finish = [&shared, fd] {
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->open_fds.erase(fd);
            shared->idle.notify_all();
        }
        ::close(fd);
    };

    std::string buffer;
    std::size_t head_end = std::string::npos;
    char chunk[16 * 1024];
    while (head_end == std::string::npos) {
        const ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            if (received < 0 && errno == EINTR) continue;
            finish();
            return;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
        head_end = buffer.find("\r\n\r\n");
        if (head_end == std::string::npos && buffer.size() > kMaxHeadBytes) {
            SendSimple(fd, 431, "Request head too large");
            finish();
            return;
        }
    }

    HttpRequest request;
    if (!ParseHttpRequestHead(std::string_view(buffer).substr(0, head_end), request)) {
        SendSimple(fd, 400, "Malformed request");
        finish();
        return;
    }
    if (!request.Header("Transfer-Encoding").empty()) {
        SendSimple(fd, 501, "Chunked request bodies are not supported");
        finish();
        return;
    }
    std::size_t content_length = 0;
    const std::string_view length_header = request.Header("Content-Length");
    if (!length_header.empty()) {
        const auto [ptr, ec] = std::from_chars(
            length_header.data(), length_header.data() + length_header.size(), content_length);
        if (ec != std::errc{} || ptr != length_header.data() + length_header.size()) {
            SendSimple(fd, 400, "Invalid Content-Length");
            finish();
            return;
        }
    } else if (request.method == "POST") {
        SendSimple(fd, 411, "Content-Length required");
        finish();
        return;
    }
    if (content_length > kMaxBodyBytes) {
        SendSimple(fd, 413, "Request body too large");
        finish();
        return;
    }
    request.body = buffer.substr(head_end + 4);
    while (request.body.size() < content_length) {
        const ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            if (received < 0 && errno == EINTR) continue;
            finish();
            return;
        }
        request.body.append(chunk, static_cast<std::size_t>(received));
    }
    request.body.resize(content_length);

    auto pending = std::make_shared<PendingResponse>();
    shared->handler(request, [pending](HttpResponse response) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        if (pending->response) return;
        pending->response = std::move(response);
        pending->ready.notify_all();
    });

    std::optional<HttpResponse> response;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(kHandlerTimeoutSeconds);
    {
        std::unique_lock<std::mutex> lock(pending->mutex);
        while (!pending->response) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            pending->ready.wait_for(lock, std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> shared_lock(shared->mutex);
            if (shared->stopping) break;
        }
        response = std::move(pending->response);
    }
    if (response) {
        SendAll(fd, SerializeHttpResponse(*response));
    } else {
        bool stopping = false;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            stopping = shared->stopping;
        }
        SendSimple(fd, stopping ? 503 : 504,
                   stopping ? "Server is shutting down" : "The browser did not respond in time");
    }
    finish();
}

#endif

}  // namespace island::agent
