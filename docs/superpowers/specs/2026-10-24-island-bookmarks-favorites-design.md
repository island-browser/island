# Island 0.9 — Bookmarks and Favorites

- **Status:** Draft — owner decisions recorded; acceptance required before implementation
- **Date:** 2026-10-24
- **Depends on:** the shipped 0.8 session, spaces, Native/Arc/Graphite appearance, bookmark
  import, command palette, tab sleep, Peek, and agent action gate
- **Supersedes:** only the deliberately limited bookmark-management non-goal in
  `2026-09-19-island-welcome-import-design.md`; its local import sources and URL-validation rule
  remain in force.

## Goals

1. Make bookmarks useful as a managed local library: folders, create, edit, delete, import, search
   and a dedicated Bookmarks page.
2. Add two persistent favorite scopes: **Global favorites**, visible in every space, and
   **space favorites**, visible only with their space.
3. Keep the sidebar Arc-like and compact: global favorite icons first, then current-space favorite
   rows, then current-space tabs. The removed New Tab row does not return.
4. Make the same model usable from ⌘K, menus, keyboard shortcuts, the local page, and the agent,
   while retaining the existing single URL validation/submission path.
5. Preserve Native, Arc and Graphite in light and dark with token-only colors: no blue selected
   states, no square focus outlines; only the active tab and address-field focus ring use the
   accent.

## Non-goals

- Sync, accounts/profiles, cloud backup, collaboration, sharing, browser-store extensions, and
  multiple top-level `CefWindow`s.
- A bookmarks bar, a restored sidebar New Tab row, drag-and-drop, or mouse reordering. The pinned
  CEF distribution does not expose the custom-view pointer events that a robust drag operation
  requires.
- A new URL parser or bypass of `BrowserWindow::ParseAndValidate` / `SubmitPaletteUrl`.
- Saving snapshots, credentials, page content, offline copies, tags, reading-list state, or history
  import.

## 1. User stories

- As a person, I can mark the page in my active space as a favorite and return to it from that
  space's sidebar.
- As a person, I can mark an important page as a global favorite and reach it in every space.
- As a person, I can keep a fuller set of bookmarks in named folders, find them in ⌘K, and organize
  them on a Bookmarks page.
- As a person, I can import from the existing local browser sources without losing prior imports or
  my manually organized bookmarks.
- As a keyboard user, I can add, remove and reorder favorites without drag-and-drop.
- As a screen-reader user, I can distinguish global favorites, this space's favorites, and tabs,
  and can operate every action by name.
- As an agent user, I can have the agent find saved references; any persistent bookmark mutation is
  explicit and, if agent tools are approved, goes through the action gate.

## 2. Model and persistence

### 2.1 One local library

`BookmarkStore` remains the single source of truth and continues to live beside the other Island
support files as `bookmarks.json`:

- macOS: `~/Library/Application Support/Island/bookmarks.json`
- Windows: `%APPDATA%\\Island\\bookmarks.json`
- Linux: `$XDG_DATA_HOME/Island/bookmarks.json` (or `~/.local/share/Island/`)

It is deliberately **not** in `session.json`: bookmarks and both favorite scopes survive a closed
space, a discarded session, and a fresh session restore. It is also not in `prefs.json`: they are
user content, not preference settings. `session.json` continues to own spaces, tab order, active
selection, pinned tabs, grouping, sleep-restoration inputs, and the session state of favorite
virtual tabs only.

Schema 2 replaces the schema-1 flat-folder library while retaining all imported data:

```json
{
  "version": 2,
  "next_id": 18,
  "global_favorite_ids": [3, 9],
  "space_favorite_ids": {
    "1": [12],
    "7": [3, 16]
  },
  "folders": [
    {"id": 1, "name": "Imported", "bookmark_ids": [3, 9]},
    {"id": 2, "name": "Work", "bookmark_ids": [12, 16]}
  ],
  "bookmarks": [
    {"id": 3, "title": "Island", "url": "https://island-browser.github.io/"}
  ]
}
```

IDs are positive, monotonically allocated local integers; no UUID or sync identity is implied.
A bookmark can be in exactly one folder, but it can be present in any number of favorite scopes.
Favorites are ordered lists of bookmark IDs, contain no duplicates within a scope, and do not copy
title or URL. A favorite is therefore also a bookmark; adding a current page to either favorite
scope first finds its exact case-insensitive URL match or creates a bookmark in **Unfiled**.

