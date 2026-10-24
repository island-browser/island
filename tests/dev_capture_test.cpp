#include "dev_capture.h"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

namespace island {
namespace {

DevCaptureOptions ParseArgs(std::vector<std::string_view> arguments) {
    return DevCaptureOptions::Parse(arguments);
}

TEST(DevCaptureOptions, IsDisabledWithoutDevFlags) {
    const DevCaptureOptions options = ParseArgs({"island_browser", "--island-smoke-test"});
    EXPECT_FALSE(options.enabled());
    EXPECT_TRUE(options.actions.empty());
    EXPECT_TRUE(options.capture_path.empty());
    EXPECT_DOUBLE_EQ(options.delay_seconds, 3.0);
    EXPECT_FALSE(options.keep_open);
}

TEST(DevCaptureOptions, ParsesActionsCaptureDelayAndKeepOpen) {
    const DevCaptureOptions options = ParseArgs(
        {"island_browser", "--island-dev-actions=welcome,theme:dark,,command_palette",
         "--island-dev-capture=/tmp/shot.png", "--island-dev-delay=1.5", "--island-dev-keep-open"});
    EXPECT_TRUE(options.enabled());
    EXPECT_EQ(options.actions,
              (std::vector<std::string>{"welcome", "theme:dark", "command_palette"}));
    EXPECT_EQ(options.capture_path, "/tmp/shot.png");
    EXPECT_DOUBLE_EQ(options.delay_seconds, 1.5);
    EXPECT_TRUE(options.keep_open);
}

TEST(DevCaptureOptions, IgnoresMalformedOrOutOfRangeDelays) {
    EXPECT_DOUBLE_EQ(ParseArgs({"--island-dev-delay=abc"}).delay_seconds, 3.0);
    EXPECT_DOUBLE_EQ(ParseArgs({"--island-dev-delay=2s"}).delay_seconds, 3.0);
    EXPECT_DOUBLE_EQ(ParseArgs({"--island-dev-delay=-1"}).delay_seconds, 3.0);
    EXPECT_DOUBLE_EQ(ParseArgs({"--island-dev-delay=600"}).delay_seconds, 3.0);
    EXPECT_DOUBLE_EQ(ParseArgs({"--island-dev-delay=0"}).delay_seconds, 0.0);
}

TEST(DevCaptureActions, MenuBackedNamesMapToTheirSelectors) {
    EXPECT_EQ(DevActionSelectorName("command_palette"), "openCommandPalette:");
    EXPECT_EQ(DevActionSelectorName("cheatsheet"), "showCheatsheet:");
    EXPECT_EQ(DevActionSelectorName("theme:dark"), "useDarkTheme:");
    EXPECT_TRUE(DevActionSelectorName("navigate:https://example.com").empty());
    EXPECT_TRUE(DevActionSelectorName("no_such_action").empty());
}

TEST(DevCaptureActions, SettingsActionOpensANamedSection) {
    EXPECT_EQ(DevSettingsSectionName("settings:shortcuts"), "shortcuts");
    EXPECT_EQ(DevSettingsSectionName("settings:appearance"), "appearance");
    EXPECT_TRUE(DevSettingsSectionName("settings:").empty());
    EXPECT_TRUE(DevSettingsSectionName("settings").empty());
    EXPECT_TRUE(DevSettingsSectionName("navigate:settings:shortcuts").empty());
}

TEST(DevCaptureActions, ParsesSpaceIconActions) {
    EXPECT_EQ(ParseDevSpaceIconAction("space_icon:1=briefcase"),
              (DevSpaceIconAction{.index = 1, .icon_id = "briefcase"}));
    EXPECT_EQ(ParseDevSpaceIconAction("space_icon:0=none"),
              (DevSpaceIconAction{.index = 0, .icon_id = ""}));
    EXPECT_FALSE(ParseDevSpaceIconAction("space_icon:x=home").has_value());
    EXPECT_FALSE(ParseDevSpaceIconAction("space_icon:0=unknown").has_value());
    EXPECT_FALSE(ParseDevSpaceIconAction("space_icon:0=").has_value());
}

}  // namespace
TEST(DevCaptureOptions, ParsesAgentFixtureConfirm) {
    EXPECT_EQ(DevAgentFixtureName("agent_fixture:confirm"), "confirm");
    EXPECT_TRUE(DevAgentFixtureName("agent_fixture:").empty());
    EXPECT_TRUE(DevAgentFixtureName("agent_fixture").empty());
    EXPECT_TRUE(DevAgentFixtureName("navigate:agent_fixture:confirm").empty());
    EXPECT_TRUE(DevActionSelectorName("agent_fixture:confirm").empty());
    const DevCaptureOptions options = ParseArgs(
        {"--island-dev-actions=theme:light,toggle_agent,agent_fixture:confirm"});
    EXPECT_EQ(options.actions.back(), "agent_fixture:confirm");
    EXPECT_EQ(DevAgentFixtureName("agent_fixture:queue"), "queue");
    EXPECT_EQ(DevAgentFixtureName("agent_fixture:thread"), "thread");
}

}  // namespace island
