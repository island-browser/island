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

Where the implementation stands (as of 2026-09-19): Phase 3 units U4/U5 (tab strip and space
switcher with live chrome projections, including space rename via F2 / the macOS Browser menu and
reorder via the menu), U6 (split view: `Cmd/Ctrl+Shift+S` pairs the active tab with its adjacent
tab, both panes render side by side through the content slot's box layout, the divider adjusts via
the Move Split Divider menu items and the `kMoveDividerLeft/Right` commands, closing either half or
selecting a tab outside the pair restores the single view), U7 (command palette), and U8 (session
save on clean quit and restore at startup) have landed. Unit U9's locally completable work (the
CI/package workflow audit and the local-evidence refresh in
[`docs/supported-platforms.md`](supported-platforms.md)) is done as of 2026-09-19; the green-run
evidence U9 exists to record — `Island CI` and `Package unsigned candidates` over the current tree
— is pending the next push to `main`. U10 (this manual pass) is still outstanding. Documented
deviations: the command palette owns
`Cmd/Ctrl+K` and the search palette moved to `Cmd/Ctrl+Shift+K`
([`docs/sidebar-palette-visual-acceptance.md`](sidebar-palette-visual-acceptance.md)); on macOS
`CefWindow::SetAccelerator` never dispatches (the NSMenu owns the command keys); the split divider
is not drag-adjustable because the pinned CEF distribution exposes no mouse events on custom
views, so the manual checklist's drag rows become divider-keyboard-adjustment rows (arrow-free:
use the menu items) on every platform.

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
- `tests/browser_window_test.cpp` — landed 2026-09-19: wired into `tests/CMakeLists.txt`, 20
  headless `BrowserWindowTest` cases passing. Covers tab/space command dispatch, direct-index and
  space bookkeeping, fresh-install vs. restored-session shapes, and the headless overlay no-op
  paths. The headless seam skips session persistence, so the cases are deterministic regardless of
  any ambient session file.
- `tests/command_palette_test.cpp` — landed 2026-09-19: wired into `tests/CMakeLists.txt`,
  29 `CommandPaletteTest` cases passing (model, filter/projection, and selection behavior).
- `tests/browser_window_test.cpp` split-view cases (U6, landed 2026-09-19) — ten headless
  `BrowserWindowTest` cases covering `kToggleSplit` pairing (right neighbor, left fallback at the
  end), `SplitTabs`/`UnsplitActiveSpace` rejection semantics (same id, unknown id, cross-space),
  pair replacement, split clearing when either half closes or the selection leaves the pair,
  per-space pairing isolation across switches, and `SetSplitRatio` clamping plus the divider
  nudge commands.

What those tests cannot verify: that the rendered tab strip and space switcher are visually
correct, that focus order and screen-reader announcements work through the real accessibility tree
for new tab/space entries, that dark-mode repaints correctly while the app runs, or that session
restore actually reconstructs the window state as intended on a real quit/relaunch cycle. (The
split-view divider clause is moot until U6 lands — there is no split-view UI to test.) That gap is
exactly what the manual checklist exists to close.

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
      Verify the file exists at `~/Library/Application Support/Island/session.json` (macOS) after
      the quit, and that no restore happens after a force-quit — `SessionStore::Save` fires only on
      the clean-quit CEF close path.
- [ ] **Malformed session file fallback:** Screenshot or terminal output showing that when
      `session.json` is corrupted (e.g., truncated or invalid JSON), the app falls back to a fresh
      session without crashing.
- [ ] **Invalid restored URL fallback:** With a session file whose tab URLs fail the current
      `ParseAndValidate` allow-list, relaunch and confirm those tabs fall back to the fixed local
      `data:` startup page rather than loading the disallowed URL — restored URLs are re-validated
      through the same path as manual entry.
- [ ] **Split view interactions (U6 landed; drag remains a documented deviation — the pinned CEF
      distribution exposes no mouse events on custom views, so divider rows use the Move Split
      Divider menu items instead of drag):** Screenshots showing:
      - Two tabs side by side with a visible divider (`Cmd/Ctrl+Shift+S` pairs the active tab
        with its adjacent tab).
      - The divider after being moved by the menu items (and by `Cmd/Ctrl+Shift+Left/Right` on
        Windows/Linux).
      - Closing one half, confirming the other tab restores to full width.
      - The command layer's rejection of cross-space pairings (headless evidence already covers
        this; the manual pass covers the visible behavior).
- [ ] **Command palette interactions:** Screenshots or terminal notes showing:
      - The palette opens on Cmd/Ctrl+K as a lazily created overlay that is hidden, not destroyed,
        between uses.
      - Results list open tabs of the active space (fuzzy-filtered by title/URL), all spaces, and a
        go-to-URL row.
      - Selecting a tab result switches to it; selecting a space result switches the active space.
      - "Go to URL" entry and submission through the same `AddressBarModel`/`ParseAndValidate`
        validation path as the rail's address control.
      - An invalid URL (relative path, `javascript:`, embedded credentials) is rejected without
        navigating: the palette stays open with the typed text intact.
      - Escape closes without navigating or switching and restores focus to the invocation point.
      - Focus trap: Tab cycles within the palette rows while it is open.
- [ ] **Tab/space keyboard shortcuts:** Evidence that:
      - Cmd/Ctrl+T creates a new tab.
      - Cmd/Ctrl+W closes the active tab.
      - Cmd/Ctrl+1..9 switch to direct tab indices.
      - Cmd/Ctrl+Shift+[ / ] switch to previous/next tab.
- [ ] **Space switcher interactions:** Evidence that:
      - New/close/rename/reorder operations work via the chrome UI: rename through the F2
        accelerator or the macOS Browser menu "Rename Space…" (overlay textfield; Enter commits a
        non-empty trimmed name, Escape cancels), reorder through the menu "Move Space Left/Right".
        There is no hover or drag affordance for rename/reorder — that is deferred, not missing
        evidence.
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
- Invalid restored URL fallback: <screenshot or notes>
- Split view interactions: <screenshots — pending U6; leave blank until split view lands>
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
