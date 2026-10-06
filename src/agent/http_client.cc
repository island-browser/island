#include "http_client.h"

#include <charconv>
#include <optional>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace island::agent {

namespace {
constexpr std::size_t kMaxResponseBytes = 64U << 20;
}  // namespace

std::optional<LoopbackUrl> ParseLoopbackUrl(std::string_view url) {
    constexpr std::string_view kScheme = "http://";
    if (url.substr(0, kScheme.size()) != kScheme) return std::nullopt;
    std::string_view rest = url.substr(kScheme.size());
    const std::size_t slash = rest.find('/');
    const std::string_view authority = rest.substr(0, slash);
    const std::string path =
        slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view host = authority.substr(0, colon);
    if (host != "127.0.0.1" && host != "localhost") return std::nullopt;
    const std::string_view port_text = authority.substr(colon + 1);
    int port = 0;
    const auto [ptr, ec] =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (ec != std::errc{} || ptr != port_text.data() + port_text.size() || port <= 0 ||
        port > 65535) {
        return std::nullopt;
    }
    return LoopbackUrl{.port = port, .path = path};
}

#if defined(_WIN32)

std::optional<HttpResponse> LoopbackHttpRequest(int, std::string_view, std::string_view,
                                                const std::vector<HttpHeader>&, std::string_view,
                                                std::string* error) {
    if (error != nullptr) *error = "Not implemented on Windows.";
    return std::nullopt;
}

#else

std::optional<HttpResponse> LoopbackHttpRequest(int port, std::string_view method,
                                                std::string_view path,
                                                const std::vector<HttpHeader>& headers,
                                                std::string_view body, std::string* error) {
    const int fd = CreateStreamSocket();
    if (fd < 0) {
        if (error != nullptr) *error = "socket() failed";
        return std::nullopt;
    }
#if defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    // A wedged peer must not hang the caller forever; tool calls such as
    // screenshots finish well within this.
    timeval timeout{};
    timeout.tv_sec = 120;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (error != nullptr) *error = "Could not connect to 127.0.0.1:" + std::to_string(port);
        ::close(fd);
        return std::nullopt;
    }
    std::string request = std::string(method) + " " + std::string(path) + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: close\r\n";
    for (const HttpHeader& header : headers) request += header.name + ": " + header.value + "\r\n";
    request += "\r\n";
    request += body;

    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    std::string_view pending = request;
    while (!pending.empty()) {
        const ssize_t sent = ::send(fd, pending.data(), pending.size(), flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (error != nullptr) *error = "send() failed";
            ::close(fd);
            return std::nullopt;
        }
        pending.remove_prefix(static_cast<std::size_t>(sent));
    }

    // Read until the declared body is complete (or EOF without a length):
    // waiting for EOF alone hangs whenever another process still holds the
    // server's end of the connection.
    std::string raw;
    char chunk[16 * 1024];
    std::optional<std::size_t> expected_total;
    for (;;) {
        const ssize_t got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        raw.append(chunk, static_cast<std::size_t>(got));
        if (raw.size() > kMaxResponseBytes) {
            if (error != nullptr) *error = "HTTP response too large";
            ::close(fd);
            return std::nullopt;
        }
        if (!expected_total) {
            const std::size_t end = raw.find("\r\n\r\n");
            if (end != std::string::npos) {
                HttpRequest probe;
                const std::string_view head = std::string_view(raw).substr(0, end);
                const std::size_t first = head.find("\r\n");
                if (first != std::string_view::npos &&
                    ParseHttpRequestHead("X / HTTP/1.1\r\n" + std::string(head.substr(first + 2)),
                                         probe)) {
                    const std::string_view length = probe.Header("Content-Length");
                    std::size_t value = 0;
                    if (!length.empty() &&
                        std::from_chars(length.data(), length.data() + length.size(), value).ec ==
                            std::errc{}) {
                        expected_total = end + 4 + value;
                    }
                }
            }
        }
        if (expected_total && raw.size() >= *expected_total) break;
    }
    ::close(fd);

    const std::size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string::npos) {
        if (error != nullptr) *error = "Malformed HTTP response";
        return std::nullopt;
    }
    const std::string_view head = std::string_view(raw).substr(0, head_end);
    // Reuse the request-head parser by rewriting the status line shape.
    const std::size_t line_end = head.find("\r\n");
    const std::string_view status_line = head.substr(0, line_end);
    if (status_line.substr(0, 5) != "HTTP/") {
        if (error != nullptr) *error = "Malformed status line";
        return std::nullopt;
    }
    const std::size_t space = status_line.find(' ');
    HttpResponse response;
    if (space == std::string_view::npos || space + 4 > status_line.size()) {
        if (error != nullptr) *error = "Malformed status line";
        return std::nullopt;
    }
    std::from_chars(status_line.data() + space + 1, status_line.data() + space + 4,
                    response.status);
    HttpRequest parsed_headers;
    if (line_end != std::string_view::npos &&
        ParseHttpRequestHead("X / HTTP/1.1\r\n" + std::string(head.substr(line_end + 2)),
                             parsed_headers)) {
        response.headers = parsed_headers.headers;
        response.content_type = std::string(parsed_headers.Header("Content-Type"));
    }
    response.body = raw.substr(head_end + 4);
    return response;
}

#endif

}  // namespace island::agent
