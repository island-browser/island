#include "link_open_policy.h"

#include <gtest/gtest.h>

namespace island {
namespace {

struct DecisionCase {
    const char* name;
    LinkDisposition disposition;
    bool user_gesture;
    LinkOpenAction expected;
};

// Shift+click (NEW_WINDOW), plain target=_blank and Cmd/Ctrl/middle-click (new tabs), and
// script-initiated opens without a gesture.
constexpr DecisionCase kCases[] = {
    {"ShiftClickPeeks", LinkDisposition::kNewWindow, true, LinkOpenAction::kPeek},
    {"PlainTargetBlankOpensForegroundTab", LinkDisposition::kNewForegroundTab, true,
     LinkOpenAction::kForegroundTab},
    {"WindowOpenWithGestureOpensForegroundTab", LinkDisposition::kNewForegroundTab, true,
     LinkOpenAction::kForegroundTab},
    {"CmdClickOpensBackgroundTab", LinkDisposition::kNewBackgroundTab, true,
     LinkOpenAction::kBackgroundTab},
    {"MiddleClickOpensBackgroundTab", LinkDisposition::kNewBackgroundTab, true,
     LinkOpenAction::kBackgroundTab},
    {"ScriptTargetBlankWithoutGestureIsRejected", LinkDisposition::kNewForegroundTab, false,
     LinkOpenAction::kReject},
    {"ScriptBackgroundTabWithoutGestureIsRejected", LinkDisposition::kNewBackgroundTab, false,
     LinkOpenAction::kReject},
    {"ScriptNewWindowWithoutGestureIsRejected", LinkDisposition::kNewWindow, false,
     LinkOpenAction::kReject},
    {"CurrentTabIsNotANewTabOpen", LinkDisposition::kCurrentTab, true, LinkOpenAction::kReject},
    {"CurrentTabWithoutGestureIsRejected", LinkDisposition::kCurrentTab, false,
     LinkOpenAction::kReject},
    {"PopupWithWindowFeaturesIsRejected", LinkDisposition::kOther, true, LinkOpenAction::kReject},
    {"PopupWithoutGestureIsRejected", LinkDisposition::kOther, false, LinkOpenAction::kReject},
};

TEST(LinkOpenPolicyTest, GivenDispositionAndGestureThenTheTableDecidesTheAction) {
    for (const DecisionCase& test_case : kCases) {
        EXPECT_EQ(DecideLinkOpen(test_case.disposition, test_case.user_gesture), test_case.expected)
            << test_case.name;
    }
}

TEST(LinkOpenPolicyTest, GivenNoGestureThenNoDispositionOpensAnything) {
    const LinkDisposition dispositions[] = {
        LinkDisposition::kNewWindow,
        LinkDisposition::kNewForegroundTab,
        LinkDisposition::kNewBackgroundTab,
        LinkDisposition::kCurrentTab,
        LinkDisposition::kOther,
    };
    for (const LinkDisposition disposition : dispositions) {
        EXPECT_EQ(DecideLinkOpen(disposition, false), LinkOpenAction::kReject);
    }
}

}  // namespace
}  // namespace island
