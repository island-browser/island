#include "peek_state.h"

#include <gtest/gtest.h>

namespace island {
namespace {

TEST(PeekStateTest, GivenANewStateThenNothingIsPeeked) {
    const PeekState peek;

    EXPECT_FALSE(peek.open());
    EXPECT_TRUE(peek.url().empty());
}

TEST(PeekStateTest, GivenAnAddressWhenOpenedThenThePeekShowsIt) {
    PeekState peek;

    EXPECT_TRUE(peek.Open("https://example.com/"));

    EXPECT_TRUE(peek.open());
    EXPECT_EQ(peek.url(), "https://example.com/");
}

TEST(PeekStateTest, GivenAnEmptyAddressWhenOpenedThenNothingOpens) {
    PeekState peek;

    EXPECT_FALSE(peek.Open(""));
    EXPECT_FALSE(peek.open());
}

TEST(PeekStateTest, GivenAnOpenPeekWhenAnotherLinkOpensThenItReplacesTheFirst) {
    PeekState peek;
    ASSERT_TRUE(peek.Open("https://a.example/"));
    peek.SetTitle("A");
    const std::uint64_t first = peek.generation();

    ASSERT_TRUE(peek.Open("https://b.example/"));

    EXPECT_EQ(peek.url(), "https://b.example/");
    EXPECT_TRUE(peek.title().empty());
    EXPECT_NE(peek.generation(), first);
}

TEST(PeekStateTest, GivenAnOpenPeekWhenClosedTwiceThenTheSecondCloseIsANoOp) {
    PeekState peek;
    ASSERT_TRUE(peek.Open("https://example.com/"));

    EXPECT_TRUE(peek.Close());
    const std::uint64_t after_close = peek.generation();
    EXPECT_FALSE(peek.Close());

    EXPECT_FALSE(peek.open());
    EXPECT_EQ(peek.generation(), after_close);
}

TEST(PeekStateTest, GivenAnOpenPeekWhenOpenedAsATabThenTheCurrentAddressIsReturnedAndItCloses) {
    PeekState peek;
    ASSERT_TRUE(peek.Open("https://example.com/"));
    peek.SetUrl("https://example.com/next");

    const std::optional<std::string> url = peek.TakeForTab();

    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, "https://example.com/next");
    EXPECT_FALSE(peek.open());
    EXPECT_FALSE(peek.TakeForTab().has_value());
}

TEST(PeekStateTest, GivenAnOpenPeekWhenEscapeIsPressedThenItClosesAndTheKeyIsConsumed) {
    PeekState peek;
    ASSERT_TRUE(peek.Open("https://example.com/"));

    EXPECT_FALSE(peek.HandleKey('A'));
    EXPECT_TRUE(peek.open());
    EXPECT_TRUE(peek.HandleKey(kPeekEscapeKeyCode));
    EXPECT_FALSE(peek.open());
}

TEST(PeekStateTest, GivenNoPeekWhenEscapeIsPressedThenTheKeyPassesThrough) {
    PeekState peek;

    EXPECT_FALSE(peek.HandleKey(kPeekEscapeKeyCode));
}

TEST(PeekStateTest, GivenAClosedPeekWhenAStaleTitleArrivesThenItIsIgnored) {
    PeekState peek;
    ASSERT_TRUE(peek.Open("https://example.com/"));
    peek.Close();

    peek.SetTitle("late");
    peek.SetUrl("https://late.example/");

    EXPECT_TRUE(peek.title().empty());
    EXPECT_TRUE(peek.url().empty());
}

}  // namespace
}  // namespace island
