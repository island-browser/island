#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "browser_chrome.h"
#include "design_tokens.h"
#include "prefs_store.h"
#include "row_raster.h"

namespace island {
namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("island_appearance_test_" + name);
}

constexpr ArgbColor kBlue{0xFF0090FFU};

// ---- prefs ---------------------------------------------------------------------------

TEST(AppearancePrefsTest, GivenCustomAppearanceWhenSavedAndLoadedThenEveryFieldSurvives) {
    PrefsState state;
    state.appearance = {.sidebar_tint_percent = 85,
                        .sidebar_side = SidebarSide::kRight,
                        .card_radius_dip = 14,
                        .density = Density::kCompact,
                        .font_family = "Helvetica Neue",
                        .card_shadow = false,
                        .full_bleed = true};
    const std::filesystem::path path = TempPath("roundtrip.json");
    ASSERT_EQ(PrefsStore::Save(path, state), PrefsError::kNone);
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    EXPECT_EQ(loaded.error, PrefsError::kNone);
    EXPECT_EQ(loaded.state.appearance, state.appearance);
    EXPECT_EQ(loaded.state, state);
}

TEST(AppearancePrefsTest, GivenDefaultAppearanceWhenSavedThenNoAppearanceObjectIsWritten) {
    const std::filesystem::path path = TempPath("default.json");
    ASSERT_EQ(PrefsStore::Save(path, PrefsState{}), PrefsError::kNone);
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("appearance"), std::string::npos);
    EXPECT_EQ(PrefsStore::Load(path).state.appearance, AppearanceSettings{});
}

TEST(AppearancePrefsTest, GivenOutOfRangeValuesWhenLoadedThenTheyAreNormalized) {
    const std::filesystem::path path = TempPath("normalize.json");
    std::ofstream(path, std::ios::trunc)
        << R"({"version":1,"onboarding_completed":false,"theme":"system","appearance":{"sidebar_tint_percent":900,"card_radius":11,"font":"  Bad\"Font,  "}})";
    const PrefsLoadResult loaded = PrefsStore::Load(path);
    ASSERT_EQ(loaded.error, PrefsError::kNone);
    const AppearanceSettings a = loaded.state.appearance;
    EXPECT_EQ(a.sidebar_tint_percent, 100);
    EXPECT_EQ(a.card_radius_dip, 10);  // snapped to the nearest of 0/6/10/14
    EXPECT_EQ(a.font_family, "BadFont");
}

TEST(AppearancePrefsTest, GivenAMalformedAppearanceObjectWhenLoadedThenItIsASchemaError) {
    const std::filesystem::path path = TempPath("malformed.json");
    std::ofstream(path, std::ios::trunc)
        << R"({"version":1,"onboarding_completed":false,"theme":"system","appearance":5})";
    EXPECT_EQ(PrefsStore::Load(path).error, PrefsError::kSchemaError);
}

// ---- token derivation ------------------------------------------------------------------

TEST(AppearanceTokensTest, GivenTintStrengthWhenResolvedThenTheCanvasMovesTowardTheSpaceColor) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens base = ChromeTokens::ForTheme(theme);
        const auto canvas = [&](int tint) {
            return base
                .Customized(kBlue, theme, {.look = LookPreset::kArc, .sidebar_tint_percent = tint})
                .canvas;
        };
        EXPECT_GT(ContrastRatio(canvas(0), canvas(100)), 1.0);
        // The farther from neutral, the closer to the space color.
        const auto distance = [](ArgbColor a, ArgbColor b) {
            const auto channel = [&](int shift) {
                return std::abs(static_cast<int>((a.argb >> shift) & 0xFF) -
                                static_cast<int>((b.argb >> shift) & 0xFF));
            };
            return channel(16) + channel(8) + channel(0);
        };
        EXPECT_LT(distance(canvas(100), kBlue), distance(canvas(50), kBlue));
        EXPECT_LT(distance(canvas(50), kBlue), distance(canvas(0), kBlue));
    }
}

