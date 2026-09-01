#ifndef ISLAND_SEARCH_PALETTE_MODEL_H_
#define ISLAND_SEARCH_PALETTE_MODEL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "chrome_snapshot.h"
#include "design_tokens.h"
#include "search_provider.h"

namespace island {

// One accepted palette submission: which provider was highlighted, the query
// exactly as typed, and the composed navigation URL. The palette never hands a
// raw CefBrowser anywhere; it produces this value and the window navigates.
struct SearchSubmission {
    SearchProviderId provider;
    std::string query;
    std::string url;

    bool operator==(const SearchSubmission&) const = default;
};

// The CEF-free palette state machine. It owns visibility, the draft query, and
// the highlighted provider row; the CEF view is a projection of it. Keeping it
// free of views code lets the whole palette contract be unit-tested without a
// running CEF message loop.
class SearchPaletteModel {
  public:
    // Makes the palette visible and resets the draft query and the highlight to
    // the first provider. Opening an already-visible palette re-resets it, which
    // is what a second Cmd/Ctrl+K should do.
    void Open();

    // Hides the palette. Returns true when it was visible, so the caller can
    // tell a real Escape-dismiss from a no-op. Never navigates.
    bool Close();

    void SetQuery(std::string query);

    // Moves the highlight by `delta` rows, wrapping at both ends so ArrowUp on
    // the first row lands on the last. The provider list is short and fixed, so
    // wrapping is the least surprising behavior.
    void MoveHighlight(int delta);

    // Highlights the row at `index` when it exists; out-of-range indices are
    // ignored so a stale row click cannot corrupt the highlight.
    void SetHighlightedIndex(std::size_t index);

    // Accepts the current query for the highlighted provider. Returns no value
    // and leaves the palette open when the query is empty or whitespace-only;
    // otherwise composes the URL and hides the palette.
    [[nodiscard]] std::optional<SearchSubmission> Submit();

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] std::size_t highlighted_index() const noexcept { return highlighted_index_; }
    [[nodiscard]] const SearchProvider& highlighted_provider() const noexcept;

  private:
    bool visible_ = false;
    std::string query_;
    std::size_t highlighted_index_ = 0;
};

// Palette surface slots. The palette resolves every one of its fills through
// this pure function so a theme-change re-assertion is deterministic and can be
// unit-tested without a live view tree, mirroring
// BrowserChrome::ChromeSurfaceRole.
enum class PaletteSurfaceSlot : std::uint8_t {
    kPanel,
    kQueryWell,
    kRow,
    kHighlightedRow,
    kBorder,
};

[[nodiscard]] ArgbColor PaletteSurfaceRole(PaletteSurfaceSlot slot, ChromeTheme theme) noexcept;
[[nodiscard]] ArgbColor PaletteSurfaceRoleForTokens(PaletteSurfaceSlot slot,
                                                    const ChromeTokens& tokens) noexcept;

// Preferred palette width in DIP and the top offset from the window's top edge.
// The palette is a centered command-bar card, so it is clamped to the window
// width and never taller than the window.
[[nodiscard]] constexpr int SearchPaletteWidthDip() noexcept { return 560; }
[[nodiscard]] constexpr int SearchPaletteTopOffsetDip() noexcept { return 96; }

// The overlay bounds for a window of `window_bounds`, in window-relative DIP.
// Pure so the placement is testable without a CefWindow.
[[nodiscard]] DipRect SearchPaletteBounds(const DipRect& window_bounds, int content_height) noexcept;

// What a submission should do once the palette has accepted it. Keeping the
// decision in a pure function makes the empty-query rejection and the
// null-active-browser no-op testable without a CefBrowser.
enum class SearchDispatch : std::uint8_t {
    kRejectedEmptyQuery,
    kNoActiveBrowser,
    kNavigate,
};

struct SearchDispatchDecision {
    SearchDispatch dispatch = SearchDispatch::kRejectedEmptyQuery;
    // Only populated when dispatch is kNavigate.
    std::string url;

    bool operator==(const SearchDispatchDecision&) const = default;
};

[[nodiscard]] SearchDispatchDecision DecideSearchDispatch(std::string_view query,
                                                          SearchProviderId provider,
                                                          bool has_active_browser);

// The accessible name for one provider row, matching the Phase 2 rail
// accessibility bar: every row states the action it performs.
[[nodiscard]] std::string SearchProviderRowAccessibleName(const SearchProvider& provider,
                                                          std::string_view query);

}  // namespace island

#endif  // ISLAND_SEARCH_PALETTE_MODEL_H_
