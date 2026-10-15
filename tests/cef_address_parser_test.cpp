#include "cef_address_parser.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "lifecycle_config.h"
#if defined(OS_MAC)
#include "include/wrapper/cef_library_loader.h"
#endif
#if defined(OS_WIN)
#include <windows.h>
#endif

namespace island {
namespace {

class MockKeychainApp final : public CefApp {
  public:
    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override {
        if (process_type.empty()) {
            for (const std::string_view name : kBrowserProcessSwitches) {
                command_line->AppendSwitch(std::string(name));
            }
        }
    }

  private:
    IMPLEMENT_REFCOUNTING(MockKeychainApp);
};

}  // namespace

int RunCefAddressParserTests(int argc, char* argv[]) {
    for (int argument_index = 0; argument_index < argc; ++argument_index) {
        if (std::string_view(argv[argument_index]) == "--gtest_list_tests") {
            testing::InitGoogleTest(&argc, argv);
            return RUN_ALL_TESTS();
        }
    }
    testing::InitGoogleTest(&argc, argv);

#if defined(OS_MAC)
    CefScopedLibraryLoader library_loader;
    if (!library_loader.LoadInMain()) {
        return 1;
    }
#endif

#if defined(OS_WIN)
    CefMainArgs arguments(::GetModuleHandle(nullptr));
#else
    CefMainArgs arguments(argc, argv);
#endif
    // Same as the app: Chromium must never reach the login keychain (macOS would show a
    // "Chromium Safe Storage" password prompt on every test run).
    CefRefPtr<CefApp> app(new MockKeychainApp());
    const int subprocess_exit_code = CefExecuteProcess(arguments, app, nullptr);
    if (subprocess_exit_code >= 0) {
        return subprocess_exit_code;
    }

    CefSettings settings;
    settings.no_sandbox = true;
    // A private cache root, so the tests never collide with a running Island or with
    // another test run over /tmp/island_cef_cache's singleton lock.
    const std::filesystem::path cache =
        std::filesystem::temp_directory_path() /
        ("island_cef_tests_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CefString(&settings.root_cache_path) = cache.string();
    if (!CefInitialize(arguments, settings, app, nullptr)) {
        return 1;
    }
    const int result = RUN_ALL_TESTS();
    CefShutdown();
    std::error_code ignored;
    std::filesystem::remove_all(cache, ignored);
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

TEST(CefAddressParser, AcceptsOnlyValidBracketedIpv6AuthorityTails) {
    for (const std::string_view input :
         {"https://[::1]", "https://[::1]:8443/", "https://[::1]/path?query=value#section"}) {
        EXPECT_TRUE(ParseAndValidate(input).is_valid()) << input;
    }

    for (const std::string_view input :
         {"https://[::1]evil", "https://[::1]:443evil/", "https://[::1]]/",
          "https://[::1]@example.test/", "https://[::1]%2fevil/", "https://[::1]%00/",
          "https://[::1]%0a/", "https://[::1"}) {
        EXPECT_FALSE(ParseAndValidate(input).is_valid()) << input;
    }
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
