# Phase 3 chrome manual acceptance checklist

This is a template for the manual verification pass required by
[Unit U10 of the Phase 3 implementation plan](../../docs/superpowers/plans/2026-08-09-island-browser-phase3.md).
It cannot be completed from an automated/headless environment: every row below requires a
human at a real display, in an interactive session, on the target OS. Run it on macOS arm64 first
(the only target with local build/run/package evidence per `docs/supported-platforms.md`); repeat on
other targets as their native CI evidence lands.

Record the actual OS build/version, screen scale, and date at the top of each run. Attach
screenshots and process output to `docs/phase3-visual-acceptance.md` (or the PR) as you go rather
than only at the end, so a partial run still leaves usable evidence.

## Setup

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
open build/src/main/island_browser.app
```

The app should launch with multiple initial tabs and spaces visible in the chrome. If not, verify
that Phase 3 implementation is complete before proceeding.

## Layout and hierarchy — multi-tab and multi-space

- [ ] At default launch size (1440x900 DIP), confirm:
      - A left rail (286 DIP wide) contains: window controls, navigation buttons (Back/Forward/Reload),
        an address control, a **tab strip** listing open tabs in the active space, and a **space
        switcher** listing available spaces.
      - The content region to the right of the rail displays the active tab's page.
      - The tab strip shows multiple tabs with titles/favicons, truncating long titles with ellipsis.
      - The space switcher shows multiple spaces with color marks and names, truncating long names with
        ellipsis.
      - Confirm the rail is exactly 286 DIP wide with a ruler or DevTools measurement, not eyeballed.
- [ ] Resize the window down toward the supported minimum (800x560 DIP). Document the exact rail
      behavior at the floor (full 286 DIP retained, or a compact layout) with a screenshot — this is
      a "document the exact platform result" requirement.
- [ ] Confirm the CefBrowserView in the content region is never clipped, masked, or shadowed by
      chrome-owned rounded/blurred surfaces.

## Tab strip — create, close, switch, keyboard navigation

- [ ] **Create a new tab:** Press Cmd+T (macOS) / Ctrl+T (Windows/Linux). A new tab appears in the
      tab strip with a default title (or the page title if it navigates to a startup page). The new
      tab becomes active (active-state visual indicator is applied to it).
- [ ] **Close a tab:** Click the close affordance (× button) on a tab in the strip. The tab closes
      immediately. If it was the active tab and other tabs exist, a neighboring tab becomes active.
- [ ] **Close the last tab in a space:** Attempt to close the final tab of the active space via
      Cmd+W or the close affordance. Verify the behavior is defined and consistent (either the tab
      remains, or the space is closed). Document which behavior is implemented.
- [ ] **Switch tabs by clicking:** Click on a different tab in the strip. That tab becomes active
      (active-state indicator moves to it), and the content region shows its page.
- [ ] **Switch tabs by keyboard direct index:** Press Cmd+1 (macOS) / Ctrl+1 (Windows/Linux) through
      Cmd+9 / Ctrl+9. Each keypress switches to the tab at that index (1-indexed) in the active space,
      if it exists. Verify that indices map correctly and out-of-range indices are ignored.
- [ ] **Switch tabs by keyboard previous/next:** Press Cmd+Shift+[ (previous tab) and Cmd+Shift+]
      (next tab). Each keypress switches to the adjacent tab in the strip, wrapping if needed (last ->
      first for next, first -> last for previous). If at an end and wrapping is not implemented,
      document this.
- [ ] **Tab strip focus and keyboard navigation:** Starting from the content region and tabbing
      forward, verify Tab cycles through the address field, then the active tab's representation, then
      leaves the chrome. Tab into the tab strip and verify arrow keys (left/right) navigate between
      tabs, and Enter/Space activates the focused tab. Shift+Tab reverses order.
- [ ] **Tab truncation:** Open a tab with a very long page title or URL. Confirm the tab-strip entry
      truncates with ellipsis rather than wrapping or resizing the rail.
- [ ] **Active/inactive visual state:** Verify the active tab has a distinct visual indicator
      (highlight, underline, background color, etc.) distinct from inactive tabs, and that switching
      tabs updates this visual state immediately.

## Space switcher — create, close, switch, rename, reorder

- [ ] **Create a new space:** Click a "+" button or affordance in the space switcher, or press an
      accelerator if one is defined (e.g., Cmd+N or similar). A new space appears in the switcher with
      a default name and color. The new space becomes active. Verify it has zero tabs or one default
      tab, depending on implementation.
- [ ] **Switch to a different space:** Click on a space in the switcher. That space becomes active
      (active-state indicator moves to it). The tab strip updates to show the active space's tabs, and
      the content region shows that space's active tab.
- [ ] **Rename a space:** Right-click (or long-press, or use a dedicated affordance) on a space in the
      switcher and select rename, or double-click to edit in-place. Type a new name and confirm (Enter
      or click elsewhere). The space name updates in the switcher, truncating if necessary.
- [ ] **Reorder spaces:** Drag a space entry in the switcher to a new position, or use keyboard
      shortcuts if defined (e.g., Cmd+Shift+>, Cmd+Shift+< or similar). The space moves in the list,
      and the switcher updates. Verify the active space stays active during reorder.
- [ ] **Close a space:** Click a close affordance (× or delete button) on a space in the switcher.
      That space is deleted unconditionally (no confirmation per the Phase 3 design). If it was the
      active space and other spaces exist, a neighboring space becomes active. The tab strip updates
      to show the new active space's tabs.
- [ ] **Close the last space:** Attempt to close the final remaining space. Verify the behavior is
      defined and consistent (either the space persists and cannot be deleted, or a new default space
      is auto-created). Document which behavior is implemented.
- [ ] **Space switcher focus and keyboard navigation:** Starting from the content region and tabbing
      forward, verify Tab cycles through address field, active tab, and space-switcher entries. Tab
      into the space switcher and verify arrow keys (up/down, or left/right) navigate between spaces,
      and Enter/Space activates the focused space. Shift+Tab reverses.
- [ ] **Space isolation — per-space cookies and local storage:** Open two spaces. In space A, navigate
      to a test site that sets a login cookie. Log in and note the session state (e.g., a user name or
      avatar displayed on the page). Switch to space B and navigate to the same site. Without logging
      in, confirm that space B shows the logged-out state — i.e., the cookie from space A is NOT
      shared. Log in again in space B with a *different* account (if available) and confirm the two
      spaces maintain independent sessions. This verifies per-space `CefRequestContext` isolation.
- [ ] **Space color indicators:** Verify that each space has a visual color mark in the switcher
      (circle, stripe, background tint, etc.) distinct from other spaces, matching the design's
      space-color feature.
- [ ] **Active/inactive visual state:** Verify the active space has a distinct visual indicator
      (highlight, underline, background color, etc.) distinct from inactive spaces, and that switching
      spaces updates this indicator immediately.

## Split view

- [ ] **Enter split view — drag affordance:** In the active space, ensure it has at least two tabs.
      Drag one tab onto another tab in the strip. Both tabs should appear side by side in the content
      region, with a draggable divider between them. Verify both pages render and are interactive.
- [ ] **Split view divider — drag to resize:** With a split active, position the cursor over the
      divider line. It should show a resize cursor (↔). Drag left and right to resize the two panes.
      Confirm the divider responds smoothly and both pages remain interactive.
- [ ] **Split view divider — keyboard resize:** With the divider focused (Tab to it), press arrow
      keys (left/right) to resize the split. Confirm the divider moves and both panes update. Document
      any key repeat rate or step size observed.
- [ ] **Exit split view — close one half:** While split, close the tab in one pane (click its close
      affordance, or press Cmd+W / Ctrl+W to close the focused tab). The remaining tab should expand
      to full width. Verify the layout returns to single-pane and content region shows the remaining
      tab's page.
- [ ] **Exit split view — explicit action:** If an explicit "un-split" or "close split" affordance is
      implemented, trigger it while split. Verify the split closes and the active tab (or previously
      visible tab) returns to full width. Document which tab becomes active after un-split.
- [ ] **Cross-space split rejection:** While in space A with multiple tabs, attempt to drag a tab from
      space A onto a tab in space B (if UI allows selection across spaces). This should be rejected — a
      split must be within a single space. Verify the action is rejected (no split created).
- [ ] **Split focus and keyboard navigation:** While split, press Tab to navigate between the two
      panes and the divider. Verify Tab cycles through both panes' content and the divider, and
      keyboard navigation within each pane works independently.

## Command palette — open, search, activate, go to URL

- [ ] **Open the palette:** Press Cmd+K (macOS) / Ctrl+K (Windows/Linux). An overlay should appear
      listing tabs and spaces. Verify it opens immediately and a text input field is visible for
      searching/typing.
- [ ] **Palette input and fuzzy search:** Start typing a tab title or URL in the palette. Results
      should filter and highlight matching entries in real-time, using fuzzy matching (e.g., typing
      "gm" might match "gmail", "github", etc.). Verify results are sorted by relevance.
- [ ] **Palette result composition:** In the palette results, verify:
      - Open tabs from the active space are listed with their title/URL and an active/inactive indicator.
      - All spaces are listed with their name and color mark.
      - A "go to URL" affordance (input field or button) is available.
- [ ] **Activate a tab from the palette:** Search for a tab title or URL, then select the tab result
      (press Enter, click it, or arrow-key to it and press Enter). The palette should close and that
      tab should become active in its space. If the tab is in a different space, the active space
      should switch to that space's.
- [ ] **Activate a space from the palette:** Search for or scroll to a space result, then select it.
      The palette should close and that space should become active. The tab strip updates to show the
      space's tabs, and the content shows the space's active tab.
- [ ] **Go to URL — submit and validate:** In the palette, focus the "go to URL" affordance and type
      a valid URL (e.g., `https://example.com`). Press Enter. The palette should close and the active
      tab should navigate to that URL through the same address-validation path as the rail's address
      control (i.e., obey the same allow-list).
