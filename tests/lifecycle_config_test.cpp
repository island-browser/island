#include "lifecycle_config.h"

#include <gtest/gtest.h>

TEST(WindowConfiguration, DefinesThePhaseZeroNativeWindow) {
    EXPECT_EQ(island::kWindowTitle, "Island");
    EXPECT_EQ(island::kWindowWidth, 1024);
    EXPECT_EQ(island::kWindowHeight, 768);
    EXPECT_EQ(island::kRemoteDebuggingPort, 9222);
}
