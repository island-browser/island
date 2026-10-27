#ifndef ISLAND_SESSION_SAVE_SCHEDULER_H_
#define ISLAND_SESSION_SAVE_SCHEDULER_H_

#include <chrono>
#include <optional>

namespace island {

// Decides when the session file is written (R-S2). It is CEF-free and takes the
// clock as an argument, so the window owns the timer and the tests drive time.
//
// A save is due 2 s after the last change, and at most 10 s after the first change
// that is still unsaved while changes keep arriving. After a save that failed the
// state stays dirty, but nothing is due until the next change, so the owner never
// spins on a failing write. Cancel() ends scheduling for good (used once closing).
//
// Owner loop: MarkDirty() on every change and arm a timer for deadline(); when the
// timer fires and Due(now) is true, save and call SaveFinished(ok).
class SessionSaveScheduler {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kDebounce{2000};
    static constexpr std::chrono::milliseconds kMaxLatency{10000};

    // Records a change at |now|. Returns the time to arm the timer for, or |now| when
    // no save can be due (cancelled). Check deadline() before arming.
    Clock::time_point MarkDirty(Clock::time_point now);

    // True when a save is due at |now|. The owner then saves and calls SaveFinished.
    [[nodiscard]] bool Due(Clock::time_point now) const;

    // Ends one save attempt. |ok| == false keeps the state dirty without a deadline.
    void SaveFinished(bool ok);

    // No deadline is ever due again; MarkDirty becomes a no-op.
    void Cancel();

    [[nodiscard]] bool dirty() const { return dirty_; }

    // The earliest time a save is due, or nullopt when clean, cancelled or after a
    // failed save (until the next change).
    [[nodiscard]] std::optional<Clock::time_point> deadline() const;

  private:
    bool dirty_ = false;
    bool retry_wait_ = false;  // set by a failed save; cleared by the next change
    bool cancelled_ = false;
    Clock::time_point last_change_;
    Clock::time_point first_unsaved_change_;
};

}  // namespace island

#endif  // ISLAND_SESSION_SAVE_SCHEDULER_H_