`global_favorite_ids` is one ordered list. `space_favorite_ids` is keyed by the persisted decimal
`SpaceId`; an empty list need not be written. A bookmarked page may appear globally and in several
spaces. Deleting the bookmark deletes every reference; deleting a space removes only that space's
favorite list when the session actually removes the space. Renaming or reordering spaces leaves the
key intact.

### 2.2 Migration and validation

`BookmarkStore::Load` accepts version 1 and version 2. For version 1, it assigns stable IDs in the
old folder/item traversal order, converts each old folder and item, and produces empty favorite
lists. On that launch it first durably copies the original bytes to `bookmarks.json.v1.bak` (replacing
the previous migration backup if one exists), then atomically replaces `bookmarks.json` with schema
2. If either write fails, Island leaves the original schema-1 file intact, keeps the successfully
parsed data read-only for that run, and reports the migration failure without claiming completion.
The migration never rewrites an unreadable, malformed, or schema-invalid document: current fallback
behavior remains an empty in-memory store plus the reported error.

The importer continues to append into its named imported folder and case-insensitively de-duplicate
URLs across the whole library. Import is permissive; stored URLs are validated only when opened.
Invalid or unsupported URLs remain visible and editable in the Bookmarks page but an activation is
a harmless no-op with an accessible error message. Empty titles use the URL/host fallback already
used by bookmark palette rows. Duplicate URLs are one record, not two favorites with diverging
titles.

Schema validation rejects non-positive/duplicate IDs, a `next_id` no greater than an existing ID,
unknown IDs in folders or favorites, duplicate bookmark membership across folders, duplicate IDs in
one favorite scope, bad space-ID keys, and missing title/URL/name strings. It loads an empty fresh
state on any validation error, matching the current all-or-nothing store policy.

### 2.3 Favorite virtual tabs

Every favorite scope entry owns exactly one virtual ("phantom") tab. Its stable session key is its
favorite scope plus bookmark ID: a global favorite and a same-bookmark favorite in a space are
different virtual tabs. The virtual tab is not a member of the space's ordinary tab list and never
gets a tab-strip/sidebar tab row. Its session record holds the favorite URL as reset URL plus the
restorable URL, history, scroll position, and normal sleep-restoration inputs; it is persisted in
`session.json`, not `bookmarks.json`.

Activating a favorite places that favorite's virtual tab in the focused current content slot. It does
not navigate, replace, pin, or create a tab in the person's ordinary space tab list. If its browser
was slept, Island recreates it and restores the last session state as far as CEF session restoration
allows. In the sidebar, that favorite alone is marked as the displayed content with a neutral rounded
raised state and `aria-current="page"`; it never receives the tab accent. The ordinary tab remains
the selected tab for its space, ready to reappear when the favorite virtual tab is closed.

## 3. Sidebar

The expanded sidebar is one scrollable collection between the existing address/navigation region
and the fixed space/footer controls. Global icons and the current space's rows scroll together with
tabs; the footer never scrolls away. The collapsed 52-DIP rail shows global favorite icons before
the active space's favorite and tab favicons, preserving the same logical order. It does not add a
New Tab control.

### 3.1 Layout and measurements

```
Expanded sidebar (248 Native; 200 compact)       Collapsed rail (52)
┌──────────────────────────────────────┐         ┌────┐
│ navigation / address                  │         │ G1 │ global favorite
│ Global favorites                      │         │ G2 │ global favorite
│ [◎] [▣] [◉] [＋3] [＋]                 │         │ ── │ section separator
│ Work · Favorites                      │         │ W1 │ space favorite
│  ◈ Project home                    ⋯  │         │ T1 │ tab
│ Work · Tabs                           │         │ T2 │ tab
│  ◈ Island                         ×   │         │    │
│  ◇ Reference                       ×   │         │ ·  │ spaces/footer
└──────────────────────────────────────┘         └────┘
```

The global grid displays at most six favorite icons. When more exist, its next control is `+N` /
More, which opens the global-favorites view in Bookmarks (with an equivalent ⌘K command); it does
not wrap or scroll the icon grid. The `＋` after that control means “Add current page as a global
favorite”; it is an icon button, not a New Tab affordance, and is hidden when no active page has an
acceptable URL. The equivalent current-space action is in the favorite section's overflow/menu and
⌘K; it does not add a permanent row.

