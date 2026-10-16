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
    const ArgbColor spaces[] = {{0xFF0090FFU}, {0xFF30A46CU}, {0xFFFFB224U}, {0xFFE5484DU},
                                {0xFF8E4EC6U}, {0xFFFFF7AEU}, {0xFFFAFAFAU}, {0xFF111111U}};
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

    EXPECT_EQ(tokens.background.argb, 0xFFFAFAFAU);
    EXPECT_EQ(tokens.surface.argb, 0xFFFFFFFFU);
    EXPECT_EQ(tokens.surface_secondary.argb, 0xFFF2F2F2U);
    EXPECT_EQ(tokens.text.argb, 0xFF0A0A0AU);
    EXPECT_EQ(tokens.text_secondary.argb, 0xFF666666U);
    EXPECT_EQ(tokens.border.argb, 0xFFE5E5E5U);
    EXPECT_EQ(tokens.accent.argb, 0xFF0068D6U);
    // Arc surfaces (neutral defaults; a space tints them at runtime).
    EXPECT_EQ(tokens.canvas.argb, 0xFFF2F2F2U);
    EXPECT_EQ(tokens.selected.argb, 0xFFFFFFFFU);
    EXPECT_EQ(tokens.hover.argb, 0xFFEAEAEAU);
    EXPECT_EQ(tokens.well.argb, 0xFFFAFAFAU);
    EXPECT_EQ(tokens.plate.argb, 0xFFFFFFFFU);
}

TEST(ChromeTokens, ProvidesTheExactDarkSemanticValues) {
    const ChromeTokens tokens = ChromeTokens::ForTheme(ChromeTheme::kDark);

    EXPECT_EQ(tokens.background.argb, 0xFF0A0A0AU);
    EXPECT_EQ(tokens.surface.argb, 0xFF111111U);
    EXPECT_EQ(tokens.surface_secondary.argb, 0xFF1A1A1AU);
    EXPECT_EQ(tokens.text.argb, 0xFFEDEDEDU);
    EXPECT_EQ(tokens.text_secondary.argb, 0xFFA1A1A1U);
    EXPECT_EQ(tokens.border.argb, 0xFF2A2A2AU);
    EXPECT_EQ(tokens.accent.argb, 0xFF3291FFU);
    EXPECT_EQ(tokens.canvas.argb, 0xFF1A1A1AU);
    EXPECT_EQ(tokens.selected.argb, 0xFF2A2A2AU);
    EXPECT_EQ(tokens.hover.argb, 0xFF222222U);
    EXPECT_EQ(tokens.well.argb, 0xFF242424U);
    EXPECT_EQ(tokens.plate.argb, 0xFFEDEDEDU);
}

TEST(NativeLookTokensTest, LightMatchesTheSpec) {
    const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    EXPECT_EQ(t.background.argb, 0xFFEBEBEFU);
    EXPECT_EQ(t.surface.argb, 0xFFFFFFFFU);
    EXPECT_EQ(t.surface_secondary.argb, 0xFFF6F6F8U);
    EXPECT_EQ(t.fill.argb, 0xFFE8E8EDU);
    EXPECT_EQ(t.text.argb, 0xFF1D1D1FU);
    EXPECT_EQ(t.text_secondary.argb, 0xFF636366U);
    EXPECT_EQ(t.text_tertiary.argb, 0xFF8E8E93U);
    EXPECT_EQ(t.border.argb, 0xFFE5E5EAU);
    EXPECT_EQ(t.border_strong.argb, 0xFFD2D2D7U);
    EXPECT_EQ(t.accent.argb, 0xFF0A64D8U);
    EXPECT_EQ(t.selection.argb, 0xFF0A64D8U);
    EXPECT_EQ(t.accent_soft.argb, 0xFFDFE9F8U);
    EXPECT_EQ(t.accent_text.argb, 0xFF0A4FA8U);
    EXPECT_EQ(t.warning_bg.argb, 0xFFFFF8E6U);
    EXPECT_EQ(t.warning_border.argb, 0xFFF5D38AU);
    EXPECT_EQ(t.warning_text.argb, 0xFF6A4A00U);
    EXPECT_EQ(t.success.argb, 0xFF248A3DU);
    EXPECT_EQ(t.danger.argb, 0xFFD70015U);
    EXPECT_EQ(t.branch[0].argb, 0xFF0A64D8U);
    EXPECT_EQ(t.branch[1].argb, 0xFF8E44ADU);
    EXPECT_EQ(t.branch[2].argb, 0xFFC25400U);
    EXPECT_EQ(t.branch[3].argb, 0xFF1F8A70U);
    EXPECT_EQ(t.canvas, t.background);  // no space tint in Native
}

