#ifndef ISLAND_COMMAND_PALETTE_H_
#define ISLAND_COMMAND_PALETTE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "address_policy.h"
#include "tab_id.h"

namespace island {

// The CEF-free command-palette model behind the Cmd/Ctrl+K overlay. Like
// SearchPaletteModel, it owns all palette state (visibility, draft query,
// highlight, last rejection) and produces plain values the window acts on; the
// CEF overlay view is a projection of it. It deliberately does NOT include
// space.h or tab.h, which pull CEF headers via cef_request_context.h /
// cef_browser.h — the window ingests lightweight snapshot structs instead.

// Read-only snapshot of one open tab of the active space. The window copies
// these out of its Space/Tab objects; identity is the real TabId so a
// selection can be routed back to the exact tab.
struct PaletteTabEntry {
    TabId id;
    std::string title;
    std::string url;

    bool operator==(const PaletteTabEntry&) const = default;
};

// Read-only snapshot of one space in the space list.
struct PaletteSpaceEntry {
    SpaceId id;
    std::string name;

    bool operator==(const PaletteSpaceEntry&) const = default;
};

// Read-only snapshot of one bookmark from the window's bookmark store. No
// identity beyond the URL: selecting one navigates the active tab through the
// single address path, exactly like a kUrl row.
struct PaletteBookmarkEntry {
    std::string title;
    std::string url;

    bool operator==(const PaletteBookmarkEntry&) const = default;
};

// The result kinds the palette can list. Canonical composition order is
// tabs-before-spaces-before-bookmarks, with the kUrl affordance always last.
// kBookmark is appended after kUrl so existing enum values stay stable.
enum class PaletteEntryKind : std::uint8_t {
    kTab,       // activate the tab (in whatever space holds it)
    kSpace,     // make the space the active space
    kUrl,       // hand the raw query to the single address-validation path
    kBookmark,  // navigate the active tab to the bookmark's stored URL
};

// One row of palette results. `tab`, `space`, `url_query`, and `bookmark` are
// meaningful for their kind only; the unused members stay defaulted so rows
// compare and sort as plain values.
struct PaletteEntry {
    PaletteEntryKind kind = PaletteEntryKind::kTab;
    PaletteTabEntry tab;
    PaletteSpaceEntry space;
    // kind == kUrl: the query exactly as typed, unvalidated. Validation happens
    // only at Submit() through the injected validator, mirroring the address
    // bar where invalid text stays editable until Enter.
    std::string url_query;
    PaletteBookmarkEntry bookmark;

    bool operator==(const PaletteEntry&) const = default;
};

// What an accepted palette row does. For kUrl rows `address` is the validator's
// ValidatedAddress — the exact value AddressBarModel::Submit consumes, so a
// palette navigation and an address-bar navigation are indistinguishable
// downstream and there is exactly one URL-validation implementation.
struct PaletteSelection {
    PaletteEntryKind kind = PaletteEntryKind::kTab;
    PaletteTabEntry tab;
    PaletteSpaceEntry space;
    ValidatedAddress address;
    PaletteBookmarkEntry bookmark;

    bool operator==(const PaletteSelection&) const = default;
};

// The one URL-validation seam. The window injects island::ParseAndValidate
// (cef_address_parser.h) at construction — the signature matches that function
// exactly, so the binding is a bare function reference and the palette never
// parses URLs itself.
using PaletteUrlValidator = std::function<ValidatedAddress(std::string_view)>;

// Ranking classes for one candidate string against one query, in decreasing
// quality. Matching folds ASCII case, trims leading/trailing ASCII whitespace
// from the query only, and treats any position whose preceding character is
// not ASCII alphanumeric (spaces, '/', '.', ':', '-', '_' …) as a word
// boundary.
enum class PaletteMatchRank : std::uint8_t {
    kExact = 0,
    kPrefix = 1,
    kWordBoundary = 2,
    kSubsequence = 3,
    kNone = 4,
};

// Classifies one candidate string. The query must be non-empty after trimming
// for a meaningful result; an empty (or all-whitespace) query returns kNone —
// callers list everything in canonical order for that case instead of routing
// it through the ranker.
[[nodiscard]] PaletteMatchRank ClassifyPaletteMatch(std::string_view candidate,
                                                    std::string_view query);

// The deterministic total order used for results:
//   1. Match class: exact > prefix > word-boundary > subsequence. A tab is
//      classified by the better of its title and URL; a space by its name; a
//      bookmark by the better of its title and URL, like a tab.
//   2. Ties inside a class are broken by input order: tabs in SetTabs order
//      first, then spaces in SetSpaces order, then bookmarks in SetBookmarks
//      order.
//   3. A non-empty query appends exactly one kUrl row after all tab, space,
//      and bookmark rows, carrying the raw query text.
//   4. An empty (or all-whitespace) query lists every tab, space, and bookmark
//      in canonical input order and omits the kUrl row.
// Non-matching entries are excluded; the kUrl row is the only unconditional
// row for a non-empty query.
[[nodiscard]] std::vector<PaletteEntry> ComposePaletteResults(
    std::string_view query, const std::vector<PaletteTabEntry>& tabs,
    const std::vector<PaletteSpaceEntry>& spaces);
// Overload for callers with a bookmark store; identical ranking after spaces.
[[nodiscard]] std::vector<PaletteEntry> ComposePaletteResults(
    std::string_view query, const std::vector<PaletteTabEntry>& tabs,
    const std::vector<PaletteSpaceEntry>& spaces,
    const std::vector<PaletteBookmarkEntry>& bookmarks);

class CommandPaletteModel {
  public:
    // The validator must outlive the model and is typically
    // &island::ParseAndValidate; a null validator rejects kUrl submissions
    // without producing an action (defensive — the window always binds one).
    explicit CommandPaletteModel(PaletteUrlValidator validator);

