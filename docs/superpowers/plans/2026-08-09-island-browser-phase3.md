# Island Browser — Phase 3 Implementation Plan

> **Status:** Approved execution plan. It implements only the Phase 3 design in
> [the canonical specification](../specs/2026-08-09-island-browser-phase3-design.md).

**Goal:** Turn Island into a multi-tab, multi-space browser with split view, a command palette, and
clean-quit session restore, without breaking any Phase 1/2 contract that the design doesn't explicitly
supersede (CEF/runtime/package baseline, port `9222`, one `CefWindow`, popup rejection, close
lifecycle).

**Implementation rule:** Work in the stated dependency order. An implementer owns only the files in
their unit. Do not modify `browser.pen`, Phase 1/2 documents, unrelated docs, or another unit's files.
No unit may introduce a second top-level `CefWindow`, crash-recovery/autosave, cross-window drag,
history/bookmarks/downloads/settings/extensions/sync/profiles UI, or any other Phase 3 non-goal.

## Shared acceptance contract

- Retain one `CefWindow`, the CEF close lifecycle, popup rejection, port `9222`, and every Phase 1/2
  fixed-region `ChromeViewId` value unchanged.
- Exactly one `CefBrowserView` is ever attached to the chrome content slot at a time, except while a
  split is active (exactly two).
- There is exactly one URL-validation implementation (`AddressModel`/`cef_address_parser`); the
  command palette and any restored session both submit through it, never duplicate it.
- Session restore only fires from a file `SessionStore` itself wrote on a clean quit and only after it
  passes schema validation; any other case (missing/unreadable/invalid file) is identical to a fresh
  install and falls back to the Phase 1/2 fixed startup page.
- `TabId`/`SpaceId` are stable, process-lifetime-unique, and never derived from vector position.

## Units

### U1 — Tab/Space core model (CEF-independent)

**Owner:** U1 implementer
**Files:** `src/main/tab.h`, `src/main/tab.cc`, `src/main/space.h`, `src/main/space.cc`,
`src/main/tab_id.h`, `tests/tab_test.cpp`, `tests/space_test.cpp`, `tests/CMakeLists.txt`
**Depends on:** none

Define `TabId`/`SpaceId` (monotonic `std::uint64_t` generators), the `Tab` owning-type (id,
`NavigationState`, and a CEF-view seam described but not implemented here — see U2), and `Space`
(id, name, color, ordered `Tab` collection, active-tab index, optional split pairing by `TabId`).
Keep this layer free of `CefBrowserView`/`CefRequestContext` construction so it is unit-testable
without a CEF runtime; U2 wires the CEF-owning pieces in. Test id uniqueness/non-reuse, add/remove/
reorder semantics, and active-index bounds behavior on removal of the active tab.

**Tests:** `cmake --build build --target island_tests`;
`ctest --test-dir build -R '(Tab|Space)' --output-on-failure`.

### U2 — BrowserWindow multi-space restructuring and CefRequestContext ownership

**Owner:** U2 implementer
**Files:** `src/main/browser_window.h`, `src/main/browser_window.cc`, `src/main/tab.h`,
`src/main/tab.cc`, `src/main/space.h`, `src/main/space.cc`, `tests/browser_window_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U1

Replace `BrowserWindow`'s single `browser_view_`/`browser_`/`navigation_state_` triad with
`std::vector<Space> spaces_` and `active_space_index_`. Give `Tab` its `CefRefPtr<CefBrowserView>`/
`CefRefPtr<CefBrowser>` members and give `Space` its `CefRefPtr<CefRequestContext>`, created once at
space-creation (fresh or restored `cache_path`) and held for the space's lifetime. Implement
re-parenting: on active tab/space change, detach the previously-attached `CefBrowserView`(s) and
attach the newly active one(s) into chrome's content slot. Adapt `OnBrowserCreated`/
`OnBrowserDestroyed`/`OnAddressChange`/`OnTitleChange`/`OnLoad*` to route by which `Tab` owns the
firing `CefBrowser`, not a single-browser identity check. Detach chrome's subscription from the
previous active tab's `NavigationState`/`AddressBarModel` and re-subscribe to the new one on switch,
mirroring the existing close-time detach pattern. Test: creating a second tab/space does not disturb
the first's navigation state; closing the active tab/space selects a defined neighbor; per-space
request contexts are distinct objects.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(BrowserWindow|Tab|Space)' --output-on-failure`.

### U3 — BrowserChrome view-tree contract restructuring

**Owner:** U3 implementer
**Files:** `src/main/browser_chrome.h`, `src/main/browser_chrome.cc`, `src/main/chrome_snapshot.h`,
`tests/chrome/browser_chrome_contract_test.cpp`, `tests/CMakeLists.txt`
**Depends on:** U1

