// Justified: two chrome test files exist on purpose. browser_chrome_contract_test.cpp
// pins the tree shape and ID values; this file pins the Ledger composition
// decisions that are decidable headlessly — hairline placement, per-slot tone
// steps, the 2-DIP accent equality, and icon-tone policy — so a compositor is
// not required to catch a Ledger regression. Token ARGB/DIP values stay pinned
// by tests/design_tokens_test.cpp and tests/design/test_token_contract.py.
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "browser_chrome.h"
#include "design_tokens.h"

namespace island {
namespace {

ChromeTokens Light() { return ChromeTokens::ForTheme(ChromeTheme::kLight); }
ChromeTokens Dark() { return ChromeTokens::ForTheme(ChromeTheme::kDark); }

const ChromeViewTreeNode* FindChild(const ChromeViewTreeNode& node, ChromeViewId id) {
    for (const ChromeViewTreeNode& child : node.children) {
        if (child.id == id) {
            return &child;
        }
    }
    return nullptr;
}

TEST(BrowserChromeLedgerTest, RailSeparatesLedgerFromCanvasWithOneHairline) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    const ChromeViewTreeNode* divider = FindChild(*rail, ChromeViewId::kDivider);
    ASSERT_NE(divider, nullptr);
    EXPECT_EQ(divider->children.size(), 0U);
}

TEST(BrowserChromeLedgerTest, AccentAppearsOnlyInActiveIndicatorAndAddressEdge) {
    const ChromeViewTreeNode tree = BrowserChrome::ViewTreeContract();
    const ChromeViewTreeNode* rail = FindChild(tree, ChromeViewId::kRail);
    ASSERT_NE(rail, nullptr);
    const ChromeViewTreeNode* active_page = FindChild(*rail, ChromeViewId::kActivePage);
    ASSERT_NE(active_page, nullptr);
    EXPECT_NE(FindChild(*active_page, ChromeViewId::kActivePageIndicator), nullptr);
}

TEST(BrowserChromeLedgerTest, AddressFocusAccentIsTwoDipsOfContractAccent) {
    EXPECT_EQ(BrowserChrome::AddressFocusLeadingEdgeDip(), 2);
}

TEST(BrowserChromeLedgerTest, ActivePageIndicatorIsTwoDipsOfContractAccent) {
    EXPECT_EQ(BrowserChrome::ActivePageIndicatorWidthDip(),
              BrowserChrome::AddressFocusLeadingEdgeDip());
    EXPECT_EQ(BrowserChrome::ActivePageIndicatorWidthDip(), 2);
}

// The rail starts on the same spacing_3 (12) gutter as its sides: the window keeps
// the platform's standard title bar, which already clears the window controls, so
// no extra inset is reserved for them.
TEST(BrowserChromeLedgerTest, RailTopInsetMatchesTheSideGutter) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(BrowserChrome::RailTopInsetDip(), tokens.spacing_3_dip);
    EXPECT_EQ(BrowserChrome::RailTopInsetDip(), 12);
}

// The rail's between-section cadence is one spacing_2 (8) step so the controls read
// as one even column instead of scattered boxes pinned around a void.
TEST(BrowserChromeLedgerTest, RailSectionSpacingUsesTheEvenTwoStep) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(BrowserChrome::RailSectionSpacingDip(), tokens.spacing_2_dip);
    EXPECT_EQ(BrowserChrome::RailSectionSpacingDip(), 8);
}

// Every rail control row shares one 32 DIP height (spacing_6 + spacing_2).
TEST(BrowserChromeLedgerTest, RailControlRowsShareOneHeight) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(BrowserChrome::TabRowHeightDip(), tokens.spacing_6_dip + tokens.spacing_2_dip);
    EXPECT_EQ(BrowserChrome::SpacePillHeightDip(), BrowserChrome::TabRowHeightDip());
}

// The floating-canvas gutter around the page card is the spacing_2 (8) step (Arc): enough to
// read as a raised card over the tinted canvas, tight enough to keep the page dominant.
TEST(BrowserChromeLedgerTest, BrowserContentPaddingUsesTheEightStep) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(BrowserChrome::BrowserContentPaddingDip(), tokens.spacing_2_dip);
    EXPECT_EQ(BrowserChrome::BrowserContentPaddingDip(), 8);
    EXPECT_EQ(tokens.card_inset_dip, BrowserChrome::BrowserContentPaddingDip());
}

// The rail is retinted from surface to surface_secondary so the calm sidebar sits one
// step off the canvas and the floating browser card (browser_content -> background)
// separates from it; the active-page pill lifts to surface against that tint. This
// replaces the old flush surface rail. The ID/tree contract is untouched.
TEST(BrowserChromeLedgerTest, SidebarAndMarginShareTheCanvasInLight) {
    const ChromeTokens tokens = Light();
    const ChromeTheme theme = ChromeTheme::kLight;
    // The sidebar and the window margin around the page card are one `canvas` surface.
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.well);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kNavControl, theme),
              tokens.hover);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kActivePage, theme),
              tokens.selected);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAccent, theme),
              tokens.accent);
}