TEST(AppearanceTokensTest, GivenTheStrongDefaultThenTheSidebarIsTintedMoreThanTheOldWash) {
    // The previous Graphite cast mixed 7-8% of the space color into the sidebar.
    const ChromeTokens base = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const ChromeTokens tinted = base.TintedForSpace(kBlue, ChromeTheme::kLight);
    EXPECT_NE(tinted.canvas, MixColor(base.surface_secondary, kBlue, 0.08));
    EXPECT_GT(ContrastRatio(base.surface_secondary, tinted.canvas), 1.1);
}

TEST(AppearanceTokensTest, GivenAnySpaceColorAndTintThenTextStaysReadableOnEverySurface) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        for (const std::uint32_t space :
             {0xFF0090FFU, 0xFFFFB224U, 0xFFFFFFFFU, 0xFF000000U, 0xFFE5484DU, 0xFF8E4EC6U}) {
            for (const int tint : {0, 25, 60, 100}) {
                const ChromeTokens t = ChromeTokens::ForTheme(theme).Customized(
                    {space}, theme, {.look = LookPreset::kArc, .sidebar_tint_percent = tint});
                for (const ArgbColor surface : {t.canvas, t.selected, t.hover, t.well}) {
                    EXPECT_GE(ContrastRatio(t.text, surface), 4.5);
                    EXPECT_GE(ContrastRatio(t.text_secondary, surface), 4.5);
                }
                EXPECT_GE(ContrastRatio(t.accent, t.canvas), 3.0);
            }
        }
    }
}

TEST(AppearanceTokensTest, GivenDensityThenControlHeightAndRowGapFollow) {
    const ChromeTokens base = ChromeTokens::ForTheme(ChromeTheme::kLight);
    const ChromeTokens comfortable = base.Customized(kBlue, ChromeTheme::kLight, {});
    const ChromeTokens compact =
        base.Customized(kBlue, ChromeTheme::kLight, {.density = Density::kCompact});
    EXPECT_EQ(comfortable.control_height_dip, 32);
    EXPECT_EQ(compact.control_height_dip, 26);
    EXPECT_LT(compact.row_gap_dip, comfortable.row_gap_dip);
}

TEST(AppearanceTokensTest, GivenRadiusShadowAndBleedThenTheCardTokensFollow) {
    const ChromeTokens base = ChromeTokens::ForTheme(ChromeTheme::kDark);
    for (const int radius : {0, 6, 10, 14}) {
        const ChromeTokens t =
            base.Customized(kBlue, ChromeTheme::kDark, {.card_radius_dip = radius});
        EXPECT_EQ(t.card_radius_dip, radius);
        EXPECT_EQ(t.card_inset_dip, 8);
        EXPECT_TRUE(t.card_shadow);
    }
    EXPECT_EQ(base.Customized(kBlue, ChromeTheme::kDark, {.card_radius_dip = 9}).card_radius_dip,
              10);
    const ChromeTokens no_shadow =
        base.Customized(kBlue, ChromeTheme::kDark, {.card_shadow = false});
    EXPECT_FALSE(no_shadow.card_shadow);
    const ChromeTokens bleed = base.Customized(kBlue, ChromeTheme::kDark, {.full_bleed = true});
    EXPECT_EQ(bleed.card_radius_dip, 0);
    EXPECT_EQ(bleed.card_inset_dip, 0);
    EXPECT_FALSE(bleed.card_shadow);
}

TEST(AppearanceTokensTest, GivenFontChoicesThenTheFamilyAndCssStackResolve) {
    const ChromeTokens base = ChromeTokens::ForTheme(ChromeTheme::kLight);
    EXPECT_EQ(base.Customized(kBlue, ChromeTheme::kLight, {}).ui_font_family, "Geist");
    EXPECT_EQ(base.Customized(kBlue, ChromeTheme::kLight, {.font_family = "geist"}).ui_font_family,
              "Geist");
    const ChromeTokens system =
        base.Customized(kBlue, ChromeTheme::kLight, {.font_family = "system"});
    EXPECT_EQ(system.ui_font_family, "system");
    EXPECT_EQ(CssFontStack(system).find("Geist"), std::string::npos);
    const ChromeTokens custom =
        base.Customized(kBlue, ChromeTheme::kLight, {.font_family = "Avenir Next"});
    EXPECT_EQ(custom.ui_font_family, "Avenir Next");
    EXPECT_EQ(CssFontStack(custom).rfind("\"Avenir Next\", Geist", 0), 0U);
}

