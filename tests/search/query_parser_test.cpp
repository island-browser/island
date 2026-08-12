#include "search/query_parser.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace island {
namespace {

using island::search::DefaultNormalize;
using island::search::kMaxPhraseLength;
using island::search::kMaxPhraseWords;
using island::search::kMaxQueryUnits;
using island::search::kMaxTermLength;
using island::search::ParseQuery;
using island::search::QueryParseError;
using island::search::QueryParseResult;

void ExpectNoUnits(const QueryParseResult& r) {
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kNone);
    EXPECT_TRUE(r.query.terms.empty());
    EXPECT_TRUE(r.query.phrases.empty());
}

TEST(QueryParser, ParsesWhitespaceSeparatedTerms) {
    const auto r = ParseQuery("island browser");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 2u);
    EXPECT_EQ(r.query.terms[0].text, "island");
    EXPECT_FALSE(r.query.terms[0].is_prefix);
    EXPECT_EQ(r.query.terms[1].text, "browser");
    EXPECT_FALSE(r.query.terms[1].is_prefix);
    EXPECT_TRUE(r.query.phrases.empty());
}

TEST(QueryParser, CollapsesRunsOfWhitespaceAndIgnoresEdgeWhitespace) {
    const auto r = ParseQuery("  island   browser\n\t");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 2u);
    EXPECT_EQ(r.query.terms[0].text, "island");
    EXPECT_EQ(r.query.terms[1].text, "browser");
}

TEST(QueryParser, ParsesQuotedPhrase) {
    const auto r = ParseQuery("\"island browser\"");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.phrases.size(), 1u);
    const auto& phrase = r.query.phrases[0];
    EXPECT_EQ(phrase.words, std::vector<std::string>({"island", "browser"}));
    EXPECT_FALSE(phrase.is_prefix);
    EXPECT_TRUE(r.query.terms.empty());
}

TEST(QueryParser, ParsesMixedTermsAndPhrasesPreservingOrder) {
    const auto r = ParseQuery("island \"coral reef\" browser");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 2u);
    ASSERT_EQ(r.query.phrases.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text, "island");
    EXPECT_EQ(r.query.phrases[0].words, std::vector<std::string>({"coral", "reef"}));
    EXPECT_EQ(r.query.terms[1].text, "browser");
}

TEST(QueryParser, MarksTrailingStarAsPrefix) {
    const auto r = ParseQuery("isla*");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text, "isla");
    EXPECT_TRUE(r.query.terms[0].is_prefix);
}

TEST(QueryParser, MarksPhrasePrefixWithTrailingStar) {
    const auto r = ParseQuery("\"coral reef\"*");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.phrases.size(), 1u);
    EXPECT_TRUE(r.query.phrases[0].is_prefix);
}

TEST(QueryParser, NormalizesTermsViaDefaultNormalizer) {
    const auto r = ParseQuery("Island Browser");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 2u);
    EXPECT_EQ(r.query.terms[0].text, "island");
    EXPECT_EQ(r.query.terms[1].text, "browser");
}

TEST(QueryParser, ReturnsEmptyQueryForEmptyOrBlankInput) {
    ExpectNoUnits(ParseQuery(""));
    ExpectNoUnits(ParseQuery("   \t\n"));
}

TEST(QueryParser, RejectsUnmatchedQuote) {
    const auto r = ParseQuery("\"island");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kUnmatchedQuote);
}

TEST(QueryParser, RejectsEmptyPhrase) {
    const auto r = ParseQuery("\"\"");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kEmptyPhrase);
}

TEST(QueryParser, RejectsEmptyTermAfterPrefixMarker) {
    const auto r = ParseQuery("*");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kEmptyTermAfterMarker);
}

TEST(QueryParser, AllowsInternalAsteriskAsLiteralCharacter) {
    const auto r = ParseQuery("pre*fix");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text, "pre*fix");
    EXPECT_FALSE(r.query.terms[0].is_prefix);
}

TEST(QueryParser, RejectsTooManyUnits) {
    std::string input;
    for (std::size_t i = 0; i < kMaxQueryUnits + 1; ++i) {
        input += "t" + std::to_string(i) + " ";
    }
    const auto r = ParseQuery(input);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kTooManyUnits);
}

TEST(QueryParser, AcceptsExactlyTheMaxUnitCount) {
    std::string input;
    for (std::size_t i = 0; i < kMaxQueryUnits; ++i) {
        input += "t" + std::to_string(i) + " ";
    }
    const auto r = ParseQuery(input);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.query.terms.size(), kMaxQueryUnits);
}

TEST(QueryParser, RejectsTermOverMaxLength) {
    const auto r = ParseQuery(std::string(kMaxTermLength + 1, 'a'));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kTermTooLong);
}

TEST(QueryParser, AcceptsTermAtMaxLength) {
    const auto r = ParseQuery(std::string(kMaxTermLength, 'a'));
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text.size(), kMaxTermLength);
}

TEST(QueryParser, RejectsPhraseOverWordBound) {
    std::string phrase = "\"";
    for (std::size_t i = 0; i < kMaxPhraseWords + 1; ++i) {
        phrase += "w" + std::to_string(i) + " ";
    }
    phrase += "\"";
    const auto r = ParseQuery(phrase);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kPhraseTooLong);
}

TEST(QueryParser, RejectsPhraseOverByteLength) {
    const auto r = ParseQuery("\"" + std::string(kMaxPhraseLength + 1, 'a') + "\"");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error.kind, QueryParseError::Kind::kPhraseTooLong);
}

TEST(QueryParser, UsesInjectableNormalizer) {
    const auto identity = [](std::string_view token) { return std::string(token); };
    const auto r = ParseQuery("Island", identity);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text, "Island");
}

TEST(QueryParser, EmptyNormalizerFallsBackToDefault) {
    const auto r = ParseQuery("Island", {});
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.query.terms.size(), 1u);
    EXPECT_EQ(r.query.terms[0].text, "island");
}

TEST(QueryParser, DefaultNormalizeIsBoundedAndLowercases) {
    EXPECT_EQ(DefaultNormalize("TeRm"), "term");
    EXPECT_EQ(DefaultNormalize(std::string(1000, 'A')).size(), kMaxTermLength);
}

}  // namespace
}  // namespace island