| Look | Global strip | Space favorites | Tabs | Size and state |
|---|---|---|---|---|
| Native | “Global favorites” sentence-case header; 28-DIP icon plates, 4-DIP gaps | “`<Space> · Favorites`” header, 28-DIP source-list rows | “`<Space> · Tabs`” header, existing rows | headers 20 DIP; rows 28 compact / 34 comfortable; neutral `segment_selected` raised plate for hover, menus, and keyboard roving focus |
| Arc | No heavy header; 28-DIP rounded icon plates under the address; small “Favorites” label | soft rounded 32-DIP rows | existing Arc tab rows | canvas/tint derives only from appearance tokens; favorite rows use neutral raised plates, not space-color selection |
| Graphite | compact “Global” label and 28-DIP icon plates | 32-DIP rows under “Favorites” | existing Graphite rows | graphite surfaces/tokens only; same order and semantics |

The active tab keeps the only sidebar accent/active treatment. A favorite, even when it opens the
currently active URL, is never painted as selected: it receives neutral `segment_selected` or
hover/raised treatment. Address editing retains the only focus accent ring. All other focus is a
rounded neutral focus/raised plate; no square outline or hard-coded blue. Favicons use the existing
16-DIP favicon plate policy and all section, row and icon colors come from `ChromeTokens`.

### 3.2 Sidebar interactions

- Activating a global or space favorite shows its virtual tab in the focused content slot, restoring
  that favorite's last state rather than navigating the person's selected ordinary tab or adding a
  tab row. Cmd/Ctrl-click or middle click opens a transient Peek, and “Open in new tab” creates an
  ordinary tab through the existing path; no favorite opens another top-level window.
- The row/icon overflow menu offers Open, Open in new tab, Open in Peek, Edit bookmark, Remove
  from this scope, Move left, Move right, and (where applicable) Add/remove global or current-space
  favorite. Removing from a scope leaves its bookmark and other scopes intact.
- The current page's context/menu and the ⌘K Actions group offer **Add to global favorites**,
  **Add to `<Space>` favorites**, and the inverse actions when its canonical stored URL matches.
  Add creates an Unfiled bookmark when necessary, using the current page title with URL/host
  fallback, and focuses no dialog unless title editing is requested. **Proposed UI:** the favorite
  overflow menu and Bookmarks-row menu also offer **Name with agent**. It asks the agent for a short
  proposed name in inline editing; the person must apply it, so the agent never silently renames a
  bookmark. Exact placement and wording remain a UI proposal, not a separate product decision.
- Reorder is menu/palette/keyboard only: **Move left/right** reorders an icon strip; **Move up/down**
  reorders a row list. In the sidebar, focus a favorite and use Option+Up/Down for a space favorite
  or Option+Left/Right for a global favorite; the menu announces the new position. These bindings
  are local row commands, not new global configurable shortcuts. No drag behavior is implied.
- A favorite's `⋯` (or accessible context menu) supports its actions. Escape closes the menu and
  restores focus to the invoking rounded row/icon.

## 4. Keyboard and ⌘K

The current keymap does not occupy these defaults; they are new configurable `KeyAction` entries
and use the existing `KeyActions()` table, conflict detection, Settings shortcuts UI, macOS NSMenu
mapping and cheatsheet conventions.

| Action | Default | Scope |
|---|---|---|
| Toggle global favorite for current page | Cmd/Ctrl+Shift+D | creates/removes the matching global favorite |
| Toggle current-space favorite for current page | Cmd/Ctrl+Alt+D | creates/removes the matching space favorite |
| Open Bookmarks | Cmd/Ctrl+Shift+B | opens the Bookmarks local page in the content slot |

`Cmd/Ctrl+D` remains **Pin or unpin tab**. The new actions are not automatically put in the
ten-row cheatsheet; Settings → Shortcuts and ⌘K expose them. In addition to the current bookmark
results, ⌘K gains a **Bookmarks** command group with Open Bookmarks, Add bookmark, Add/remove
global favorite, Add/remove current-space favorite, New folder, Import bookmarks, and scoped
reorder commands when a favorite is selected. Its existing bookmark-result section remains the
full library search, labelled with folder path as secondary text; favorites may show a `Global` or
space-name badge but are not duplicated as separate search results.

The palette kind registry is still `kPaletteKindStyles`: `kBookmark` remains the one result kind.
New action rows are `PaletteCommandEntry` records from the existing command registry, not a new
hard-coded search branch. Selection retains the palette's existing keyboard semantics and its
look-specific neutral/active token behavior.

## 5. Bookmarks page

`src/main/pages/bookmarks.html` is a local `data:` page in the content slot, parallel to Settings,
All tabs, Plan and Report. It has no network requests and sends validated `\x01island:` messages;
native state arrives through `islandRender(state)`.