- [ ] **Go to URL — rejection on invalid input:** In the palette's "go to URL" field, type an invalid
      URL (e.g., a relative path, `javascript:`, or a URL with embedded credentials). Press Enter.
      Verify the URL is rejected without navigating, the palette stays open with the text intact, and a
      validation message is shown (or announced). This confirms the palette uses the same validation as
      the rail.
- [ ] **Close the palette — Escape:** Press Escape while the palette is open. Verify the palette closes
      without navigating or switching tabs/spaces. Focus should return to the point of invocation (e.g.,
      the active content area).
- [ ] **Close the palette — blur/click outside:** Click outside the palette, or press Alt+Tab to switch
      windows. The palette should close without side effects.
- [ ] **Palette focus trap:** While the palette is open, verify Tab cycles only within the palette (input
      field, results list, go-to-URL affordance) and does not escape to the window's chrome. Pressing
      Escape should break the trap and return focus to the invocation point.
- [ ] **Palette keyboard navigation:** Inside the palette, use arrow keys (up/down) to navigate the
      results list, and Enter to activate the highlighted result. Verify navigation wraps if needed.
- [ ] **Palette re-opening:** Close the palette (Escape or select a result). Press Cmd/Ctrl+K again to
      re-open it. Verify it opens in a clean state with the search field cleared (or with previous
      search preserved, depending on design choice). Document the behavior.

