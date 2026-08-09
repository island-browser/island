# Island Browser — Phase 3 Design

## Status and baseline

This is the canonical, decision-complete design for Phase 3. Phase 2 remains the implemented
baseline: one `CefWindow`, a native `BrowserChrome` (286 DIP focus rail, contextual address control,
one active-tab focus representation — explicitly not a tab system), OS light/dark tokens, and exactly
one `CefBrowserView`/`NavigationState` pair owned directly by `BrowserWindow`. See the
[Phase 2 design](2026-08-08-island-browser-phase2-design.md) and
[Phase 2 plan](../plans/2026-08-08-island-browser-phase2.md).

Phase 3 turns Island into a multi-tab, multi-space browser with split view, a command palette, and
session restore. This is the single largest change to the project since Phase 1 and touches the
ownership model of every chrome-adjacent class. It is scoped as one phase (not split into Phase 3/4/5)
per explicit product direction, but is implemented as dependency-ordered units, the same way Phase 2
was, because later units (Spaces, Split view, Command, Session restore) all sit on top of the tab
foundation and cannot be built or reviewed independently of it.

## Scope

Phase 3 delivers all of the following:

- **Tabs.** A window can hold more than one browser surface. Each tab owns exactly one
  `CefBrowserView`/`CefBrowser` and one `NavigationState`. A tab strip in the rail lists open tabs in
  the active space, supports creating (`Cmd/Ctrl+T`), closing (`Cmd/Ctrl+W`, middle-click, or a close
  affordance), and switching tabs (click, `Cmd/Ctrl+1..9`, `Cmd/Ctrl+Shift+[`/`]` for previous/next).
- **Spaces.** A window holds an ordered list of named, colored spaces. Each space owns its own tab
  list, its own active-tab index, and its own `CefRequestContext` — cookies, local storage, cache, and
  any other per-context CEF state are isolated per space, not shared. A space switcher in the rail
  (mirroring `browser.pen`'s space-switch mock) lists spaces and supports creating, renaming,
  reordering, deleting (with confirmation if it holds unsaved-state tabs is out of scope — deletion is
  unconditional in Phase 3), and switching spaces (click or from the command palette).
- **Split view.** Within a space, any two of that space's tabs can be paired into a side-by-side
  split occupying the browser-content region, with a draggable divider. Splitting and un-splitting are
  explicit user actions (drag a tab onto another, or a "split with..." affordance); a split is a
  transient view arrangement, not a new persisted entity — closing either half returns the surviving
  tab to full width.
- **Command palette.** A keyboard-first overlay (`Cmd/Ctrl+K`) over the current window listing: open
  tabs in the active space (fuzzy-matched by title/URL, activates on selection), other spaces
  (switches the active space on selection), and a "go to URL" affordance that hands off to the same
  address-submission path as the rail's address control (same validation/allow-list — the palette does
  not add a second URL-parsing implementation). Escape closes it without side effects.
- **Session restore.** On normal application quit, Island persists spaces (name, color, order,
  request-context storage directory) and, per tab, its last-committed URL and position — not
  navigation history, which does not exist yet in this project. On next launch, if a valid session
  file exists, Island reconstructs spaces/tabs/active selections from it and skips the Phase 1/2 fixed
  `data:` startup page for the restored window; if the file is absent, unreadable, or fails validation,
  Island falls back to today's fixed startup page rather than guessing at partial state.

## Explicit non-goals

The following are not Phase 3 behavior, even though they appear in `browser.pen` or are adjacent to
what's being built:

- Frame 07/08's literal top-tabs/hybrid-tabs visual treatment is a reference for behavior, not a
  pixel contract the way Frame 01/06 were for Phase 2 — Phase 3 does not require matching them
  exactly; the chrome/design review at spec-approval time is authoritative over the mock.
- Crash recovery / periodic autosave of session state. Session restore in this phase covers **clean
  quit only**; a crash or force-quit loses unsaved tab state, matching how Phase 3 is scoped, not a
  guarantee this project makes generally.
