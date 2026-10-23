#include "keymap.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

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
    EXPECT_FALSE(ParseKeyBinding("F25").has_value());
    // Long digit runs used to overflow the accumulator and wrap to a real key.
    EXPECT_FALSE(ParseKeyBinding("Mod+F4294967301").has_value());
    EXPECT_FALSE(ParseKeyBinding("F0005").has_value());
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

TEST(KeymapTest, ConflictForReportsTheOtherActionBoundToAChord) {
    const Keymap keymap = Keymap::Defaults();
    EXPECT_EQ(keymap.ConflictFor(KeyAction::kNewTab, *ParseKeyBinding("Mod+B")),
              KeyAction::kToggleSidebar);
    EXPECT_EQ(keymap.ConflictFor(KeyAction::kNewTab, *ParseKeyBinding("Mod+Shift+Y")),
              std::nullopt);
    // Its own current chord is not a conflict.
    EXPECT_EQ(keymap.ConflictFor(KeyAction::kToggleSidebar, *ParseKeyBinding("Mod+B")),
              std::nullopt);
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

TEST(KeymapTest, ControlIsItsOwnModifierAndCtrlStillMeansMod) {
    EXPECT_EQ(ParseKeyBinding("Control+Tab"), (KeyBinding{.key_code = 0x09, .control = true}));
    EXPECT_EQ(ParseKeyBinding("Ctrl+Tab"), (KeyBinding{.key_code = 0x09, .primary = true}));
    EXPECT_EQ(ParseKeyBinding("Control+Shift+Tab"),
              (KeyBinding{.key_code = 0x09, .control = true, .shift = true}));
    // Shift alone is still plain typing, Control or not.
    EXPECT_FALSE(ParseKeyBinding("Shift+Tab").has_value());
    EXPECT_FALSE(ParseKeyBinding("Control").has_value());
}

TEST(KeymapTest, FormatsControlAndRoundTripsIt) {
    EXPECT_EQ(FormatKeyBinding(*ParseKeyBinding("Control+Shift+Tab")), "Control+Shift+Tab");
    EXPECT_EQ(FormatKeyBinding(*ParseKeyBinding("Control+Tab"), true), "Control+Tab");
    EXPECT_EQ(FormatKeyBinding(*ParseKeyBinding("Ctrl+Tab"), true), "Cmd+Tab");
    EXPECT_EQ(ParseKeyBinding(FormatKeyBinding(*ParseKeyBinding("Mod+Control+K"))),
              ParseKeyBinding("Mod+Control+K"));
}

TEST(KeymapTest, MacKeyEquivalentsCarryTheControlMaskAndBackTab) {
    const auto control_tab = ToMacKeyEquivalent(*ParseKeyBinding("Control+Tab"));
    ASSERT_TRUE(control_tab.has_value());
    EXPECT_EQ(control_tab->key, "\t");
    EXPECT_TRUE(control_tab->control);
    EXPECT_FALSE(control_tab->command);
    EXPECT_FALSE(control_tab->shift);

    // Shift+Tab arrives on macOS as the back-tab character.
    const auto back_tab = ToMacKeyEquivalent(*ParseKeyBinding("Control+Shift+Tab"));
    ASSERT_TRUE(back_tab.has_value());
    EXPECT_EQ(back_tab->key, "\x19");
    EXPECT_TRUE(back_tab->control);
    EXPECT_TRUE(back_tab->shift);

    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("Mod+="))->key, "=");
    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("Mod+."))->key, ".");
    EXPECT_EQ(ToMacKeyEquivalent(*ParseKeyBinding("Mod+Alt+Down"))->key, "\xEF\x9C\x81");
}

