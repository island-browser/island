#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "search_palette_model.h"
#include "search_provider.h"

namespace island {
namespace {

SearchPaletteModel OpenedPalette(std::string query) {
    SearchPaletteModel palette;
    palette.Open();
    palette.SetQuery(std::move(query));
    return palette;
}

TEST(SearchPaletteTest, GivenANewPaletteWhenInspectedThenItIsHiddenAndEmpty) {
    const SearchPaletteModel palette;

    EXPECT_FALSE(palette.visible());
    EXPECT_EQ(palette.query(), "");
    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(SearchPaletteTest, GivenAPaletteWhenOpenedThenItResetsTheQueryAndHighlight) {
    SearchPaletteModel palette = OpenedPalette("stale draft");
    palette.MoveHighlight(2);

    palette.Open();

    EXPECT_TRUE(palette.visible());
    EXPECT_EQ(palette.query(), "");
    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(SearchPaletteTest, GivenAVisiblePaletteWhenClosedThenItHidesWithoutNavigating) {
    SearchPaletteModel palette = OpenedPalette("island");

    EXPECT_TRUE(palette.Close());
    EXPECT_FALSE(palette.visible());
    EXPECT_FALSE(palette.Close());
}

TEST(SearchPaletteTest, GivenTheHighlightWhenMovedThenItWrapsAtBothEnds) {
    SearchPaletteModel palette = OpenedPalette("island");
    const std::size_t last = SearchProviders().size() - 1U;

    palette.MoveHighlight(-1);
    EXPECT_EQ(palette.highlighted_index(), last);
    palette.MoveHighlight(1);
    EXPECT_EQ(palette.highlighted_index(), 0U);
    palette.MoveHighlight(static_cast<int>(SearchProviders().size()) + 1);
    EXPECT_EQ(palette.highlighted_index(), 1U);
}

TEST(SearchPaletteTest, GivenAnOutOfRangeRowWhenHighlightedThenTheHighlightIsUnchanged) {
    SearchPaletteModel palette = OpenedPalette("island");
    palette.SetHighlightedIndex(2);

    palette.SetHighlightedIndex(SearchProviders().size());

    EXPECT_EQ(palette.highlighted_index(), 2U);
    EXPECT_EQ(palette.highlighted_provider().id, SearchProviderId::kClaude);
}

TEST(SearchPaletteTest, GivenAnEmptyQueryWhenSubmittedThenNothingNavigatesAndThePaletteStaysOpen) {
    SearchPaletteModel palette = OpenedPalette("   \t");

    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_TRUE(palette.visible());
}

TEST(SearchPaletteTest, GivenAQueryWhenSubmittedThenItYieldsTheComposedUrlAndHides) {
    SearchPaletteModel palette = OpenedPalette("island browser");
    palette.SetHighlightedIndex(1);

    const std::optional<SearchSubmission> submission = palette.Submit();

    ASSERT_TRUE(submission.has_value());
    EXPECT_EQ(submission->provider, SearchProviderId::kPerplexity);
    EXPECT_EQ(submission->query, "island browser");
    EXPECT_EQ(submission->url, "https://www.perplexity.ai/search?q=island%20browser");
    EXPECT_FALSE(palette.visible());
}

TEST(SearchPaletteTest, GivenEveryProviderRowWhenSubmittedThenTheUrlMatchesTheProviderTable) {
    for (std::size_t index = 0; index < SearchProviders().size(); ++index) {
        SearchPaletteModel palette = OpenedPalette("caf\xC3\xA9 near me");
        palette.SetHighlightedIndex(index);

        const std::optional<SearchSubmission> submission = palette.Submit();

        ASSERT_TRUE(submission.has_value());
        EXPECT_EQ(submission->url,
                  ComposeSearchUrl(SearchProviders()[index], "caf\xC3\xA9 near me"));
    }
}

TEST(SearchPaletteTest, GivenABlankQueryWhenDispatchedThenNothingNavigates) {
    const SearchDispatchDecision decision =
        DecideSearchDispatch("   ", SearchProviderId::kGoogle, /*has_active_browser=*/true);

    EXPECT_EQ(decision.dispatch, SearchDispatch::kRejectedEmptyQuery);
    EXPECT_EQ(decision.url, "");
}

TEST(SearchPaletteTest, GivenNoActiveBrowserWhenDispatchedThenItIsADefinedNoOp) {
    const SearchDispatchDecision decision =
        DecideSearchDispatch("island", SearchProviderId::kGoogle, /*has_active_browser=*/false);

    EXPECT_EQ(decision.dispatch, SearchDispatch::kNoActiveBrowser);
    EXPECT_EQ(decision.url, "");
}

TEST(SearchPaletteTest, GivenAnActiveBrowserWhenDispatchedThenTheComposedUrlIsNavigated) {
    const SearchDispatchDecision decision = DecideSearchDispatch(
        "island browser", SearchProviderId::kClaude, /*has_active_browser=*/true);

    EXPECT_EQ(decision.dispatch, SearchDispatch::kNavigate);
    EXPECT_EQ(decision.url, "https://claude.ai/new?q=island%20browser");
    EXPECT_EQ(decision.url,
              ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kClaude), "island browser"));
}

TEST(SearchPaletteTest, GivenAProviderRowWhenNamedThenItStatesTheActionItPerforms) {
    EXPECT_EQ(SearchProviderRowAccessibleName(SearchProviders()[0], "island"),
              "Search with ChatGPT for island");
    EXPECT_EQ(SearchProviderRowAccessibleName(SearchProviders()[4], ""),
              "Search with Google for ");
}

}  // namespace
}  // namespace island
