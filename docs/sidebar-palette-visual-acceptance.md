# Sidebar/palette visual acceptance evidence

Tracks the manual/visual sign-off required by
[Unit U7 of the sidebar/palette plan](superpowers/plans/2026-08-12-island-sidebar-palette.md)
against the [sidebar/palette design](superpowers/specs/2026-08-12-island-sidebar-palette-design.md).
The plan requires recorded macOS arm64 evidence for the hover bands and the palette flow before the
feature is called visually accepted. The checklist lives at
[`tests/manual/sidebar_palette_checklist.md`](../tests/manual/sidebar_palette_checklist.md) and
cannot be completed from a headless/automated session.

## Status

No visual evidence has been captured yet. This file is the place to attach it once someone runs
the checklist on real hardware with a display; the sign-off table stays empty until then. The
CI/package confirmation U7 also asks for ("the existing native CI matrix and package checks still
pass with U1–U6 landed") has had its locally completable part done as of 2026-09-19: the workflow
definitions were re-audited against the current tree and need no change — `Island CI` and
`Package unsigned candidates` trigger on every push to `main` with no path filters or caches, so
the sidebar/palette sources are built and tested by the default suite automatically. The run
evidence itself is still not recorded here: the newest green full-matrix `Island CI` run
(33511735871, commit `b939cd0`) predates these units — see
[`docs/supported-platforms.md`](supported-platforms.md). Treat CI/package run evidence for the
sidebar/palette code as outstanding, not as passing, until a green run covers the current tree.

Automated coverage that *is* in place and does not require a human:

- `tests/search_provider_test.cpp` — `PercentEncodeQuery` encodings and the five composed provider
  URLs byte-for-byte against the spec table.
- `tests/search_palette_test.cpp` — palette model behavior: lazy create, Escape hides without
  navigation, the recorded `(provider, URL)` submission, empty-query rejection, null-browser
  no-op, and the `OnThemeChanged` surface re-assertion.
- `tests/sidebar_state_test.cpp` — the reveal/hide state machine: toggle precedence over hover,
  hover enter/leave with the 12/16-DIP bands, idempotent repeated hover.

What those tests cannot verify: that the hidden 0-DIP rail plus 1–2-DIP sliver actually renders at
the window edge, that macOS hover reveal/hide behaves on a real pointer, that focus trapping and
restoration work through the real window, that the five provider submissions reach the live URLs,
or that the palette survives a light/dark theme switch without surface drift. That gap is exactly
what the manual checklist exists to close.

## Accelerator deviation

The Phase 3 design (2026-08-09) fixes `Cmd/Ctrl+K` on the **command palette** (tabs, spaces,
go-to-URL). The sidebar/palette design (2026-08-12) had assigned `Cmd/Ctrl+K` to the **search
palette**. Resolution as implemented (commit `c63641e`): the command palette owns `Cmd/Ctrl+K`,
and the search palette moved to **`Cmd/Ctrl+Shift+K`**. On macOS these key equivalents are owned
by the `NSMenu` — `CefWindow::SetAccelerator` never dispatches there — so the menu wiring, not
only the `SetAccelerator` registration, is what makes the shortcuts work on macOS. The checklist
and the evidence rows below use the resolved bindings.

## Evidence artifacts

The following artifacts must be captured and attached to this file during a manual run:

### Required screenshots (macOS arm64)

- [ ] **Hidden sidebar state:** rail hidden (0 DIP layout width) with the 1–2-DIP sliver visible at
      the window's left edge, light and dark themes.
- [ ] **Hover reveal:** cursor inside the leftmost 12-DIP band while hidden — rail revealed to its
      full 286 DIP, sliver hidden, content reflowed with no animation.
- [ ] **Hover hide:** cursor moved away from the revealed rail by more than the 16-DIP grace band —
      rail hidden again.
- [ ] **Toggle-pinned state:** rail revealed via `Cmd+B` and still revealed while the cursor leaves
      the window entirely; then hidden again by a second press.
- [ ] **Search palette open (`Cmd/Ctrl+Shift+K`):** query field focused, five provider rows in spec
      order (ChatGPT, Perplexity, Claude, Gemini, Google).
- [ ] **Palette theme switch:** OS theme switched light→dark (and back) with the palette open —
      palette surfaces re-assert their token backgrounds with no stale-color drift.

### Required measurements or process output

- [ ] **Hover band measurements:** ruler or annotated screenshots confirming reveal triggers inside
      the 12-DIP band and hide triggers only beyond the 16-DIP grace band, and that repeated
      band crossings are idempotent (no flicker, no stuck state).
- [ ] **Startup/network posture:** network monitor output showing no provider URL is fetched at
      startup, on palette open, or on sidebar reveal — the first external request happens only
      after an explicit submission.

### Required completion entries

- [ ] **Cmd/Ctrl+B toggle:** evidence the toggle works from any focus state, that toggle state wins
      over hover (a pinned reveal stays up under hover; a toggled-off sidebar does not hover-reveal),
      and that repeated presses are idempotent.
- [ ] **Palette flow:** open with the query field focused; `Tab` cycles within the palette only;
      `ArrowUp`/`ArrowDown` move the highlighted provider; `Enter` submits the highlighted
      provider; `Escape` hides the palette with no navigation and restores focus to the invocation
      point.
- [ ] **One real submission per provider:** each of the five providers navigates the active tab to
      its spec URL (`base + "?q=" + PercentEncodeQuery(query)` — uppercase hex, spaces as `%20`),
      verified against the spec's provider table; the palette hides after submission.
- [ ] **Empty-query rejection:** submitting with an empty or whitespace-only query navigates nowhere
      and the palette stays open.
- [ ] **One `CefWindow` throughout:** the palette and the sliver are window overlays — no second
      top-level window ever appears.

### Windows/Linux rows

The checklist marks the Windows/Linux rows **"toggle-only, hover deferred"** (plan unit U5): the
`Cmd/Ctrl+B` toggle and the palette are expected to work through the shared state machine and the
same accelerator registrations, but no native hover seam ships for those platforms, and no native
CI evidence covers Phase 3-era code on them yet. Do not fill those rows from a macOS run.

## Recording evidence

When you run the checklist, attach results here per target:

```
### macosarm64 - <OS build> - <date>

#### Screenshots
- Hidden state + sliver: <screenshot>
- Hover reveal / hide: <screenshots>
- Toggle-pinned: <screenshot>
- Palette open, light and dark: <screenshots>

#### Measurements / Process output
- Hover band measurements: <evidence>
- Startup/network posture: <output or notes>

#### Completion checkmarks
- Cmd/Ctrl+B toggle: <evidence>
- Palette flow: <evidence>
- Per-provider submissions: <evidence per provider>
- Empty-query rejection: <evidence>
- One CefWindow throughout: <notes>

#### Notes
<anything that deviated from the design, platform-specific observations, etc.>
```

Keep prose to what was actually observed. If a box could not be verified (no second display, no
network access for the live provider submissions, etc.), say so explicitly rather than leaving it
implied as passed.

## Sign-off

Fill in once every macOS arm64 box above is checked:

| Target | OS build | Date | Verified by | Notes / known deviations |
| --- | --- | --- | --- | --- |
| macosarm64 | | | | |
| macosx64 (toggle-only, hover deferred) | | | | |
| windows64 (toggle-only, hover deferred) | | | | |
| windowsarm64 (toggle-only, hover deferred) | | | | |
| linux64 (toggle-only, hover deferred) | | | | |
| linuxarm64 (toggle-only, hover deferred) | | | | |
