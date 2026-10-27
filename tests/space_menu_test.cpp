#include "space_menu.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace island {
namespace {

TEST(SpaceMenuTest, EveryRowIdParsesBackToItsAction) {
    const std::vector<PaletteCommandEntry> commands = SpaceMenuPaletteCommands(/*can_delete=*/true);
    // Rename, change icon, five colour presets, new space, delete space.
    ASSERT_EQ(commands.size(), 9U);
    EXPECT_EQ(ParseSpaceMenuCommandId(commands[0].id)->action, SpaceMenuAction::kRename);
    EXPECT_EQ(ParseSpaceMenuCommandId(commands[1].id)->action, SpaceMenuAction::kChangeIcon);
    for (std::size_t i = 2; i < 7; ++i) {
        const std::optional<SpaceMenuCommand> parsed = ParseSpaceMenuCommandId(commands[i].id);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->action, SpaceMenuAction::kSetColor);
        EXPECT_EQ(parsed->hex, kSpaceMenuColorChoices[i - 2].hex);
    }
    EXPECT_EQ(ParseSpaceMenuCommandId(commands[7].id)->action, SpaceMenuAction::kNewSpace);
    EXPECT_EQ(ParseSpaceMenuCommandId(commands[8].id)->action, SpaceMenuAction::kDeleteSpace);
    for (const PaletteCommandEntry& command : commands) {
        EXPECT_TRUE(command.id.starts_with("space_menu:")) << command.id;
    }
}

TEST(SpaceMenuTest, DeleteIsOfferedOnlyWhenAnotherSpaceWouldRemain) {
    const std::vector<PaletteCommandEntry> commands =
        SpaceMenuPaletteCommands(/*can_delete=*/false);
    ASSERT_EQ(commands.size(), 8U);
    for (const PaletteCommandEntry& command : commands) {
        const std::optional<SpaceMenuCommand> parsed = ParseSpaceMenuCommandId(command.id);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_NE(parsed->action, SpaceMenuAction::kDeleteSpace);
    }
}

TEST(SpaceMenuTest, RejectsIdsThatAreNotMenuRows) {
    EXPECT_FALSE(ParseSpaceMenuCommandId("space_icon:none").has_value());
    EXPECT_FALSE(ParseSpaceMenuCommandId("rename_space").has_value());
    EXPECT_FALSE(ParseSpaceMenuCommandId("space_menu:explode").has_value());
    EXPECT_FALSE(ParseSpaceMenuCommandId("space_menu:color:#123456").has_value());
    EXPECT_FALSE(ParseSpaceMenuCommandId("space_menu:color:").has_value());
}

TEST(SpaceMenuTest, ColourChoicesMatchTheirHexValues) {
    for (const SpaceMenuColorChoice& choice : kSpaceMenuColorChoices) {
        ASSERT_EQ(choice.hex.size(), 7U);
        std::uint32_t rgb = 0;
        for (const char ch : choice.hex.substr(1)) {
            rgb = rgb * 16 +
                  static_cast<std::uint32_t>(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
        }
        EXPECT_EQ(choice.color.argb, 0xFF000000U | rgb) << choice.name;
    }
}

}  // namespace
}  // namespace island
