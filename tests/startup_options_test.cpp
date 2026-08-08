#include "startup_options.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <type_traits>

namespace {

static_assert(std::is_const_v<std::remove_reference_t<decltype(island::kProductionInitialUrl)>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(island::kSmokeTestInitialUrl)>>);

TEST(StartupOptions, DefaultsToTheFixedProductionPage) {
    constexpr std::array<std::string_view, 0> kArguments;

    const island::StartupOptions options = island::StartupOptions::Parse(kArguments);

    EXPECT_FALSE(options.is_smoke_test());
    EXPECT_EQ(options.initial_url(), island::kProductionInitialUrl);
}

TEST(StartupOptions, EnablesSmokeModeForTheExactSwitch) {
    constexpr std::array<std::string_view, 1> kArguments = {"--island-smoke-test"};

    const island::StartupOptions options = island::StartupOptions::Parse(kArguments);

    EXPECT_TRUE(options.is_smoke_test());
    EXPECT_EQ(options.initial_url(), island::kSmokeTestInitialUrl);
}

TEST(StartupOptions, IgnoresUnknownAndSimilarSwitches) {
    constexpr std::array<std::string_view, 3> kArguments = {
        "--island-smoke-test=true",
        "--island-smoke-testing",
        "--unrelated-option",
    };

    const island::StartupOptions options = island::StartupOptions::Parse(kArguments);

    EXPECT_FALSE(options.is_smoke_test());
    EXPECT_EQ(options.initial_url(), island::kProductionInitialUrl);
}

TEST(StartupOptions, ExposesOnlyFixedLocalDataPages) {
    EXPECT_EQ(island::kProductionInitialUrl.substr(0, 29), "data:text/html;charset=utf-8,");
    EXPECT_EQ(island::kSmokeTestInitialUrl.substr(0, 29), "data:text/html;charset=utf-8,");
    EXPECT_NE(island::kProductionInitialUrl.find("%3Ctitle%3EIsland%3C%2Ftitle%3E"),
              std::string_view::npos);
    EXPECT_NE(island::kProductionInitialUrl.find("Island%20Browser"), std::string_view::npos);
    EXPECT_NE(island::kSmokeTestInitialUrl.find("Island%20Smoke%20Test"), std::string_view::npos);
    EXPECT_NE(island::kSmokeTestInitialUrl.find("ISLAND_PHASE1_SMOKE_OK"), std::string_view::npos);
    EXPECT_NE(island::kSmokeTestInitialUrl.find("%23island-phase1-smoke-history"),
              std::string_view::npos);
    EXPECT_EQ(island::kProductionInitialUrl.find("http"), std::string_view::npos);
    EXPECT_EQ(island::kSmokeTestInitialUrl.find("http"), std::string_view::npos);
    EXPECT_EQ(island::kProductionInitialUrl.find("%3Cscript"), std::string_view::npos);
    EXPECT_EQ(island::kSmokeTestInitialUrl.find("%3Cscript"), std::string_view::npos);
    EXPECT_EQ(island::kProductionInitialUrl.find("src%3D"), std::string_view::npos);
    EXPECT_EQ(island::kSmokeTestInitialUrl.find("src%3D"), std::string_view::npos);
    EXPECT_EQ(island::kProductionInitialUrl.find("%3Clink"), std::string_view::npos);
    EXPECT_EQ(island::kSmokeTestInitialUrl.find("%3Clink"), std::string_view::npos);
    EXPECT_EQ(island::kProductionInitialUrl.find("url%28"), std::string_view::npos);
    EXPECT_EQ(island::kSmokeTestInitialUrl.find("url%28"), std::string_view::npos);
}

}  // namespace
