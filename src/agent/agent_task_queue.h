#ifndef ISLAND_AGENT_AGENT_TASK_QUEUE_H_
#define ISLAND_AGENT_AGENT_TASK_QUEUE_H_

// The agent task queue: prompts the person sends to the in-browser agent run
// one at a time. The queue decides when a task starts and what its state is;
// it does not touch confirmations (the window withdraws those when a task
// stops). CEF-free.
//
// Storage is kept in display order: the live task (running or waiting) first,
// then queued tasks in queue order, then finished tasks, newest finish first.
//
// Turn-scoped calls carry the task id. Calls for a task that is not the live
// one are ignored, so a late turn end from a stopped turn cannot finish the
// task that replaced it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace island::agent {

enum class TaskKind : std::uint8_t { kChat, kResearch, kCollect, kAsk };

enum class TaskState : std::uint8_t {
    kQueued,
    kRunning,
    kWaitingForYou,
    kDone,
    kFailed,
    kCancelled,
};

struct AgentTask {
    std::string id;  // "t1", "t2", ...
    TaskKind kind = TaskKind::kChat;
    std::string title;  // first non-blank line, trimmed, at most 60 code points + "…"
    std::string prompt;
    std::string context;
    std::int64_t created_ms = 0;
    TaskState state = TaskState::kQueued;
    std::size_t done = 0, total = 0;  // plan progress
};

class AgentTaskQueue {
  public:
    // Sends one task's prompt to the agent. Called synchronously from Enqueue,
    // OnTurnFinished, OnWaiting(id, false) and StopCurrentAndAdvanceNow when the
    // queue advances. It may call back into the queue: a turn that fails at once
    // calls OnTurnFinished(task.id, false) from inside StartFn.
    using StartFn = std::function<void(const AgentTask&)>;

    explicit AgentTaskQueue(StartFn start);

    // Appends a queued task and starts it when nothing else is live. Returns a
    // copy taken after the queue advanced, so the task may already be kFailed
    // (StartFn failed it synchronously) or kRunning.
    AgentTask Enqueue(TaskKind kind, std::string prompt, std::string context, std::int64_t now_ms);
    // Queued tasks only, and only past another queued task; returns false otherwise.
    bool MoveUp(std::string_view id);
    bool MoveDown(std::string_view id);
    // Queued tasks only; the live task cannot be removed.
    bool Remove(std::string_view id);
    // The live task becomes kCancelled. If its turn is still in flight, the queue
    // waits for that turn's end (OnTurnFinished(id, ...)) before it starts the
    // next task; the cancelled task stays kCancelled. If its turn already ended
    // (recorded while waiting), the queue advances at once.
    // Task 6 uses this while the agent session is busy with the prompt.
    void StopCurrent();
    // Cancels the live task (if any) and advances at once, without waiting for a
    // turn end. Clears any pending stop. Task 6 uses this only when no turn is in
    // flight: the prompt was never sent, or the session died and will not report.
    void StopCurrentAndAdvanceNow();
    // Turn end for task `id`. A running task becomes kDone or kFailed and the queue
    // advances. A waiting task records the outcome and stays waiting; OnWaiting(id,
    // false) applies it. The turn end that ends a stopped task clears the stop and
    // advances; the cancelled state is kept.
    void OnTurnFinished(std::string_view id, bool ok);
    // Running ↔ kWaitingForYou for task `id`. A waiting task blocks the queue
    // until answered. Answering applies a turn end recorded while waiting.
    void OnWaiting(std::string_view id, bool waiting);
    // Plan progress of the live task `id`.
    void OnProgress(std::string_view id, std::size_t done, std::size_t total);
    // Drops kDone, kFailed and kCancelled tasks; live tasks keep their order.
    void ClearFinished();

    [[nodiscard]] const AgentTask* current() const;
    [[nodiscard]] const std::vector<AgentTask>& tasks() const;  // display order
    [[nodiscard]] std::size_t queued_count() const;

  private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    [[nodiscard]] std::size_t ActiveIndex() const;  // running or waiting task, or kNone
    [[nodiscard]] std::size_t LiveCount() const;    // running, waiting and queued tasks
    [[nodiscard]] std::size_t IndexOf(std::string_view id) const;
    // Sets a live task's final state and moves it to the front of the finished
    // block, then advances.
    void Finish(std::size_t index, TaskState state);
    void Advance();

    StartFn start_;
    std::vector<AgentTask> tasks_;  // display order, see the file comment
    std::size_t next_id_ = 1;
    // Id of a stopped task whose turn is still in flight; Advance waits for it.
    std::string stop_latch_;
    // Id of a waiting task whose turn already ended, and that turn's outcome.
    std::string pending_id_;
    bool pending_ok_ = false;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_TASK_QUEUE_H_