- Tab/space drag-and-drop reordering between windows, tab hibernation/discarding for memory pressure,
  reader mode, downloads, history UI, bookmarks, settings, sharing, agent actions, extensions, sync,
  profiles/accounts, or multiple top-level `CefWindow`s. Island remains one window.
- Full browsing history. "Last-committed URL" persisted for session restore is not a history feature
  and must not grow into one (no back-stack persistence, no history UI, no search over past URLs).
- Any change to port `9222`, signing, notarization, or platform-support claims beyond what Phase 3's
  own CI evidence proves.

## Architecture

### Ownership model

```text
IslandApp
└── BrowserWindow (one CefWindow)
    ├── BrowserChrome (rail: nav controls, address control, tab strip, space switcher)
    ├── CommandPalette (overlay, created lazily on first Cmd/Ctrl+K, hidden otherwise)
    ├── SessionStore (owns the on-disk session file path and (de)serialization)
    └── std::vector<Space> spaces_; size_t active_space_index_;
        Space
        ├── name, color, CefRequestContext (created once, held for the space's lifetime)
        ├── std::vector<Tab> tabs_; size_t active_tab_index_;
        └── std::optional<SplitPairing> split_; // indices of the two tabs in a live split, if any
            Tab
            ├── CefRefPtr<CefBrowserView> browser_view_
            ├── CefRefPtr<CefBrowser> browser_ (set once OnBrowserCreated fires)
            └── NavigationState navigation_state_
```

