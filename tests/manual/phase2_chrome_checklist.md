# Phase 2 chrome manual acceptance checklist

This is a template for the manual verification pass required by
[Unit U8 of the Phase 2 implementation plan](../../docs/superpowers/plans/2026-08-08-island-browser-phase2.md).
It cannot be completed from an automated/headless environment: every row below requires a
human at a real display, in an interactive session, on the target OS. Run it on macOS arm64 first
(the only target with local build/run/package evidence per `docs/supported-platforms.md`); repeat on
other targets as their native CI evidence lands.

Record the actual OS build/version, screen scale, and date at the top of each run. Attach
screenshots to `docs/phase2-visual-acceptance.md` (or the PR) as you go rather than only at the end,
so a partial run still leaves usable evidence.

## Setup

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
open build/src/main/island_browser.app
```

## Layout and hierarchy

- [ ] At the default launch size (macOS default web content size, resize to 1440x900 DIP), the
      window shows: a 286 DIP left rail (window controls/Back/Forward/Reload, contextual address
      control, one active-tab focus representation) and a browser-content region filling the rest.
      Confirm the rail is exactly 286 DIP wide with a ruler/DevTools measurement, not eyeballed.
- [ ] Resize the window down toward the supported minimum (800x560 DIP). The window does not shrink
      below 800x560. `BrowserContent` shrinks first; document what happens to the rail at the floor
      (full 286 DIP retained, or a compact layout) with a screenshot - this is a "document the exact
      platform result" requirement, not a pass/fail on a specific behavior.
- [ ] Title and favicon in the active-tab focus representation truncate (ellipsis) rather than
      wrapping to a second row or resizing the rail, when given a long page title.
- [ ] No requirement depends on this, but visually confirm the CefBrowserView itself is never
      clipped, masked, or shadowed by chrome-owned rounded/blurred surfaces.

## Theme

- [ ] Switch the OS to light mode. Confirm chrome colors match the Frame 01 light tokens
      (background `#F3F0E9`, surface `#FFFEFB`, text `#18303A`, accent `#168C99`, etc.)
- [ ] Switch the OS to dark mode while the app is running. Confirm the chrome repaints to the Frame
      06 dark tokens (background `#0D1B26`, surface `#142633`, text `#EAF3F3`, accent `#168C99`)
      without restarting the app.
- [ ] Fonts render as Geist (UI) / Geist Mono (monospace elements), not a system-font fallback, in
      both themes.

## Address control

- [ ] Idle state shows the current page's URL/location, not editable.
- [ ] Click the address control: it becomes editable and selects existing text.
- [ ] Press Cmd+L (macOS) / Ctrl+L (Windows/Linux): same effect as clicking, from anywhere in the
      window.
- [ ] Type a valid absolute `https://` URL and press Enter: navigates, returns to display mode
      showing the new location.
- [ ] Type a valid `http://localhost:PORT` and a `*.localhost` host: both accepted.
- [ ] Type a relative path, a `javascript:` URL, a URL with embedded credentials
      (`https://user:pass@host/`), and an empty string: each is rejected without navigating; the
      field stays editable with the typed text intact and a visible/announced validation message.
- [ ] Press Escape while editing with unsaved changes: reverts to the last displayed value without
      navigating.
- [ ] Click elsewhere (blur) while editing with unsaved changes: reverts to the display value
      without navigating.

## Navigation buttons

- [ ] Back/Forward reflect `NavigationSnapshot::can_go_back`/`can_go_forward` - disabled when there
      is nothing to go back/forward to, and this is announced (not just visually greyed) to a screen
      reader.
- [ ] Reload is available whenever the browser exists (not tied to back/forward state).
- [ ] Trigger Back/Forward/Reload from both mouse and keyboard activation (Enter/Space while
      focused).

## Keyboard and focus order

- [ ] Starting from the window, Tab cycles: Back -> Forward -> Reload -> address field -> active-tab
      focus representation, then leaves the chrome. Shift+Tab reverses it.
- [ ] A disabled button (e.g., Back with no history) is skipped by Tab, not just visually dimmed.
- [ ] Every interactive control has an accessible name/role audible via the OS accessibility
      inspector (macOS: Accessibility Inspector) - confirm at least Back, Forward, Reload, the
      address field, and the active-tab representation.

## Regression carryover from Phase 1

- [ ] App still starts from the fixed local `data:` startup page with no external network request
      (check a network monitor/proxy, not just visual absence of a spinner).
- [ ] Attempting to open a popup/new window is still rejected.
- [ ] `open build/src/main/island_browser.app --args --island-smoke-test` still shows the
      `ISLAND_PHASE1_SMOKE_OK` marker page.
- [ ] After quitting the app normally, `pgrep -fl island_browser` shows no remaining helper
      processes.

## Sign-off

Fill in once every box above is checked on a given target:

| Target | OS build | Date | Verified by | Notes / known deviations |
| --- | --- | --- | --- | --- |
| macosarm64 | | | | |
