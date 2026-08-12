# Island Browser — Sidebar/Palette Implementation Plan

> **Status:** Approved execution plan. It implements only the sidebar/palette design in
> [the canonical specification](../specs/2026-08-12-island-sidebar-palette-design.md).

**Goal:** Give Island an Arc-like left sidebar that hides to a window-edge sliver and reveals on
hover (macOS) or `Cmd/Ctrl+B` (everywhere), plus a `Cmd/Ctrl+K` search palette whose providers
(ChatGPT, Perplexity, Claude, Gemini, Google) navigate the active tab to a prefilled query URL
through plain `CefFrame::LoadURL` — without breaking any Phase 1/2 contract or merged Phase 3 unit
(one `CefWindow`, popup rejection, close lifecycle, port `9222`, data-only startup, the
`1001–1027` `ChromeViewId` set, and the fixed-or-collection view-tree contract).

**Implementation rule:** Work in the stated dependency order. An implementer owns only the files in
their unit. Do not modify `browser.pen`, Phase 1/2/3 documents, unrelated docs, or another unit's
files. No unit may introduce a second top-level `CefWindow`, an API key/token, a provider account,
web content inside the palette, layout-animation of the sidebar, or any other spec non-goal.
`src/main/browser_window.h`/`.cc` are concurrently being restructured by the in-flight Phase 3 U2:
units that must touch them (U2, U3, U4) confine their diffs to the explicitly shared blocks named
below and rebase on the current head before committing — they never rewrite surrounding in-flight
code.

## Shared acceptance contract

- Exactly one `CefWindow` exists at all times; the sidebar sliver and the palette are window
  overlays created via `window->AddOverlayView(view, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/...)`
  and positioned through `CefOverlayController` — never child panels re-parented into the rail
  tree, never separate windows.
- `ChromeViewId` values `1001–1018` (Phase 2) and `1019–1027` (Phase 3 collections) are hard
  invariants; new IDs are allocated exactly as the spec tables them (1028 sliver, 1029–1032
  palette) and never renumber existing ones. `ViewTreeContract()` stays byte-identical; overlays
  are not tree nodes.
- `PercentEncodeQuery` (spec §Percent-encoding rules) is the only percent-encoder in the project;
  provider submission composes exactly `base + "?q=" + PercentEncodeQuery(query)` and hands it to
  `CefFrame::LoadURL`. No other code path builds provider URLs.
