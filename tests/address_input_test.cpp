#include "address_input.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace island {
namespace {

AddressInput Url(std::string text) { return {AddressInputKind::kUrl, std::move(text)}; }
AddressInput Search(std::string text) { return {AddressInputKind::kSearch, std::move(text)}; }

TEST(AddressInputTest, GivenExplicitSchemesWhenClassifiedThenTheyStayUrlsUnchanged) {
    for (const std::string_view input :
         {"http://example.test/", "HTTPS://Example.test/a?b=c", "https://exa mple.test/a b",
          "about:blank", "data:text/html,hello", "file:///tmp/x", "javascript:alert(1)",
          "mailto:a@b.test", "ftp://example.test", "view-source:https://example.test",
          "custom://thing", "http:/example.test"}) {
        EXPECT_EQ(ClassifyAddressInput(input), Url(std::string(input))) << input;
    }
}

TEST(AddressInputTest, GivenEdgeWhitespaceWhenClassifiedThenItIsTrimmed) {
    EXPECT_EQ(ClassifyAddressInput("  \texample.test \n"), Url("https://example.test"));
    EXPECT_EQ(ClassifyAddressInput("  hello world  "), Search("hello world"));
    EXPECT_EQ(ClassifyAddressInput(" https://example.test/ "), Url("https://example.test/"));
}

TEST(AddressInputTest, GivenEmptyInputWhenClassifiedThenTheValidatorStillSeesIt) {
    EXPECT_EQ(ClassifyAddressInput(""), Url(""));
    EXPECT_EQ(ClassifyAddressInput("   "), Url(""));
}

TEST(AddressInputTest, GivenWhitespaceWhenClassifiedThenItIsASearch) {
    for (const std::string_view input :
         {"hello world", "example.test is down", "example.test/a b", "what is localhost",
          "note: buy milk", "c++: tips", "tab\tseparated", "line\nbreak"}) {
        EXPECT_EQ(ClassifyAddressInput(input), Search(std::string(input))) << input;
    }
}

TEST(AddressInputTest, GivenDomainNamesWhenClassifiedThenTheyGetAnHttpsScheme) {
    for (const std::string_view input :
         {"example.com", "www.example.com", "Example.COM/Path", "example.com:8080",
          "example.com:8080/a/b", "example.com?q=1", "example.com#top", "sub.domain.example.org",
          "bucher.de",
          "b\xC3\xBC"
          "cher.example",
          "site.\xD1\x80\xD1\x84", "example.xn--p1ai", "node.js", "my_host.example.test",
          "a.b.co"}) {
        EXPECT_EQ(ClassifyAddressInput(input), Url("https://" + std::string(input))) << input;
    }
}

TEST(AddressInputTest, GivenLocalhostOrIpLiteralsWhenClassifiedThenTheyGetAnHttpScheme) {
    // Dev servers and LAN devices rarely serve TLS; https://localhost:3000
    // would fail where http:// works.
    for (const std::string_view input :
         {"localhost", "LocalHost", "localhost:3000", "localhost:8080/path?x=1#frag", "127.0.0.1",
          "127.0.0.1:9222/json", "192.168.1.10", "[::1]", "[::1]:8080/x", "[2001:db8::1]"}) {
        EXPECT_EQ(ClassifyAddressInput(input), Url("http://" + std::string(input))) << input;
    }
}

TEST(AddressInputTest, GivenNonHostsWhenClassifiedThenTheyAreSearches) {
    for (const std::string_view input : {"google",
                                         "cats",
                                         "3.14",
                                         "1.2.3",
                                         "1.2.3.4.5",
                                         "v1.0",
                                         "e.g.",
                                         "example.",
                                         ".example",
                                         "example..com",
                                         "-bad.example",
                                         "bad-.example",
                                         "a.b",
                                         "example.c",
                                         "user@example.com",
                                         "foo:3000",
                                         "foo:bar",
                                         "localhost:",
                                         "localhost:99999x",
                                         "example.com:",
                                         "[::1",
                                         "[]",
                                         "[abc]",
                                         "[::1]x",
                                         "[::1]:",
                                         "/path",
                                         "?q=1",
                                         "#frag",
                                         "c:\\dir",
                                         "//host.test",
                                         "9999",
                                         "what?",
                                         "100%"}) {
        EXPECT_EQ(ClassifyAddressInput(input), Search(std::string(input))) << input;
    }
}

TEST(AddressInputTest, GivenALiteralThePolicyRejectsWhenClassifiedThenItIsStillAUrl) {
    // 192.168.1.10 is a URL candidate; address_policy reports kInvalidHost for
    // it rather than the address bar silently searching for an IP address.
    EXPECT_EQ(ClassifyAddressInput("192.168.1.10").kind, AddressInputKind::kUrl);
}

}  // namespace
}  // namespace island
