#include "cef_address_parser.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "include/cef_parser.h"

namespace island {
namespace {

bool IsAsciiWhitespace(char character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

bool HasInvalidInputCharacter(std::string_view input) {
    return std::any_of(input.begin(), input.end(), [](unsigned char character) {
        return character == '\0' || character <= 0x1fU || character == 0x7fU ||
               std::isspace(character) != 0;
    });
}

std::string_view TrimAsciiWhitespace(std::string_view input) {
    while (!input.empty() && IsAsciiWhitespace(input.front())) {
        input.remove_prefix(1);
    }
    while (!input.empty() && IsAsciiWhitespace(input.back())) {
        input.remove_suffix(1);
    }
    return input;
}

std::string ExplicitScheme(std::string_view input) {
    const std::size_t colon = input.find(':');
    if (colon == std::string_view::npos || colon == 0 ||
        !std::isalpha(static_cast<unsigned char>(input.front()))) {
        return {};
    }
    if (!std::all_of(input.begin() + 1, input.begin() + colon, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '+' || character == '-' ||
                   character == '.';
        })) {
        return {};
    }

    std::string scheme(input.substr(0, colon));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return scheme;
}

std::string PortFromAuthority(std::string_view input) {
    const std::size_t authority_start = input.find("://");
    if (authority_start == std::string_view::npos) {
        return {};
    }
    input.remove_prefix(authority_start + 3);
    input = input.substr(0, input.find_first_of("/?#"));
    const std::size_t at = input.rfind('@');
    if (at != std::string_view::npos) {
        input.remove_prefix(at + 1);
    }
    if (input.starts_with('[')) {
        const std::size_t bracket = input.find(']');
        if (bracket == std::string_view::npos || bracket + 1 == input.size() ||
            input[bracket + 1] != ':') {
            return {};
        }
        return std::string(input.substr(bracket + 2));
    }
    const std::size_t colon = input.rfind(':');
    return colon == std::string_view::npos ? std::string() : std::string(input.substr(colon + 1));
}

std::string HostFromAuthority(std::string_view input) {
    const std::size_t authority_start = input.find("://");
    if (authority_start == std::string_view::npos) {
        return {};
    }
    input.remove_prefix(authority_start + 3);
    input = input.substr(0, input.find_first_of("/?#"));
    const std::size_t at = input.rfind('@');
    if (at != std::string_view::npos) {
        input.remove_prefix(at + 1);
    }
    if (input.starts_with('[')) {
        const std::size_t bracket = input.find(']');
        return bracket == std::string_view::npos ? std::string(input)
                                                 : std::string(input.substr(0, bracket + 1));
    }
    const std::size_t colon = input.rfind(':');
    return std::string(input.substr(0, colon));
}

bool HasCredentialsDelimiter(std::string_view input) {
    const std::size_t authority_start = input.find("://");
    if (authority_start == std::string_view::npos) {
        return false;
    }
    input.remove_prefix(authority_start + 3);
    return input.substr(0, input.find_first_of("/?#")).find('@') != std::string_view::npos;
}

bool HasEmptyAuthority(std::string_view input) {
    const std::size_t authority_start = input.find("://");
    if (authority_start == std::string_view::npos) {
        return false;
    }
    const std::size_t host_start = authority_start + 3;
    return host_start == input.size() || input[host_start] == '/' || input[host_start] == '?' ||
           input[host_start] == '#';
}

}  // namespace

ValidatedAddress ParseAndValidate(std::string_view input) {
    input = TrimAsciiWhitespace(input);
    if (HasInvalidInputCharacter(input)) {
        ParsedAddressParts parts{
            .original = std::string(input), .scheme = "https", .host = "example.test", .port = {}};
        return ValidateAddress(parts);
    }

    const std::string scheme = ExplicitScheme(input);
    CefURLParts parsed;
    if (!CefParseURL(CefString(std::string(input)), parsed)) {
        ParsedAddressParts parts{.original = std::string(input),
                                 .scheme = scheme,
                                 .host = HostFromAuthority(input),
                                 .port = PortFromAuthority(input),
                                 .is_absolute = !scheme.empty(),
                                 .has_credentials = HasCredentialsDelimiter(input)};
        return ValidateAddress(parts);
    }

    const std::string explicit_port = PortFromAuthority(input);
    ParsedAddressParts parts{
        .original = CefString(&parsed.spec).ToString(),
        .scheme = CefString(&parsed.scheme).ToString(),
        .host = HasEmptyAuthority(input) ? "" : CefString(&parsed.host).ToString(),
        .port = explicit_port.empty() ? CefString(&parsed.port).ToString() : explicit_port,
        .is_absolute = parsed.scheme.length > 0,
        .has_credentials = parsed.username.length > 0 || parsed.password.length > 0 ||
                           HasCredentialsDelimiter(input)};
    return ValidateAddress(parts);
}

}  // namespace island
