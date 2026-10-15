#include "search_engine.h"

#include <gtest/gtest.h>

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