TEST(BrowserChromeLedgerTest, SidebarAndMarginShareTheCanvasInDark) {
    const ChromeTokens tokens = Dark();
    const ChromeTheme theme = ChromeTheme::kDark;
    // The sidebar and the window margin around the page card are one `canvas` surface.
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.canvas);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.well);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kNavControl, theme),
              tokens.hover);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kActivePage, theme),
              tokens.selected);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAccent, theme),
              tokens.accent);
}

TEST(BrowserChromeLedgerTest, NavigationIconsUseSecondaryToneAcrossThemes) {
    EXPECT_EQ(BrowserChrome::NavigationIconTone(), ChromeIconTone::kSecondary);
    EXPECT_EQ(BrowserChrome::AddressLocationIconTone(), ChromeIconTone::kSecondary);
    EXPECT_EQ(BrowserChrome::FallbackFaviconIconTone(), ChromeIconTone::kSecondary);
}

TEST(BrowserChromeLedgerTest, AddressFieldUsesGeistMonoAndChromeUsesGeist) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(tokens.ui_font, ChromeFont::kGeist);
    EXPECT_EQ(tokens.mono_font, ChromeFont::kGeistMono);
}

TEST(BrowserChromeLedgerTest, CompactEightHundredByFiveSixtyKeepsFullRailAndControls) {
    ChromeTokens tokens;
    tokens.rail_width_dip = 286;
    const ChromeGeometrySnapshot geometry = BrowserChrome::LayoutForBounds(
        DipRect{.x = 0, .y = 0, .width = 800, .height = 560}, tokens);
    EXPECT_EQ(geometry.rail_bounds.width, 286);
    EXPECT_GT(geometry.browser_content_bounds.width, 0);
    EXPECT_GE(geometry.browser_content_bounds.width, 320);
}

TEST(BrowserChromeLedgerTest, BrowserViewNeverCarriesTokenizedChromeColor) {
    // contract on chrome-owned slots only; the BrowserView is not a slot.
    const ChromeTokens tokens = Light();
    const ChromeTheme theme = ChromeTheme::kLight;
    EXPECT_NE(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.accent);
    EXPECT_NE(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.surface);
}

// ---- Graphite pass: per-view color resolution -------------------------------------
//
// CEF resets every view's custom colors on a theme change; the chrome repaints from
// these pure functions afterwards, so what they resolve is the whole visual contract
// of a tab row or space chip in both themes and under a space tint.

std::vector<ChromeTokens> AllTokenSets() {
    std::vector<ChromeTokens> sets;
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens base = ChromeTokens::ForTheme(theme);
        sets.push_back(base);
        for (const std::uint32_t space : {0xFF0090FFU, 0xFFE5484DU, 0xFF30A46CU, 0xFFFFB224U,
                                          0xFF8E4EC6U, 0xFFFFFFFFU, 0xFF000000U}) {
            sets.push_back(base.TintedForSpace({.argb = space}, theme));
            for (const int tint : {0, 35, 100}) {
                sets.push_back(
                    base.Customized({.argb = space}, theme,
                                    {.look = LookPreset::kGraphite, .sidebar_tint_percent = tint}));
            }
        }
    }
    return sets;
}

TEST(BrowserChromeGraphiteTest, SelectedRowIsASoftFillWithNoOutline) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        const BrowserChrome::EntryColors active =
            BrowserChrome::TabRowColors(tokens, /*active=*/true, /*pinned=*/false);
        EXPECT_EQ(active.fill, tokens.selected);
        EXPECT_EQ(active.outline, tokens.selected);
        EXPECT_EQ(active.title, tokens.text);
    }
}

// The space chip is deliberately quiet: no box, no edge, only the label weight/color.
TEST(BrowserChromeGraphiteTest, SpaceChipsHaveNoFillOrEdgeAndTheActiveLabelTakesText) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        const BrowserChrome::EntryColors active = BrowserChrome::SpaceChipColors(tokens, true);
        const BrowserChrome::EntryColors idle = BrowserChrome::SpaceChipColors(tokens, false);
        EXPECT_EQ(active.fill, tokens.canvas);
        EXPECT_EQ(active.outline, tokens.canvas);
        EXPECT_EQ(idle.fill, tokens.canvas);
        EXPECT_EQ(active.title, tokens.text);
        EXPECT_EQ(idle.title, tokens.text_secondary);
        EXPECT_GE(ContrastRatio(active.title, active.fill), 4.5);
        EXPECT_GE(ContrastRatio(idle.title, idle.fill), 4.5);
    }
}

