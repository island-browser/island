// Pins the welcome card's headless decisions: the unreadable-sources note wraps into
// full lines instead of ellipsizing, the overlay height follows the source mix, and
// the button color roles meet contrast in both themes (the card paints them from
// these functions again after every CEF theme reset).
#include <gtest/gtest.h>

#include <numeric>
#include <string>
#include <vector>

#include "design_tokens.h"
#include "welcome_flow.h"

namespace island {
namespace {

std::string Join(const std::vector<std::string>& lines) {
    std::string joined;
    for (const std::string& line : lines) {
        joined += (joined.empty() ? "" : " ") + line;
    }
    return joined;
}

TEST(WelcomeFlowStyleTest, NoteWrapsIntoFullLinesWithoutDroppingAnyName) {
    const std::vector<std::string> names = {"Google Chrome", "Microsoft Edge", "Brave",
                                            "Chromium",      "Vivaldi",        "Opera"};
    const std::vector<std::string> lines = WelcomeFlow::WrapUnreadableNote(names, 40);
    ASSERT_GT(lines.size(), 1U);
    const std::string text = Join(lines);
    EXPECT_EQ(text.rfind("Not readable on this machine: ", 0), 0U);
    for (const std::string& name : names) {
        EXPECT_NE(text.find(name), std::string::npos) << name;
    }
    EXPECT_EQ(text.find("\xE2\x80\xA6"), std::string::npos);
    EXPECT_EQ(text.back(), names.back().back());
    // The lead-in plus the first name always share the first line; every later line
    // respects the limit.
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            EXPECT_LE(lines[i].size(), 40U);
        }
        if (i + 1 < lines.size()) {
            EXPECT_EQ(lines[i].back(), ',');
        }
    }
}

TEST(WelcomeFlowStyleTest, ShortNoteStaysOnOneLineAndEmptyNoteHasNoLines) {
    EXPECT_EQ(WelcomeFlow::WrapUnreadableNote({"Brave"}, 64).size(), 1U);
    EXPECT_TRUE(WelcomeFlow::WrapUnreadableNote({}, 64).empty());
}

TEST(WelcomeFlowStyleTest, OverlayHeightGrowsWithRowsAndNoteLines) {
    const int base = WelcomeFlow::ContentHeightDip(0, 0);
    EXPECT_GT(WelcomeFlow::ContentHeightDip(1, 0), base);
    EXPECT_GT(WelcomeFlow::ContentHeightDip(2, 0), WelcomeFlow::ContentHeightDip(1, 0));
    EXPECT_GT(WelcomeFlow::ContentHeightDip(0, 2), WelcomeFlow::ContentHeightDip(0, 1));
    // 560 wide card, never taller than the 800x560 minimum window with a full list.
    EXPECT_LT(WelcomeFlow::ContentHeightDip(4, 3), 560);
    EXPECT_EQ(WelcomeFlow::WidthDip(), 560);
}

TEST(WelcomeFlowStyleTest, ButtonRolesFollowDesignSystemInBothThemes) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        // Primary: --text background, --surface foreground.
        const WelcomeFlow::ButtonColors primary = WelcomeFlow::PrimaryButtonColors(tokens);
        EXPECT_EQ(primary.fill, tokens.text);
        EXPECT_EQ(primary.text, tokens.surface);
        EXPECT_GE(ContrastRatio(primary.text, primary.fill), 4.5);
        // Secondary: surface with --text.
        const WelcomeFlow::ButtonColors secondary = WelcomeFlow::SecondaryButtonColors(tokens);
        EXPECT_EQ(secondary.fill, tokens.surface);
        EXPECT_GE(ContrastRatio(secondary.text, secondary.fill), 4.5);
        // Segments: the selected one is a neutral plate with normal text.
        const WelcomeFlow::ButtonColors selected = WelcomeFlow::SegmentColors(tokens, true);
        const WelcomeFlow::ButtonColors idle = WelcomeFlow::SegmentColors(tokens, false);
        EXPECT_EQ(selected.fill, tokens.segment_selected);
        EXPECT_EQ(selected.text, tokens.text);
        EXPECT_NE(selected.text, tokens.accent);
        EXPECT_NE(selected.fill, idle.fill);
        EXPECT_GE(ContrastRatio(selected.text, selected.fill), 4.5);
        EXPECT_GE(ContrastRatio(idle.text, idle.fill), 4.5);
    }
}

}  // namespace
}  // namespace island