// ---- layout ---------------------------------------------------------------------------

constexpr DipRect kWindow{.x = 0, .y = 0, .width = 1440, .height = 900};

TEST(AppearanceLayoutTest, GivenSidebarOnTheRightThenRailAndCardMirror) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.sidebar_side = SidebarSide::kRight;
    const ChromeGeometrySnapshot g = BrowserChrome::LayoutForBounds(kWindow, t);
    EXPECT_EQ(g.rail_bounds, (DipRect{.x = 1440 - 286, .y = 0, .width = 286, .height = 900}));
    EXPECT_EQ(g.browser_content_bounds,
              (DipRect{.x = 0, .y = 0, .width = 1440 - 286, .height = 900}));
    // Inset on the top, left and bottom; flush with the sidebar on the right.
    EXPECT_EQ(g.browser_view_bounds,
              (DipRect{.x = 8, .y = 8, .width = 1440 - 286 - 8, .height = 900 - 16}));
}

TEST(AppearanceLayoutTest, GivenSidebarOnTheRightAndAgentOpenThenThePanelTakesTheLeftEdge) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.sidebar_side = SidebarSide::kRight;
    const ChromeGeometrySnapshot g = BrowserChrome::LayoutForBounds(kWindow, t, true, true);
    EXPECT_EQ(g.agent_panel_bounds.x, 0);
    EXPECT_EQ(g.agent_panel_bounds.width, BrowserChrome::AgentPanelWidthDip());
    EXPECT_EQ(g.browser_content_bounds.x, BrowserChrome::AgentPanelWidthDip());
    EXPECT_EQ(g.rail_bounds.x + g.rail_bounds.width, 1440);
}

TEST(AppearanceLayoutTest, GivenCollapsedRailOnTheRightThenItHugsTheRightEdge) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.sidebar_side = SidebarSide::kRight;
    const ChromeGeometrySnapshot g = BrowserChrome::LayoutForBounds(kWindow, t, false);
    EXPECT_EQ(g.rail_bounds.width, kCollapsedRailWidthDip);
    EXPECT_EQ(g.rail_bounds.x, 1440 - kCollapsedRailWidthDip);
}

TEST(AppearanceLayoutTest, GivenFullBleedThenThePageFillsItsColumn) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.card_inset_dip = 0;
    const ChromeGeometrySnapshot g = BrowserChrome::LayoutForBounds(kWindow, t);
    EXPECT_EQ(g.browser_view_bounds, g.browser_content_bounds);
}

TEST(AppearanceLayoutTest, GivenAFramelessWindowThenCardsClearTheTrafficLights) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.titlebar_band_dip = BrowserChrome::TrafficLightBandDip();
    // Expanded left sidebar: the page starts right of the lights, so the normal inset holds.
    EXPECT_EQ(BrowserChrome::LayoutForBounds(kWindow, t).browser_view_bounds.y, 8);
    // Collapsed rail (52 DIP) and a right sidebar both leave the lights over the page column.
    EXPECT_EQ(BrowserChrome::LayoutForBounds(kWindow, t, false).browser_view_bounds.y,
              t.titlebar_band_dip);
    t.sidebar_side = SidebarSide::kRight;
    EXPECT_EQ(BrowserChrome::LayoutForBounds(kWindow, t).browser_view_bounds.y,
              t.titlebar_band_dip);
}

