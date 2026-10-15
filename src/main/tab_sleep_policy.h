#ifndef ISLAND_MAIN_TAB_SLEEP_POLICY_H_
#define ISLAND_MAIN_TAB_SLEEP_POLICY_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "tab_id.h"

namespace island {

// The Settings choices for tab sleep, in minutes; 0 means never. Anything else read from
// prefs falls back to the default.
inline constexpr int kTabSleepDefaultMinutes = 30;

[[nodiscard]] inline bool IsValidTabSleepMinutes(int minutes) noexcept {
    return minutes == 0 || minutes == 15 || minutes == 30 || minutes == 60;
}

[[nodiscard]] inline int NormalizeTabSleepMinutes(int minutes) noexcept {
    return IsValidTabSleepMinutes(minutes) ? minutes : kTabSleepDefaultMinutes;
}

// What the window knows about one tab at decision time. The policy never sees CEF objects:
// the caller fills in the exclusions (the page-side ones, `playing` and `dirty`, come from a
// probe and may be false when not yet probed; the caller probes the candidates before
// closing).
struct TabSleepInput {
    TabId id;
    bool active = false;  // selected in the active space
    bool pinned = false;
    bool split_partner = false;  // member of any space's split pairing
    bool sleeping = false;       // already asleep (no browser)
    bool has_browser = false;
    bool playing_audio = false;
    bool unsaved_input = false;
    // Local pages (Settings, All tabs, start page) hold no state worth waking.
    bool web_page = true;
};

// CEF-free, clock-injected tab sleep decisions. Last-active times are recorded by the window
// when a tab is selected; a tab never selected since the policy learned of it counts from the
// moment it was first seen, so a fresh session does not put every restored tab to sleep at once.
class TabSleepPolicy {
  public:
    using Clock = std::chrono::steady_clock;

    explicit TabSleepPolicy(int minutes = kTabSleepDefaultMinutes) noexcept
        : minutes_(NormalizeTabSleepMinutes(minutes)) {}

    void SetMinutes(int minutes) noexcept { minutes_ = NormalizeTabSleepMinutes(minutes); }
    [[nodiscard]] int minutes() const noexcept { return minutes_; }
    [[nodiscard]] bool enabled() const noexcept { return minutes_ > 0; }
    [[nodiscard]] Clock::duration timeout() const noexcept {
        return std::chrono::minutes(minutes_);
    }

    // The tab was used (selected, or shown in a split) at |now|.
    void Touch(TabId id, Clock::time_point now) { last_active_[id.value] = now; }
    // Remember a tab the policy has not seen yet (no-op when already known).
    void Observe(TabId id, Clock::time_point now) { last_active_.try_emplace(id.value, now); }
    void Forget(TabId id) { last_active_.erase(id.value); }

    [[nodiscard]] bool Eligible(const TabSleepInput& tab) const noexcept {
        return tab.has_browser && !tab.sleeping && tab.web_page && !tab.active && !tab.pinned &&
               !tab.split_partner && !tab.playing_audio && !tab.unsaved_input;
    }

    // Tabs idle for at least the timeout and not excluded; empty when sleep is off.
    [[nodiscard]] std::vector<TabId> Expired(const std::vector<TabSleepInput>& tabs,
                                             Clock::time_point now) const {
        std::vector<TabId> result;
        if (!enabled()) {
            return result;
        }
        for (const TabSleepInput& tab : tabs) {
            const auto found = last_active_.find(tab.id.value);
            if (found == last_active_.end() || !Eligible(tab)) {
                continue;
            }
            if (now - found->second >= timeout()) {
                result.push_back(tab.id);
            }
        }
        return result;
    }

    // "Sleep inactive tabs now": the same exclusions, no timeout, and it works with the
    // timed setting on Never.
    [[nodiscard]] std::vector<TabId> AllEligible(const std::vector<TabSleepInput>& tabs) const {
        std::vector<TabId> result;
        for (const TabSleepInput& tab : tabs) {
            if (Eligible(tab)) {
                result.push_back(tab.id);
            }
        }
        return result;
    }

  private:
    int minutes_;
    std::unordered_map<decltype(TabId::value), Clock::time_point> last_active_;
};

}  // namespace island

#endif  // ISLAND_MAIN_TAB_SLEEP_POLICY_H_