```
┌ Bookmarks ──────────────────────────────────────────────────────────────┐
│ Search bookmarks…                                  Import  New folder   │
├ folders ──────────────┬ bookmarks in Work ───────────────────────────────┤
│ ▾ All bookmarks (24)  │ [favicon] Project home       example.test     ⋯ │
│ ▾ Work (8)            │ [favicon] Design notes       docs.example     ⋯ │
│   Imported (6)        │                                                    │
│   Unfiled (2)         │                                                    │
└───────────────────────┴──────────────────────────────────────────────────┘
```

The page supports keyboard-first folder disclosure, search across title/URL/folder, create/rename/
delete folders, create/edit/delete bookmarks, move a bookmark to a folder, import through the
existing detected-source/import path, and favorite toggles with visible scope labels. Deleting a
non-empty folder requires choosing “Move bookmarks to Unfiled” or “Delete bookmarks”; the latter
has an in-page confirmation. The sidebar/palette update immediately after each successful native
mutation. Opening a bookmark uses the same validator as the palette. A failed native save rolls the
attempted change back. The Bookmarks page then shows a non-modal inline live status directly below
its toolbar with the error and a Retry action; Retry repeats only that rolled-back operation.

It uses real buttons, links and form labels; a two-column responsive layout collapses to a single
logical reading order on narrow content. Page CSS uses `ThemeJson` variables only. Selected folder,
row hover, pressed controls, and non-address focus use neutral raised token surfaces; primary
destructive confirmation may use the existing danger token, never an ad-hoc blue selection.

The sidebar presents the same failure without a modal: a compact live status row immediately above
its fixed footer, with the error and Retry. It remains until Retry succeeds, the person dismisses it,
or a newer bookmark operation replaces it. The sidebar does not need to navigate to the Bookmarks
page to make failure visible.

## 6. Accessibility

- Global icons are named “Global favorite: `<title>`”; their position and total are included in the
  accessible description. The `+N` control is named “Show N more global favorites.” The add button
  is “Add current page as a global favorite.”
- Section headers identify global favorites, current-space favorites, and current-space tabs. A
  screen reader can never mistake one scope for another.
- Every favorite row has an independently named overflow menu and an action-specific confirmation
  for deleting a bookmark or folder. Decorative favicon plates are not focusable.
- Sidebar traversal is predictable: global strip, space favorites, tabs, then existing footer.
  Arrow navigation within a collection and Tab between controls must retain visible rounded neutral
  focus. Reorder announces “Moved `<title>` to position N of M.”
- `bookmarks.html` uses a labelled search field, `aria-current` for the active folder, live status
  text for import/mutation outcomes, Escape to exit inline editing, and a confirmation focus trap
  only while that confirmation is visible. It honors Reduce Motion.

## 7. Lifecycle and edge cases

- **A favorite virtual tab:** activation always restores its one virtual tab in the focused content
  slot; it never switches to a URL-equal ordinary tab, nor changes that tab. The displayed favorite
  gets the neutral active-content marker described in §2.3, while URL-equal ordinary tabs stay
  unaltered.
- **Closing a favorite virtual tab:** Close unloads its browser, clears its restorable history and
  scroll state, and resets it to the favorite URL for its next activation. The favorite/bookmark and
  ordinary tab list survive. The focused slot returns to its selected ordinary tab. In split view,
  it returns only that pane; the other pane is unchanged.
- **Split view:** a favorite virtual tab may occupy either focused pane, replacing only that pane's
  displayed ordinary tab. One virtual tab cannot occupy both panes: activating it in the other pane
  moves the display there and returns the former pane to its selected ordinary tab.
- **Tab sleep:** a non-displayed favorite virtual tab is eligible for the ordinary sleep policy;
  sleep releases its `CefBrowser` but retains its session record. A displayed favorite virtual tab
  is not eligible. The favorite remains visible and can restore when activated.
- **Peek:** opening a favorite in Peek creates the existing one-window transient card from the
  favorite URL. It does not activate, mutate, or consume the favorite virtual tab's state. Removing
  or editing the favorite never closes Peek; “Open in new tab” creates an ordinary tab.
- **Deleted bookmark:** remove all global and space references atomically before rendering. Empty
  folders remain unless the user deletes them. Removing a scoped favorite disposes its virtual-tab
  session record; if it is displayed, the slot returns to its selected ordinary tab. Removing the
  final favorite hides its section header, not the global strip's add affordance.
