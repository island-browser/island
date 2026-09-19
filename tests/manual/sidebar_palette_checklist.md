# Sidebar/palette manual acceptance checklist

This is a template for the manual verification pass required by
[Unit U7 of the sidebar/palette implementation plan](../../docs/superpowers/plans/2026-08-12-island-sidebar-palette.md).
It cannot be completed from an automated/headless environment: every row below requires a human at
a real display, in an interactive session, on the target OS. Run it on macOS arm64 first (the only
target with local build/run evidence per `docs/supported-platforms.md`); repeat on other targets as
their native CI evidence lands.

Record the actual OS build/version, screen scale, and date at the top of each run. Attach
screenshots to `docs/sidebar-palette-visual-acceptance.md` (or the PR) as you go rather than only
at the end, so a partial run still leaves usable evidence.

## Accelerator deviation

The Phase 3 design fixes `Cmd/Ctrl+K` on the **command palette**; the sidebar/palette design
originally assigned `Cmd/Ctrl+K` to the **search palette**. As implemented (commit `c63641e`), the
command palette owns `Cmd/Ctrl+K` and the search palette uses **`Cmd/Ctrl+Shift+K`**. On macOS the
`NSMenu` owns these key equivalents because `CefWindow::SetAccelerator` never dispatches there, so
the menu wiring is part of what makes the shortcuts work. Every palette row below uses
`Cmd/Ctrl+Shift+K` for the search palette.

## Setup

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
open build/src/main/island_browser.app
```

The sidebar starts hidden (0 DIP rail plus the edge sliver). If the rail is permanently visible or
`Cmd/Ctrl+B` does nothing, the feature is not wired in this build; stop and fix before recording
any results.

## Sidebar hover-reveal (macOS arm64 only)

- [ ] **Hidden default state:** on launch, the rail occupies 0 DIP of layout width — content starts
      at the window's left edge — and a visible 1–2 DIP vertical sliver marks the left edge. Confirm
      the sliver exists with a screenshot, not just the absence of the rail.
- [ ] **Reveal band (12 DIP):** with the sidebar hidden, move the cursor into the leftmost 12-DIP
      band of the window. The rail reveals immediately to its full 286 DIP and the sliver hides.
      The transition is instantaneous — no slide, fade, or animated width change.
- [ ] **Grace band (16 DIP):** with the rail hover-revealed, move the cursor away from the rail.
      It stays revealed while the cursor is within the 16-DIP grace band past the rail's edge, and
      hides only once the cursor leaves that band. Document the measured behavior.
- [ ] **Hover idempotence:** cross the reveal and grace bands repeatedly (enter, leave, enter). The
      rail reveals and hides each time with no flicker, no missed transition, and no stuck state.
- [ ] **Revealed layout:** while hover-revealed, the rail is the standard 286 DIP Phase 2 rail
      (window controls, navigation buttons, address control, tab strip, space switcher) — the
      reveal changes width only, not the rail's internal structure.

## Cmd/Ctrl+B toggle (all platforms)

- [ ] **Toggle reveal (macOS: Cmd+B):** press Cmd+B while the sidebar is hidden. The rail reveals
      and stays revealed (pinned) even when the cursor leaves the window entirely.
- [ ] **Toggle hide:** press Cmd+B again. The rail hides back to the 0-DIP-plus-sliver state.
- [ ] **Toggle precedence over hover:** with a pinned reveal, hovering outside the rail must NOT
      hide it; with the sidebar hidden by toggle, moving the cursor into the 12-DIP band must NOT
      reveal it. Hover only applies to the unpinned hidden state.
- [ ] **Repeated presses are idempotent:** toggling quickly several times in a row leaves the
      sidebar in a consistent state matching the last press.
- [ ] **Windows/Linux rows — toggle-only, hover deferred:** repeat the toggle rows on
      `windows64`/`windowsarm64`/`linux64`/`linuxarm64` builds (Ctrl+B). Hover reveal/hide is a
      macOS-only feature (plan unit U5 deferral): mark the hover section N/A on those targets and
      record "toggle-only, hover deferred" in the notes. No native CI evidence covers Phase 3-era
      code on those targets yet (`docs/supported-platforms.md`); do not claim parity from a macOS
      run.

## Search palette — open, navigate, submit (all platforms)

- [ ] **Open the palette:** press Cmd/Ctrl+Shift+K. On the first press the palette is created
      lazily; afterwards it is shown again, never recreated or destroyed. The query field is
      focused and five provider rows are listed in this exact order: ChatGPT, Perplexity, Claude,
      Gemini, Google.
- [ ] **Focus trap:** while the palette is open, Tab cycles only within the palette (query field,
      provider rows) and never escapes into the rail or the web content.
- [ ] **Arrow-key selection:** ArrowUp/ArrowDown move the highlighted provider row. Document
      whether selection wraps at the ends and which row is highlighted on open.
- [ ] **Enter submits the highlighted provider:** pressing Enter navigates the active tab to that
      provider's query URL (see the table below) and hides the palette.
- [ ] **Escape:** the palette hides, nothing navigates, and focus returns to the invocation point
      (the web content, by default).
- [ ] **Re-opening state:** after Escape or a submission, press Cmd/Ctrl+Shift+K again. Document
      whether the previous query text is preserved or cleared.
- [ ] **Command palette separation:** pressing Cmd/Ctrl+K must open the command palette, NOT the
      search palette. If the search palette appears on Cmd/Ctrl+K, the accelerator deviation
      resolution has regressed — record it as a failure.

## Per-provider submission (macOS arm64 first; live network required)

The palette performs exactly one validation — empty/whitespace-only queries are rejected — and
composes exactly `base + "?q=" + PercentEncodeQuery(query)` (RFC 3986 unreserved passthrough, every
other UTF-8 byte as uppercase `%XX`, spaces as `%20`). Expected bases, in palette order:

| Provider | Expected base URL |
| --- | --- |
| ChatGPT | `https://chatgpt.com/?q=` |
| Perplexity | `https://www.perplexity.ai/search?q=` |
| Claude | `https://claude.ai/new?q=` |
| Gemini | `https://gemini.google.com/app?q=` |
| Google | `https://www.google.com/search?q=` |

