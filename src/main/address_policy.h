#ifndef ISLAND_ADDRESS_POLICY_H_
#define ISLAND_ADDRESS_POLICY_H_

#include <optional>
#include <string>

namespace island {

enum class AddressError {
    kNotAbsolute,
    kUnsupportedScheme,
    kCredentialsNotAllowed,
    kInvalidCharacter,
    kInvalidHost,
    kInvalidPort,
};

struct ParsedAddressParts {
    std::string original;
    std::string scheme;
    std::string host;
    std::string port;
    bool is_absolute = true;
    bool has_credentials = false;
};

struct ValidatedAddress {
    std::string url;
    std::optional<AddressError> error;

    [[nodiscard]] bool is_valid() const noexcept;
    bool operator==(const ValidatedAddress&) const = default;
};

[[nodiscard]] ValidatedAddress ValidateAddress(const ParsedAddressParts& address);

}  // namespace island

#endif
