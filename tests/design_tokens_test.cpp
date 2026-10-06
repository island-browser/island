#include "design_tokens.h"

#include <gtest/gtest.h>

#include <type_traits>

#include "chrome_snapshot.h"

namespace island {
namespace {

class RecordingChromeObserver final : public ChromeObserver {
  public:
    void OnChromeChanged(const ChromeSnapshot& snapshot) override { latest_snapshot = snapshot; }

    ChromeSnapshot latest_snapshot;
};

TEST(ChromeTokens, MixesAndMeasuresContrastLikeWcag) {
    EXPECT_EQ(MixColor({0xFF000000U}, {0xFFFFFFFFU}, 0.0).argb, 0xFF000000U);
    EXPECT_EQ(MixColor({0xFF000000U}, {0xFFFFFFFFU}, 1.0).argb, 0xFFFFFFFFU);
    EXPECT_EQ(MixColor({0xFF000000U}, {0xFFFFFFFFU}, 0.5).argb, 0xFF808080U);
    EXPECT_NEAR(ContrastRatio({0xFF000000U}, {0xFFFFFFFFU}), 21.0, 0.01);
    EXPECT_NEAR(ContrastRatio({0xFF777777U}, {0xFF777777U}), 1.0, 1e-9);
}

TEST(ChromeTokens, SpaceTintKeepsTextReadableAndTheAccentVisible) {
    // The window's space palette plus deliberately hard cases (pale yellow,
    // near-white, near-black).
    const ArgbColor spaces[] = {{0xFF5B8DEFU}, {0xFF34A853U}, {0xFFFBBC05U}, {0xFFEA4335U},
                                {0xFF9334E6U}, {0xFFFFF7AEU}, {0xFFFAFAFAU}, {0xFF111111U}};
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens base = ChromeTokens::ForTheme(theme);
        for (const ArgbColor space : spaces) {
            const ChromeTokens tinted = base.TintedForSpace(space, theme);
            SCOPED_TRACE(testing::Message()
                         << std::hex << space.argb << " dark=" << (theme == ChromeTheme::kDark));
            EXPECT_GE(ContrastRatio(tinted.text, tinted.surface_secondary), 4.5);
            EXPECT_GE(ContrastRatio(tinted.text, tinted.background), 4.5);
            EXPECT_GE(ContrastRatio(tinted.text, tinted.surface), 4.5);
            EXPECT_GE(ContrastRatio(tinted.accent, tinted.surface), 3.0);
            EXPECT_EQ(tinted.text, base.text);
            EXPECT_EQ(tinted.rail_width_dip, base.rail_width_dip);
            EXPECT_NE(tinted.surface_secondary, base.surface_secondary);
        }
    }
}

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

TEST(ChromeSnapshot, DefaultsToTheLightChromeTheme) {
    EXPECT_EQ(ChromeSnapshot{}.theme, ChromeTheme::kLight);
}

TEST(ChromeSnapshot, NotifiesObserversThroughTheNonOwningInterface) {
    static_assert(std::has_virtual_destructor_v<ChromeObserver>);

    RecordingChromeObserver observer;
    const ChromeSnapshot snapshot{
        .focus_target = FocusTarget::kReload,
        .back_enabled = true,
        .theme = ChromeTheme::kDark,
    };

    observer.OnChromeChanged(snapshot);

    EXPECT_EQ(observer.latest_snapshot, snapshot);
    EXPECT_EQ(observer.latest_snapshot.theme, ChromeTheme::kDark);
}

}  // namespace
}  // namespace island
