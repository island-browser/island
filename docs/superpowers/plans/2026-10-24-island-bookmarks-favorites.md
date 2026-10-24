# Island 0.9 Bookmarks and Favorites Implementation Plan

> **For implementation workers:** Implement only after the owner accepts
> `2026-10-24-island-bookmarks-favorites-design.md`, including its binding §9 decisions. Keep this
> plan's checkboxes as the implementation record.

**Goal:** Turn the existing imported-bookmark store into a local bookmark library with folders,
global favorites, and per-space favorites, shown in the sidebar and usable through ⌘K and a local
Bookmarks page.

**Architecture:** `BookmarkStore` remains CEF-free and schema-validates one durable
`bookmarks.json`; it owns bookmark, folder, and favorite identity/order. `session.json` owns each
favorite's one virtual-tab state. `BrowserWindow` projects the active space's favorites and tabs into
`BrowserChrome`, restores a favorite virtual tab in the focused content slot, routes ordinary URL
openings through the existing address-validation path, and gives the local page an explicit
message/state contract. `BrowserChrome` is a renderer of snapshots, not a second bookmark model.
MCP/ACP tools cross the existing CEF-free agent host seam and action gate.

**Tech stack:** C++20, CEF 150 Alloy + `cef_views`, GoogleTest, pytest design checks, local HTML
pages rendered as `data:` URLs using `ThemeJson` CSS variables.

**Spec:** `docs/superpowers/specs/2026-10-24-island-bookmarks-favorites-design.md`.

## Global Constraints

- Work from the accepted 0.9 branch; §9 decisions are binding before implementation.
- This is one `CefWindow`; no sync, profiles/accounts, extensions, or multiple top-level windows.
- `BookmarkStore` is the only persistent bookmark/favorite library. `bookmarks.json` owns schema 2;
  `session.json` owns ordinary space/tab state plus favorite virtual-tab state, and `prefs.json` only
  preferences.
- A clean schema-1 `bookmarks.json` migrates at startup: retain `bookmarks.json.v1.bak`, then
  atomically replace the original with schema 2. Corrupt or invalid stores retain the current
  all-or-nothing empty fallback.
- All bookmark/favorite navigation goes through `ParseAndValidate` and the existing submission
  path. Do not create a second URL parser or force-load stored values.
- Do not restore the retired 0.8 sidebar New Tab row or reuse `ChromeViewId` 1042. New view IDs
  append after 1080; existing IDs 1001–1080 stay unchanged.
- No persistent `CefWindow` overlay. A local Bookmarks page lives in the content slot; menus and
  palette use their existing transient lifetimes.
- The pinned CEF distribution has no usable custom-view mouse event path for drag reorder. Provide
  menu, palette and keyboard reorder only.
- Native, Arc and Graphite × light/dark all use `ChromeTokens`/`ThemeJson` variables only. No hard-
  coded colors, blue selected states, or square focus outlines. Accent is reserved for active tab
  and address focus ring.
- The existing `kPaletteKindStyles` is the palette-kind registry. Keep `kBookmark` as the one
  bookmark result type and add new actions as `PaletteCommandEntry` records in the command table.
- A `.cc` source means updating the core source list and matching explicit tests list. New
  CEF-free units have CEF-free tests. Run focused tests per task; run full suites once after merge.
- Update `CHANGELOG.md` under `[Unreleased]`, `DESIGN.md`, `src/main/AGENTS.md`, the relevant
  `docs/**/AGENTS.md` files, and `docs/user-guide/` in the implementation change. The 0.9 draft
  itself does not change the shipped product documentation.
- Every implementation commit includes exactly one trailer:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A schema-1 store containing nested imported-looking names, duplicate case variants, and empty
   folders migrates to schema 2 without changing user-visible order or losing any accepted item.
2. Deleting a bookmark removes every global/space favorite reference atomically; removing from one
   favorite scope does not delete the underlying bookmark or another scope's reference.
3. A space ID is never rebound: deleting a space removes its favorite list, while renaming/reordering
   it preserves the same list.
4. A favorite is not blue-selected merely because its URL matches the active tab; only the actual
   tab uses accent. Check Native, Arc and Graphite in light and dark.
5. An invalid imported URL cannot bypass address policy from the sidebar, palette, Bookmarks page,
   new tab, or Peek path.
6. Reorder works fully by keyboard/menu with no drag implementation and announces the resulting
   position.
7. A trusted web site cannot silently add or remove local bookmarks/favorites: every mutation reaches
   the action-gate confirmation and a timeout/cancel writes nothing.

