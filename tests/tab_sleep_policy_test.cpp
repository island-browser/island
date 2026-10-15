#include "tab_sleep_policy.h"

#include <gtest/gtest.h>

#include <chrono>

namespace island {
namespace {

using std::chrono::minutes;

const TabSleepPolicy::Clock::time_point kStart{};

TabSleepInput Idle(std::uint64_t id) {
    TabSleepInput input;
    input.id = TabId{id};
    input.has_browser = true;
    return input;
}

TEST(TabSleepPolicyTest, GivenNoSettingThenTheDefaultIsThirtyMinutes) {
    const TabSleepPolicy policy;

    EXPECT_EQ(policy.minutes(), 30);
    EXPECT_TRUE(policy.enabled());
}

TEST(TabSleepPolicyTest, GivenAnUnknownMinuteCountThenTheDefaultApplies) {
    EXPECT_EQ(NormalizeTabSleepMinutes(7), 30);
    EXPECT_EQ(NormalizeTabSleepMinutes(-1), 30);
    EXPECT_EQ(NormalizeTabSleepMinutes(0), 0);
    EXPECT_EQ(NormalizeTabSleepMinutes(15), 15);
    EXPECT_EQ(NormalizeTabSleepMinutes(60), 60);
}

TEST(TabSleepPolicyTest, GivenAnIdleTabWhenTheTimeoutPassesThenItExpires) {
    TabSleepPolicy policy(15);
    policy.Touch(TabId{1}, kStart);

    EXPECT_TRUE(policy.Expired({Idle(1)}, kStart + minutes(14)).empty());
    const std::vector<TabId> expired = policy.Expired({Idle(1)}, kStart + minutes(15));
    ASSERT_EQ(expired.size(), 1U);
    EXPECT_EQ(expired[0], TabId{1});
}

TEST(TabSleepPolicyTest, GivenATouchedTabThenItsTimerRestarts) {
    TabSleepPolicy policy(15);
    policy.Touch(TabId{1}, kStart);
    policy.Touch(TabId{1}, kStart + minutes(10));

    EXPECT_TRUE(policy.Expired({Idle(1)}, kStart + minutes(20)).empty());
    EXPECT_EQ(policy.Expired({Idle(1)}, kStart + minutes(25)).size(), 1U);
}

TEST(TabSleepPolicyTest, GivenNeverThenNothingExpiresButSleepNowStillWorks) {
    TabSleepPolicy policy(0);
    policy.Touch(TabId{1}, kStart);

    EXPECT_FALSE(policy.enabled());
    EXPECT_TRUE(policy.Expired({Idle(1)}, kStart + minutes(600)).empty());
    EXPECT_EQ(policy.AllEligible({Idle(1)}).size(), 1U);
}

TEST(TabSleepPolicyTest, GivenAnUnseenTabThenItNeverExpiresUntilObserved) {
    TabSleepPolicy policy(15);

    EXPECT_TRUE(policy.Expired({Idle(1)}, kStart + minutes(100)).empty());
    policy.Observe(TabId{1}, kStart + minutes(100));
    policy.Observe(TabId{1}, kStart + minutes(200));  // already known: keeps the first time
    EXPECT_EQ(policy.Expired({Idle(1)}, kStart + minutes(115)).size(), 1U);
}

TEST(TabSleepPolicyTest, GivenExcludedTabsThenTheyNeverSleep) {
    TabSleepPolicy policy(15);
    TabSleepInput active = Idle(1);
    active.active = true;
    TabSleepInput pinned = Idle(2);
    pinned.pinned = true;
    TabSleepInput split = Idle(3);
    split.split_partner = true;
    TabSleepInput audio = Idle(4);
    audio.playing_audio = true;
    TabSleepInput dirty = Idle(5);
    dirty.unsaved_input = true;
    TabSleepInput asleep = Idle(6);
    asleep.sleeping = true;
    TabSleepInput local_page = Idle(7);
    local_page.web_page = false;
    TabSleepInput no_browser = Idle(8);
    no_browser.has_browser = false;
    const std::vector<TabSleepInput> tabs{active, pinned,     split,      audio,  dirty,
                                          asleep, local_page, no_browser, Idle(9)};
    for (const TabSleepInput& tab : tabs) {
        policy.Touch(tab.id, kStart);
    }

    const std::vector<TabId> expired = policy.Expired(tabs, kStart + minutes(60));
    const std::vector<TabId> now = policy.AllEligible(tabs);

    ASSERT_EQ(expired.size(), 1U);
    EXPECT_EQ(expired[0], TabId{9});
    ASSERT_EQ(now.size(), 1U);
    EXPECT_EQ(now[0], TabId{9});
}

TEST(TabSleepPolicyTest, GivenAForgottenTabThenItIsNoLongerTracked) {
    TabSleepPolicy policy(15);
    policy.Touch(TabId{1}, kStart);
    policy.Forget(TabId{1});

    EXPECT_TRUE(policy.Expired({Idle(1)}, kStart + minutes(60)).empty());
}

}  // namespace
}  // namespace island
