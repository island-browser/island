#include "search_engine.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace island {
namespace {

TEST(SearchEngineTest, GivenTheTableWhenListedThenGoogleIsTheDefaultAndKeysAreUnique) {
    const auto engines = SearchEngines();
    ASSERT_EQ(engines.size(), 3U);
    EXPECT_EQ(engines[0].key, "google");
    EXPECT_EQ(engines[1].key, "duckduckgo");
    EXPECT_EQ(engines[2].key, "yandex");
    for (const SearchEngine& engine : engines) {
        EXPECT_EQ(engine.url_prefix.rfind("https://", 0), 0U) << engine.key;
        EXPECT_EQ(engine.url_prefix.back(), '=') << engine.key;
        EXPECT_FALSE(engine.display_name.empty());
        EXPECT_TRUE(IsSearchEngineKey(engine.key));
        EXPECT_EQ(SearchEngineForKey(engine.key), engine);
    }
}

TEST(SearchEngineTest, GivenAnUnknownKeyWhenResolvedThenItFallsBackToGoogle) {
    EXPECT_FALSE(IsSearchEngineKey("bing"));
    EXPECT_FALSE(IsSearchEngineKey(""));
    EXPECT_FALSE(IsSearchEngineKey("Google"));
    EXPECT_EQ(SearchEngineForKey("bing").id, SearchEngineId::kGoogle);
    EXPECT_EQ(SearchEngineForKey("").id, SearchEngineId::kGoogle);
}

TEST(SearchEngineTest, GivenAQueryWhenBuiltThenGoogleGetsAPercentEncodedResultsUrl) {
    const SearchEngine& google = SearchEngineForKey("google");
    EXPECT_EQ(BuildSearchUrl(google, "hello world"),
              "https://www.google.com/search?q=hello%20world");
    EXPECT_EQ(BuildSearchUrl(google, "  padded  "), "https://www.google.com/search?q=padded");
    EXPECT_EQ(BuildSearchUrl(google, "a&b=c#d?e/f+g"),
              "https://www.google.com/search?q=a%26b%3Dc%23d%3Fe%2Ff%2Bg");
    EXPECT_EQ(BuildSearchUrl(google, "caf\xC3\xA9"), "https://www.google.com/search?q=caf%C3%A9");
    EXPECT_EQ(BuildSearchUrl(google, "\"quoted\" 100%"),
              "https://www.google.com/search?q=%22quoted%22%20100%25");
}

TEST(SearchEngineTest, GivenEachEngineWhenBuiltThenItUsesItsOwnQueryParameter) {
    EXPECT_EQ(BuildSearchUrl(SearchEngineForKey("duckduckgo"), "island browser"),
              "https://duckduckgo.com/?q=island%20browser");
    EXPECT_EQ(BuildSearchUrl(SearchEngineForKey("yandex"), "island browser"),
              "https://yandex.com/search/?text=island%20browser");
}

TEST(SearchEngineTest, GivenAGoogleResultsUrlWhenReadThenItsQueryIsDecoded) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island+browser"),
              "island browser");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=caf%C3%A9%20au%20lait"),
              "caf\xC3\xA9 au lait");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=a%26b%3Dc%2Bd"),
              "a&b=c+d");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=100%25"), "100%");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=a%2"), "a%2");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=50%zz"), "50%zz");
}

TEST(SearchEngineTest, GivenOtherParametersWhenReadThenTheyAreIgnored) {
    EXPECT_EQ(
        SearchQueryFromResultsUrl("https://www.google.com/search?newwindow=1&q=island&sca_esv=1"),
        "island");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?hl=en&q=island#frag"),
              "island");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=first&q=second"), "first");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?qq=x&aq=y&q=z"), "z");
}

TEST(SearchEngineTest, GivenBlankOrMissingQueryWhenReadThenNothingIsReturned) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search"), std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?"), std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q="), std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=+%20+"), std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?hl=en&q"), std::nullopt);
}

TEST(SearchEngineTest, GivenNonResultsUrlsWhenReadThenNothingIsReturned) {
    for (const char* url : {
             "",
             "island://local-page",
             "data:text/html,<p>q=x</p>",
             "http://www.google.com/search?q=x",
             "https://www.google.com.evil.example/search?q=x",
             "https://evilwww.google.com/search?q=x",
             "https://www.google.com/searchx?q=x",
             "https://www.google.com/maps?q=x",
             "https://www.google.com:8443/search?q=x",
             "https://user@www.google.com/search?q=x",
             "https://example.com/search?q=x",
             "https://yandex.com/search/?q=x",
             "https://yandex.com/search?text=x",
             "https://duckduckgo.com/html/?q=x",
         }) {
        EXPECT_EQ(SearchQueryFromResultsUrl(url), std::nullopt) << url;
    }
}

TEST(SearchEngineTest, GivenDuckDuckGoAndYandexResultsUrlsWhenReadThenTheirParameterIsUsed) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://duckduckgo.com/?q=island+browser"),
              "island browser");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://duckduckgo.com/?t=h_&q=island"), "island");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://yandex.com/search/?text=island+browser&lr=213"),
              "island browser");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://yandex.com/search/?lr=213&text=island%20browser"),
              "island browser");
}

TEST(SearchEngineTest, GivenAHostInAnyCaseWhenReadThenItStillMatches) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://WWW.GOOGLE.COM/search?q=island"), "island");
}

TEST(SearchEngineTest, GivenAQueryWhenBuiltThenReadingTheUrlBackGivesTheQuery) {
    for (const SearchEngine& engine : SearchEngines()) {
        EXPECT_EQ(SearchQueryFromResultsUrl(BuildSearchUrl(engine, "a&b=c+d #e/\xC3\xA9 100%")),
                  "a&b=c+d #e/\xC3\xA9 100%")
            << engine.key;
    }
}

TEST(SearchEngineTest, GivenStartModesWhenNormalizedThenOnlyAgentAndResearchStick) {
    EXPECT_EQ(NormalizeStartMode("agent"), kStartModeAgent);
    EXPECT_EQ(NormalizeStartMode("research"), kStartModeResearch);
    EXPECT_EQ(NormalizeStartMode("Research"), kStartModeSearch);
    EXPECT_EQ(NormalizeStartMode("search"), kStartModeSearch);
    EXPECT_EQ(NormalizeStartMode(""), kStartModeSearch);
    EXPECT_EQ(NormalizeStartMode("Agent"), kStartModeSearch);
    EXPECT_EQ(NormalizeStartMode("bogus"), kStartModeSearch);
}

}  // namespace
}  // namespace island
