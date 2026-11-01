#include "relaunch_args.h"

#include <cctype>
#include <string_view>

namespace island {
namespace {

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(text[index])) != prefix[index]) {
            return false;
        }
    }
    return true;
}

// `host[:port][/path...]` with a dotted or `localhost` host.
bool LooksLikeBareHost(std::string_view text) {
    const std::size_t end = text.find_first_of(":/?#");
    const std::string_view host = text.substr(0, end);
    if (host.empty() || (host != "localhost" && host.find('.') == std::string_view::npos)) {
        return false;
    }
    for (const char c : host) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '.' && c != '-') {
            return false;
        }
    }
    if (end != std::string_view::npos && text[end] == ':') {
        const std::size_t port_end = text.find_first_of("/?#", end);
        const std::string_view port = text.substr(end + 1, port_end - end - 1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::string> RelaunchUrls(std::span<const std::string> arguments) {
    std::vector<std::string> urls;
    for (const std::string& argument : arguments) {
        if (urls.size() >= kMaxRelaunchUrls) {
            break;
        }
        if (argument.empty() || argument.front() == '-') {
            continue;
        }
        if (StartsWithIgnoreCase(argument, "http:") || StartsWithIgnoreCase(argument, "https:") ||
            LooksLikeBareHost(argument)) {
            urls.push_back(argument);
        }
    }
    return urls;
}

}  // namespace island
