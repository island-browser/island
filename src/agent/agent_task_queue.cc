#include "agent_task_queue.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace island::agent {
namespace {

constexpr std::size_t kMaxTitleCodePoints = 60;
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026
constexpr std::string_view kBlank = " \t\r";

bool IsContinuationByte(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }

std::string_view TrimBlank(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlank);
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(kBlank);
    return text.substr(first, last - first + 1);
}

// First non-blank line of the prompt, trimmed, clipped to 60 code points on a
// UTF-8 boundary. Clipping counts code points, not grapheme clusters, so a
// combining sequence at the cut can be split; acceptable for a 60-char title.
std::string MakeTitle(std::string_view prompt) {
    std::string_view line;
    std::string_view rest = prompt;
    while (line.empty() && !rest.empty()) {
        const std::size_t newline = rest.find('\n');
        line = TrimBlank(rest.substr(0, newline));
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
    }
    std::size_t points = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (IsContinuationByte(line[i])) continue;
        if (points == kMaxTitleCodePoints) {
            std::string title(line.substr(0, i));
            title += kEllipsis;
            return title;
        }
        ++points;
    }
    return std::string(line);
}

bool IsFinished(TaskState state) {
    return state == TaskState::kDone || state == TaskState::kFailed ||
           state == TaskState::kCancelled;
}

}  // namespace

AgentTaskQueue::AgentTaskQueue(StartFn start) : start_(std::move(start)) {}

AgentTask AgentTaskQueue::Enqueue(TaskKind kind, std::string prompt, std::string context,
                                  std::int64_t now_ms) {
    AgentTask task;
    task.id = "t" + std::to_string(next_id_++);
    task.kind = kind;
    task.title = MakeTitle(prompt);
    task.prompt = std::move(prompt);
    task.context = std::move(context);
    task.created_ms = now_ms;
    const std::string id = task.id;
    AgentTask fallback = task;
    // Queued tasks sit after the live block and before the finished block.
    tasks_.insert(tasks_.begin() + static_cast<std::ptrdiff_t>(LiveCount()), std::move(task));
    Advance();
    // StartFn may have re-entered the queue, so look the task up by id again.
    // It can only be gone if a re-entrant ClearFinished dropped it; then the
    // copy taken at enqueue time is returned.
    const std::size_t index = IndexOf(id);
    return index == kNone ? fallback : tasks_[index];
}

bool AgentTaskQueue::MoveUp(std::string_view id) {
    const std::size_t i = IndexOf(id);
    if (i == kNone || i == 0) return false;
    if (tasks_[i].state != TaskState::kQueued || tasks_[i - 1].state != TaskState::kQueued) {
        return false;
    }
    std::swap(tasks_[i], tasks_[i - 1]);
    return true;
}

bool AgentTaskQueue::MoveDown(std::string_view id) {
    const std::size_t i = IndexOf(id);
    if (i == kNone || i + 1 >= tasks_.size()) return false;
    if (tasks_[i].state != TaskState::kQueued || tasks_[i + 1].state != TaskState::kQueued) {
        return false;
    }
    std::swap(tasks_[i], tasks_[i + 1]);
    return true;
}

bool AgentTaskQueue::Remove(std::string_view id) {
    const std::size_t i = IndexOf(id);
    if (i == kNone || tasks_[i].state != TaskState::kQueued) return false;
    tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

void AgentTaskQueue::StopCurrent() {
    const std::size_t active = ActiveIndex();
    if (active == kNone) return;
    const std::string id = tasks_[active].id;
    const bool turn_ended = pending_id_ == id;
    // A turn whose end is already recorded has nothing left to wait for.
    if (!turn_ended) stop_latch_ = id;
    Finish(active, TaskState::kCancelled);
}

void AgentTaskQueue::StopCurrentAndAdvanceNow() {
    stop_latch_.clear();
    const std::size_t active = ActiveIndex();
    if (active != kNone) {
        Finish(active, TaskState::kCancelled);
        return;
    }
    Advance();
}

void AgentTaskQueue::OnTurnFinished(std::string_view id, bool ok) {
    if (!stop_latch_.empty() && id == stop_latch_) {
        // The stopped turn has ended; its task keeps kCancelled and the queue moves on.
        stop_latch_.clear();
        Advance();
        return;
    }
    const std::size_t active = ActiveIndex();
    if (active == kNone || tasks_[active].id != id) return;
    if (tasks_[active].state == TaskState::kWaitingForYou) {
        pending_id_ = tasks_[active].id;
        pending_ok_ = ok;
        return;
    }
    Finish(active, ok ? TaskState::kDone : TaskState::kFailed);
}

void AgentTaskQueue::OnWaiting(std::string_view id, bool waiting) {
    const std::size_t active = ActiveIndex();
    if (active == kNone || tasks_[active].id != id) return;
    if (waiting && tasks_[active].state == TaskState::kRunning) {
        tasks_[active].state = TaskState::kWaitingForYou;
    } else if (!waiting && tasks_[active].state == TaskState::kWaitingForYou) {
        if (pending_id_ == tasks_[active].id) {
            Finish(active, pending_ok_ ? TaskState::kDone : TaskState::kFailed);
            return;
        }
        tasks_[active].state = TaskState::kRunning;
    }
}

void AgentTaskQueue::OnProgress(std::string_view id, std::size_t done, std::size_t total) {
    const std::size_t active = ActiveIndex();
    if (active == kNone || tasks_[active].id != id) return;
    tasks_[active].done = done;
    tasks_[active].total = total;
}

void AgentTaskQueue::ClearFinished() {
    std::erase_if(tasks_, [](const AgentTask& task) { return IsFinished(task.state); });
}

const AgentTask* AgentTaskQueue::current() const {
    const std::size_t active = ActiveIndex();
    return active == kNone ? nullptr : &tasks_[active];
}

const std::vector<AgentTask>& AgentTaskQueue::tasks() const { return tasks_; }

std::size_t AgentTaskQueue::queued_count() const {
    return static_cast<std::size_t>(
        std::count_if(tasks_.begin(), tasks_.end(),
                      [](const AgentTask& task) { return task.state == TaskState::kQueued; }));
}

std::size_t AgentTaskQueue::ActiveIndex() const {
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].state == TaskState::kRunning ||
            tasks_[i].state == TaskState::kWaitingForYou) {
            return i;
        }
    }
    return kNone;
}

std::size_t AgentTaskQueue::LiveCount() const {
    return static_cast<std::size_t>(
        std::count_if(tasks_.begin(), tasks_.end(),
                      [](const AgentTask& task) { return !IsFinished(task.state); }));
}

std::size_t AgentTaskQueue::IndexOf(std::string_view id) const {
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].id == id) return i;
    }
    return kNone;
}

void AgentTaskQueue::Finish(std::size_t index, TaskState state) {
    // Live tasks are a contiguous prefix, so after the move the finished block
    // starts right after the remaining live tasks.
    AgentTask task = std::move(tasks_[index]);
    tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(index));
    task.state = state;
    pending_id_.clear();
    pending_ok_ = false;
    tasks_.insert(tasks_.begin() + static_cast<std::ptrdiff_t>(LiveCount()), std::move(task));
    Advance();
}

void AgentTaskQueue::Advance() {
    if (!stop_latch_.empty() || ActiveIndex() != kNone) return;
    for (AgentTask& task : tasks_) {
        if (task.state != TaskState::kQueued) continue;
        task.state = TaskState::kRunning;
        // Copy first: the callback may enqueue or finish tasks, which moves tasks_.
        const AgentTask started = task;
        start_(started);
        return;
    }
}

}  // namespace island::agent
