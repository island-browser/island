#include "keymap.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace island {
namespace {

TEST(KeymapTest, EveryActionHasAUniqueIdAndAValidOrEmptyDefault) {
    std::set<std::string_view> ids;
    ASSERT_EQ(KeyActions().size(), kKeyActionCount);
    for (std::size_t i = 0; i < KeyActions().size(); ++i) {
        const KeyActionInfo& info = KeyActions()[i];
        EXPECT_EQ(static_cast<std::size_t>(info.action), i) << info.id;
        EXPECT_TRUE(ids.insert(info.id).second) << info.id;
        EXPECT_FALSE(info.label.empty());
        if (!info.default_binding.empty()) {
            EXPECT_TRUE(ParseKeyBinding(info.default_binding).has_value()) << info.default_binding;
        }
        EXPECT_EQ(KeyActionFromId(info.id), info.action);
    }
    EXPECT_FALSE(KeyActionFromId("format_disk").has_value());
}

TEST(KeymapTest, DefaultsHaveNoConflicts) { EXPECT_TRUE(Keymap::Defaults().Conflicts().empty()); }

TEST(KeymapTest, ParsesModifiersKeysAndRejectsPlainTyping) {
    EXPECT_EQ(ParseKeyBinding("Mod+Shift+K"),
              (KeyBinding{.key_code = 'K', .primary = true, .shift = true}));
    EXPECT_EQ(ParseKeyBinding(" cmd + option + left "),
              (KeyBinding{.key_code = 0x25, .primary = true, .alt = true}));
    EXPECT_EQ(ParseKeyBinding("Ctrl+]"), (KeyBinding{.key_code = 0xDD, .primary = true}));
    EXPECT_EQ(ParseKeyBinding("Mod+,"), (KeyBinding{.key_code = 0xBC, .primary = true}));
    EXPECT_EQ(ParseKeyBinding("F5"), (KeyBinding{.key_code = 0x74}));
    EXPECT_EQ(ParseKeyBinding("Shift+F12"), (KeyBinding{.key_code = 0x7B, .shift = true}));
    EXPECT_EQ(ParseKeyBinding("Mod+7"), (KeyBinding{.key_code = '7', .primary = true}));
    // Without Mod/Alt a non-function key would hijack typing.
    EXPECT_FALSE(ParseKeyBinding("K").has_value());
    EXPECT_FALSE(ParseKeyBinding("Shift+S").has_value());
    EXPECT_FALSE(ParseKeyBinding("Mod+").has_value());
    EXPECT_FALSE(ParseKeyBinding("Mod++").has_value());
    EXPECT_FALSE(ParseKeyBinding("Hyper+K").has_value());
    EXPECT_FALSE(ParseKeyBinding("Mod+F99").has_value());
    EXPECT_FALSE(ParseKeyBinding("").has_value());
}

TEST(KeymapTest, FormatsCanonicallyAndRoundTrips) {
    for (const KeyActionInfo& info : KeyActions()) {
        const auto binding = Keymap::Defaults().Binding(info.action);
        if (!binding) continue;
        EXPECT_EQ(ParseKeyBinding(FormatKeyBinding(*binding)), binding) << info.id;
        EXPECT_EQ(ParseKeyBinding(FormatKeyBinding(*binding, true)), binding) << info.id;
    }
    EXPECT_EQ(FormatKeyBinding(*ParseKeyBinding("shift+mod+alt+left")), "Mod+Alt+Shift+Left");
    EXPECT_EQ(FormatKeyBinding(*ParseKeyBinding("Mod+Shift+K"), true), "Cmd+Shift+K");
}

TEST(KeymapTest, OverridesApplyRemapAndUnbind) {
    std::vector<std::string> errors;
    const Keymap keymap = Keymap::WithOverrides(
        {{"new_tab", "Mod+Shift+T"}, {"close_tab", ""}, {"nonsense", "Mod+X"}, {"reload", "R"}},
        &errors);
    EXPECT_EQ(keymap.Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Shift+T"));
    EXPECT_FALSE(keymap.Binding(KeyAction::kCloseTab).has_value());
    EXPECT_EQ(keymap.Binding(KeyAction::kReload), ParseKeyBinding("Mod+R"));  // kept default
    EXPECT_EQ(errors.size(), 2U);
    EXPECT_EQ(keymap.ActionFor(*ParseKeyBinding("Mod+Shift+T")), KeyAction::kNewTab);

    const auto overrides = keymap.Overrides();
    EXPECT_EQ(overrides, (std::vector<std::pair<std::string, std::string>>{
                             {"new_tab", "Mod+Shift+T"}, {"close_tab", ""}}));
    EXPECT_EQ(Keymap::WithOverrides(overrides), keymap);
}

TEST(KeymapTest, ConflictsAreReportedAndResetRestoresTheDefault) {
    Keymap keymap = Keymap::Defaults();
    keymap.SetBinding(KeyAction::kSettings, ParseKeyBinding("Mod+T"));
    const auto conflicts = keymap.Conflicts();
    ASSERT_EQ(conflicts.size(), 1U);
    EXPECT_EQ(conflicts[0], std::make_pair(KeyAction::kNewTab, KeyAction::kSettings));
    keymap.ResetToDefault(KeyAction::kSettings);
    EXPECT_TRUE(keymap.Conflicts().empty());
    EXPECT_EQ(keymap, Keymap::Defaults());
}

TEST(KeymapTest, MacKeyEquivalentsUseMenuConventions) {
    const auto letter = ToMacKeyEquivalent(*ParseKeyBinding("Mod+Shift+K"));
    ASSERT_TRUE(letter.has_value());
    EXPECT_EQ(letter->key, "k");
    EXPECT_TRUE(letter->command);
    EXPECT_TRUE(letter->shift);
    EXPECT_FALSE(letter->option);
    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("Mod+,"))->key, ",");
    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("F2"))->key, "\xEF\x9C\x85");        // U+F705
    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("Alt+Left"))->key, "\xEF\x9C\x82");  // U+F702
}

}  // namespace
}  // namespace island
