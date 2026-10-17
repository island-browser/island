#include "settings_focus.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace island {
namespace {

TEST(SettingsFocus, StartsWithoutARequest) {
    const SettingsFocus focus;
    EXPECT_TRUE(focus.section().empty());
    EXPECT_EQ(focus.sequence(), 0u);
}

TEST(SettingsFocus, RepeatedRequestForTheSameSectionGetsANewToken) {
    SettingsFocus focus;
    focus.Request("privacy");
    const std::uint64_t first = focus.sequence();
    focus.Clear();
    focus.Request("privacy");
    EXPECT_NE(focus.sequence(), first);
    EXPECT_EQ(focus.section(), "privacy");
}

TEST(SettingsFocus, ClearEmptiesTheSectionButKeepsTheToken) {
    SettingsFocus focus;
    focus.Request("agent");
    const std::uint64_t token = focus.sequence();
    focus.Clear();
    EXPECT_TRUE(focus.section().empty());
    EXPECT_EQ(focus.sequence(), token);
}

}  // namespace
}  // namespace island
