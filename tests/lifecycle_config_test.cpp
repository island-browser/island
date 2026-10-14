#include "lifecycle_config.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

TEST(WindowConfiguration, DefinesThePhaseZeroNativeWindow) {
    EXPECT_EQ(island::kWindowTitle, "Island");
    EXPECT_EQ(island::kWindowWidth, 1024);
    EXPECT_EQ(island::kWindowHeight, 768);
    EXPECT_EQ(island::kRemoteDebuggingPort, 9222);
}

TEST(WindowConfiguration, NeverReachesTheMacOSKeychain) {
    // Without the mock keychain Chromium looks up "Chromium Safe Storage" in the
    // login keychain at startup and macOS answers with a "Keychain Not Found"
    // or access dialog.
    const bool mock_keychain =
        std::find(island::kBrowserProcessSwitches.begin(), island::kBrowserProcessSwitches.end(),
                  std::string_view("use-mock-keychain")) != island::kBrowserProcessSwitches.end();
#if defined(__APPLE__)
    EXPECT_TRUE(mock_keychain);
#else
    EXPECT_FALSE(mock_keychain);
#endif
}
