#include "cef_address_parser.h"

#include <gtest/gtest.h>

#include <string>

#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

namespace island {

int RunCefAddressParserTests(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);

    CefScopedLibraryLoader library_loader;
    if (!library_loader.LoadInMain()) {
        return 1;
    }

    CefMainArgs arguments(argc, argv);
    const int subprocess_exit_code = CefExecuteProcess(arguments, nullptr, nullptr);
    if (subprocess_exit_code >= 0) {
        return subprocess_exit_code;
    }

    CefSettings settings;
    settings.no_sandbox = true;
    if (!CefInitialize(arguments, settings, nullptr, nullptr)) {
        return 1;
    }
    const int result = RUN_ALL_TESTS();
    CefShutdown();
    return result;
}

namespace {

TEST(CefAddressParser, AcceptsAndCanonicalizesAbsoluteHttpUrls) {
    const ValidatedAddress address =
        ParseAndValidate(" \thTTps://WWW.Example.test:443/a/../path?q=value#section\r\n");

    EXPECT_TRUE(address.is_valid());
    EXPECT_EQ(address.url, "https://www.example.test/path?q=value#section");
    EXPECT_EQ(address.error, std::nullopt);
}

TEST(CefAddressParser, AcceptsTheExplicitLocalDevelopmentHosts) {
    for (const std::string_view input : {"http://localhost:8080/", "https://api.localhost/",
                                         "http://127.0.0.1:3000/", "https://[::1]/"}) {
        EXPECT_TRUE(ParseAndValidate(input).is_valid()) << input;
    }
}

TEST(CefAddressParser, CanonicalizesInternationalizedAndIpv6InputDeterministically) {
    const ValidatedAddress idn = ParseAndValidate("https://bücher.example/");
    const ValidatedAddress ipv6 = ParseAndValidate("https://[0:0:0:0:0:0:0:1]/");

    EXPECT_TRUE(idn.is_valid());
    EXPECT_EQ(idn.url, "https://xn--bcher-kva.example/");
    EXPECT_TRUE(ipv6.is_valid());
    EXPECT_EQ(ipv6.url, "https://[::1]/");
}

TEST(CefAddressParser, RejectsRelativeSearchAndImplicitSchemeInput) {
    for (const std::string_view input : {"/relative/path", "example.test"}) {
        EXPECT_EQ(ParseAndValidate(input).error, AddressError::kNotAbsolute) << input;
    }
    EXPECT_EQ(ParseAndValidate("example test").error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ParseAndValidate("search terms").error, AddressError::kInvalidCharacter);
}

TEST(CefAddressParser, RejectsUnsupportedSchemes) {
    for (const std::string_view input : {"data:text/html,Island", "file:///tmp/island.html",
                                         "javascript:alert(1)", "ftp://example.test/"}) {
        EXPECT_EQ(ParseAndValidate(input).error, AddressError::kUnsupportedScheme) << input;
    }
}

TEST(CefAddressParser, RejectsCredentialsBeforeNavigation) {
    for (const std::string_view input :
         {"https://user@example.test/", "https://:password@example.test/"}) {
        EXPECT_EQ(ParseAndValidate(input).error, AddressError::kCredentialsNotAllowed) << input;
    }
}

TEST(CefAddressParser, RejectsWhitespaceControlsAndNulOutsideTrimmedEdges) {
    const std::string nul = std::string("https://example.test/") + '\0';

    EXPECT_EQ(ParseAndValidate("https://example.test/a b").error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ParseAndValidate("https://example.test/a\tb").error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ParseAndValidate("https://example.test/a\nb").error, AddressError::kInvalidCharacter);
    EXPECT_EQ(ParseAndValidate(nul).error, AddressError::kInvalidCharacter);
}

TEST(CefAddressParser, RejectsMalformedAndOutOfRangePorts) {
    for (const std::string_view input :
         {"https://example.test:0/", "https://example.test:65536/", "https://example.test:abc/",
          "https://example.test:80x/"}) {
        EXPECT_EQ(ParseAndValidate(input).error, AddressError::kInvalidPort) << input;
    }
}

TEST(CefAddressParser, RejectsEmptyAndMalformedHosts) {
    for (const std::string_view input :
         {"https:///path", "https://.localhost/", "https://192.0.2.1/", "https://[::2]/"}) {
        EXPECT_FALSE(ParseAndValidate(input).is_valid()) << input;
    }
}

}  // namespace
}  // namespace island

int main(int argc, char* argv[]) { return island::RunCefAddressParserTests(argc, argv); }
