#include "session_save_scheduler.h"

#include <algorithm>

namespace island {

SessionSaveScheduler::Clock::time_point SessionSaveScheduler::MarkDirty(Clock::time_point now) {
    if (cancelled_) return now;
    // The first unsaved change starts the max-latency window. A clean state, or one
    // whose last save failed, starts a new window at this change.
    if (!dirty_ || retry_wait_) first_unsaved_change_ = now;
    dirty_ = true;
    retry_wait_ = false;
    last_change_ = now;
    return deadline().value_or(now);
}

bool SessionSaveScheduler::Due(Clock::time_point now) const {
    const std::optional<Clock::time_point> due = deadline();
    return due.has_value() && now >= *due;
}

void SessionSaveScheduler::SaveFinished(bool ok) {
    if (ok) {
        dirty_ = false;
        retry_wait_ = false;
    } else {
        // Stay dirty, but do not make anything due until the next change (no retry loop).
        retry_wait_ = true;
    }
}

void SessionSaveScheduler::Cancel() { cancelled_ = true; }

std::optional<SessionSaveScheduler::Clock::time_point> SessionSaveScheduler::deadline() const {
    if (!dirty_ || retry_wait_ || cancelled_) return std::nullopt;
    return std::min(last_change_ + kDebounce, first_unsaved_change_ + kMaxLatency);
}

}  // namespace island
