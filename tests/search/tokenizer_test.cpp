#include "search/tokenizer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace island {
namespace {

using island::search::NormalizeToken;
using island::search::Tokenize;

TEST(Tokenizer, ReturnsNoTokensForEmptyInput) {
    EXPECT_TRUE(Tokenize("").empty());
    EXPECT_TRUE(Tokenize(std::string_view{}).empty());
}

TEST(Tokenizer, SplitsSimpleAsciiWordsOnWhitespace) {
    EXPECT_EQ(Tokenize("hello world"), std::vector<std::string>({"hello", "world"}));
}

TEST(Tokenizer, LowercasesMixedCaseAscii) {
    EXPECT_EQ(Tokenize("Hello World"), std::vector<std::string>({"hello", "world"}));
    EXPECT_EQ(Tokenize("MiXeD"), std::vector<std::string>({"mixed"}));
}

TEST(Tokenizer, SplitsTokensOnPunctuation) {
    EXPECT_EQ(Tokenize("foo.bar/baz"), std::vector<std::string>({"foo", "bar", "baz"}));
    EXPECT_EQ(Tokenize("a!b?c,d"), std::vector<std::string>({"a", "b", "c", "d"}));
    EXPECT_EQ(Tokenize("https://example.test/path?q=1"),
              std::vector<std::string>({"https", "example", "test", "path", "q", "1"}));
}

TEST(Tokenizer, CollapsesConsecutiveWhitespace) {
    EXPECT_EQ(Tokenize("  a   b  "), std::vector<std::string>({"a", "b"}));
}

TEST(Tokenizer, StripsTokensThatAreOnlyPunctuation) {
    EXPECT_EQ(Tokenize("..."), std::vector<std::string>{});
    EXPECT_EQ(Tokenize("a...b"), std::vector<std::string>({"a", "b"}));
}

TEST(Tokenizer, PassesAsciiThroughAndLowercasesIt) {
    EXPECT_EQ(NormalizeToken("URL"), "url");
    EXPECT_EQ(NormalizeToken("Island"), "island");
    EXPECT_EQ(NormalizeToken("123"), "123");
    EXPECT_EQ(NormalizeToken("already-lower"), "already-lower");
}

TEST(Tokenizer, KeepsAsciiDigitsAsTokenCharacters) {
    EXPECT_EQ(Tokenize("island2026"), std::vector<std::string>({"island2026"}));
    EXPECT_EQ(NormalizeToken("A1B2"), "a1b2");
}

TEST(Tokenizer, PassesMalformedUtf8BytesThroughWithoutThrowing) {
    const std::string malformed{"\xFF\xFE hello \xC3"};
    EXPECT_NO_THROW(Tokenize(malformed));
    const auto tokens = Tokenize(malformed);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], std::string("\xff\xfe"));
    EXPECT_EQ(tokens[1], "hello");
    EXPECT_EQ(tokens[2], std::string("\xc3"));
}

TEST(Tokenizer, PassesValidUtf8BytesThroughWithoutThrowing) {
    const std::string input{"caf\xC3\xA9 \xE2\x9C\x93"};
    EXPECT_NO_THROW(Tokenize(input));
    const auto tokens = Tokenize(input);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], std::string("caf\xc3\xa9"));
    EXPECT_EQ(tokens[1], "\xE2\x9C\x93");
}

TEST(Tokenizer, BoundsMaxTokenLengthViaTruncation) {
    const std::string long_token(500, 'a');
    const auto tokens = Tokenize(long_token);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].size(), 128u);
    EXPECT_EQ(tokens[0], std::string(128, 'a'));
}

TEST(Tokenizer, TruncatesOverlongUrlFragment) {
    const std::string path(1000, 'x');
    const std::string url = "https://example.test/" + path;
    const auto tokens = Tokenize(url);
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0], "https");
    EXPECT_EQ(tokens[1], "example");
    EXPECT_EQ(tokens[2], "test");
    EXPECT_EQ(tokens[3].size(), 128u);
}

TEST(Tokenizer, HandlesUrlLikeInput) {
    const auto tokens = Tokenize("https://News.Example.test/Article?Ref=42");
    EXPECT_EQ(tokens, std::vector<std::string>(
                          {"https", "news", "example", "test", "article", "ref", "42"}));
}

TEST(Tokenizer, NormalizeTokenIsLengthBounded) {
    EXPECT_EQ(NormalizeToken(std::string(1000, 'A')).size(), 128u);
}

}  // namespace
}  // namespace island