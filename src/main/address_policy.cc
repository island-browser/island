#include "address_policy.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace island {
namespace {

bool HasInvalidCharacter(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character == '\0' || std::iscntrl(character) != 0 || std::isspace(character) != 0;
    });
}

bool IsDnsLabel(std::string_view label) {
    if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') {
        return false;
    }

    return std::all_of(label.begin(), label.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-';
    });
}

bool IsDnsHost(std::string_view host) {
    if (host.empty() || host.size() > 253 || host.back() == '.') {
        return false;
    }

    std::size_t label_start = 0;
    while (label_start < host.size()) {
        const std::size_t label_end = host.find('.', label_start);
        const std::string_view label = host.substr(label_start, label_end - label_start);
        if (!IsDnsLabel(label)) {
            return false;
        }
        if (label_end == std::string_view::npos) {
            return true;
        }
        label_start = label_end + 1;
    }
    return false;
}

bool IsLoopbackIpv4(std::string_view host) {
    std::size_t part_start = 0;
    int parts = 0;
    int first_part = -1;
    while (part_start < host.size()) {
        const std::size_t part_end = host.find('.', part_start);
        const std::string_view part = host.substr(part_start, part_end - part_start);
        if (part.empty() || part.size() > 3 ||
            !std::all_of(part.begin(), part.end(),
                         [](unsigned char character) { return std::isdigit(character) != 0; })) {
            return false;
        }

        int value = 0;
        for (const char character : part) {
            value = value * 10 + (character - '0');
        }
        if (value > 255) {
            return false;
        }
        if (parts == 0) {
            first_part = value;
        }
        ++parts;
        if (part_end == std::string_view::npos) {
            break;
        }
        part_start = part_end + 1;
    }
    return parts == 4 && first_part == 127;
}

bool LooksLikeIpv4(std::string_view host) {
    return host.find_first_not_of("0123456789.") == std::string_view::npos &&
           host.find('.') != std::string_view::npos;
}

bool IsAllowedHost(std::string_view host) {
    if (host == "[::1]" || IsLoopbackIpv4(host)) {
        return true;
    }
    if (LooksLikeIpv4(host)) {
        return false;
    }
    return IsDnsHost(host);
}

bool IsValidPort(std::string_view port) {
    if (port.empty()) {
        return true;
    }
    if (!std::all_of(port.begin(), port.end(),
                     [](unsigned char character) { return std::isdigit(character) != 0; })) {
        return false;
    }

    unsigned int value = 0;
    for (const char character : port) {
        if (value > (std::numeric_limits<unsigned int>::max() - 9U) / 10U) {
            return false;
        }
        value = value * 10U + static_cast<unsigned int>(character - '0');
    }
    return value >= 1U && value <= 65535U;
}

ValidatedAddress Rejected(AddressError error) { return {.url = {}, .error = error}; }

}  // namespace

bool ValidatedAddress::is_valid() const noexcept { return !url.empty() && !error.has_value(); }

ValidatedAddress ValidateAddress(const ParsedAddressParts& address) {
    if (!address.is_absolute) {
        return Rejected(AddressError::kNotAbsolute);
    }
    if (address.scheme != "http" && address.scheme != "https") {
        return Rejected(AddressError::kUnsupportedScheme);
    }
    if (address.has_credentials) {
        return Rejected(AddressError::kCredentialsNotAllowed);
    }
    if (HasInvalidCharacter(address.original)) {
        return Rejected(AddressError::kInvalidCharacter);
    }
    if (!IsAllowedHost(address.host)) {
        return Rejected(AddressError::kInvalidHost);
    }
    if (!IsValidPort(address.port)) {
        return Rejected(AddressError::kInvalidPort);
    }
    return {.url = address.original, .error = std::nullopt};
}

}  // namespace island
