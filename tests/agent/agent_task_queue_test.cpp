#include "agent_task_queue.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island::agent {
namespace {

// Records the ids of every task the queue starts, in order.
struct Recorder {
    std::vector<std::string> started;
    AgentTaskQueue queue{[this](const AgentTask& task) { started.push_back(task.id); }};
};

// Starts tasks like Recorder does, and fails `fail_id` synchronously the way a
// missing agent would: StartFn reports the turn end from inside the queue call.
struct SyncFailure {
    std::vector<std::string> started;
    std::string fail_id;
    AgentTaskQueue queue{[this](const AgentTask& task) {
        started.push_back(task.id);
        if (task.id == fail_id) queue.OnTurnFinished(task.id, false);
    }};
};

std::string Repeat(const std::string& unit, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += unit;
    return out;
}

// The task with this id; fails the test and returns an empty task when absent.
const AgentTask& Get(const AgentTaskQueue& queue, std::string_view id) {
    for (const AgentTask& task : queue.tasks()) {
        if (task.id == id) return task;
    }
    ADD_FAILURE() << "no task " << id;
    static const AgentTask kMissing;
    return kMissing;
}

std::vector<std::string> Ids(const AgentTaskQueue& queue) {
    std::vector<std::string> ids;
    for (const AgentTask& task : queue.tasks()) ids.push_back(task.id);
    return ids;
}

TEST(AgentTaskQueue, EnqueueStartsWhenIdle) {
    Recorder r;
    const AgentTask task = r.queue.Enqueue(TaskKind::kChat, "hello", "", 5);
    EXPECT_EQ(task.id, "t1");
    EXPECT_EQ(task.created_ms, 5);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    ASSERT_NE(r.queue.current(), nullptr);
    EXPECT_EQ(r.queue.current()->state, TaskState::kRunning);
}

TEST(AgentTaskQueue, SecondTaskWaitsForTheFirst) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "first", "", 0);
    r.queue.Enqueue(TaskKind::kResearch, "second", "ctx", 0);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    EXPECT_EQ(r.queue.queued_count(), 1u);
    r.queue.OnTurnFinished("t1", true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kDone);
    EXPECT_EQ(Get(r.queue, "t2").context, "ctx");
}

TEST(AgentTaskQueue, MoveUpAndDownReorderQueuedOnly) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);  // t1 running
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);  // t2
    r.queue.Enqueue(TaskKind::kChat, "c", "", 0);  // t3
    r.queue.Enqueue(TaskKind::kChat, "d", "", 0);  // t4
    EXPECT_FALSE(r.queue.MoveUp("t1"));
    EXPECT_FALSE(r.queue.MoveUp("t2"));  // would pass the running task
    EXPECT_TRUE(r.queue.MoveUp("t3"));
    EXPECT_TRUE(r.queue.MoveUp("t2"));
    EXPECT_FALSE(r.queue.MoveDown("t4"));  // already last
    EXPECT_TRUE(r.queue.MoveDown("t2"));
    EXPECT_EQ(r.queue.tasks()[1].id, "t3");
    EXPECT_EQ(r.queue.tasks()[2].id, "t2");
    r.queue.OnTurnFinished("t1", true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t3"}));
}

TEST(AgentTaskQueue, RemoveRefusesTheRunningTask) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    EXPECT_FALSE(r.queue.Remove("t1"));
    EXPECT_TRUE(r.queue.Remove("t2"));
    EXPECT_EQ(r.queue.tasks().size(), 1u);
    EXPECT_EQ(r.queue.queued_count(), 0u);
    r.queue.OnTurnFinished("t1", true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
}

TEST(AgentTaskQueue, StopWaitsForTheStoppedTurnToEnd) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.StopCurrent();
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kCancelled);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));  // t2 waits for t1's turn end
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kQueued);

    r.queue.OnTurnFinished("t1", false);  // the cancelled turn ends
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kCancelled);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);

    r.queue.OnTurnFinished("t1", true);  // a stale end for the stopped task is ignored
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, StopAfterTheTurnEndedAdvancesAtOnce) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kAsk, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnWaiting("t1", true);
    r.queue.OnTurnFinished("t1", true);  // recorded while waiting
    r.queue.StopCurrent();               // nothing left to wait for
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kCancelled);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);
}

TEST(AgentTaskQueue, StopCurrentAndAdvanceNowStartsTheNextWithoutATurnEnd) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.StopCurrentAndAdvanceNow();  // t1 was never sent
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kCancelled);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);
}

TEST(AgentTaskQueue, StopCurrentAndAdvanceNowClearsAPendingStop) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "c", "", 0);
    r.queue.StopCurrent();  // t1's turn is in flight, so t2 waits
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.StopCurrentAndAdvanceNow();  // the session died and will not report t1's end
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kCancelled);
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);
    EXPECT_EQ(Get(r.queue, "t3").state, TaskState::kQueued);
}

TEST(AgentTaskQueue, StopCurrentAndAdvanceNowWithNothingLiveDoesNothing) {
    Recorder r;
    r.queue.StopCurrentAndAdvanceNow();
    EXPECT_TRUE(r.started.empty());
    EXPECT_EQ(r.queue.current(), nullptr);
}