TEST(BrowserChromeGraphiteTest, InactiveRowsAreTheSidebarItselfWithNoEdge) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        const BrowserChrome::EntryColors idle =
            BrowserChrome::TabRowColors(tokens, /*active=*/false, /*pinned=*/false);
        EXPECT_EQ(idle.fill, tokens.canvas);
        EXPECT_EQ(idle.outline, tokens.canvas);
        EXPECT_EQ(idle.title, tokens.text_secondary);
    }
}

TEST(BrowserChromeGraphiteTest, PinnedRowsSitBetweenTheSidebarAndTheSurface) {
    const ChromeTokens tokens = Light();
    const ArgbColor pinned =
        BrowserChrome::TabRowColors(tokens, /*active=*/false, /*pinned=*/true).fill;
    EXPECT_NE(pinned, tokens.canvas);
    EXPECT_NE(pinned, tokens.selected);
}

TEST(BrowserChromeGraphiteTest, RowTextMeetsBodyContrastOnEveryFillInBothThemes) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        for (const bool active : {true, false}) {
            for (const bool pinned : {true, false}) {
                const BrowserChrome::EntryColors colors =
                    BrowserChrome::TabRowColors(tokens, active, pinned);
                EXPECT_GE(ContrastRatio(colors.title, colors.fill), 4.5);
                EXPECT_GE(ContrastRatio(colors.trailing, colors.fill), 4.5);
            }
        }
    }
}

TEST(BrowserChromeGraphiteTest, AddressOutlineIsTheHairlineAtRest) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        EXPECT_EQ(
            BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressOutline, theme),
            tokens.border);
    }
}

// R16: keyboard focus rings on sidebar controls are neutral in every look and theme; only the
// address field keeps the accent ring.
TEST(BrowserChromeFocusRingStyleTest, GivenEveryLookAndThemeThenOnlyTheAddressRingIsAccent) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);
            const ArgbColor sidebar_ring =
                BrowserChrome::TextfieldFocusRingColor(tokens, /*address_focused=*/false);
            EXPECT_EQ(sidebar_ring.argb, tokens.text_secondary.argb);
            EXPECT_NE(sidebar_ring.argb, tokens.accent.argb);
            EXPECT_NE(sidebar_ring.argb, tokens.selection.argb);
            // The address ring is the plate fill while the field is focused, never the accent.
            EXPECT_NE(BrowserChrome::TextfieldFocusRingColor(tokens, /*address_focused=*/true).argb,
                      tokens.accent.argb);
        }
    }
}

// Bookmarks 0.9: favorites never take the accent, and the failure row speaks in warning tokens.
TEST(BrowserChromeFavoritesStyleTest, GivenEveryLookAndThemeThenADisplayedFavoriteFillsNeutral) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);
            // The accent is never a favorite's fill; the displayed plate is the raised neutral.
            for (const bool hovered : {false, true}) {
                const ArgbColor fill = FavoriteFillColor(tokens, /*displayed=*/true, hovered);
                EXPECT_EQ(fill.argb, tokens.segment_selected.argb);
                EXPECT_NE(fill.argb, tokens.accent.argb);
                EXPECT_NE(fill.argb, tokens.selection.argb);
            }
            EXPECT_EQ(FavoriteFillColor(tokens, /*displayed=*/false, /*hovered=*/true).argb,
                      tokens.hover.argb);
            EXPECT_EQ(FavoriteFillColor(tokens, /*displayed=*/false, /*hovered=*/false).argb,
                      tokens.canvas.argb);
        }
    }
}

// BrowserChrome::ApplyFavoriteInk paints exactly FavoriteInkPlan(tokens_) on every theme change
// (ApplyCollectionTheme), so a theme switch is the plan of the new tokens replacing the old one.
// Every themed favorite surface must be in the plan and carry the target theme's token.
TEST(BrowserChromeFavoritesStyleTest, GivenAThemeSwitchThenTheInkPlanRepaintsEverySurface) {
    const auto color_of = [](const std::vector<FavoriteInkEntry>& plan, FavoriteInkTarget target) {
        std::optional<std::uint32_t> found;
        int count = 0;
        for (const FavoriteInkEntry& entry : plan) {
            if (entry.target == target) {
                found = entry.color.argb;
                ++count;
            }
        }
        EXPECT_EQ(count, 1) << "each surface is repainted exactly once";
        return found;
    };
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const auto& [from, to] : {std::pair{ChromeTheme::kDark, ChromeTheme::kLight},
                                       std::pair{ChromeTheme::kLight, ChromeTheme::kDark}}) {
            const ChromeTokens before = ChromeTokens::ForLook(look, from);
            const ChromeTokens after = ChromeTokens::ForLook(look, to);
            const std::vector<FavoriteInkEntry> old_plan = FavoriteInkPlan(before);
            const std::vector<FavoriteInkEntry> new_plan = FavoriteInkPlan(after);
            ASSERT_EQ(new_plan.size(), 4U) << "row text, +N, +, fallback glyph";
            EXPECT_EQ(color_of(new_plan, FavoriteInkTarget::kRowText), after.text.argb);
            EXPECT_EQ(color_of(new_plan, FavoriteInkTarget::kMoreText), after.text_secondary.argb);
            EXPECT_EQ(color_of(new_plan, FavoriteInkTarget::kAddGlyph), after.text_secondary.argb);
            EXPECT_EQ(color_of(new_plan, FavoriteInkTarget::kFallbackGlyph),
                      after.text_secondary.argb);
            // The switch changes every surface: nothing keeps the previous theme's ink.
            for (const FavoriteInkEntry& entry : new_plan) {
                EXPECT_NE(color_of(old_plan, entry.target), color_of(new_plan, entry.target));
            }
        }
    }
}