`BrowserWindow` no longer owns a single `browser_view_`/`browser_`/`navigation_state_` triad directly;
those move onto `Tab`. `BrowserWindow` owns the `spaces_` vector, dispatches window-level commands
(new tab, close tab, next/previous tab, new space, switch space, toggle split, open palette) to the
active space/tab, and re-parents the active tab's (or split pair's) `CefBrowserView`(s) into
`BrowserChrome`'s content slot whenever the active tab or space changes. Only the active tab's (or
split pair's) `CefBrowserView` is ever attached to the view tree at a time; inactive tabs keep their
`CefBrowserView`/`CefBrowser` alive but detached (CEF supports an off-screen-parented `CefBrowserView`
between `Detach`/`AddChildView` calls — this is not a novel technique, it's how Phase 2 already treats
the single view during window construction before the first attach).

`IslandApp` continues to own exactly one `BrowserWindow`. This phase does not introduce multiple
top-level windows.

### Tab and Space identity

Tabs and spaces are identified by a monotonically increasing `TabId`/`SpaceId` (`std::uint64_t`,
process-lifetime unique, not reused after close), not by vector index — vector index changes on
reorder/close and must never leak into persisted state, chrome view IDs, or command-palette results.

### BrowserChrome and the ChromeViewId contract

Phase 2's `ViewTreeContract()` returns a fixed-shape tree with hardcoded child counts and positional
indices (`tests/chrome/browser_chrome_contract_test.cpp` asserts exact `children.size()` and indexes
like `rail.children[5]`). That contract cannot represent a variable-length tab strip or space list.
Phase 3 replaces it with a contract that separates **fixed** rail regions (window/navigation controls,
address row — unchanged from Phase 2, same `ChromeViewId` values) from **collection** regions (tab
strip, space switcher), where collection regions are asserted by structural shape (each entry has a
title/color/close-affordance node in a fixed sub-shape) and count, not by absolute positional index
into the whole rail. Existing fixed-region `ChromeViewId` values (`kBack`, `kForward`, `kReload`,
`kAddress`, etc.) keep their Phase 2 integer values; new IDs for tab-strip entries, the space switcher,
and the command palette are allocated starting after the highest Phase 2 value
(`kActivePageIndicator = 1018`), never reusing or renumbering existing ones.

A tab-strip entry node exposes: favicon-or-fallback, title (truncates, same rule as Phase 2's
active-tab representation), a close affordance, and an active/inactive visual state. A space-switcher
entry exposes: color mark, name (truncates), and an active/inactive visual state.

### NavigationState, AddressBarModel

Both become per-`Tab` members instead of per-`BrowserWindow` singletons. `BrowserChrome`'s projection
of navigation/address snapshots into the rail switches from "the window's one state" to "the active
tab's state" — on tab/space switch, `BrowserWindow` re-subscribes chrome to the newly active tab's
`NavigationState`/`AddressBarModel` and detaches from the previous one, mirroring how Phase 2 already
detaches observers on close.

### CommandPalette

A chrome-owned overlay view (not a second `CefWindow`), created lazily on first invocation and reused
thereafter (hidden, not destroyed, between uses — matching the rest of chrome's "no work while
invisible" posture is a nice-to-have, not a hard requirement). It receives read-only snapshots of
open tabs (active space only) and the space list from `BrowserWindow`; on tab selection it requests a
tab/space switch; on "go to URL" submission it delegates to the same `AddressModel`
validate-and-submit path the rail uses — there is exactly one URL-policy implementation in this
project, reused, not duplicated.

### SessionStore

Owns a single JSON file at a platform-appropriate app-data directory (macOS:
`~/Library/Application Support/Island/session.json`; Windows/Linux locations follow the platform's
standard app-data convention — exact paths are a unit-level decision, not re-litigated here). On clean
quit (the existing CEF close lifecycle path), `BrowserWindow` asks `SessionStore` to serialize
`spaces_` (name, color, order, request-context storage directory) and each tab's last-committed URL
and position. On startup, before falling back to the Phase 1/2 fixed `data:` page, `BrowserWindow`
asks `SessionStore` to load and validate the file; a missing, unreadable, or schema-invalid file is
treated identically to "no session" — Island does not attempt partial recovery of a malformed file.
Restored tabs navigate to their persisted URL through the same address-submission path (so a URL that
was valid at save time but violates the current allow-list, e.g. after a policy change, is rejected
the same way manual entry would be, not force-navigated).

### CefRequestContext per space

Each `Space` creates its `CefRequestContext` once, at space-creation time (either from a fresh
`CefRequestContextSettings` with a space-specific `cache_path` for a new space, or restored from a
persisted `cache_path` for a session-restored space), and holds it for the space's lifetime. Deleting
a space releases the context; Phase 3 does not implement reference-counted context reuse across
spaces or explicit "clear space data" — that is future scope.

## Input, commands, and accessibility

- New window-level commands: New Tab, Close Tab, Next/Previous Tab, New Space, Close Space, toggle
  Split with the adjacent tab, Open Command Palette. Each gets a documented accelerator (see Scope)
  and a menu/chrome entry point — Phase 3 does not require a full menu bar, but every new command
  must be reachable without memorizing a shortcut (a chrome-visible button or palette entry).
- Tab-strip and space-switcher entries are keyboard-focusable and follow the same
  accessible-name/role requirement Phase 2 set for the rail; each announces title/name and
  active/inactive state.
- The command palette traps focus while open, returns focus to its invocation point on close, and its
  result list is navigable by arrow keys with Enter to activate — same pattern as any accessible
  combobox/listbox, not a novel interaction to design from scratch.
- Split view's divider is keyboard-adjustable (arrow keys while focused) in addition to drag, so
  resizing is not mouse-only.

## Build, package, workflow, and acceptance

No change to the CEF/runtime/package baseline, port `9222`, or target matrix. `CefRequestContext`
per space means the packaged app now creates per-space storage directories under its existing app-data
location at runtime — this is application behavior, not a new packaged asset, and needs no CMake/
package-script changes beyond what Phase 2 already established for runtime asset staging.

Acceptance requires: unit coverage for `Tab`/`Space` ownership and identity, `SessionStore`
serialize/deserialize round-trips (including the "malformed file falls back cleanly" case), command
palette result composition and URL-submission delegation, and the restructured `BrowserChrome`
view-tree contract (fixed regions unchanged from Phase 2, collection regions asserted by shape/count);
existing Phase 1/2 tests remain green; and a manual/visual acceptance pass (tabs, spaces, split,
palette, restore-after-quit) on macOS arm64 before other targets, following the same
evidence-over-claims posture `docs/phase2-visual-acceptance.md` established — this design does not
itself constitute that evidence.
