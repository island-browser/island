#include "address_policy.h"

#include <gtest/gtest.h>

#include <string>

namespace island {
namespace {

ParsedAddressParts Parts(std::string scheme, std::string host, std::string port = {}) {
    return {.original = scheme + "://" + host + (port.empty() ? "" : ":" + port),
            .scheme = std::move(scheme),
            .host = std::move(host),
            .port = std::move(port)};
}

TEST(AddressPolicy, AcceptsAbsoluteHttpAndHttpsDnsHosts) {
    const ValidatedAddress https = ValidateAddress(Parts("https", "www.example.test", "8443"));
    const ValidatedAddress http = ValidateAddress(Parts("http", "example.test"));

    EXPECT_TRUE(https.is_valid());
    EXPECT_EQ(https.url, "https://www.example.test:8443");
    EXPECT_TRUE(http.is_valid());
    EXPECT_EQ(http.url, "http://example.test");
}

TEST(AddressPolicy, AcceptsExplicitLocalDevelopmentHosts) {
    for (const ParsedAddressParts& parts :
         {Parts("http", "localhost"), Parts("https", "api.localhost"),
          Parts("http", "127.0.0.1", "8080"), Parts("https", "[::1]", "443")}) {
        EXPECT_TRUE(ValidateAddress(parts).is_valid()) << parts.original;
    }
}

TEST(AddressPolicy, RejectsUnsupportedSchemesAndNonAbsoluteInput) {
    ParsedAddressParts relative = Parts("https", "example.test");
    relative.is_absolute = false;

    EXPECT_EQ(ValidateAddress(Parts("data", "text/html")).error, AddressError::kUnsupportedScheme);
    EXPECT_EQ(ValidateAddress(Parts("ftp", "example.test")).error,
              AddressError::kUnsupportedScheme);
    EXPECT_EQ(ValidateAddress(relative).error, AddressError::kNotAbsolute);
}

TEST(AddressPolicy, RejectsEmptyMalformedOrDisallowedHosts) {
    for (const ParsedAddressParts& parts :
         {Parts("https", ""), Parts("https", ".localhost"), Parts("https", "-example.test"),
          Parts("https", "192.0.2.1"), Parts("https", "[::2]"), Parts("https", "example..test")}) {
        EXPECT_EQ(ValidateAddress(parts).error, AddressError::kInvalidHost) << parts.original;
    }
}

TEST(AddressPolicy, RejectsCredentialsControlsNulAndWhitespace) {
    ParsedAddressParts credentials = Parts("https", "example.test");
    credentials.has_credentials = true;
    ParsedAddressParts whitespace = Parts("https", "example.test");
    whitespace.original = "https://example.test/path with-space";
    ParsedAddressParts control = Parts("https", "example.test");
    control.original = "https://example.test/\n";
    ParsedAddressParts nul = Parts("https", "example.test");
    nul.original = "https://example.test/";
    nul.original.push_back('\0');

    EXPECT_EQ(ValidateAddress(credentials).error, AddressError::kCredentialsNotAllowed);
    EXPECT_EQ(ValidateAddress(whitespace).error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ValidateAddress(control).error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ValidateAddress(nul).error, AddressError::kInvalidCharacter);
}

TEST(AddressPolicy, RejectsMalformedPorts) {
    for (const std::string& port : {"0", "65536", "abc", "80x"}) {
        EXPECT_EQ(ValidateAddress(Parts("https", "example.test", port)).error,
                  AddressError::kInvalidPort)
            << port;
    }
}

}  // namespace
}  // namespace island