---

### Task 1: Establish schema 2 and its CEF-free operations

**Files:**
- Modify: `src/main/bookmark_store.{h,cc}`, `src/main/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/bookmark_store_test.cpp`

**Extension points:** `BookmarkState::kCurrentSchemaVersion`; the bookmark/folder/favorite record
types; `BookmarkStore` operation methods. Keep ordering, mutation, serialization and validation
inside this store rather than spreading ID logic into `BrowserWindow`.

- [ ] Write failing tests for startup schema-1 → schema-2 migration with a retained original-byte
  backup, backup/write failure that preserves the v1 file, deterministic schema-2 output,
  bookmark/folder/favorite ID uniqueness, atomic save, duplicate rejection, favorite reorder,
  removing one scope, deleting a bookmark, and invalid dangling/duplicate scope IDs.
- [ ] Replace schema-1 item-in-folder representation with IDs, folders and ordered favorite scope
  references as accepted by spec §2; retain a convenient read-only flattened projection for old
  palette callers during migration.
- [ ] Implement `AddOrFindByUrl`, folder CRUD/move, favorite add/remove/reorder, and deletion as
  durable operations with the accepted save-failure policy. Preserve case-insensitive library-wide
  import de-duplication.
- [ ] Run `cmake --build build --target island_tests && ./build/tests/island_tests
  --gtest_filter='BookmarkStoreTest.*'`.

### Task 2: Project the store through BrowserWindow and spaces

**Files:**
- Modify: `src/main/browser_window.{h,cc}`, `src/main/space.{h,cc}` only if a stable space-removal
  hook is absent, `src/main/session_store.{h,cc}`, `src/main/browser_chrome.{h,cc}`,
  `src/main/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/browser_window_test.cpp`, `tests/browser_chrome_test.cpp` or the existing focused
  collection/contract test files

**Extension points:** `BrowserWindow::UpdateChromeCollections`, favorite virtual-tab/session
serialization and space lifecycle hooks, `BrowserChrome::SetTabStripEntries` (replace with a typed
sidebar collection snapshot rather than smuggling favorites through `TabStripEntrySnapshot`), and
append-only `ChromeViewId`.

- [ ] Add failing tests that global favorites project before the active space's favorite rows and
  tabs, scope follows active `SpaceId`, a deleted space drops only its list, activation displays a
  virtual favorite tab without changing/pinning/adding an ordinary tab, and the virtual state
  round-trips through the session.
- [ ] Define `FavoriteEntrySnapshot` / sidebar collection snapshot with stable favorite identity,
  bookmark ID, title, URL, scope, favicon and displayed-virtual-tab metadata. Rebuild the projection
  after bookmark mutations, virtual-tab state changes, tab title/favicon changes, and space switches.
- [ ] Allocate only new `ChromeViewId`s after 1080. Keep the 1042 New Tab ID retired and make no
  new permanent New Tab button in expanded or collapsed sidebar.
- [ ] Run the focused browser-window/chrome tests and
  `ctest --test-dir build -R 'BrowserChrome.*|BrowserWindow.*' --output-on-failure` as appropriate.

### Task 3: Render the token-compliant sidebar collections

**Files:**
- Modify: `src/main/browser_chrome.{h,cc}`, `src/main/design_tokens.{h,cc}`, `src/main/row_raster.*`
  only if a tokenized favorite icon plate needs a reusable raster seam
- Test: `tests/chrome/browser_chrome_contract_test.cpp`,
  `tests/chrome/browser_chrome_style_test.cpp`, `tests/design_tokens_test.cpp`,
  `tests/design/test_token_contract.py`

**Extension points:** `ChromeViewId`, `CollectionButtonAction`, sidebar snapshot collections,
`ApplyCollectionTheme`, `PushCornerPatches`, and the `ChromeTokens`/DESIGN token contract.

- [ ] Write failing contract/style tests for six visible global icons plus a `+N` overflow control,
  section ordering, favorite menus, collapsed rail ordering, accessible names, 28/34-DIP density
  geometry, displayed-favorite neutral state, inline failure status, and the absence of a New Tab row.
- [ ] Build one scrollable sidebar collection with the six-icon global strip, `+N` overflow and add
  affordances, current-space favorite rows, and current-space tab rows; preserve fixed existing
  footer/space controls and collapse behavior.
- [ ] Apply the three-look, two-theme visual rules in spec §3. Do not make favorites active-blue;
  use rounded neutral raised/hover/focus token surfaces and existing favicon plates.
