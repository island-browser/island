#ifndef ISLAND_SPACE_H_
#define ISLAND_SPACE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "design_tokens.h"
#include "include/cef_request_context.h"
#include "tab.h"
#include "tab_id.h"

namespace island {

using SpaceColor = ArgbColor;

// The two tabs rendered side by side while a split is active. Members identify
// tabs by TabId, never by vector position, so a split survives reordering and
// stays valid until a split member is closed or the pairing is cleared.
struct SplitPairing {
    TabId first;
    TabId second;

    bool operator==(const SplitPairing&) const = default;
};

// A named, colored space owning an ordered collection of Tabs, an active-tab
// selection, and an optional split pairing. CEF-free: U2 adds the space's
// CefRequestContext member (created once at space creation, held for the
// space's lifetime) without changing these contracts. A space is move-only
// because its Tabs are move-only.
class Space {
  public:
    Space(SpaceId id, std::string name, SpaceColor color, std::string icon_id = {});

    Space(const Space&) = delete;
    Space& operator=(const Space&) = delete;
    Space(Space&&) noexcept = default;
    Space& operator=(Space&&) noexcept = default;
    ~Space() = default;

    [[nodiscard]] SpaceId id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] SpaceColor color() const noexcept;
    [[nodiscard]] const std::string& icon_id() const noexcept;
    void Rename(std::string name);
    void SetColor(SpaceColor color);
    void SetIconId(std::string icon_id);

    // Host patterns that open tabs in this space (R-R1). Normalized by
    // NormalizeRoutePattern (space_routing.h); the space only stores them.
    [[nodiscard]] const std::vector<std::string>& routes() const noexcept;
    void SetRoutes(std::vector<std::string> routes);

    [[nodiscard]] const std::vector<Tab>& tabs() const noexcept;
    [[nodiscard]] std::vector<Tab>& tabs() noexcept;
    [[nodiscard]] std::size_t tab_count() const noexcept;

    // The appended tab becomes the active tab.
    void AppendTab(Tab tab);
    // Inserts an ordinary (ungrouped) tab right after the active tab's group: after the active
    // tab, or after its parent and that parent's children when the active tab is grouped. A
    // pinned group is never split, so the tab lands after it at the earliest. The tab becomes
    // active only when `activate` is true; otherwise the active tab stays selected.
    TabId InsertTabAfterActive(Tab tab, bool activate);
    // Inserts a one-level grouped tab immediately after the parent's existing children. The tab
    // becomes active only when `activate` is true; otherwise the active tab stays selected. A
    // pinned parent's children never split the pinned group: they start the unpinned tabs, right
    // after the pinned group. An unknown parent falls back to a regular append.
    [[nodiscard]] TabId AppendChildTab(TabId parent, Tab tab, bool activate = true);
    // Keeps the groups well formed. A tab whose parent is missing (or is itself a child) or that
    // is pinned is ungrouped, and so is the `moved` tab (the one the person just dragged) when it
    // no longer follows its parent or a sibling. The other children are re-placed right after
    // their parent, or after the pinned group when the parent is pinned. Moves, pinning and
    // session restore run it so an indent cannot drift away.
    void NormalizeGroups(std::optional<TabId> moved = std::nullopt);
    [[nodiscard]] Tab* FindTab(TabId id);
    [[nodiscard]] const Tab* FindTab(TabId id) const;
    [[nodiscard]] std::optional<std::size_t> IndexOfTab(TabId id) const;

    [[nodiscard]] bool has_active_tab() const noexcept;
    // Valid only while has_active_tab().
    [[nodiscard]] std::size_t active_tab_index() const noexcept;
    [[nodiscard]] TabId active_tab_id() const;
    bool SelectTab(TabId id);
    bool SelectTabIndex(std::size_t index);
    // Wrap around; both are no-ops with fewer than two tabs.
    void SelectNextTab();
    void SelectPreviousTab();

    // Returns the removed tab and keeps the active-tab invariant: removing a tab
    // before the active one keeps the same active tab; removing the active tab
    // selects its right neighbor, or the left neighbor at the end; removing the
    // only tab leaves no active tab. Removing a group parent ungroups its children; closing one
    // child leaves the rest of the group intact. Unknown ids remove nothing.
    std::optional<Tab> RemoveTab(TabId id);

    // Reorders; the active tab keeps its identity. A drop inside a child run lands after the
    // complete group. Returns true only when the tab's final position differs from its start;
    // out-of-range and normalized no-op moves return false.
    bool MoveTab(std::size_t from_index, std::size_t to_index);

    // Pins or unpins a tab and keeps the pinned group contiguous at the front:
    // a newly pinned tab joins the end of the pinned group, an unpinned one
    // becomes the first regular tab. The active tab keeps its identity.
    // Unknown ids return false.
    bool SetTabPinned(TabId id, bool pinned);
    [[nodiscard]] std::size_t pinned_count() const noexcept;

    // A pairing is accepted only when both ids name different live tabs of this
    // space; a successful SetSplit replaces any previous pairing.
    [[nodiscard]] const std::optional<SplitPairing>& split() const noexcept;
    bool SetSplit(SplitPairing pairing);
    void ClearSplit();

    // U2: returns the space's CefRequestContext, creating it lazily on first access.
    [[nodiscard]] CefRefPtr<CefRequestContext> request_context() const noexcept;

    // The color mode the space's pages see (prefers-color-scheme and scrollbars). Applied to the
    // context now when it exists, and when it is created.
    void SetWebColorScheme(cef_color_variant_t scheme);

  private:
    void CreateRequestContextIfNeeded(const std::string& cache_path) const;
    void MaintainActiveIndexAfterRemoval(std::size_t removed_index);

    SpaceId id_;
    std::string name_;
    SpaceColor color_;
    std::string icon_id_;
    std::vector<std::string> routes_;
    std::vector<Tab> tabs_;
    std::size_t active_tab_index_ = 0;
    std::optional<SplitPairing> split_;
    mutable CefRefPtr<CefRequestContext> request_context_;
    cef_color_variant_t web_color_scheme_ = CEF_COLOR_VARIANT_SYSTEM;
};

}  // namespace island

#endif  // ISLAND_SPACE_H_
