#include "address_input.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace island {
namespace {

constexpr std::array<std::string_view, 14> kKnownSchemes = {
    "http", "https",  "ftp",         "file",   "about", "data", "javascript",
    "blob", "mailto", "view-source", "chrome", "ws",    "wss",  "tel",
};

bool IsAsciiAlpha(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

bool IsAsciiAlnum(char character) { return IsAsciiAlpha(character) || IsAsciiDigit(character); }

bool IsNonAscii(char character) { return static_cast<unsigned char>(character) >= 0x80U; }

bool IsEdgeWhitespace(char character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

std::string_view Trim(std::string_view input) {
    while (!input.empty() && IsEdgeWhitespace(input.front())) {
        input.remove_prefix(1);
    }
    while (!input.empty() && IsEdgeWhitespace(input.back())) {
        input.remove_suffix(1);
    }
    return input;
}

bool HasWhitespaceOrControl(std::string_view input) {
    return std::any_of(input.begin(), input.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte <= 0x20U || byte == 0x7fU;
    });
}

std::string Lowercase(std::string_view input) {
    std::string lowered(input);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

// "scheme:" at the start of the input, where the scheme is an RFC 3986 scheme
// that is followed by "//" or is one of the well-known schemes.
bool HasExplicitScheme(std::string_view input) {
    const std::size_t colon = input.find(':');
    if (colon == std::string_view::npos || colon == 0 || !IsAsciiAlpha(input.front())) {
        return false;
    }
    const std::string_view scheme = input.substr(0, colon);
    if (!std::all_of(scheme.begin(), scheme.end(), [](char character) {
            return IsAsciiAlnum(character) || character == '+' || character == '-' ||
                   character == '.';
        })) {
        return false;
    }
    if (input.substr(colon + 1).starts_with("//")) {
        return true;
    }
    const std::string lowered = Lowercase(scheme);
    return std::find(kKnownSchemes.begin(), kKnownSchemes.end(), lowered) != kKnownSchemes.end();
}

bool IsPort(std::string_view port) {
    return !port.empty() && port.size() <= 5 && std::all_of(port.begin(), port.end(), IsAsciiDigit);
}

bool IsIpv4Literal(std::string_view host) {
    int parts = 0;
    std::size_t start = 0;
    while (start <= host.size()) {
        const std::size_t end = std::min(host.find('.', start), host.size());
        const std::string_view part = host.substr(start, end - start);
        if (part.empty() || part.size() > 3 ||
            !std::all_of(part.begin(), part.end(), IsAsciiDigit)) {
            return false;
        }
        ++parts;
        start = end + 1;
    }
    return parts == 4;
}

bool IsDottedName(std::string_view host) {
    if (host.find('.') == std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    std::string_view last;
    while (start <= host.size()) {
        const std::size_t end = std::min(host.find('.', start), host.size());
        const std::string_view label = host.substr(start, end - start);
        if (label.empty() || label.front() == '-' || label.back() == '-' ||
            !std::all_of(label.begin(), label.end(), [](char character) {
                return IsAsciiAlnum(character) || character == '-' || character == '_' ||
                       IsNonAscii(character);
            })) {
            return false;
        }
        last = label;
        start = end + 1;
    }
    // The top-level label must be alphabetic ("example.com", not "3.14"), or a
    // punycode label such as xn--p1ai.
    if (last.size() < 2) {
        return false;
    }
    if (Lowercase(last).starts_with("xn--")) {
        return true;
    }
    return std::all_of(last.begin(), last.end(), [](char character) {
        return IsAsciiAlpha(character) || IsNonAscii(character);
    });
}

bool IsBracketedIpv6(std::string_view authority) {
    const std::size_t close = authority.find(']');
    if (!authority.starts_with('[') || close == std::string_view::npos || close < 3) {
        return false;
    }
    const std::string_view address = authority.substr(1, close - 1);
    if (address.find(':') == std::string_view::npos ||
        !std::all_of(address.begin(), address.end(), [](char character) {
            return std::isxdigit(static_cast<unsigned char>(character)) != 0 || character == ':' ||
                   character == '.';
        })) {
        return false;
    }
    const std::string_view tail = authority.substr(close + 1);
    return tail.empty() || (tail.front() == ':' && IsPort(tail.substr(1)));
}

enum class HostKind : std::uint8_t {
    kNotAHost,
    // localhost and IP literals: development servers and LAN devices that
    // usually serve plain http, so "localhost:3000" must not be sent to TLS.
    kLocalOrIp,
    kDomain,
};

HostKind ClassifyHost(std::string_view input) {
    const std::string_view authority = input.substr(0, input.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return HostKind::kNotAHost;
    }
    if (authority.front() == '[') {
        return IsBracketedIpv6(authority) ? HostKind::kLocalOrIp : HostKind::kNotAHost;
    }
    std::string_view host = authority;
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        if (!IsPort(authority.substr(colon + 1))) {
            return HostKind::kNotAHost;
        }
        host = authority.substr(0, colon);
    }
    if (host.empty() || host.find(':') != std::string_view::npos) {
        return HostKind::kNotAHost;
    }
    if (Lowercase(host) == "localhost" || IsIpv4Literal(host)) {
        return HostKind::kLocalOrIp;
    }
    return IsDottedName(host) ? HostKind::kDomain : HostKind::kNotAHost;
}

}  // namespace

AddressInput ClassifyAddressInput(std::string_view input) {
    input = Trim(input);
    if (input.empty() || HasExplicitScheme(input)) {
        return {.kind = AddressInputKind::kUrl, .text = std::string(input)};
    }
    if (HasWhitespaceOrControl(input)) {
        return {.kind = AddressInputKind::kSearch, .text = std::string(input)};
    }
    switch (ClassifyHost(input)) {
        case HostKind::kLocalOrIp:
            return {.kind = AddressInputKind::kUrl, .text = "http://" + std::string(input)};
        case HostKind::kDomain:
            return {.kind = AddressInputKind::kUrl, .text = "https://" + std::string(input)};
        case HostKind::kNotAHost:
            break;
    }
    return {.kind = AddressInputKind::kSearch, .text = std::string(input)};
}

}  // namespace island