- [ ] Add/review design-token tests so every new native/CSS token has light/dark values and local
  pages/chrome never hard-code a color.
- [ ] Run the listed focused tests and `python3 -m pytest tests/design/test_token_contract.py`.

### Task 4: Add activation, menus, and non-drag reorder

**Files:**
- Modify: `src/main/browser_window.{h,cc}`, `src/main/browser_chrome.{h,cc}`, relevant platform
  menu files (`src/main/main_mac.mm` and cross-platform command dispatch), `src/main/keymap.{h,cc}`
- Test: `tests/browser_window_test.cpp`, `tests/keymap_test.cpp`,
  `tests/chrome/browser_chrome_contract_test.cpp`

**Extension points:** `CollectionButtonAction`, Browser/menu command dispatch table, `KeyAction` /
`kActions`, `Keymap` conflict registry, existing context-menu IDs, and palette command registry.

- [ ] Add failing tests for add/remove global and current-space favorites, duplicate URL reuse and
  page-title default, Cmd/Ctrl+Shift+D, Cmd/Ctrl+Alt+D, Cmd/Ctrl+Shift+B defaults and override
  conflicts, keyboard/menu reorder, virtual-tab activation versus ordinary new-tab opening, and all
  ordinary URL-opening paths using the address-validation seam.
- [ ] Implement favorite overflow/context actions: Open virtual tab, new ordinary tab, Peek, edit,
  proposed Name with agent, remove scoped favorite, and scoped move. Use Option+arrows locally for
  reorder and announce positions; do not add drag listeners.
- [ ] Add three configurable key actions with the exact accepted defaults, Settings labels and macOS
  menu equivalents. Keep Cmd/Ctrl+D for tab pinning.
- [ ] Run `./build/tests/island_tests --gtest_filter='KeymapTest.*:BrowserWindowTest.*:BrowserChrome*'`.

### Task 5: Expand ⌘K without a second bookmark result kind

**Files:**
- Modify: `src/main/command_palette.{h,cc}`, `src/main/command_palette_view.{h,cc}` if rendering
  folder/scope metadata needs a generic field, `src/main/browser_window.{h,cc}`
- Test: `tests/command_palette_test.cpp`, `tests/browser_window_test.cpp`

**Extension points:** `kPaletteKindStyles`, `PaletteBookmarkEntry`, `PaletteCommandEntry`,
`BrowserWindow::PaletteCommands`, and the palette selection dispatch switch.

- [ ] Write failing tests that full-library search preserves folder path metadata and order, favorites
  do not duplicate `kBookmark` rows, new commands appear in the Commands group, and each selected
  bookmark still reaches the one validator.
- [ ] Carry bookmark ID/folder path/scope labels through `PaletteBookmarkEntry`; keep its kind
  `kBookmark` and update the `kPaletteKindStyles` table only if its generic presentation needs it.
- [ ] Register Open Bookmarks, favorite toggle, New folder, and Import commands in the existing
  `PaletteCommands` registry; make contextual reorder commands available only when meaningful.
- [ ] Run `./build/tests/island_tests --gtest_filter='CommandPalette*:*Palette*'`.

### Task 6: Build the local Bookmarks page and its native message contract

**Files:**
- Create: `src/main/pages/bookmarks.html`
- Modify: `src/main/local_page.{h,cc}`, `src/main/local_pages_html.h`, root `CMakeLists.txt` if
  page embedding requires it, `src/main/browser_window.{h,cc}`, `src/main/pages/settings.html`
  only to add a navigation entry if accepted
- Test: `tests/browser_window_pages_test.cpp`, `tests/design/test_local_pages.py`,
  `tests/browser_window_test.cpp`

**Extension points:** `LocalPageKind`, generated/inlined local-page registry,
`BrowserWindow::OnConsoleMessage` local-page allow-list, `islandRender(state)`, and page message
type table. Every message validates its IDs and fields before mutating the store.

- [ ] Write failing tests for the local page URL, state JSON, accepted/rejected messages, folder
  delete confirmation, import handoff, title/URL editing, save rollback with inline error/Retry,
  token-only CSS, focus order, reduced motion and no external requests.
- [ ] Implement the local `data:` page described in spec §5 with folder/library search, CRUD,
  favorite labels/toggles and current import sources. Use native state render, not direct filesystem
  access from JavaScript.
- [ ] Use ThemeJson variables exclusively and rounded neutral focus/selection; ensure the page works
  in Native, Arc and Graphite light/dark.
- [ ] Run `python3 -m pytest tests/design/test_local_pages.py` and the focused page/window tests.

