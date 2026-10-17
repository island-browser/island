#ifndef ISLAND_AGENT_AGENT_TASK_QUEUE_H_
#define ISLAND_AGENT_AGENT_TASK_QUEUE_H_

// The agent task queue: prompts the person sends to the in-browser agent run
// one at a time, in display order. The queue decides when a task starts and
// what its state is; it does not touch confirmations (the window withdraws
// those when a task stops). CEF-free.

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
    std::string id;     // "t1", "t2", ...
    TaskKind kind = TaskKind::kChat;
    std::string title;  // first line of the prompt, at most 60 code points + "…"
    std::string prompt;
    std::string context;
    std::int64_t created_ms = 0;
    TaskState state = TaskState::kQueued;
    std::size_t done = 0, total = 0;  // plan progress
};

class AgentTaskQueue {
  public:
    // Sends one task's prompt to the agent. Called synchronously from Enqueue,
    // OnTurnFinished, StopCurrent and OnWaiting(false) when the queue advances.
    using StartFn = std::function<void(const AgentTask&)>;

    explicit AgentTaskQueue(StartFn start);

    // Appends a queued task and starts it when nothing else is running. The
    // reference is valid until the next Enqueue or ClearFinished.
    const AgentTask& Enqueue(TaskKind kind, std::string prompt, std::string context,
                             std::int64_t now_ms);
    // Queued tasks only, and only past another queued task; returns false otherwise.
    bool MoveUp(std::string_view id);
    bool MoveDown(std::string_view id);
    // Queued tasks only; the running task cannot be removed.
    bool Remove(std::string_view id);
    // Running or waiting task becomes kCancelled, then the queue advances.
    void StopCurrent();
    // Running task becomes kDone or kFailed, then the queue advances. A task that
    // is waiting for the person is not finished here; call OnWaiting(false) first.
    void OnTurnFinished(bool ok);
    // Running ↔ kWaitingForYou. A waiting task blocks the queue until answered.
    void OnWaiting(bool waiting);
    // Plan progress of the running or waiting task.
    void OnProgress(std::size_t done, std::size_t total);
    // Drops kDone, kFailed and kCancelled tasks; live tasks keep their order.
    void ClearFinished();

    [[nodiscard]] const AgentTask* current() const;
    [[nodiscard]] const std::vector<AgentTask>& tasks() const;  // display order
    [[nodiscard]] std::size_t queued_count() const;

  private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    [[nodiscard]] std::size_t ActiveIndex() const;  // running or waiting task, or kNone
    [[nodiscard]] std::size_t IndexOf(std::string_view id) const;
    void Advance();

    StartFn start_;
    std::vector<AgentTask> tasks_;
    std::size_t next_id_ = 1;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_TASK_QUEUE_H_
