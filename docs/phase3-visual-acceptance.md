# Phase 3 visual acceptance evidence

Tracks the manual/visual sign-off required by
[Unit U10 of the Phase 3 plan](superpowers/plans/2026-08-09-island-browser-phase3.md) and the
[Phase 3 design's acceptance section](superpowers/specs/2026-08-09-island-browser-phase3-design.md).
The design requires recorded macOS arm64 evidence at 1440x900 and 800x560 DIP, in both OS themes,
before Phase 3 can be called visually accepted.

## Status

No visual evidence has been captured yet. This file is the place to attach it once someone runs
[`tests/manual/phase3_chrome_checklist.md`](../tests/manual/phase3_chrome_checklist.md) on real
hardware with a display — that checklist cannot be completed from a headless/automated session.

Automated coverage that *is* in place and does not require a human:

- `tests/tab_test.cpp` — `TabId` uniqueness, tab add/remove/active-index semantics.
- `tests/space_test.cpp` — `SpaceId` uniqueness, space add/remove/reorder, active-tab index bounds
  on active-tab removal.
- `tests/chrome/browser_chrome_contract_test.cpp` — fixed rail regions (Phase 2 `ChromeViewId`
  values unchanged), tab-strip collection region shape and count, space-switcher collection region
  shape and count.
- `tests/session_store_test.cpp` — serialize/deserialize round-trip, each malformed-file case
  (missing/unreadable/schema-invalid) falls back cleanly to fresh session, restored URLs violating
  current allow-list are rejected.
- `tests/cef_address_parser_test.cpp` — URL allow/reject rules (already existed in Phase 2).
- `tests/design_tokens_test.cpp` — light/dark token values.
- `tests/browser_window_test.cpp` — planned in units U2/U4/U5/U6; not yet implemented.
- `tests/command_palette_test.cpp` — planned in unit U7; not yet implemented.

What those tests cannot verify: that the rendered tab strip and space switcher are visually
correct, that the divider in split view is draggable on a real compositor, that focus order and
screen-reader announcements work through the real accessibility tree for new tab/space entries, that
dark-mode repaints correctly while the app runs, or that session restore actually reconstructs the
window state as intended on a real quit/relaunch cycle. That gap is exactly what the manual checklist
exists to close.

## Evidence artifacts

The following artifacts must be captured and attached to this file during a manual run:

### Required screenshots (macOS arm64)

- [ ] **Light theme, 1440x900 DIP:** Startup state with multiple tabs and spaces visible in chrome.
- [ ] **Dark theme, 1440x900 DIP:** Same layout, confirming theme repaint while running.
- [ ] **Light theme, 800x560 DIP (minimum):** Layout at the supported floor, documenting chrome
      behavior at minimum bounds.
- [ ] **Dark theme, 800x560 DIP (minimum):** Confirming theme change at minimum bounds.

### Required measurements or pgrep output

- [ ] **Per-space `CefRequestContext` isolation:** Run the checklist's login verification and
      capture evidence (e.g., screenshot of two spaces logged into the same site with independent
      sessions, or terminal output showing distinct `CefRequestContext` object addresses).
- [ ] **CEF process isolation on space close:** Terminal output from `pgrep -fl island_browser`
      before and after closing a space, confirming the closed space's renderer process(es) exit.
- [ ] **Memory sanity check:** Terminal output showing private footprint or proportional set size
      (PSS) for `island_browser` processes after opening/closing multiple spaces, confirming no
      obvious memory leak (growth should be proportional to cached content, not accumulated on close).

### Required completion entries

- [ ] **Session restore round-trip:** Screenshots or terminal output confirming that after a normal
      quit and relaunch, the window state (spaces, tabs, active selections) is restored as expected.
- [ ] **Malformed session file fallback:** Screenshot or terminal output showing that when
      `session.json` is corrupted (e.g., truncated or invalid JSON), the app falls back to a fresh
      session without crashing.
- [ ] **Split view interactions:** Screenshots showing:
      - Two tabs side by side with a visible divider.
      - The divider after being resized by drag and by keyboard (arrow keys).
      - Closing one half, confirming the other tab restores to full width.
      - Rejection of split attempts across different spaces.
- [ ] **Command palette interactions:** Screenshots or terminal notes showing:
      - Palette opens on Cmd/Ctrl+K.
      - Fuzzy search by tab title/URL returns matching tabs.
      - Selecting a tab result switches to it.
      - Selecting a space result switches the active space.
      - "Go to URL" entry and submission through the same `AddressModel` validation path.
      - Escape closes without navigating or switching.
      - Focus trap: Tab cycles within the palette while it is open.
- [ ] **Tab/space keyboard shortcuts:** Evidence that:
      - Cmd/Ctrl+T creates a new tab.
      - Cmd/Ctrl+W closes the active tab.
      - Cmd/Ctrl+1..9 switch to direct tab indices.
      - Cmd/Ctrl+Shift+[ / ] switch to previous/next tab.
- [ ] **Space switcher interactions:** Evidence that:
      - New/close/rename/reorder operations work via chrome UI.
      - Closing the active space selects a defined neighbor.
      - Closing the last remaining space is handled without leaving the window in a broken state.
- [ ] **Accessibility/focus order:** Confirmation that:
      - Tab cycles through all interactive chrome elements (nav buttons, address field, tab-strip
        entries, space-switcher entries) in a sensible order.
      - All new entries announce their accessible name and active/inactive state via OS
        accessibility tree.
      - Split view divider is focusable and keyboard-adjustable.
- [ ] **Popup rejection:** Confirmation that attempting to open popups is still rejected.
- [ ] **Regression:** No external network requests at startup, smoke test marker page still works,
      no dangling helper processes after quit.

## Recording evidence

When you run the checklist, attach results here per target:

```
### macosarm64 - <OS build> - <date>

#### Screenshots
- Light, 1440x900: <screenshot>
- Dark, 1440x900: <screenshot>
- Light, 800x560: <screenshot>
- Dark, 800x560: <screenshot>

#### Measurements / Process output
- Per-space context isolation: <output or screenshot>
- CEF process cleanup on space close: <pgrep output>
- Memory sanity: <process footprint output>

#### Completion checkmarks
- Session restore round-trip: <screenshot>
- Malformed session fallback: <screenshot or notes>
- Split view interactions: <screenshots>
- Command palette interactions: <screenshots>
- Tab/space keyboard shortcuts: <evidence>
- Space switcher interactions: <evidence>
- Accessibility/focus order: <evidence>
- Popup rejection: <notes>
- Regression: <notes>

#### Notes
<anything that deviated from the design, platform-specific observations, etc.>
```

Keep prose to what was actually observed. If a box could not be verified (missing hardware, no
screen reader available, network restrictions, etc.), say so explicitly rather than leaving it
implied as passed.