TEST(KeymapTest, PhaseFourDefaultsAreBound) {
    const Keymap defaults = Keymap::Defaults();
    EXPECT_EQ(defaults.Binding(KeyAction::kReopenClosedTab), ParseKeyBinding("Mod+Shift+T"));
    EXPECT_EQ(defaults.Binding(KeyAction::kZoomIn), ParseKeyBinding("Mod+="));
    EXPECT_EQ(defaults.Binding(KeyAction::kZoomInAlternate), ParseKeyBinding("Mod+Shift+="));
    EXPECT_EQ(defaults.Binding(KeyAction::kZoomOut), ParseKeyBinding("Mod+-"));
    EXPECT_EQ(defaults.Binding(KeyAction::kZoomReset), ParseKeyBinding("Mod+0"));
    EXPECT_EQ(defaults.Binding(KeyAction::kReloadIgnoringCache), ParseKeyBinding("Mod+Shift+R"));
    EXPECT_EQ(defaults.Binding(KeyAction::kStopLoading), ParseKeyBinding("Mod+."));
    EXPECT_EQ(defaults.Binding(KeyAction::kNextSpace), ParseKeyBinding("Mod+Alt+Down"));
    EXPECT_EQ(defaults.Binding(KeyAction::kPreviousSpace), ParseKeyBinding("Mod+Alt+Up"));
    EXPECT_EQ(defaults.Binding(KeyAction::kCopyTabUrl), ParseKeyBinding("Mod+Shift+C"));
    EXPECT_EQ(defaults.Binding(KeyAction::kNextTabAlternate), ParseKeyBinding("Control+Tab"));
    EXPECT_EQ(defaults.Binding(KeyAction::kPreviousTabAlternate),
              ParseKeyBinding("Control+Shift+Tab"));
}

TEST(KeymapTest, FindAndReaderActionsAreBoundWithoutConflicts) {
    const Keymap defaults = Keymap::Defaults();
    EXPECT_EQ(defaults.Binding(KeyAction::kFindInPage), ParseKeyBinding("Mod+F"));
    EXPECT_EQ(defaults.Binding(KeyAction::kFindNext), ParseKeyBinding("Mod+G"));
    EXPECT_EQ(defaults.Binding(KeyAction::kFindPrevious), ParseKeyBinding("Mod+Shift+G"));
    EXPECT_EQ(defaults.Binding(KeyAction::kReaderMode), ParseKeyBinding("Mod+Alt+R"));
    EXPECT_TRUE(defaults.Conflicts().empty());
    EXPECT_EQ(KeyActionFromId("find_in_page"), KeyAction::kFindInPage);
    EXPECT_EQ(KeyActionFromId("reader_mode"), KeyAction::kReaderMode);
    // Reload ignoring cache keeps Mod+Shift+R; Reader mode must not shadow it.
    EXPECT_EQ(defaults.ActionFor(*ParseKeyBinding("Mod+Shift+R")), KeyAction::kReloadIgnoringCache);
    EXPECT_EQ(defaults.ActionFor(*ParseKeyBinding("Mod+Alt+R")), KeyAction::kReaderMode);
}

TEST(KeymapTest, EveryDefaultBindingIsUniqueIncludingFixedAccelerators) {
    std::set<std::string> seen;
    for (const KeyActionInfo& info : KeyActions()) {
        const std::optional<KeyBinding> binding = Keymap::Defaults().Binding(info.action);
        if (!binding.has_value()) continue;
        EXPECT_TRUE(seen.insert(FormatKeyBinding(*binding)).second) << info.id;
    }
    // Accelerators BrowserWindow registers outside the keymap: F5 reload,
    // Mod+1..9 tab selection (9 is the last tab), and Escape (welcome
    // dismiss / stop loading).
    for (const std::string fixed : {"F5", "Mod+1", "Mod+2", "Mod+3", "Mod+4", "Mod+5", "Mod+6",
                                    "Mod+7", "Mod+8", "Mod+9", "Escape"}) {
        EXPECT_TRUE(seen.insert(fixed).second) << fixed;
    }
}

TEST(KeymapTest, ControlBindingsSurviveOverridesAndRoundTrip) {
    const Keymap keymap = Keymap::WithOverrides({{"next_tab", "Control+Tab"}});
    EXPECT_EQ(keymap.ActionFor(*ParseKeyBinding("Control+Tab")), KeyAction::kNextTab);
    EXPECT_EQ(Keymap::WithOverrides(keymap.Overrides()), keymap);
}
TEST(KeymapTest, ModSlashShowsTheCheatsheet) {
    const Keymap defaults = Keymap::Defaults();
    EXPECT_EQ(defaults.Binding(KeyAction::kShowCheatsheet), ParseKeyBinding("Mod+/"));
    EXPECT_EQ(defaults.ActionFor(*ParseKeyBinding("Mod+/")), KeyAction::kShowCheatsheet);
    EXPECT_EQ(KeyActionFromId("cheatsheet"), KeyAction::kShowCheatsheet);
    EXPECT_TRUE(defaults.Conflicts().empty());
}