## Session restore — quit and relaunch

- [ ] **Setup for round-trip test:** With the app running, create multiple spaces with distinct names
      and colors. In each space, create multiple tabs and navigate each to a different URL (or bookmark).
      Make the active space and tab something other than the first. Note the exact configuration
      (spaces, tabs, URLs, active selections).
- [ ] **Clean quit:** Quit the app normally (Cmd+Q on macOS, or menu Quit, or close the window with
      Cmd+W at the top level). Verify the app exits cleanly and leaves no dangling `island_browser`
      helper processes: `pgrep -fl island_browser || true` should return nothing.
- [ ] **Verify session file is written:** After quitting, check that a session file exists at the
      platform-appropriate location (macOS: `~/Library/Application Support/Island/session.json`). Verify
      it is valid JSON and contains the spaces/tabs/URLs you created.
- [ ] **Relaunch the app:** Open the app again (`open build/src/main/island_browser.app`). Verify:
      - The window shows the same spaces as before (with correct names and colors).
      - Each space lists the same tabs as before (with correct titles).
      - The same space is active (indicated by the switcher).
      - Within the active space, the same tab is active.
      - Clicking on tabs in the restored state shows their correct URLs.
      - Verify no external network requests occurred during startup (check network monitor).
- [ ] **Malformed session file — fallback to fresh session:** Before launching, edit the session file
      (e.g., truncate it to invalid JSON, or remove a required field). Relaunch the app. Verify:
      - The app does not crash.
      - The app falls back to a fresh session with the default startup page (Phase 1/2 behavior).
      - A log message indicates the session file was unreadable or invalid (check console if available).
      - The user is not presented with a partially-restored state or a confusing error.
