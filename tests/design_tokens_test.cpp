#include "design_tokens.h"

#include <gtest/gtest.h>

#include "chrome_snapshot.h"

namespace island {
namespace {

TEST(ChromeTokens, ProvidesTheExactLightSemanticValues) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kLight);

    EXPECT_EQ(tokens.background.argb, 0xFFF3F0E9U);
    EXPECT_EQ(tokens.surface.argb, 0xFFFFFEFBU);
    EXPECT_EQ(tokens.surface_secondary.argb, 0xFFECE9E2U);
    EXPECT_EQ(tokens.text.argb, 0xFF18303AU);
    EXPECT_EQ(tokens.text_secondary.argb, 0xFF687A7DU);
    EXPECT_EQ(tokens.border.argb, 0xFFD8D8D0U);
    EXPECT_EQ(tokens.accent.argb, 0xFF168C99U);
}

TEST(ChromeTokens, ProvidesTheExactDarkSemanticValues) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kDark);

    EXPECT_EQ(tokens.background.argb, 0xFF0D1B26U);
    EXPECT_EQ(tokens.surface.argb, 0xFF142633U);
    EXPECT_EQ(tokens.surface_secondary.argb, 0xFF1B3040U);
    EXPECT_EQ(tokens.text.argb, 0xFFEAF3F3U);
    EXPECT_EQ(tokens.text_secondary.argb, 0xFF9CB0B5U);
    EXPECT_EQ(tokens.border.argb, 0xFF29414EU);
    EXPECT_EQ(tokens.accent.argb, 0xFF168C99U);
}

TEST(ChromeTokens, UsesTheSpecifiedLayoutAndFontTokensInEveryTheme) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);

        EXPECT_EQ(tokens.rail_width_dip, 286);
        EXPECT_EQ(tokens.radius_small_dip, 8);
        EXPECT_EQ(tokens.radius_medium_dip, 12);
        EXPECT_EQ(tokens.spacing_1_dip, 4);
        EXPECT_EQ(tokens.spacing_2_dip, 8);
        EXPECT_EQ(tokens.spacing_3_dip, 12);
        EXPECT_EQ(tokens.spacing_4_dip, 16);
        EXPECT_EQ(tokens.spacing_6_dip, 24);
        EXPECT_EQ(tokens.ui_font, ChromeFont::kGeist);
        EXPECT_EQ(tokens.mono_font, ChromeFont::kGeistMono);
    }
}

TEST(ChromeSnapshot, UsesValueOnlyChromeGeometryAndFocusContracts) {
    const DipRect bounds{.x = 286, .y = 0, .width = 1154, .height = 900};
    const ChromeSnapshot snapshot{
        .focus_target = FocusTarget::kAddress,
        .rail_bounds = {.x = 0, .y = 0, .width = 286, .height = 900},
        .content_bounds = bounds,
    };

    EXPECT_EQ(snapshot.focus_target, FocusTarget::kAddress);
    EXPECT_EQ(snapshot.rail_bounds.width, 286);
    EXPECT_EQ(snapshot.content_bounds, bounds);
}

}  // namespace
}  // namespace island