TEST(AppearanceLayoutTest, GivenAFramelessWindowThenTheNavigationRowClearsTheTrafficLights) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    t.titlebar_band_dip = BrowserChrome::TrafficLightBandDip();
    EXPECT_GE(t.spacing_3_dip + BrowserChrome::NavigationLeadDip(),
              BrowserChrome::TrafficLightClearDip());
    // Controls fit inside the band, centered on the lights' row.
    EXPECT_EQ(BrowserChrome::RailTopInsetDip(t) * 2 + t.control_height_dip, t.titlebar_band_dip);
}

TEST(AppearanceLayoutTest, GivenAFramelessWindowThenDragRegionsCoverTheBandButNotTheButtons) {
    ChromeTokens t = ChromeTokens::ForTheme(ChromeTheme::kLight);
    EXPECT_TRUE(BrowserChrome::DragRegionsForBounds(kWindow, t, true, false).empty());
    t.titlebar_band_dip = BrowserChrome::TrafficLightBandDip();
    const auto regions = BrowserChrome::DragRegionsForBounds(kWindow, t, true, false);
    ASSERT_GE(regions.size(), 3U);
    EXPECT_TRUE(regions[0].draggable);
    EXPECT_EQ(regions[0].bounds, (DipRect{.x = 0, .y = 0, .width = 286, .height = 40}));
    const auto& carve = regions.back();
    EXPECT_FALSE(carve.draggable);
    // The carve-out sits right of the lights and inside the band.
    EXPECT_GE(carve.bounds.x, BrowserChrome::TrafficLightClearDip());
    EXPECT_LE(carve.bounds.y + carve.bounds.height, 40);
}

// ---- raster ---------------------------------------------------------------------------

TEST(RowRasterTest, GivenARoundedRectThenCornersAreTransparentAndTheCenterIsOpaque) {
    RasterBitmap bitmap = NewRasterBitmap(20, 20, 2);
    FillRoundedRect(bitmap, 0, 0, 20, 20, {8, 8, 8, 8}, {0xFF336699U});
    const auto alpha = [&](int x, int y) {
        return bitmap.bgra[(static_cast<std::size_t>(y) * bitmap.width_px + x) * 4 + 3];
    };
    EXPECT_EQ(alpha(0, 0), 0);
    EXPECT_EQ(alpha(39, 0), 0);
    EXPECT_EQ(alpha(0, 39), 0);
    EXPECT_EQ(alpha(39, 39), 0);
    EXPECT_EQ(alpha(20, 20), 255);
    EXPECT_EQ(alpha(20, 0), 255);  // straight top edge
}

TEST(RowRasterTest, GivenOneRoundedSideThenOnlyThoseCornersAreCut) {
    RasterBitmap bitmap = NewRasterBitmap(16, 16, 1);
    FillRoundedRect(bitmap, 0, 0, 16, 16, {8, 0, 0, 8}, {0xFFFFFFFFU});
    const auto alpha = [&](int x, int y) {
        return bitmap.bgra[(static_cast<std::size_t>(y) * bitmap.width_px + x) * 4 + 3];
    };
    EXPECT_EQ(alpha(0, 0), 0);
    EXPECT_EQ(alpha(15, 0), 255);
    EXPECT_EQ(alpha(15, 15), 255);
    EXPECT_EQ(alpha(0, 15), 0);
}

TEST(RowRasterTest, GivenAGlyphThenItCompositesOverTheFill) {
    RasterBitmap bitmap = NewRasterBitmap(4, 4, 1);
    FillRoundedRect(bitmap, 0, 0, 4, 4, {}, {0xFF000000U});
    const std::uint8_t white_pixel[4] = {255, 255, 255, 255};
    CompositePremultiplied(bitmap, white_pixel, 1, 1, 2, 2);
    const std::size_t at = (2 * static_cast<std::size_t>(bitmap.width_px) + 2) * 4;
    EXPECT_EQ(bitmap.bgra[at], 255);
    // Out-of-range pixels clip instead of crashing.
    CompositePremultiplied(bitmap, white_pixel, 1, 1, 99, -5);
}

}  // namespace
}  // namespace island