- [ ] **Session file missing — fresh session:** Delete the session file or move it away. Relaunch the
      app. Verify it starts fresh with the default startup page, no errors or warnings that would
      confuse a user.
- [ ] **Restored URL validation:** If the session file contains a URL that is no longer in the
      allow-list (e.g., due to a policy change), verify that when the tab navigates to that URL, it is
      rejected by the same validation path as manual entry, not force-loaded. The tab should show the
      validation error, not navigate.

## Theme and styling — light/dark, fonts, minimum bounds

- [ ] **Light theme at 1440x900:** Launch or switch the OS to light mode. Verify the chrome colors
      match the Frame 01 light tokens (background `#F3F0E9`, surface `#FFFEFB`, text `#18303A`, accent
      `#168C99`, etc.). Take a screenshot.
- [ ] **Dark theme at 1440x900:** Switch the OS to dark mode *while the app is running*. Verify the
      chrome repaints to the Frame 06 dark tokens (background `#0D1B26`, surface `#142633`, text
      `#EAF3F3`, accent `#168C99`) without requiring a restart. Take a screenshot.
- [ ] **Light theme at 800x560 (minimum):** Resize to the minimum supported height and width. Verify
      chrome layout adapts correctly (tab strip, space switcher, rail width, address field positioning)
      without clipping. Take a screenshot and note any layout changes compared to 1440x900.
- [ ] **Dark theme at 800x560:** While at minimum bounds, switch OS theme to dark. Verify theme
      repaints correctly at minimum bounds. Take a screenshot.
- [ ] **Fonts — Geist and Geist Mono:** In both light and dark themes, confirm tab titles, space
      names, the address field, and the command palette text use Geist (UI) or Geist Mono (if
      applicable), not system-font fallbacks.
- [ ] **Tab/space truncation at minimum bounds:** At 800x560, create tabs with very long titles and
      spaces with very long names. Verify they truncate with ellipsis, not wrap or overflow, and the
      rail remains within bounds.

## Accessibility and keyboard navigation

- [ ] **Focus order — full cycle:** Starting from the content region (empty window or default page) and
      pressing Tab repeatedly, verify the focus order is:
      1. Back button (if enabled)
      2. Forward button (if enabled)
      3. Reload button
      4. Address field
      5. Active tab entry (from the tab strip)
      6. Leave chrome (focus enters content)
      And Shift+Tab reverses this order. If new elements (space-switcher entries, palette button, etc.)
      are reachable via Tab, document their exact position in the cycle.
- [ ] **Skip disabled buttons:** When Back is disabled (no history), Tab should skip it and move to
      Forward. Same for Forward when no forward history exists. Verify disabled buttons are truly
      skipped, not just visually dimmed.
- [ ] **Accessible names and roles:** Using the OS accessibility inspector (macOS: Accessibility
      Inspector, or a screen reader), verify:
      - Back, Forward, Reload buttons announce their name and role ("Back Button", "Forward Button",
        "Reload Button" or similar).
      - Address field announces as "Address" or "URL" field with edit/text role.
      - Active tab entry announces as a button with name "Tab title" or similar, plus the active/inactive
        state ("active" or "inactive").
      - Space-switcher entries announce with their space name and active/inactive state.
      - Command palette results announce as a list or combobox with tab/space items and their state.
      - The divider in split view announces as an adjustable splitter or similar.
- [ ] **Screen reader and announcements:** If a screen reader is available, navigate the entire chrome
      using only the screen reader's item-by-item navigation (e.g., rotor mode). Verify all interactive
      elements are discoverable and their state is announced (active/inactive, enabled/disabled, etc.).
- [ ] **Keyboard-only operation:** Without touching the mouse, verify every feature can be accessed:
      - Create, close, switch, and reorder tabs (all via keyboard shortcuts or Tab + arrow keys).
      - Create, close, switch, rename, and reorder spaces (all via keyboard).
      - Resize split-view divider (Tab to it, arrow keys to resize).
      - Open command palette, search, navigate, activate (all keyboard).
      - Navigation buttons and address field (already tested in Phase 2).

