#include <gtest/gtest.h>

#include <string>

#include "search_provider.h"

namespace island {
namespace {

TEST(SearchProviderTest, GivenUnreservedCharactersWhenEncodedThenTheyPassThroughVerbatim) {
    const std::string unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

    EXPECT_EQ(PercentEncodeQuery(unreserved), unreserved);
}

TEST(SearchProviderTest, GivenAnEmptyQueryWhenEncodedThenTheOutputIsEmpty) {
    EXPECT_EQ(PercentEncodeQuery(""), "");
}

TEST(SearchProviderTest, GivenSpacesWhenEncodedThenTheyBecomePercentTwentyNeverPlus) {
    EXPECT_EQ(PercentEncodeQuery("island browser"), "island%20browser");
    EXPECT_EQ(PercentEncodeQuery(" "), "%20");
}

TEST(SearchProviderTest, GivenReservedCharactersWhenEncodedThenTheyUseUppercaseHex) {
    EXPECT_EQ(PercentEncodeQuery("/"), "%2F");
    EXPECT_EQ(PercentEncodeQuery("?"), "%3F");
    EXPECT_EQ(PercentEncodeQuery("&"), "%26");
    EXPECT_EQ(PercentEncodeQuery("="), "%3D");
    EXPECT_EQ(PercentEncodeQuery("#"), "%23");
    EXPECT_EQ(PercentEncodeQuery("+"), "%2B");
    EXPECT_EQ(PercentEncodeQuery("%"), "%25");
    EXPECT_EQ(PercentEncodeQuery("a/b?c&d=e"), "a%2Fb%3Fc%26d%3De");
}

TEST(SearchProviderTest, GivenMultibyteUtf8WhenEncodedThenEachByteIsEncodedIndependently) {
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE -> C3 A9
    EXPECT_EQ(PercentEncodeQuery("\xC3\xA9"), "%C3%A9");
    // U+4E2D CJK ideograph -> E4 B8 AD
    EXPECT_EQ(PercentEncodeQuery("\xE4\xB8\xAD"), "%E4%B8%AD");
    // U+1F600 GRINNING FACE -> F0 9F 98 80
    EXPECT_EQ(PercentEncodeQuery("\xF0\x9F\x98\x80"), "%F0%9F%98%80");
    EXPECT_EQ(PercentEncodeQuery("caf\xC3\xA9 near me"), "caf%C3%A9%20near%20me");
}

TEST(SearchProviderTest, GivenInvalidUtf8WhenEncodedThenEncodingStaysTotalAndDeterministic) {
    const std::string invalid("\xFF\xFE\x00\x80", 4);

    EXPECT_EQ(PercentEncodeQuery(invalid), "%FF%FE%00%80");
    EXPECT_EQ(PercentEncodeQuery(invalid), PercentEncodeQuery(invalid));
}

TEST(SearchProviderTest, GivenTheProviderTableWhenInspectedThenItHasFiveEntriesInSpecOrder) {
    const std::span<const SearchProvider> providers = SearchProviders();

    ASSERT_EQ(providers.size(), 5U);
    EXPECT_EQ(providers[0].id, SearchProviderId::kChatGpt);
    EXPECT_EQ(providers[0].display_name, "ChatGPT");
    EXPECT_EQ(providers[1].id, SearchProviderId::kPerplexity);
    EXPECT_EQ(providers[1].display_name, "Perplexity");
    EXPECT_EQ(providers[2].id, SearchProviderId::kClaude);
    EXPECT_EQ(providers[2].display_name, "Claude");
    EXPECT_EQ(providers[3].id, SearchProviderId::kGemini);
    EXPECT_EQ(providers[3].display_name, "Gemini");
    EXPECT_EQ(providers[4].id, SearchProviderId::kGoogle);
    EXPECT_EQ(providers[4].display_name, "Google");
}

TEST(SearchProviderTest, GivenEveryProviderWhenInspectedThenTheBaseUrlIsHttpsAndCarriesNoQuery) {
    for (const SearchProvider& provider : SearchProviders()) {
        EXPECT_TRUE(provider.base_url.starts_with("https://")) << provider.display_name;
        EXPECT_EQ(provider.base_url.find('?'), std::string_view::npos) << provider.display_name;
    }
}

TEST(SearchProviderTest, GivenAFixedQueryWhenComposedThenEachUrlMatchesTheSpecByteForByte) {
    const std::string query = "island browser caf\xC3\xA9";
    const std::string encoded = "island%20browser%20caf%C3%A9";

    EXPECT_EQ(ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kChatGpt), query),
              "https://chatgpt.com/?q=" + encoded);
    EXPECT_EQ(ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kPerplexity), query),
              "https://www.perplexity.ai/search?q=" + encoded);
    EXPECT_EQ(ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kClaude), query),
              "https://claude.ai/new?q=" + encoded);
    EXPECT_EQ(ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kGemini), query),
              "https://gemini.google.com/app?q=" + encoded);
    EXPECT_EQ(ComposeSearchUrl(*FindSearchProvider(SearchProviderId::kGoogle), query),
              "https://www.google.com/search?q=" + encoded);
}

TEST(SearchProviderTest, GivenAnyProviderIdWhenLookedUpThenTheTableEntryIsReturned) {
    for (const SearchProvider& provider : SearchProviders()) {
        const SearchProvider* found = FindSearchProvider(provider.id);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(*found, provider);
    }
}

TEST(SearchProviderTest, GivenBlankQueriesWhenValidatedThenTheyAreRejected) {
    EXPECT_FALSE(IsSubmittableQuery(""));
    EXPECT_FALSE(IsSubmittableQuery("   "));
    EXPECT_FALSE(IsSubmittableQuery("\t\n\r "));
    EXPECT_TRUE(IsSubmittableQuery("a"));
    EXPECT_TRUE(IsSubmittableQuery("  a  "));
}

}  // namespace
}  // namespace island
