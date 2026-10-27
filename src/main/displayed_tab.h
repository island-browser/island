#ifndef ISLAND_DISPLAYED_TAB_H_
#define ISLAND_DISPLAYED_TAB_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "favorite_tabs.h"
#include "tab_id.h"

namespace island {

// The stored split layout. |focused| is supplied separately because Space keeps the focused
// ordinary tab as its active selection.
struct DisplayedSplit {
    TabId first;
    TabId second;

    bool operator==(const DisplayedSplit&) const = default;
};

// The tab identities that occupy the content slot from left to right.
struct DisplayedPanes {
    TabId first;
    std::optional<TabId> second;

    bool operator==(const DisplayedPanes&) const = default;
};

enum class DisplayedSurfaceKind {
    kTab,
    kLocalPage,
};

// CEF-free identity of one surface in the content slot. The slot is part of the identity because
// moving a view between split slots removes and re-adds it.
struct DisplayedSurface {
    DisplayedSurfaceKind kind = DisplayedSurfaceKind::kTab;
    std::uint64_t id = 0;
    bool second_slot = false;

    [[nodiscard]] static DisplayedSurface Tab(TabId tab, bool second_slot = false);
    [[nodiscard]] static DisplayedSurface LocalPage(std::uint64_t page, bool second_slot = false);

    bool operator==(const DisplayedSurface&) const = default;
};

// Current lifecycle facts for a surface the model recorded as attached. BrowserWindow derives
// these from its Tab/LocalPage owners; tests supply them without constructing CEF views.
struct AttachedSurfaceState {
    DisplayedSurface surface;
    bool live = false;
    bool browser_created = false;
    std::optional<std::chrono::steady_clock::time_point> browser_created_at;
};

struct AttachDecision {
    bool can_replace = true;
    bool waiting_for_creation = false;
    std::chrono::milliseconds retry_after{0};

    bool operator==(const AttachDecision&) const = default;
};

// Owns the answer to "what is shown?" and the gate that prevents a newly created CEF view from
// being removed too early. Space remains the source of the stored ordinary selection; callers
// report that selection here and use focused_tab()/panes() for every on-screen projection.
class DisplayedTabModel {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kMinimumAttachedViewAge{100};

    // Reports an explicit ordinary-tab activation. It ends any favorite display.
    void ActivateOrdinary(TabId focused, std::optional<DisplayedSplit> split = std::nullopt);
    // Updates the ordinary layout without changing a favorite already covering it (for example,
    // clearing a split while the favorite is shown).
    void UpdateOrdinaryLayout(TabId focused, std::optional<DisplayedSplit> split = std::nullopt);
    void ClearOrdinary();

    // Covers the currently focused ordinary pane. False when there is no ordinary pane.
    bool ActivateFavorite(FavoriteTabKey key, TabId virtual_tab);
    // Returns true only when a favorite display ended.
    bool DeactivateFavorite();

    [[nodiscard]] std::optional<TabId> focused_tab() const noexcept;
    [[nodiscard]] std::optional<DisplayedPanes> panes() const noexcept;
    [[nodiscard]] bool IsShown(TabId tab) const noexcept;
    [[nodiscard]] const std::optional<FavoriteTabKey>& favorite() const noexcept {
        return favorite_;
    }
    [[nodiscard]] std::optional<TabId> favorite_tab() const noexcept { return favorite_tab_; }

    // Records the identities just attached and advances the generation delayed work must match.
    std::uint64_t DidAttach(std::vector<DisplayedSurface> surfaces);
    [[nodiscard]] std::uint64_t attachment_generation() const noexcept {
        return attachment_generation_;
    }
    [[nodiscard]] const std::vector<DisplayedSurface>& attached_surfaces() const noexcept {
        return attached_surfaces_;
    }

    // A surface that remains in the same slot is never gated. A live surface that leaves must have
    // a created browser at least 100 ms old. |allow_uncreated| is the generation-scoped 2 s
    // fallback; it never bypasses the age rule once a browser exists.
    [[nodiscard]] AttachDecision EvaluateReplacement(
        const std::vector<AttachedSurfaceState>& states,
        const std::vector<DisplayedSurface>& desired, Clock::time_point now,
        bool allow_uncreated) const;

  private:
    std::optional<TabId> ordinary_focused_;
    std::optional<DisplayedSplit> ordinary_split_;
    std::optional<FavoriteTabKey> favorite_;
    std::optional<TabId> favorite_tab_;
    std::optional<TabId> favorite_pane_;
    std::vector<DisplayedSurface> attached_surfaces_;
    std::uint64_t attachment_generation_ = 0;
};

}  // namespace island

#endif  // ISLAND_DISPLAYED_TAB_H_