- Palette submission targets the active tab's `CefBrowser` through the `ActiveTabProvider` seam
  only — it works now against `BrowserWindow::browser_` and keeps working after Phase 3 U2 lands
  (the seam's implementation, not the palette, changes).
- The palette traps focus while visible, `Escape` hides it without navigation and restores focus,
  and `Cmd/Ctrl+B` toggles the sidebar on every platform (the hover seam is an enhancement, not an
  accessibility dependency).
- Startup remains data-only: no provider URL is fetched before an explicit user submission;
  palette open/close prewarm nothing.

## Units

### U1 — PercentEncodeQuery, provider table, and URL composition

**Owner:** U1 implementer
**Files:** `src/main/search_provider.h`, `src/main/search_provider.cc`,
`tests/search_provider_test.cpp`, `src/main/CMakeLists.txt`, `tests/CMakeLists.txt`
**Depends on:** none

Define `island::PercentEncodeQuery` exactly per the spec (RFC 3986 unreserved passthrough, every
other UTF-8 byte as `%XX` uppercase hex, deterministic, total) and the static, ordered five-entry
provider table (ChatGPT, Perplexity, Claude, Gemini, Google — display name and HTTPS base URL per
the spec table, in that order), plus
`ComposeSearchUrl(provider, query) = base + "?q=" + PercentEncodeQuery(query)`. Keep the unit
entirely CEF-free so it builds in `island_tests` like `app_resources_test` does (compile the `.cc`
directly into the test target). Test: unreserved passthrough; exact expected encodings for spaces,
reserved characters, and multibyte UTF-8; empty input; the five composed URLs byte-for-byte against
the spec's bases for a fixed mixed query; provider order and count.

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R SearchProvider --output-on-failure`.

### U2 — Palette overlay shell, Cmd/Ctrl+K accelerator, focus trap

**Owner:** U2 implementer
**Files:** `src/main/search_palette.h`, `src/main/search_palette.cc`,
`src/main/browser_window.h`, `src/main/browser_window.cc`, `src/main/browser_chrome.h`,
`tests/search_palette_test.cpp`, `src/main/CMakeLists.txt`, `tests/CMakeLists.txt`
**Depends on:** U1

Build the palette as a window-owned overlay: a panel with the query `CefTextfield` and one row per
provider from U1's table (`kSearchPalette=1029`, `kSearchPaletteQuery=1030`,
`kSearchPaletteProvider=1031`, `kSearchPaletteProviderName=1032` added to the `ChromeViewId` enum
after 1027). Create it lazily on first `Cmd/Ctrl+K` via
`window_->AddOverlayView(palette, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true)`, size/position
it through the returned `CefOverlayController`, and hide (never destroy) it on `Escape` or after
submission. Register `kOpenPaletteAccelerator` in the `browser_window.h` `AcceleratorId` enum with
`window_->SetAccelerator(kOpenPaletteAccelerator, 'K', false, true, false, /*high_priority=*/true)`
and dispatch it in `BrowserWindow::OnAccelerator` — the shared block is exactly that enum entry,
the registration line, and the `OnAccelerator` case. Implement the focus trap: query field focuses
on open, `Tab` cycles within the palette only, arrow keys move the highlighted provider keying off
`CefTextfieldDelegate::OnKeyEvent` the way `BrowserChrome::HandleAddressKeyEvent` does, `Escape`
restores focus to the invocation point. This unit stubs submission to record `(provider, query)`
pairs only — no navigation yet (U3 wires it). Test palette model behavior (create-once, hide on
Escape, recorded submission pair, empty-query rejection, focus-state bookkeeping) in CEF-free model
tests; assert `ViewTreeContract()` is unchanged and the integration contract test stays green.

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R '(SearchPalette|BrowserChrome)' --output-on-failure`.

### U3 — Provider navigation wiring via ActiveTabProvider

**Owner:** U3 implementer
**Files:** `src/main/active_tab_provider.h`, `src/main/browser_window.h`, `src/main/browser_window.cc`,
`src/main/search_palette.h`, `src/main/search_palette.cc`, `tests/search_palette_test.cpp`,
`src/main/CMakeLists.txt`, `tests/CMakeLists.txt`
**Depends on:** U2

Define the `ActiveTabProvider` interface (one method returning the active tab's `CefBrowser`,
nullable) in its own header. `BrowserWindow` implements it: from its single `browser_` member while
the Phase 3 U2 restructuring is unmerged, from the active space's active tab once it lands — the
implementation swaps as one line, the palette never changes. Wire
`BrowserWindow::SubmitSearchQuery(query, provider)`: reject empty/whitespace queries (palette stays
open), treat a null `ActiveBrowser()` as a defined no-op, otherwise
`browser->GetMainFrame()->LoadURL(ComposeSearchUrl(provider, query))` and hide the palette. The
shared `browser_window` block is exactly the `ActiveTabProvider` override, the
`SubmitSearchQuery` method, and the palette's wiring call. Test: null-browser no-op, empty-query
rejection, and that a recorded submission matches the byte-exact composed URL from U1 (navigation
itself is manual evidence in U7; these tests verify composition and dispatch selection, not
network behavior).

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R 'SearchPalette' --output-on-failure`.

### U4 — macOS hover seam and Cmd/Ctrl+B sidebar toggle

**Owner:** U4 implementer
**Files:** `src/main/sidebar_state.h`, `src/main/macos/sidebar_hover_mac.mm`,
`src/main/browser_chrome.h`, `src/main/browser_window.h`, `src/main/browser_window.cc`,
`src/main/CMakeLists.txt`, `tests/sidebar_state_test.cpp`, `tests/CMakeLists.txt`
**Depends on:** U2

Factor the reveal/hide state machine into CEF-free `sidebar_state.h`: hidden / hover-revealed /
toggle-pinned, with the spec's precedence (toggle state wins over hover; hover only applies to the
unpinned hidden state) and the 12-DIP reveal / 16-DIP grace bands as plain constants. Implement
the macOS seam in `sidebar_hover_mac.mm`: an `NSTrackingArea`/`NSEvent` watch on the CEF window's
`NSView`, posting `BrowserWindow::SetSidebarRevealed(bool)` onto the CEF UI thread — all chrome
mutation stays on CEF-UI, the seam only observes. Add `kHoverSliver=1028` to the `ChromeViewId`
enum, render the hidden state as 0-DIP rail width plus a visible 1–2-DIP sliver window overlay
(raised z-order via `CefOverlayController`), and revealed state as the existing 286-DIP rail — a
width change routed through the existing `LayoutForBounds()`/`RootPanelDelegate` layout path, no
second layout implementation, no animation. Register `kToggleSidebarAccelerator`
(`window_->SetAccelerator(kToggleSidebarAccelerator, 'B', false, true, false, /*high_priority=*/true)`)
and dispatch it in `OnAccelerator`; the shared `browser_window` block is the enum entry, the
registration line, the `OnAccelerator` case, `sidebar_revealed_` state, and the seam install hook
in `OnWindowCreated`. Add the `.mm` under the existing `APPLE` branch of `src/main/CMakeLists.txt`.
Test the state machine exhaustively in `island_tests` (toggle precedence, hover enter/leave with
bands, idempotent repeated hover); manual hover verification against the running app is U7's
checklist, not this unit's claim.

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R '(SidebarState|BrowserChrome)' --output-on-failure`.

### U5 — Windows/Linux hover seams: explicit deferral

**Owner:** U5 implementer
**Files:** none (deferral unit)
**Depends on:** U4

Record, not implement, the remaining seams: the spec's boundary contract (`SetSidebarRevealed(bool)`
plus a per-OS install hook) is the fixed interface, and Windows/Linux get no native hover seam in
this feature. `Cmd/Ctrl+B` already covers those platforms through U4's shared state machine — once
U4 lands, the rail defaults to hidden on Windows/Linux exactly as on macOS and toggles identically
everywhere; only hover differs. This unit's deliverable is the U7 checklist rows marking
Windows/Linux as "toggle-only, hover deferred" so the evidence posture never overstates platform
parity.