For each row: focus the query field, type `island browser test`, select the provider, press Enter.

- [ ] **ChatGPT:** active tab navigates to `https://chatgpt.com/?q=island%20browser%20test`; palette
      hides.
- [ ] **Perplexity:** active tab navigates to
      `https://www.perplexity.ai/search?q=island%20browser%20test`; palette hides.
- [ ] **Claude:** active tab navigates to `https://claude.ai/new?q=island%20browser%20test`;
      palette hides.
- [ ] **Gemini:** active tab navigates to
      `https://gemini.google.com/app?q=island%20browser%20test`; palette hides.
- [ ] **Google:** active tab navigates to
      `https://www.google.com/search?q=island%20browser%20test`; palette hides.
- [ ] **Multibyte and reserved characters:** submit `héllo / world?` on one provider and verify the
      address bar shows the percent-encoded form with uppercase hex (`%C3%A9`, `%2F`, `%3F`) and
      `%20` for spaces — never `+`, never lowercase hex.
- [ ] **Empty query:** press Enter with an empty or whitespace-only query: no navigation occurs and
      the palette stays open.
- [ ] **Provider rows announce:** via the OS accessibility inspector, each provider row exposes an
      accessible name of the form "Search with <provider> for <query>".

## Theme switch with the palette open

- [ ] **Light→dark with palette open:** switch the OS to dark mode while the palette is visible.
      The palette's surfaces re-assert their token backgrounds — no stale light-theme rectangles,
      no unreadable text, no flicker. Screenshot.
- [ ] **Dark→light with palette open:** same in reverse. Screenshot.
- [ ] **Sidebar under theme switch:** with the OS in the new theme, toggle and hover the sidebar;
      the sliver and rail colors match the active theme's tokens in both states.

## Regression carryover

- [ ] **Startup remains data-only:** opening/closing the palette and revealing/hiding the sidebar
      triggers no network request; the only external request is an explicit provider submission
      (check a network monitor, not visual absence of a spinner).
- [ ] **One `CefWindow` throughout:** the palette and the sliver are window overlays — no second
      top-level window, Dock icon, or taskbar entry ever appears.
- [ ] **Popup rejection:** attempting to open a popup/new window is still rejected.
- [ ] **Smoke test and process cleanup:** `open build/src/main/island_browser.app --args
      --island-smoke-test` still shows the `ISLAND_PHASE1_SMOKE_OK` marker; after quitting
      normally, `pgrep -fl island_browser || true` shows no remaining helper processes.

## Sign-off

Fill in once every box above is checked on a given target:

| Target | OS build | Date | Verified by | Notes / known deviations |
| --- | --- | --- | --- | --- |
| macosarm64 | | | | |
| macosx64 (toggle-only, hover deferred) | | | | |
| windows64 (toggle-only, hover deferred) | | | | |
| windowsarm64 (toggle-only, hover deferred) | | | | |
| linux64 (toggle-only, hover deferred) | | | | |
| linuxarm64 (toggle-only, hover deferred) | | | | |

**Instructions:**
1. Run this checklist on macOS arm64 first; the Windows/Linux rows are toggle-only until a native
   hover seam and native CI evidence land for them.
2. For each checkbox, test the behavior and mark complete. If a feature cannot be verified (e.g.,
   no network access for the live provider submissions), write "N/A — <reason>" instead of leaving
   it blank.
3. At the end, fill in the sign-off table with the target, OS build/version, date, and your name or
   identifier, including any platform-specific deviations.
4. Attach all screenshots and terminal output to `docs/sidebar-palette-visual-acceptance.md`.
5. Do not record any row as passing without an actual run; leave the table empty until then.
