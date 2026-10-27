#include "session_save_scheduler.h"

#include <gtest/gtest.h>

#include <chrono>

namespace island {
namespace {

using Clock = SessionSaveScheduler::Clock;

// A fixed origin; every test moves the clock with At().
Clock::time_point At(std::chrono::milliseconds offset) { return Clock::time_point{} + offset; }

TEST(SessionSaveScheduler, DebouncesBurstsIntoOneSave) {
    SessionSaveScheduler scheduler;
    scheduler.MarkDirty(At(std::chrono::milliseconds(0)));
    scheduler.MarkDirty(At(std::chrono::milliseconds(500)));
    scheduler.MarkDirty(At(std::chrono::milliseconds(1000)));

    // Two seconds after the last change (t=1 s), not before.
    EXPECT_FALSE(scheduler.Due(At(std::chrono::milliseconds(2900))));
    EXPECT_TRUE(scheduler.Due(At(std::chrono::milliseconds(3000))));
    ASSERT_TRUE(scheduler.deadline().has_value());
    EXPECT_EQ(*scheduler.deadline(), At(std::chrono::milliseconds(3000)));
}

TEST(SessionSaveScheduler, MaxLatencyBoundsContinuousChange) {
    SessionSaveScheduler scheduler;
    // A change every second from 0 s to 9 s keeps the debounce from ever expiring.
    for (int second = 0; second <= 9; ++second) {
        scheduler.MarkDirty(At(std::chrono::seconds(second)));
        EXPECT_FALSE(
            scheduler.Due(At(std::chrono::seconds(second) + std::chrono::milliseconds(999))))
            << "second " << second;
    }
    // The first unsaved change was at 0 s, so the save is due at 10 s.
    EXPECT_FALSE(scheduler.Due(At(std::chrono::milliseconds(9999))));
    EXPECT_TRUE(scheduler.Due(At(std::chrono::milliseconds(10000))));
}

TEST(SessionSaveScheduler, CleanAfterSuccessfulSave) {
    SessionSaveScheduler scheduler;
    scheduler.MarkDirty(At(std::chrono::milliseconds(0)));
    ASSERT_TRUE(scheduler.Due(At(std::chrono::milliseconds(2000))));
    scheduler.SaveFinished(true);
    EXPECT_FALSE(scheduler.dirty());
    EXPECT_FALSE(scheduler.deadline().has_value());
    EXPECT_FALSE(scheduler.Due(At(std::chrono::hours(1))));
}

TEST(SessionSaveScheduler, CancelledAfterClose) {
    SessionSaveScheduler scheduler;
    scheduler.MarkDirty(At(std::chrono::milliseconds(0)));
    scheduler.Cancel();

    scheduler.MarkDirty(At(std::chrono::milliseconds(1000)));
    EXPECT_FALSE(scheduler.Due(At(std::chrono::hours(1))));
    EXPECT_FALSE(scheduler.deadline().has_value());
}

TEST(SessionSaveScheduler, FailedSaveStaysDirtyAndRetriesOnNextChange) {
    SessionSaveScheduler scheduler;
    scheduler.MarkDirty(At(std::chrono::milliseconds(0)));
    ASSERT_TRUE(scheduler.Due(At(std::chrono::milliseconds(2000))));

    // The save failed (disk full, read-only directory): the state stays dirty, but nothing
    // is due again until a new change arrives, so the owner does not spin on retries.
    scheduler.SaveFinished(false);
    EXPECT_TRUE(scheduler.dirty());
    EXPECT_FALSE(scheduler.deadline().has_value());
    EXPECT_FALSE(scheduler.Due(At(std::chrono::hours(1))));

    // The next change retries: due two seconds after it.
    scheduler.MarkDirty(At(std::chrono::seconds(3600)));
    EXPECT_FALSE(scheduler.Due(At(std::chrono::seconds(3601))));
    EXPECT_TRUE(scheduler.Due(At(std::chrono::seconds(3602))));
}

}  // namespace
}  // namespace island