## Regression and Phase 1/2 carryover

- [ ] **Fixed startup page — no external network:** Launch the app fresh (or with a valid session that
      doesn't bypass the startup-page flow). Monitor network activity (Instruments, proxy, etc.) and
      verify no external network requests occur at startup. The page should load from local `data:` or
      from the restored session's on-disk storage. No calls to cdn.example.com, metrics servers, or
      update checkers.
- [ ] **Popup rejection:** While viewing any page, attempt to trigger a popup (e.g., `window.open()` in
      DevTools console, or a page with a popup link). Verify the popup is rejected and does not create
      a new window or tab. The main window and tab remain unchanged.
- [ ] **Smoke test marker page:** Run `open build/src/main/island_browser.app --args --island-smoke-test`.
      Verify the window shows the `ISLAND_PHASE1_SMOKE_OK` marker page. Quit normally and verify no
      helper processes remain.
- [ ] **Process cleanup on quit:** After quitting the app (normal Cmd+Q or menu Quit), wait a few
      seconds and run `pgrep -fl island_browser || true`. Verify no processes remain. If processes
      persist, they may indicate a CEF shutdown issue or a missing close handler in Phase 3.
- [ ] **Navigation history — Back/Forward:** Load a page, navigate to another, and verify Back button
      works. Navigate forward and verify Forward works. This is Phase 1/2 behavior carried forward —
      confirm it still works after Phase 3 multi-tab changes.
- [ ] **Reload button:** Press Reload on any page. Verify the page refreshes and content updates. Confirm
      the address and title remain the same.
- [ ] **Address control — edit, validate, navigate:** Click the address field, type a valid URL, press
      Enter. Verify navigation occurs. Type an invalid URL (relative path, `javascript:`, etc.) and
      verify rejection without navigation. This is Phase 2 behavior — confirm it still works with Phase 3
      multi-tab.

## Memory and process management

- [ ] **CEF process cleanup on space close:** With multiple spaces open, use `pgrep -ef island_browser`
      or a process monitor (Instruments) to identify the renderer process(es) for one space. Close that
      space via the UI. After a few seconds, run `pgrep -ef island_browser` again and verify the
      renderer process(es) for that space have exited. Do this for at least two spaces to confirm the
      pattern.
- [ ] **Memory growth — private footprint or PSS:** Open the app and note the private memory footprint
      of the `island_browser` main process (Instruments, Activity Monitor via "Memory > Real Memory" or
      `ps` column `RSIZE` on macOS, or `/proc` on Linux). Create and close several spaces (e.g., 5 new
      spaces, then close each). After all closes, measure the footprint again. Verify it has not grown
      unbounded — some growth is expected (cached content, internal buffers), but should return close to
      baseline after tabs/spaces are closed (allowing for CEF's internal memory pooling). Document
      observations; an obvious memory leak would show footprint grow to 200%+ after a small number of
      close operations.
- [ ] **Process count — no accumulation:** Using `pgrep -c island_browser`, measure the process count
      when the window is empty (no tabs/spaces), when it has multiple tabs/spaces, and after closing all
      but one tab/space. Verify the count drops back down after closures, not remaining elevated.

## Acceptance sign-off

Fill in once every box above is checked on a given target:

| Target | OS build | Date | Verified by | Notes / known deviations |
| --- | --- | --- | --- | --- |
| macosarm64 | | | | |
| macosx64 | | | | |
| windows64 | | | | |
| windowsarm64 | | | | |
| linux64 | | | | |
| linuxarm64 | | | | |

**Instructions:**
1. Run this checklist on each target where evidence is required (start with macOS arm64).
2. For each checkbox, test the behavior and mark complete. If a feature cannot be verified (e.g., no
   screen reader available), write "N/A — <reason>" instead of leaving it blank.
3. At the end, fill in the sign-off table with the target, OS build/version, date, and your name or
   identifier. Include any notes about platform-specific deviations or missing tools.
4. Attach all screenshots and terminal output to `docs/phase3-visual-acceptance.md`.
5. Submit the completed checklist as evidence of Phase 3 visual acceptance.
