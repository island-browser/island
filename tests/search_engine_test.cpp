#include "search_engine.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

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

TEST(SearchEngineTest, GivenAQueryThatReadsAsAnAddressWhenReadThenTheUrlIsShown) {
    // A link to a results page must not make the address field show another origin.
    EXPECT_EQ(SearchQueryFromResultsUrl(
                  "https://www.google.com/search?q=https%3A%2F%2Fbank.example%2Flogin"),
              std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=bank.example%2Flogin"),
              std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=javascript%3Aalert(1)"),
              std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://duckduckgo.com/?q=localhost%3A3000"),
              std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=google.com"),
              std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island"), "island");
}

TEST(SearchEngineTest, GivenAControlCharacterInTheQueryWhenReadThenTheUrlIsShown) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%0Abrowser"),
              std::nullopt);  // LF
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%00browser"),
              std::nullopt);  // NUL
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%09browser"),
              std::nullopt);  // TAB
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%7Fbrowser"),
              std::nullopt);  // DEL
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%C2%85browser"),
              std::nullopt);  // U+0085, C1
}

TEST(SearchEngineTest, GivenABidiOrFormatControlInTheQueryWhenReadThenTheUrlIsShown) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%80%8Ebrowser"),
              std::nullopt);  // U+200E LRM
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%80%8Fbrowser"),
              std::nullopt);  // U+200F RLM
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%80%AAbrowser"),
              std::nullopt);  // U+202A LRE
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%80%AEbrowser"),
              std::nullopt);  // U+202E RLO
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%81%A6browser"),
              std::nullopt);  // U+2066 LRI
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%E2%81%A9browser"),
              std::nullopt);  // U+2069 PDI
}

TEST(SearchEngineTest, GivenInvalidUtf8InTheQueryWhenReadThenTheUrlIsShown) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=island%FFbrowser"),
              std::nullopt);  // lone continuation-invalid lead byte
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=caf%C3"),
              std::nullopt);  // truncated sequence
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=a%C0%AFb"),
              std::nullopt);  // overlong
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=a%ED%A0%80b"),
              std::nullopt);  // UTF-16 surrogate
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=a%F4%90%80%80b"),
              std::nullopt);  // above U+10FFFF
    EXPECT_EQ(SearchQueryFromResultsUrl("https://www.google.com/search?q=caf%C3%A9%20%F0%9F%8C%8A"),
              "caf\xC3\xA9 \xF0\x9F\x8C\x8A");  // valid multi-byte text is still shown
}

TEST(SearchEngineTest, GivenYandexRegionalResultsUrlsWhenReadThenTheirQueryIsShown) {
    EXPECT_EQ(SearchQueryFromResultsUrl("https://yandex.ru/search/?text=island+browser"),
              "island browser");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://ya.ru/search/?text=island+browser&lr=213"),
              "island browser");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://YANDEX.RU/search/?text=island"), "island");
    EXPECT_EQ(SearchQueryFromResultsUrl("https://evil-yandex.ru/search/?text=x"), std::nullopt);
    EXPECT_EQ(SearchQueryFromResultsUrl("https://ya.ru/search?text=x"), std::nullopt);
}

TEST(SearchEngineTest, GivenEveryEngineRowWhenCheckedThenItsShapeIsOneTheReaderParses) {
    for (const SearchEngine& engine : SearchEngines()) {
        const std::string_view prefix = engine.url_prefix;
        ASSERT_EQ(prefix.substr(0, 8), "https://") << engine.key;
        const std::string_view rest = prefix.substr(8);
        const std::size_t slash = rest.find('/');
        const std::size_t question = rest.find('?');
        ASSERT_NE(slash, std::string_view::npos) << engine.key;
        ASSERT_NE(question, std::string_view::npos) << engine.key;
        EXPECT_LT(slash, question) << engine.key;
        // The parameter name sits between '?' and the trailing '='.
        EXPECT_GT(prefix.size(), question + 2) << engine.key;
        EXPECT_EQ(prefix.back(), '=') << engine.key;
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