TEST(AgentTaskQueue, WaitingBlocksAdvanceUntilAnswered) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kAsk, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnWaiting("t2", true);  // not the live task: ignored
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kRunning);
    r.queue.OnWaiting("t1", true);
    EXPECT_EQ(r.queue.current()->state, TaskState::kWaitingForYou);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.OnWaiting("t1", false);
    EXPECT_EQ(r.queue.current()->state, TaskState::kRunning);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.OnTurnFinished("t1", true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, TurnEndWhileWaitingCompletesOnAnswer) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kAsk, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnWaiting("t1", true);
    r.queue.OnTurnFinished("t1", false);  // the turn ends while the card is open
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kWaitingForYou);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.OnWaiting("t1", false);  // answered: the recorded outcome applies
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kFailed);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(Get(r.queue, "t2").state, TaskState::kRunning);
}

TEST(AgentTaskQueue, FailedTurnStillAdvances) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnTurnFinished("t1", false);
    EXPECT_EQ(Get(r.queue, "t1").state, TaskState::kFailed);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, SynchronousFailureInStartFnAdvancesOnce) {
    SyncFailure s;
    s.fail_id = "t2";
    s.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    s.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    s.queue.Enqueue(TaskKind::kChat, "c", "", 0);
    s.queue.OnTurnFinished("t1", true);  // starts t2, which fails at once; t3 starts
    EXPECT_EQ(s.started, (std::vector<std::string>{"t1", "t2", "t3"}));
    EXPECT_EQ(Get(s.queue, "t1").state, TaskState::kDone);
    EXPECT_EQ(Get(s.queue, "t2").state, TaskState::kFailed);
    EXPECT_EQ(Get(s.queue, "t3").state, TaskState::kRunning);
}

TEST(AgentTaskQueue, EnqueueReturnsTheTaskAsItStandsAfterStart) {
    SyncFailure s;
    s.fail_id = "t1";
    const AgentTask first = s.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    EXPECT_EQ(first.state, TaskState::kFailed);  // failed inside StartFn
    EXPECT_EQ(s.started, (std::vector<std::string>{"t1"}));
    const AgentTask second = s.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    EXPECT_EQ(second.state, TaskState::kRunning);
    EXPECT_EQ(s.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, ClearFinishedKeepsLiveTasks) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "c", "", 0);
    r.queue.OnTurnFinished("t1", true);  // t1 done, t2 running
    r.queue.ClearFinished();
    ASSERT_EQ(r.queue.tasks().size(), 2u);
    EXPECT_EQ(r.queue.tasks()[0].id, "t2");
    EXPECT_EQ(r.queue.tasks()[0].state, TaskState::kRunning);
    EXPECT_EQ(r.queue.tasks()[1].id, "t3");
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, TasksAreInDisplayOrder) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "c", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "d", "", 0);
    r.queue.OnTurnFinished("t1", true);   // t1 done, t2 running
    r.queue.OnTurnFinished("t2", false);  // t2 failed, t3 running
    EXPECT_EQ(Ids(r.queue), (std::vector<std::string>{"t3", "t4", "t2", "t1"}));
    r.queue.Enqueue(TaskKind::kChat, "e", "", 0);  // queued after t4, before the finished
    EXPECT_EQ(Ids(r.queue), (std::vector<std::string>{"t3", "t4", "t5", "t2", "t1"}));
    EXPECT_EQ(r.queue.current()->id, "t3");
}

TEST(AgentTaskQueue, TitlesClipOnUtf8Boundaries) {
    Recorder r;
    const std::string cyrillic_long = Repeat("Ж", 70);
    const AgentTask clipped = r.queue.Enqueue(TaskKind::kChat, cyrillic_long, "", 0);
    EXPECT_EQ(clipped.title, Repeat("Ж", 60) + "…");
    EXPECT_EQ(clipped.prompt, cyrillic_long);

    const AgentTask exact = r.queue.Enqueue(TaskKind::kChat, Repeat("Ж", 60), "", 0);
    EXPECT_EQ(exact.title, Repeat("Ж", 60));
}

TEST(AgentTaskQueue, TitleIsTheFirstNonBlankLineTrimmed) {
    Recorder r;
    const AgentTask first = r.queue.Enqueue(TaskKind::kChat, "\n   \n  hello  \nx", "", 0);
    EXPECT_EQ(first.title, "hello");
    const AgentTask crlf = r.queue.Enqueue(TaskKind::kChat, "one\r\ntwo", "", 0);
    EXPECT_EQ(crlf.title, "one");
    const AgentTask blank = r.queue.Enqueue(TaskKind::kChat, " \t\n\n", "", 0);
    EXPECT_EQ(blank.title, "");
    const AgentTask padded_long =
        r.queue.Enqueue(TaskKind::kChat, "  " + Repeat("Ж", 70) + "  \nnext", "", 0);
    EXPECT_EQ(padded_long.title, Repeat("Ж", 60) + "…");
}

TEST(AgentTaskQueue, ProgressTracksTheCurrentTask) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kCollect, "a", "", 0);
    r.queue.Enqueue(TaskKind::kCollect, "b", "", 0);
    r.queue.OnProgress("t1", 2, 5);
    EXPECT_EQ(Get(r.queue, "t1").done, 2u);
    EXPECT_EQ(Get(r.queue, "t1").total, 5u);
    r.queue.OnProgress("t2", 9, 9);  // not the live task: ignored
    EXPECT_EQ(Get(r.queue, "t2").done, 0u);
    r.queue.OnTurnFinished("t1", true);
    EXPECT_EQ(Get(r.queue, "t2").done, 0u);
    r.queue.OnProgress("t2", 1, 3);
    EXPECT_EQ(Get(r.queue, "t2").done, 1u);
    EXPECT_EQ(Get(r.queue, "t2").total, 3u);
    EXPECT_EQ(Get(r.queue, "t1").done, 2u);  // finished task keeps its last progress
}

}  // namespace
}  // namespace island::agent