TEST(CheatsheetRowsTest, DefaultsListTheTenRowsInSpecOrder) {
    const std::vector<CheatsheetRow> rows = CheatsheetRows(Keymap::Defaults(), /*mac=*/true);
    ASSERT_EQ(rows.size(), 10U);
    EXPECT_EQ(rows[0], (CheatsheetRow{"Command palette", "⌘K"}));
    const std::vector<std::string> labels = {
        "Command palette", "Ask agent",    "Toggle sidebar", "Split view", "New tab",
        "Close tab",       "Find in page", "Reader view",    "Settings",   "Cheatsheet"};
    const std::vector<std::string> keys = {"⌘K", "⌘J", "⌘B",  "⇧⌘S", "⌘T",
                                           "⌘W", "⌘F", "⌥⌘R", "⌘,",  "⌘/"};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].label, labels[i]) << i;
        EXPECT_EQ(rows[i].keys, keys[i]) << i;
    }
}

TEST(CheatsheetRowsTest, RowsFollowTheCurrentBinding) {
    Keymap keymap = Keymap::Defaults();
    keymap.SetBinding(KeyAction::kCommandPalette, ParseKeyBinding("Mod+P"));
    EXPECT_EQ(CheatsheetRows(keymap, /*mac=*/true)[0], (CheatsheetRow{"Command palette", "⌘P"}));
    EXPECT_EQ(CheatsheetRows(keymap, /*mac=*/false)[0],
              (CheatsheetRow{"Command palette", "Ctrl+P"}));
}

TEST(CheatsheetRowsTest, OtherPlatformsSpellTheModifiers) {
    const std::vector<CheatsheetRow> rows = CheatsheetRows(Keymap::Defaults(), /*mac=*/false);
    ASSERT_EQ(rows.size(), 10U);
    EXPECT_EQ(rows[0].keys, "Ctrl+K");
    EXPECT_EQ(rows[3].keys, "Ctrl+Shift+S");
    EXPECT_EQ(rows[7].keys, "Ctrl+Alt+R");
    EXPECT_EQ(rows[9].keys, "Ctrl+/");
}

TEST(CheatsheetRowsTest, AnUnboundActionKeepsItsRowWithoutKeys) {
    Keymap keymap = Keymap::Defaults();
    keymap.SetBinding(KeyAction::kToggleAgentPanel, std::nullopt);
    const std::vector<CheatsheetRow> rows = CheatsheetRows(keymap, /*mac=*/true);
    ASSERT_EQ(rows.size(), 10U);
    EXPECT_EQ(rows[1], (CheatsheetRow{"Ask agent", ""}));
}

TEST(CheatsheetRowsTest, KeyGlyphsMatchTheSettingsKeycaps) {
    // Control, Option, Shift, Command: the order Settings > Shortcuts renders.
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+Control+Alt+Shift+K"), true), "⌃⌥⇧⌘K");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Control+Tab"), true), "⌃⇥");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+Alt+Down"), true), "⌥⌘↓");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("F2"), true), "F2");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Alt+Left"), false), "Alt+Left");
}

TEST(CheatsheetRowsTest, KeyGlyphsFitWidthsFromOneToFourModifiers) {
    // The palette keycap takes its width from these strings, so each length must format.
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+J"), true), "⌘J");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+J"), false), "Ctrl+J");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+Control+Alt+Shift+J"), true), "⌃⌥⇧⌘J");
    EXPECT_EQ(FormatKeyGlyphs(*ParseKeyBinding("Mod+Control+Alt+Shift+J"), false),
              "Ctrl+Alt+Shift+J");
}
}  // namespace
}  // namespace island