### Task 7: Cover virtual-tab lifecycle, sleep, Peek, and persistence failures

**Files:**
- Modify: `src/main/browser_window.{h,cc}`, `src/main/tab_sleep_policy.h` only if a pure policy seam
  needs a favorite-related guard, `src/main/peek_overlay.{h,cc}` only for a generic favorite open
  command path
- Test: `tests/browser_window_test.cpp`, `tests/tab_sleep_policy_test.cpp`,
  `tests/peek_state_test.cpp`, `tests/bookmark_store_test.cpp`

**Extension points:** `OpenFavoriteVirtualTab` / close-reset lifecycle, favorite virtual-tab session
serialization, tab sleep activation/restore path, `PeekState` transition API, split-pane selection,
space-removal lifecycle, and the bookmark save result seam.

- [ ] Write tests that a favorite opens its one virtual tab rather than an ordinary tab, close unloads
  and resets it to the favorite URL, split moves it between focused panes without duplication,
  sleep/wake restores session state, Peek is independent, agent targeting treats it as the person's
  tab, invalid stored URL is harmless, space deletion cleans scope/state, and save failure rolls back
  with a retryable status.
- [ ] Implement only the glue necessary to keep favorite library metadata separate from
  `CefBrowser`, retain favorite virtual-tab session state, and preserve Peek overlay lifetime. Do
  not create a persistent overlay.
- [ ] Run the listed focused tests.

### Task 8: Add owner-approved agent bookmark and favorite tools

**Files:**
- Modify: `src/agent/browser_tools.{h,cc}`, `src/agent/action_gate.{h,cc}`, `src/main/window_agent_host.{h,cc}`,
  `src/main/browser_window.{h,cc}`, `tests/agent/browser_tools_test.cpp`,
  `tests/agent/action_gate_test.cpp`, `tests/window_agent_host_test.cpp`
- Test: the listed agent/window tests; standalone `src/agent` build

**Extension points:** tool registration table in `BrowserToolbox`, `GateRule` / `Classify`,
`AgentBrowserHost` virtual seam, and the window-side bridge. `src/agent/` stays CEF-free.

- [ ] Write failing tests for bookmark/favorite list filtering, invalid add/remove, confirmation
  content, trusted-site non-bypass, Allow once/task, cancel, timeout, no writes before confirmation,
  and favorite virtual-tab targeting as the person's tab.
- [ ] Add `browser_list_bookmarks`, `browser_list_favorites`, and gated `browser_add_bookmark`,
  `browser_remove_bookmark`, `browser_add_favorite`, and `browser_remove_favorite`. Do not add bulk,
  import, folder mutation, or reorder tools.
- [ ] Run `cmake -S src/agent -B build-agent && cmake --build build-agent && ctest --test-dir
  build-agent --output-on-failure` plus focused root tests.

### Task 9: Documentation, acceptance, and release record

**Files:**
- Modify: `AGENTS.md`, `src/main/AGENTS.md`, `docs/AGENTS.md`, `docs/superpowers/AGENTS.md` if its
  archive/index convention changes, `DESIGN.md`, `CHANGELOG.md`, `docs/user-guide/README.md`,
  `docs/user-guide/SUMMARY.md`, and the appropriate user-guide page(s)
- Test: `tests/design/test_token_contract.py`, `tests/design/test_local_pages.py`; manual visual
  acceptance evidence under the existing process

**Extension points:** directory maps and Key Files tables, DESIGN token contract/version, GitBook
SUMMARY navigation, and `[Unreleased]` release notes.

- [ ] Document `BookmarkStore` schema 2/startup migration backup, favorite scopes and virtual-tab
  lifecycle, six-plus-overflow sidebar order, no-drag rationale, palette and page entry points,
  shortcuts, inline save-failure retry, import continuity, action-gated agent tools, and how to add
  a future sidebar collection/palette command.
- [ ] Add concise user-facing `[Unreleased]` release notes without changing `VERSION` until the
  release PR does so through `scripts/version.py`.
- [ ] Capture and visually review all six Native/Arc/Graphite × light/dark states for expanded and
  collapsed sidebar, Bookmarks page, ⌘K and focus/reorder states; complete the manual acceptance
  checklist with paths/evidence.
- [ ] Run final verification:

```bash
cmake -G Ninja -B build -S . -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/Users/impelix/hsse/my-browser/.claude/worktrees/reverent-khayyam-a6066d/build-search-root/_deps/googletest-src
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest tests/design
python3 scripts/version.py check
```
