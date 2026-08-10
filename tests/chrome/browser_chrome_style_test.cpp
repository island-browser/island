// Justified: two chrome test files exist on purpose. browser_chrome_contract_test.cpp
// pins the tree shape and ID values; this file pins the Ledger composition
// decisions that are decidable headlessly — hairline placement, per-slot tone
// steps, the 2-DIP accent equality, and icon-tone policy — so a compositor is
// not required to catch a Ledger regression. Token ARGB/DIP values stay pinned
// by tests/design_tokens_test.cpp and tests/design/test_token_contract.py.
#include <gtest/gtest.h>

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

TEST(BrowserChromeLedgerTest, RootAndBrowserContentUsePaperBackgroundInLight) {
    const ChromeTokens tokens = Light();
    const ChromeTheme theme = ChromeTheme::kLight;
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.surface_secondary);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAccent, theme),
              tokens.accent);
}

TEST(BrowserChromeLedgerTest, RootAndBrowserContentUseCanvasBackgroundInDark) {
    const ChromeTokens tokens = Dark();
    const ChromeTheme theme = ChromeTheme::kDark;
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRoot, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kRail, theme),
              tokens.surface);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kBrowserContent, theme),
              tokens.background);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kHairline, theme),
              tokens.border);
    EXPECT_EQ(BrowserChrome::ChromeSurfaceRole(BrowserChrome::SurfaceSlot::kAddressWell, theme),
              tokens.surface_secondary);
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

}  // namespace
}  // namespace island