- **Deleted/renamed space:** a renamed space updates labels immediately. On permanent space
  deletion, its scoped IDs are removed on the next bookmark-store save; no orphan scope is
  presented. A stale ID from a corrupt/manual file is schema-invalid rather than silently rebound.
- **Import and duplicates:** imported duplicate URLs reuse the existing bookmark's title and folder;
  imports never auto-favorite. A bookmark may be unfavorited without changing folder order.
- **Local/start/report/reader URLs:** they cannot be newly favorited or bookmarked through the
  current-page commands. Existing imported unsupported URLs remain editable but cannot navigate.
- **Persistence failure:** a failed save rolls the attempted mutation back before projection, so
  every surface continues to show durable state. Show the inline sidebar or Bookmarks-page status
  with error and Retry; do not claim success or retain an unsaved in-memory edit.

## 8. Agent tools

The CEF-free agent kernel must not depend on `BookmarkStore` or CEF. The window-side
`AgentBrowserHost` provides an asynchronous bookmark host seam, just as it does for tabs and page
tools. 0.9 MCP/ACP tools are `browser_list_bookmarks` and `browser_list_favorites` (read-only,
optionally folder/scoped) plus `browser_add_bookmark`, `browser_remove_bookmark`,
`browser_add_favorite`, and `browser_remove_favorite`. No bulk mutation, import, folder mutation,
or reorder tool lands in 0.9.

Listing is free. Every add or remove is a persistent browser-state change and must be classified by
an explicit action-gate rule. It always asks first with the existing card/120-second timeout/Allow
once/Allow for task semantics; a trusted site does not bypass this local-state confirmation. The
card names the operation, title, host, folder and favorite scope where applicable. Password/card
rules remain irrelevant because the tools receive no form data.

When agent browser tools address the displayed favorite virtual tab, it is the person's tab: page
actions, cross-origin navigation, close, and every existing consequential action retain the normal
action-gate treatment. A virtual favorite tab is not an agent-owned background tab merely because
it was opened from a favorite.

## §9 Decisions

Binding owner decisions, 2026-10-23:

1. Island has bookmarks, per-space favorites, and global favorites.
2. The sidebar order is global favorite icon grid, current space's favorite rows, then current
   space's tabs; global favorites appear in every space.
3. Full bookmark management (folders and import) lives in ⌘K and on a separate Bookmarks page.
4. The sidebar New Tab row removed in 0.8.0 remains removed.
5. Native, Arc and Graphite each work in light and dark. Theme tokens are the only UI colors; no
   blue selected states or square focus outlines; only the active tab and address focus ring use
   the accent.
6. Drag-and-drop reordering is out: menu, palette and keyboard reorder instead.

Binding owner decisions, 2026-10-24:

1. **Migration write timing.** Owner: «при запуске». **Ruling:** migrate schema 1 to schema 2 on
   launch: first retain the schema-1 backup, then atomically write the migrated file. See §2.2.
2. **Favorite activation and existing tabs.** Owner: «в текущей но не должен создавать, там мнимая
   вкладка и он открывает ее в последнем состоянии». **Ruling:** every favorite scope entry owns one
   virtual tab. Activating it shows that tab in the focused current content slot in its last
   restorable state; it neither navigates the person's ordinary active tab nor adds an entry to the
   space tab list. Close/reset, sidebar active state, split view, Peek, sleep, and agent treatment
   are defined in §§2.3 and 7.
3. **Save failure.** Owner: «откатывается, надо статус как то отображать». **Ruling:** roll the
   failed mutation back and show a non-modal inline error with Retry in the sidebar or Bookmarks
   page, as defined in §§5 and 7.
4. **Agent scope.** Owner: «да». **Ruling:** 0.9 exposes read-only list tools plus action-gated
   add/remove bookmark and favorite tools to MCP/ACP. Listing is free; every add/remove asks first,
   including for trusted sites. See §8.
5. **Default title and agent naming.** Owner: «можно через агента мб». **Ruling:** a new favorite
   uses the page title by default, with URL/host fallback. **Name with agent** is an optional naming
   action that proposes a short name for the person to apply; its exact UI placement is a proposal.
6. **Global-grid capacity.** Owner: «6+». **Ruling:** display six global-favorite icons and then a
   `+N` / More overflow control; keep the separate current-page add affordance. See §3.1.

There are no remaining owner OPEN decisions in this phase. The proposed exact UI placement for
**Name with agent** is deliberately not a blocking product decision.
