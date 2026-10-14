// Justified: two chrome test files exist on purpose. browser_chrome_contract_test.cpp
// pins the tree shape and ID values; this file pins the Ledger composition
// decisions that are decidable headlessly — hairline placement, per-slot tone
// steps, the 2-DIP accent equality, and icon-tone policy — so a compositor is
// not required to catch a Ledger regression. Token ARGB/DIP values stay pinned
// by tests/design_tokens_test.cpp and tests/design/test_token_contract.py.
#include <gtest/gtest.h>

#include <cstdint>
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

// The floating-canvas gutter that insets the browser card on every side is the calm
// spacing_3 (12) step: wide enough to read as a raised card over the tinted
// background, tight enough to keep the canvas dominant at the 800x560 minimum.
TEST(BrowserChromeLedgerTest, BrowserContentPaddingUsesTheCalmThreeStep) {
    const ChromeTokens tokens = Light();
    EXPECT_EQ(BrowserChrome::BrowserContentPaddingDip(), tokens.spacing_3_dip);
    EXPECT_EQ(BrowserChrome::BrowserContentPaddingDip(), 12);
}

// The rail is retinted from surface to surface_secondary so the calm sidebar sits one
// step off the canvas and the floating browser card (browser_content -> background)
// separates from it; the active-page pill lifts to surface against that tint. This
// replaces the old flush surface rail. The ID/tree contract is untouched.
TEST(BrowserChromeLedgerTest, RootAndBrowserContentUsePaperBackgroundInLight) {
    const ChromeTokens tokens = Light();
    const ChromeTheme theme = ChromeTheme::kLight;
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.surface_secondary);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kNavControl, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kActivePage, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAccent, theme),
              tokens.accent);
}

TEST(BrowserChromeLedgerTest, RootAndBrowserContentUseCanvasBackgroundInDark) {
    const ChromeTokens tokens = Dark();
    const ChromeTheme theme = ChromeTheme::kDark;
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.surface_secondary);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kNavControl, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kActivePage, theme),
              tokens.surface);
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
              tokens.surface_secondary);
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
        for (const std::uint32_t space : {0xFF0090FFU, 0xFFE5484DU, 0xFF30A46CU, 0xFFFFB224U}) {
            sets.push_back(base.TintedForSpace({.argb = space}, theme));
        }
    }
    return sets;
}

TEST(BrowserChromeGraphiteTest, SelectedRowIsSurfaceFilledAndHairlineOutlined) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        const BrowserChrome::EntryColors active =
            BrowserChrome::TabRowColors(tokens, /*active=*/true, /*pinned=*/false);
        EXPECT_EQ(active.fill, tokens.surface);
        EXPECT_EQ(active.outline, tokens.border);
        EXPECT_EQ(active.title, tokens.text);
        EXPECT_EQ(BrowserChrome::SpaceChipColors(tokens, true).fill, tokens.surface);
    }
}

TEST(BrowserChromeGraphiteTest, InactiveRowsAreTheSidebarItselfWithNoEdge) {
    for (const ChromeTokens& tokens : AllTokenSets()) {
        const BrowserChrome::EntryColors idle =
            BrowserChrome::TabRowColors(tokens, /*active=*/false, /*pinned=*/false);
        EXPECT_EQ(idle.fill, tokens.surface_secondary);
        EXPECT_EQ(idle.outline, tokens.surface_secondary);
        EXPECT_EQ(idle.title, tokens.text_secondary);
    }
}

TEST(BrowserChromeGraphiteTest, PinnedRowsSitBetweenTheSidebarAndTheSurface) {
    const ChromeTokens tokens = Light();
    const ArgbColor pinned =
        BrowserChrome::TabRowColors(tokens, /*active=*/false, /*pinned=*/true).fill;
    EXPECT_NE(pinned, tokens.surface_secondary);
    EXPECT_NE(pinned, tokens.surface);
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

}  // namespace
}  // namespace island
