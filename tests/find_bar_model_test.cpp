#include "find_bar_model.h"

#include <gtest/gtest.h>

namespace island {
namespace {

using Kind = FindRequest::Kind;

TEST(FindBarModelTest, StartsClosedAndEmpty) {
    FindBarModel model;
    EXPECT_FALSE(model.open());
    EXPECT_EQ(model.CountLabel(), "");
    EXPECT_EQ(model.Close().kind, Kind::kNone);
    EXPECT_EQ(model.Step(true).kind, Kind::kNone);
    EXPECT_EQ(model.SetQuery("x").kind, Kind::kNone);  // typing needs the bar to be open
}

TEST(FindBarModelTest, TypingStartsANewSearchFromTheTop) {
    FindBarModel model;
    EXPECT_EQ(model.Open().kind, Kind::kNone);
    EXPECT_TRUE(model.open());
    const FindRequest request = model.SetQuery("island");
    EXPECT_EQ(request, (FindRequest{.kind = Kind::kFind,
                                    .text = "island",
                                    .forward = true,
                                    .match_case = false,
                                    .find_next = false}));
    EXPECT_EQ(model.SetQuery("island").kind, Kind::kNone);  // unchanged text is not a search
}

TEST(FindBarModelTest, EmptyingTheFieldStopsAndClearsTheSelection) {
    FindBarModel model;
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("a"));
    model.OnResult(4, 1);
    const FindRequest request = model.SetQuery("");
    EXPECT_EQ(request.kind, Kind::kStop);
    EXPECT_TRUE(request.clear_selection);
    EXPECT_EQ(model.CountLabel(), "");
    EXPECT_EQ(model.total(), 0);
}

TEST(FindBarModelTest, StepUsesFindNextInTheRequestedDirection) {
    FindBarModel model;
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("cat"));
    const FindRequest next = model.Step(true);
    EXPECT_EQ(next.kind, Kind::kFind);
    EXPECT_TRUE(next.find_next);
    EXPECT_TRUE(next.forward);
    const FindRequest previous = model.Step(false);
    EXPECT_TRUE(previous.find_next);
    EXPECT_FALSE(previous.forward);
    EXPECT_EQ(previous.text, "cat");
}

TEST(FindBarModelTest, MatchCaseRestartsTheSearchOnlyWhenThereIsAQuery) {
    FindBarModel model;
    static_cast<void>(model.Open());
    EXPECT_EQ(model.SetMatchCase(true).kind, Kind::kNone);
    EXPECT_TRUE(model.match_case());
    const FindRequest request = model.SetQuery("Cat");
    EXPECT_TRUE(request.match_case);
    const FindRequest toggled = model.SetMatchCase(false);
    EXPECT_EQ(toggled.kind, Kind::kFind);
    EXPECT_FALSE(toggled.match_case);
    EXPECT_FALSE(toggled.find_next);
    EXPECT_EQ(model.SetMatchCase(false).kind, Kind::kNone);
}

TEST(FindBarModelTest, ResultsFormTheNOfMLabel) {
    FindBarModel model;
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("a"));
    EXPECT_EQ(model.CountLabel(), "");  // searched, but nothing reported yet: no flash of 0 of 0
    EXPECT_TRUE(model.searched());
    EXPECT_FALSE(model.reported());
    model.OnResult(12, 3);
    EXPECT_EQ(model.CountLabel(), "3 of 12");
    // CEF reports -1 / 0 for fields that did not change.
    model.OnResult(-1, 4);
    EXPECT_EQ(model.CountLabel(), "4 of 12");
    model.OnResult(13, 0);
    EXPECT_EQ(model.CountLabel(), "4 of 13");
    model.OnResult(0, 0);
    EXPECT_EQ(model.CountLabel(), "0 of 0");
}

TEST(FindBarModelTest, ResultsAfterCloseOrForAnEmptyFieldAreIgnored) {
    FindBarModel model;
    model.OnResult(5, 1);
    EXPECT_EQ(model.total(), 0);
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("a"));
    static_cast<void>(model.Close());
    model.OnResult(5, 1);
    EXPECT_EQ(model.total(), 0);
    EXPECT_EQ(model.CountLabel(), "");
}

TEST(FindBarModelTest, CloseKeepsTheSelectionAndReopenReplaysTheQuery) {
    FindBarModel model;
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("word"));
    model.OnResult(2, 1);
    const FindRequest closed = model.Close();
    EXPECT_EQ(closed.kind, Kind::kStop);
    EXPECT_FALSE(closed.clear_selection);
    EXPECT_FALSE(model.open());
    EXPECT_EQ(model.query(), "word");  // remembered for the next Cmd+F
    const FindRequest reopened = model.Open();
    EXPECT_EQ(reopened.kind, Kind::kFind);
    EXPECT_EQ(reopened.text, "word");
    EXPECT_FALSE(reopened.find_next);
    EXPECT_EQ(model.Open().kind, Kind::kNone);  // already open
}

TEST(FindBarModelTest, ClosingWithoutASearchNeedsNoStop) {
    FindBarModel model;
    static_cast<void>(model.Open());
    EXPECT_EQ(model.Close().kind, Kind::kNone);
}

TEST(FindBarModelTest, NavigationInvalidatesCountsButKeepsTheQuery) {
    FindBarModel model;
    static_cast<void>(model.Open());
    static_cast<void>(model.SetQuery("a"));
    model.OnResult(7, 2);
    model.ClearResults();
    EXPECT_EQ(model.CountLabel(), "");
    EXPECT_EQ(model.query(), "a");
    EXPECT_TRUE(model.open());
}

}  // namespace
}  // namespace island