Split `ViewTreeContract()` into fixed rail regions (window/navigation controls, address row — same
`ChromeViewId` values as Phase 2, unchanged) and collection regions (tab strip, space switcher).
Allocate new `ChromeViewId` values starting after `kActivePageIndicator = 1018`, never renumbering
existing ones. Define the per-entry node shape for a tab-strip item (favicon-or-fallback, truncating
title, close affordance, active-state) and a space-switcher item (color mark, truncating name,
active-state). Rewrite the contract test to assert fixed regions by the existing positional style and
collection regions by per-entry shape and count, not by absolute index into the whole rail. This unit
does not wire real tab/space data in yet — it establishes the tree shape and its test contract that
U4/U5 project real snapshots into.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R BrowserChrome --output-on-failure`.

### U4 — Tab strip UI, commands, and accelerators

**Owner:** U4 implementer
**Files:** `src/main/browser_chrome.cc`, `src/main/browser_command.h`, `src/main/browser_window.cc`,
`tests/chrome/browser_chrome_contract_test.cpp`, `tests/browser_window_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U2, U3

Project the active space's `Tab` list into the tab-strip collection region defined by U3. Wire New
Tab (`Cmd/Ctrl+T`), Close Tab (`Cmd/Ctrl+W`, middle-click, close affordance), Next/Previous Tab
(`Cmd/Ctrl+Shift+[`/`]`), and direct-index switch (`Cmd/Ctrl+1..9`) through `BrowserWindow`'s command
dispatch into the active `Space`. Every tab-strip entry is keyboard-focusable with an accessible name/
role and announces active/inactive state, matching the Phase 2 rail requirement. Test command
dispatch reaches the right tab/space and that closing the last tab in a space does not leave the
window with zero tabs (define and test the fallback: either block closing the last tab, or close the
space — pick one and encode it in the test, don't leave it implicit).

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(BrowserChrome|BrowserWindow)' --output-on-failure`.

### U5 — Space switcher UI and commands

**Owner:** U5 implementer
**Files:** `src/main/browser_chrome.cc`, `src/main/browser_command.h`, `src/main/browser_window.cc`,
`tests/chrome/browser_chrome_contract_test.cpp`, `tests/browser_window_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U2, U3

Project `spaces_` into the space-switcher collection region. Wire New Space, Close Space (deletion is
unconditional per the design — releases its `CefRequestContext`, no confirmation UI), rename, reorder,
and switch (click or accelerator) through `BrowserWindow`. Same accessibility bar as U4. Test that
closing the active space selects a defined neighbor and that closing the last remaining space is
handled (define and test the chosen behavior, e.g. recreate a single default space rather than leaving
zero).

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(BrowserChrome|BrowserWindow)' --output-on-failure`.

### U6 — Split view

**Owner:** U6 implementer
**Files:** `src/main/browser_chrome.h`, `src/main/browser_chrome.cc`, `src/main/browser_window.cc`,
`tests/chrome/browser_chrome_contract_test.cpp`, `tests/browser_window_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U4

Implement pairing two tabs from the same space into `Space::split_` and rendering both
`CefBrowserView`s side by side in the content slot via a `BoxLayout` with a keyboard- and drag-
adjustable divider. Un-splitting (closing either half, or an explicit un-split action) returns the
surviving tab to full width and clears `split_`. Reject attempts to pair tabs from different spaces
at the command layer (the design scopes split view to one space; don't silently allow it). Test pair/
unpair transitions and that closing one half correctly restores single-view layout and state.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(BrowserChrome|BrowserWindow)' --output-on-failure`.

### U7 — Command palette

**Owner:** U7 implementer
**Files:** `src/main/command_palette.h`, `src/main/command_palette.cc`, `src/main/browser_window.cc`,
`src/main/browser_chrome.h`, `tests/command_palette_test.cpp`, `tests/CMakeLists.txt`
**Depends on:** U4, U5

Build the palette as a chrome-owned overlay, created lazily on first `Cmd/Ctrl+K`, hidden (not
destroyed) on close/Escape, with focus trapped while open and restored to the invocation point on
close. Populate results from read-only snapshots: open tabs in the active space (fuzzy-matched by
title/URL) and the space list; selecting a tab or space result requests the corresponding switch from
`BrowserWindow`. The "go to URL" affordance calls the same `AddressModel` validate-and-submit entry
point U2/Phase 2 already expose — do not add a second URL parser. Test result composition/fuzzy match
ordering, that Escape closes without navigating or switching, and that URL submission through the
palette is rejected/accepted by the identical rules `cef_address_parser_test.cpp` already covers (add
palette-specific tests for the delegation, not a parallel policy suite).

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R CommandPalette --output-on-failure`.

### U8 — SessionStore and clean-quit restore

**Owner:** U8 implementer
**Files:** `src/main/session_store.h`, `src/main/session_store.cc`, `src/main/browser_window.cc`,
`src/main/island_app.cc`, `tests/session_store_test.cpp`, `tests/CMakeLists.txt`
**Depends on:** U2

Define the session JSON schema (spaces: id, name, color, order, request-context storage directory;
tabs: id, last-committed URL, position; active indices) and `SessionStore::Save`/`Load` with schema
validation. Hook `Save` into the existing CEF close lifecycle (clean quit only — no periodic autosave,
no crash hook). Hook `Load` into startup before the Phase 1/2 fixed-page fallback: a missing,
unreadable, or schema-invalid file must behave identically to no session (log and fall back, don't
attempt partial recovery). Restored tabs navigate through the same address-submission path as manual
entry, so a URL that's no longer allow-listed is rejected the same way, not force-loaded. Test
round-trip serialize/deserialize, each malformed-file case falling back cleanly, and that a restored
URL violating current policy is rejected rather than bypassing validation.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R SessionStore --output-on-failure`.

### U9 — CI and package evidence for Phase 3

**Owner:** U9 implementer
**Files:** `.github/workflows/ci.yml`, `.github/workflows/package.yml`, `docs/supported-platforms.md`
**Depends on:** U1–U8

Confirm the existing native CI matrix and package checks still pass with the Phase 3 units (no new
target platforms, no new port, no signing/notarization claims). Update `docs/supported-platforms.md`
only with what this unit's own CI runs actually evidence — do not carry forward claims from Phase 2's
evidence without re-verifying them against Phase 3's changes.

**Tests:** the same clean-checkout sequence as Phase 2's U9, plus
`ctest --test-dir build -R '(Tab|Space|BrowserChrome|BrowserWindow|CommandPalette|SessionStore)' --output-on-failure`.

### U10 — Visual, keyboard, and accessibility acceptance

**Owner:** U10 implementer
**Files:** `docs/phase3-visual-acceptance.md`, `tests/manual/phase3_chrome_checklist.md`
**Depends on:** U4, U5, U6, U7, U8

Follow the same honesty posture Phase 2's U8 established (`docs/phase2-visual-acceptance.md`,
`tests/manual/phase2_chrome_checklist.md`): write a checklist for a human on real hardware to run —
tab create/close/switch, space create/close/switch with isolated cookies (verify two spaces logged
into the same site independently don't share a session), split view drag and keyboard resize, command
palette keyboard flow, and a full quit/relaunch session-restore pass. Do not record results as passing
without an actual run; leave the sign-off table empty until someone executes it.

**Tests:** manual only, per the checklist.

### U11 — End-to-end regression and targeted rework

**Owner:** U11 integrator
**Files:** only failing unit-owned files, plus any Phase 1/2 regression test named in a failure
**Depends on:** U1–U10

Run the clean-checkout sequence and repair only demonstrated Phase 3 integration failures. Rework is
targeted: model/identity defects return to U1; ownership/re-parenting/request-context defects return
to U2; chrome contract/shape defects return to U3; tab or space command defects return to U4/U5; split
defects return to U6; palette defects return to U7; restore defects return to U8; CI/package evidence
gaps return to U9; manual acceptance gaps return to U10. Do not use this unit for refactors, new
features, or Phase 4+ scope.

**Tests:**

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
python3 scripts/deps.py verify
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## Dependencies and handoff order

```text
U1 ─┬─ U2 ─┬─ U3 ─┬─ U4 ─┬─ U6 ────────┬─ U9 ─┐
    │      │      └─ U5 ─┴─ U7 ────────┤      ├─ U11
    │      └───────────────── U8 ──────┴─ U10 ┘
```

U1 has no dependencies. U2 and U3 both depend only on U1 and may run in parallel. U4 and U5 both
depend on U2+U3 and may run in parallel with each other, but U6 and U7 each depend on both U4 and U5
(split view needs the tab strip; the palette needs both tab and space snapshots). U8 depends only on
U2. U9 depends on all code units (U1–U8). U10 depends on U4/U5/U6/U7/U8. U11 is the only integration/
rework unit and depends on everything.

## Per-implementer commit policy

Each implementer creates commits only for their owned files after focused checks pass. Keep an
implementation file and its direct tests in the same commit; do not stage generated CEF/fonts/icon
output, `browser.pen`, tool state, or another implementer's files. Use the repository's plain
imperative English style. A unit may use multiple atomic commits when its owned changes are
independently reversible; each commit includes the required project attribution footer and co-author
trailer. U11 may commit only the minimal regression fix and its direct test after identifying the
responsible unit. No implementer pushes or rewrites history as part of this plan.
