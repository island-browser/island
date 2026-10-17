#include "agent_task_queue.h"

#include <algorithm>
#include <utility>

namespace island::agent {
namespace {

constexpr std::size_t kMaxTitleCodePoints = 60;
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026

bool IsContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

// First line of the prompt, clipped to 60 code points on a UTF-8 boundary.
std::string MakeTitle(std::string_view prompt) {
    std::string_view line = prompt.substr(0, prompt.find('\n'));
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
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

const AgentTask& AgentTaskQueue::Enqueue(TaskKind kind, std::string prompt, std::string context,
                                         std::int64_t now_ms) {
    AgentTask task;
    task.id = "t" + std::to_string(next_id_++);
    task.kind = kind;
    task.title = MakeTitle(prompt);
    task.prompt = std::move(prompt);
    task.context = std::move(context);
    task.created_ms = now_ms;
    tasks_.push_back(std::move(task));
    const std::size_t index = tasks_.size() - 1;
    Advance();
    return tasks_[index];
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
    tasks_[active].state = TaskState::kCancelled;
    Advance();
}

void AgentTaskQueue::OnTurnFinished(bool ok) {
    const std::size_t active = ActiveIndex();
    if (active == kNone || tasks_[active].state != TaskState::kRunning) return;
    tasks_[active].state = ok ? TaskState::kDone : TaskState::kFailed;
    Advance();
}

void AgentTaskQueue::OnWaiting(bool waiting) {
    const std::size_t active = ActiveIndex();
    if (active == kNone) return;
    AgentTask& task = tasks_[active];
    if (waiting && task.state == TaskState::kRunning) {
        task.state = TaskState::kWaitingForYou;
    } else if (!waiting && task.state == TaskState::kWaitingForYou) {
        task.state = TaskState::kRunning;
    }
}

void AgentTaskQueue::OnProgress(std::size_t done, std::size_t total) {
    const std::size_t active = ActiveIndex();
    if (active == kNone) return;
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

const std::vector<AgentTask>& AgentTaskQueue::tasks() const {
    return tasks_;
}

std::size_t AgentTaskQueue::queued_count() const {
    return static_cast<std::size_t>(
        std::count_if(tasks_.begin(), tasks_.end(), [](const AgentTask& task) {
            return task.state == TaskState::kQueued;
        }));
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

std::size_t AgentTaskQueue::IndexOf(std::string_view id) const {
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].id == id) return i;
    }
    return kNone;
}

void AgentTaskQueue::Advance() {
    if (ActiveIndex() != kNone) return;
    for (AgentTask& task : tasks_) {
        if (task.state != TaskState::kQueued) continue;
        task.state = TaskState::kRunning;
        // Copy first: the callback may enqueue, which can reallocate tasks_.
        const AgentTask started = task;
        start_(started);
        return;
    }
}

}  // namespace island::agent
