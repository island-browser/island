#ifndef ISLAND_SPACE_H_
#define ISLAND_SPACE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "design_tokens.h"
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
    Space(SpaceId id, std::string name, SpaceColor color);

    Space(const Space&) = delete;
    Space& operator=(const Space&) = delete;
    Space(Space&&) noexcept = default;
    Space& operator=(Space&&) noexcept = default;
    ~Space() = default;

    [[nodiscard]] SpaceId id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] SpaceColor color() const noexcept;
    void Rename(std::string name);
    void SetColor(SpaceColor color);

    [[nodiscard]] const std::vector<Tab>& tabs() const noexcept;
    [[nodiscard]] std::size_t tab_count() const noexcept;

    // The appended tab becomes the active tab.
    void AppendTab(Tab tab);
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
    // only tab leaves no active tab. Unknown ids remove nothing.
    std::optional<Tab> RemoveTab(TabId id);

    // Reorders; the active tab keeps its identity. Rejects out-of-range positions.
    bool MoveTab(std::size_t from_index, std::size_t to_index);

    // A pairing is accepted only when both ids name different live tabs of this
    // space; a successful SetSplit replaces any previous pairing.
    [[nodiscard]] const std::optional<SplitPairing>& split() const noexcept;
    bool SetSplit(SplitPairing pairing);
    void ClearSplit();

  private:
    void MaintainActiveIndexAfterRemoval(std::size_t removed_index);

    SpaceId id_;
    std::string name_;
    SpaceColor color_;
    std::vector<Tab> tabs_;
    std::size_t active_tab_index_ = 0;
    std::optional<SplitPairing> split_;
};

}  // namespace island

#endif  // ISLAND_SPACE_H_