TEST(NativeLookTokensTest, DarkMatchesTheSpec) {
    const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    EXPECT_EQ(t.background.argb, 0xFF2A2A2DU);
    EXPECT_EQ(t.surface.argb, 0xFF1E1E20U);
    EXPECT_EQ(t.surface_secondary.argb, 0xFF252527U);
    EXPECT_EQ(t.fill.argb, 0xFF3A3A3DU);
    EXPECT_EQ(t.text.argb, 0xFFF5F5F7U);
    EXPECT_EQ(t.text_secondary.argb, 0xFF98989DU);
    EXPECT_EQ(t.text_tertiary.argb, 0xFF6E6E73U);
    EXPECT_EQ(t.border.argb, 0xFF38383AU);
    EXPECT_EQ(t.border_strong.argb, 0xFF48484AU);
    EXPECT_EQ(t.accent.argb, 0xFF0A84FFU);
    EXPECT_EQ(t.selection.argb, 0xFF0A6FE0U);
    EXPECT_EQ(t.accent_soft.argb, 0xFF16324FU);
    EXPECT_EQ(t.accent_text.argb, 0xFF64B5FFU);
    EXPECT_EQ(t.warning_bg.argb, 0xFF3A2E12U);
    EXPECT_EQ(t.warning_border.argb, 0xFF6B5420U);
    EXPECT_EQ(t.warning_text.argb, 0xFFFFD60AU);
    EXPECT_EQ(t.success.argb, 0xFF30D158U);
    EXPECT_EQ(t.danger.argb, 0xFFFF453AU);
    EXPECT_EQ(t.branch[0].argb, 0xFF0A84FFU);
    EXPECT_EQ(t.branch[1].argb, 0xFFBF5AF2U);
    EXPECT_EQ(t.branch[2].argb, 0xFFFF9F0AU);
    EXPECT_EQ(t.branch[3].argb, 0xFF40C8A0U);
}

TEST(NativeLookTokensTest, EveryTextPairPassesAa) {
    for (ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, theme);
        const ArgbColor white{.argb = 0xFFFFFFFFU};
        EXPECT_GE(ContrastRatio(t.text, t.background), 4.5);
        EXPECT_GE(ContrastRatio(t.text_secondary, t.background), 4.5);
        EXPECT_GE(ContrastRatio(t.text_secondary, t.surface), 4.5);
        EXPECT_GE(ContrastRatio(white, t.selection), 4.5);
        EXPECT_GE(ContrastRatio(t.accent_text, t.accent_soft), 4.5);
        EXPECT_GE(ContrastRatio(t.warning_text, t.warning_bg), 4.5);
    }
}

TEST(NativeLookTokensTest, NativeIgnoresTheSpaceColor) {
    AppearanceSettings appearance;
    appearance.look = LookPreset::kNative;
    const ChromeTokens base = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const ChromeTokens tinted =
        base.Customized({.argb = 0xFFE5484DU}, ChromeTheme::kLight, appearance);
    EXPECT_EQ(tinted.canvas, base.background);
    EXPECT_EQ(tinted.accent, base.accent);
}

TEST(ChromeTokens, ProvidesTheArcCardAndDensityDefaultsInEveryTheme) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);

        EXPECT_EQ(tokens.card_radius_dip, 10);
        EXPECT_EQ(tokens.card_inset_dip, 8);
        EXPECT_EQ(tokens.control_height_dip, 32);
        EXPECT_EQ(tokens.titlebar_band_dip, 0);
        EXPECT_TRUE(tokens.card_shadow);
        EXPECT_EQ(tokens.sidebar_side, SidebarSide::kLeft);
    }
    const AppearanceSettings defaults;
    EXPECT_EQ(defaults.look, LookPreset::kNative);
    EXPECT_EQ(defaults.sidebar_tint_percent, 60);
    EXPECT_EQ(defaults.card_radius_dip, 10);
    EXPECT_EQ(defaults.density, Density::kComfortable);
    EXPECT_FALSE(defaults.full_bleed);
}

TEST(ChromeTokens, UsesTheSpecifiedLayoutAndFontTokensInEveryTheme) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);

        EXPECT_EQ(tokens.rail_width_dip, 286);
        EXPECT_EQ(tokens.radius_small_dip, 6);
        EXPECT_EQ(tokens.radius_medium_dip, 10);
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