    // Makes the palette visible and resets the draft query, highlight, and any
    // surfaced rejection. Opening an already-visible palette re-resets it,
    // which is what a second Cmd/Ctrl+K should do.
    void Open();

    // The Escape path. Hides the palette and drops any surfaced rejection
    // without producing an action — only Submit() ever produces one. Returns
    // true when it was visible, so the caller can tell a real Escape-dismiss
    // from a no-op.
    bool Close();

    // Ingests read-only snapshots. Both reset the highlight to the first row,
    // because any ranking change invalidates what the old index pointed at.
    // SetTabs receives only the open tabs of the active space; the model never
    // sees space membership and so never filters by it.
    void SetTabs(std::vector<PaletteTabEntry> tabs);
    void SetSpaces(std::vector<PaletteSpaceEntry> spaces);
    void SetBookmarks(std::vector<PaletteBookmarkEntry> bookmarks);

    // Replaces the draft query and resets the highlight to the first row, so
    // narrowing results can never leave the highlight past the last row. A new
    // draft also clears any surfaced rejection, mirroring
    // AddressBarModel::SetEditText.
    void SetQuery(std::string query);

    // Moves the highlight by `delta` rows, wrapping at both ends of the current
    // result list. A no-op when there are no results.
    void MoveHighlight(int delta);

    // Highlights the row at `index` when it exists; out-of-range indices are
    // ignored so a stale row click cannot corrupt the highlight.
    void SetHighlightedIndex(std::size_t index);

    // The ranked result rows for the current query and snapshots. A pure
    // function of state; recomputed on every call.
    [[nodiscard]] std::vector<PaletteEntry> Results() const;

    // Accepts the highlighted row.
    //   - Hidden palette, empty results, or a stale index: no action
    //     (std::nullopt) and no state change.
    //   - kTab / kSpace row: the selection is returned exactly once and the
    //     palette closes, so a repeat Submit() finds it hidden and returns no
    //     action — the same exactly-once consumption AddressBarModel::Submit
    //     gets from its resting state.
    //   - kUrl row: the raw query goes through the injected validator. A
    //     rejection surfaces it in last_rejection(), keeps the palette open,
    //     and consumes nothing (the draft stays editable, like the address
    //     bar's kInvalid mode); a valid address is returned once and the
    //     palette closes.
    [[nodiscard]] std::optional<PaletteSelection> Submit();

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] std::size_t highlighted_index() const noexcept { return highlighted_index_; }
    // The rejection from the most recent invalid kUrl submission, if any.
    // Cleared by Open, Close, and SetQuery.
    [[nodiscard]] const std::optional<AddressError>& last_rejection() const noexcept {
        return last_rejection_;
    }

  private:
    std::vector<PaletteTabEntry> tabs_;
    std::vector<PaletteSpaceEntry> spaces_;
    std::vector<PaletteBookmarkEntry> bookmarks_;
    PaletteUrlValidator validator_;
    std::string query_;
    std::size_t highlighted_index_ = 0;
    bool visible_ = false;
    std::optional<AddressError> last_rejection_;
};

}  // namespace island

#endif  // ISLAND_COMMAND_PALETTE_H_
