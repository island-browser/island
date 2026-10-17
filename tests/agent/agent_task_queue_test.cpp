#include "agent_task_queue.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace island::agent {
namespace {

// Records the ids of every task the queue starts, in order.
struct Recorder {
    std::vector<std::string> started;
    AgentTaskQueue queue{[this](const AgentTask& task) { started.push_back(task.id); }};
};

std::string Repeat(const std::string& unit, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += unit;
    return out;
}

TEST(AgentTaskQueue, EnqueueStartsWhenIdle) {
    Recorder r;
    const AgentTask& task = r.queue.Enqueue(TaskKind::kChat, "hello", "", 5);
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
    r.queue.OnTurnFinished(true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(r.queue.tasks()[0].state, TaskState::kDone);
    EXPECT_EQ(r.queue.tasks()[1].context, "ctx");
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
    r.queue.OnTurnFinished(true);
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
    r.queue.OnTurnFinished(true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
}

TEST(AgentTaskQueue, StopAdvancesToTheNext) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.StopCurrent();
    EXPECT_EQ(r.queue.tasks()[0].state, TaskState::kCancelled);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(r.queue.tasks()[1].state, TaskState::kRunning);
}

TEST(AgentTaskQueue, WaitingBlocksAdvanceUntilAnswered) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kAsk, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnWaiting(true);
    EXPECT_EQ(r.queue.current()->state, TaskState::kWaitingForYou);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.OnTurnFinished(true);  // ignored while waiting
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1"}));
    r.queue.OnWaiting(false);
    EXPECT_EQ(r.queue.current()->state, TaskState::kRunning);
    r.queue.OnTurnFinished(true);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, FailedTurnStillAdvances) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.OnTurnFinished(false);
    EXPECT_EQ(r.queue.tasks()[0].state, TaskState::kFailed);
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, ClearFinishedKeepsLiveTasks) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kChat, "a", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "b", "", 0);
    r.queue.Enqueue(TaskKind::kChat, "c", "", 0);
    r.queue.OnTurnFinished(true);  // t1 done, t2 running
    r.queue.ClearFinished();
    ASSERT_EQ(r.queue.tasks().size(), 2u);
    EXPECT_EQ(r.queue.tasks()[0].id, "t2");
    EXPECT_EQ(r.queue.tasks()[0].state, TaskState::kRunning);
    EXPECT_EQ(r.queue.tasks()[1].id, "t3");
    EXPECT_EQ(r.started, (std::vector<std::string>{"t1", "t2"}));
}

TEST(AgentTaskQueue, TitlesClipOnUtf8Boundaries) {
    Recorder r;
    const std::string cyrillic_long = Repeat("Ж", 70);
    const AgentTask& clipped = r.queue.Enqueue(TaskKind::kChat, cyrillic_long, "", 0);
    EXPECT_EQ(clipped.title, Repeat("Ж", 60) + "…");
    EXPECT_EQ(clipped.prompt, cyrillic_long);

    const AgentTask& exact = r.queue.Enqueue(TaskKind::kChat, Repeat("Ж", 60), "", 0);
    EXPECT_EQ(exact.title, Repeat("Ж", 60));

    const AgentTask& first_line =
        r.queue.Enqueue(TaskKind::kChat, "line one\nline two", "", 0);
    EXPECT_EQ(first_line.title, "line one");
}

TEST(AgentTaskQueue, ProgressTracksTheCurrentTask) {
    Recorder r;
    r.queue.Enqueue(TaskKind::kCollect, "a", "", 0);
    r.queue.Enqueue(TaskKind::kCollect, "b", "", 0);
    r.queue.OnProgress(2, 5);
    EXPECT_EQ(r.queue.tasks()[0].done, 2u);
    EXPECT_EQ(r.queue.tasks()[0].total, 5u);
    r.queue.OnTurnFinished(true);
    EXPECT_EQ(r.queue.tasks()[1].done, 0u);
    r.queue.OnProgress(1, 3);
    EXPECT_EQ(r.queue.tasks()[1].done, 1u);
    EXPECT_EQ(r.queue.tasks()[1].total, 3u);
    EXPECT_EQ(r.queue.tasks()[0].done, 2u);  // finished task keeps its last progress
}

}  // namespace
}  // namespace island::agent