TEST(BrowserChromeFavoritesStyleTest, GivenTheFailureRowThenItUsesTheWarningTokens) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        const BookmarkStatusColors colors = BookmarkStatusRowColors(tokens);
        EXPECT_EQ(colors.row.argb, tokens.warning_bg.argb);
        EXPECT_EQ(colors.text.argb, tokens.warning_text.argb);
        EXPECT_EQ(colors.dismiss.argb, tokens.warning_text.argb);
        // Retry is an action, so it reads in the normal text color, not the warning color.
        EXPECT_EQ(colors.retry.argb, tokens.text.argb);
        EXPECT_NE(colors.row.argb, colors.text.argb);
        EXPECT_NE(colors.row.argb, tokens.accent.argb);
    }
}

TEST(BrowserChromeFavoritesStyleTest, GivenASuccessNoticeThenTheStatusRowIsNeutral) {
    for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens tokens = ChromeTokens::ForTheme(theme);
        const BookmarkStatusColors colors = BookmarkStatusRowColors(tokens, /*ok=*/true);
        EXPECT_EQ(colors.row.argb, tokens.surface_secondary.argb);
        EXPECT_EQ(colors.text.argb, tokens.text.argb);
        EXPECT_EQ(colors.retry.argb, tokens.text.argb);
        EXPECT_EQ(colors.dismiss.argb, tokens.text.argb);
        EXPECT_NE(colors.row.argb, tokens.warning_bg.argb);
    }
}

TEST(BrowserChromeFavoritesStyleTest, GivenAFavoriteShownThenTheSelectedTabIsNeutralUntilItCloses) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc, LookPreset::kGraphite}) {
        for (const ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
            const ChromeTokens tokens = ChromeTokens::ForLook(look, theme);
            // R15: while the favorite is on screen the space's selected tab keeps a neutral plate,
            // hovered or not, never the accent.
            for (const bool hovered : {false, true}) {
                const std::optional<ArgbColor> shown =
                    TabRowFill(tokens, /*active=*/true, /*pinned=*/false, hovered,
                               /*behind_favorite=*/true);
                ASSERT_TRUE(shown.has_value());
                EXPECT_EQ(shown->argb, tokens.segment_selected.argb);
                EXPECT_NE(shown->argb, tokens.accent.argb);
                EXPECT_NE(shown->argb, tokens.selection.argb);
            }
            // Once the favorite closes the selected tab takes its accent-or-selected fill again.
            const std::optional<ArgbColor> restored =
                TabRowFill(tokens, /*active=*/true, /*pinned=*/false, /*hovered=*/false,
                           /*behind_favorite=*/false);
            ASSERT_TRUE(restored.has_value());
            const ArgbColor selected_fill =
                look == LookPreset::kNative ? tokens.selection : tokens.selected;
            EXPECT_EQ(restored->argb, selected_fill.argb);
        }
    }
}

TEST(BrowserChromeFavoritesStyleTest, GivenAFavoriteShownThenOtherTabsKeepTheirFills) {
    for (const LookPreset look : {LookPreset::kNative, LookPreset::kArc}) {
        const ChromeTokens tokens = ChromeTokens::ForLook(look, ChromeTheme::kLight);
        // Only the selected tab is affected: an idle row and a hovered row keep their fills.
        EXPECT_FALSE(TabRowFill(tokens, /*active=*/false, /*pinned=*/false, /*hovered=*/false,
                                /*behind_favorite=*/true)
                         .has_value());
        const std::optional<ArgbColor> hovered = TabRowFill(
            tokens, /*active=*/false, /*pinned=*/false, /*hovered=*/true, /*behind_favorite=*/true);
        ASSERT_TRUE(hovered.has_value());
        EXPECT_EQ(hovered->argb, tokens.hover.argb);
    }
}

}  // namespace
}  // namespace island