**Tests:** none (no code change); verified as part of U7's checklist completeness.

### U6 — Palette OnThemeChanged hardening

**Owner:** U6 implementer
**Files:** `src/main/search_palette.cc`, `tests/search_palette_test.cpp`
**Depends on:** U3

`CefView::SetBackgroundColor` is reset asynchronously by the window's `ThemeChanged()` cascade, so
the palette re-asserts its surface backgrounds when its root panel delegate's `OnThemeChanged`
fires — the exact pattern PR #10's `SurfacePanelDelegate::OnThemeChanged` re-assertion added to
`browser_chrome.cc`, applied to the palette's own panel delegate. Palette surfaces resolve through
the same `ChromeTokens::ForTheme` palette the rail uses (spec §Acceptance req. 8); add a pure
palette-surface-role function mirroring `ChromeSurfaceRoleForResolvedTokens` and unit-test that each
palette slot resolves from tokens alone (so a re-assertion is deterministic), plus a test that the
delegate override invokes the re-assertion path for both themes.

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R SearchPalette --output-on-failure`.

### U7 — CI and QA evidence

**Owner:** U7 implementer
**Files:** `docs/sidebar-palette-visual-acceptance.md`,
`tests/manual/sidebar_palette_checklist.md`
**Depends on:** U1–U6

Confirm the existing native CI matrix and package checks still pass with U1–U6 landed (no new
target platforms, no new port, no signing/notarization claims). Write the manual checklist in the
same honesty posture as `docs/phase3-visual-acceptance.md` /
`tests/manual/phase3_chrome_checklist.md`: hover reveal/hide with the 12/16-DIP bands on macOS arm64;
`Cmd/Ctrl+B` toggle on every platform row (Windows/Linux rows marked toggle-only per U5);
`Cmd/Ctrl+K` palette open with focus trap, arrow-key provider selection, `Escape` with focus
restore and no navigation; one real submission per provider against the live URL; empty-query
rejection; and a light/dark theme switch with the palette open to catch theme-reset drift. Do not
record results as passing without an actual run; leave the sign-off table empty until someone
executes it.

**Tests:** the same clean-checkout sequence as Phase 3's final unit, plus
`ctest --test-dir build -R '(SearchProvider|SearchPalette|SidebarState|BrowserChrome)' --output-on-failure`.

## Dependencies and handoff order

```text
U1 ── U2 ─┬─ U3 ── U6 ─┐
          └─ U4 ── U5 ─┴─ U7
```

U1 has no dependencies. U2 (palette shell) depends on U1 and takes the first `browser_window`
shared block. U3 (provider navigation) and U4 (hover seam + toggle) both depend on U2 and may run
in parallel *only* when no in-flight Phase 3 U2 rebase is outstanding on `browser_window`; their
`browser_window` blocks are disjoint (submission method vs. sidebar state), but each implementer
rebases on the current head before committing. U5 is a deferral record depending on U4's shared
state machine. U6 touches palette files only and depends on U3 so the two palette edits never
race. U7 closes the chain and is the only evidence-gathering unit.

## Per-implementer commit policy

Each implementer creates commits only for their owned files after focused checks pass. Keep an
implementation file and its direct tests in the same commit; do not stage generated CEF/fonts/icon
output, `browser.pen`, tool state, or another implementer's files. Use the repository's plain
imperative English style. A unit may use multiple atomic commits when its owned changes are
independently reversible; each commit includes the required project attribution footer and co-author
trailer. No implementer pushes or rewrites history as part of this plan.
