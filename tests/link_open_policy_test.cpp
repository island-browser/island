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

// Cmd/Ctrl+click and middle-click arrive as the same disposition (NEW_BACKGROUND_TAB), so one
// row covers both gestures. Shift+click (NEW_WINDOW) and plain target=_blank or window.open
// with a gesture (NEW_FOREGROUND_TAB) both open a foreground tab. Script-initiated opens without
// a gesture are rejected.
constexpr DecisionCase kCases[] = {
    {"CmdCtrlOrMiddleClickPeeks", LinkDisposition::kNewBackgroundTab, true, LinkOpenAction::kPeek},
    {"ShiftClickOpensForegroundTab", LinkDisposition::kNewWindow, true,
     LinkOpenAction::kForegroundTab},
    {"PlainClickOrWindowOpenWithGestureOpensForegroundTab", LinkDisposition::kNewForegroundTab,
     true, LinkOpenAction::kForegroundTab},
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
