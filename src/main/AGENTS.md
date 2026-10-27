<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-10-18 -->

# main

## Purpose

The Island browser application: CEF app/window lifecycle, the native `cef_views` chrome, address
handling, navigation state, design tokens, icon and font resource lookup, and the per-platform
process entrypoints. Most of this compiles into the `island_browser_core` static library, which the
platform executable links.

## Key Files

| File | Description |
|------|-------------|
| `CMakeLists.txt` | Defines `island_browser_core` plus the macOS bundle / Windows / Linux executable wiring |
| `island_app.{h,cc}` | `CefApp` + `CefBrowserProcessHandler`; owns `BrowserWindow`, dispatches `BrowserCommand`, holds navigation/chrome observers |
| `browser_window.{h,cc}` | Top-level `CefWindow` host, owns the `Space` vector, creates the active tab's `CefBrowserView`, and drives the CEF close lifecycle |
| `browser_chrome.{h,cc}` | Native chrome layout; declares the `ChromeViewId` enum (`kRoot = 1001` …). Native's sidebar bottom is a footer with a switcher row (segments, `+`, overview, settings; `kRailSwitcherRow`) above the full-width Agent entry (`kRailAgentButton`); the toolbar has no Agent button under Native, and a collapsed rail relayout lays its toolbar content immediately so Back/Forward clear the macOS traffic lights |
| `chrome_snapshot.h` | Observable snapshot struct + `ChromeObserver` seam used by tests |
| `browser_command.h` | The Back / Forward / Reload command enum |
| `navigation_state.{h,cc}` | Back/forward/reload availability and navigation snapshot |
| `address_bar_model.{h,cc}` | Address field text/editing state. `kAddressPlaceholderText` is drawn (textfield placeholder; Native's idle label) only while the field is empty and at rest, never stored as text. `UpdateSearchQuery` shows a search engine's query as the resting text |
| `address_policy.{h,cc}` | Validates an already-parsed absolute http/https address (allowed schemes, hosts, ports) |
| `address_input.{h,cc}` | CEF-free address-bar heuristic: explicit scheme / host-like text becomes a URL (bare hosts get `https://`), everything else is a web search. Used only by `BrowserWindow::ResolveAddressInput` |
| `search_engine.{h,cc}` | CEF-free table of web search engines (Google default, DuckDuckGo, Yandex), `BuildSearchUrl`, `SearchQueryFromResultsUrl`, and the start page's remembered mode. Address display rule: `BrowserWindow::UpdateAddressBarForNavigation` shows the query, not the URL, when `SearchQueryFromResultsUrl` matches the committed URL (https, the row's host or one of its `extra_results_hosts` (Yandex: yandex.ru, ya.ru), the exact path, its query parameter; other parameters ignored). The helper returns nullopt, so the URL shows, when the decoded query classifies as a URL (`ClassifyAddressInput`), holds a control, is not valid UTF-8 (`IsShowableQuery`), or has a non-ASCII code point that is not a letter, mark, number or space separator (`IsDisplayShownNonAscii` over the generated `query_display_ranges.h`; regenerate with `scripts/gen_query_display_ranges.py`). The URL stays in the tab (Copy URL, the agent, bookmarks). To recognise another engine, add its row to the table; the match is derived from `url_prefix`, so no second list exists, and `GivenEveryEngineRow...` checks each row's shape. Bing is not in the table, so its results URLs are shown as they are |
| `start_tiles.{h,cc}` | CEF-free speed-dial model for the start page: `BuildStartTiles` turns the most-visited history into at most six `{title, url, host}` tiles (http/https only, no credentials, one per host, host as the label fallback); `IsWebTileUrl` guards `open_tile`. `BrowserWindow` refreshes them on the page's `ready` message from `HistoryRecorder::MostVisitedAsync`, which groups visits by `WebUrlHost` before its limit of six applies |
| `cef_address_parser.{h,cc}` | CEF-dependent URL parsing behind the policy seam |
| `design_tokens.{h,cc}` | `ArgbColor`, the token palette shared with `tests/design/`, the Arc `canvas/selected/hover/well/plate` surfaces, `AppearanceSettings` and `ChromeTokens::Customized` (tint, density, radius, inset, shadow, side, font) |
| `page_card.h`, `macos/page_card_mac.mm` | Native macOS overlay that rounds and shadows the page card and repaints row/pill corners (`PageCardStyle`, `CornerPatch`); no-op elsewhere. `BrowserChrome::PushCornerPatches` runs on layout, pointer, tab-strip, space-switcher and sidebar changes and starts a 150 ms poll burst (8 quiet ticks), then a 1 s tick (`CornerPatchPollDelayMs`), because CEF reports no scroll events |
| `row_raster.{h,cc}` | CEF-free rounded-rect raster used for favicon plates (and tested) |
| `icon_catalog.{h,cc}` | Resolves icon names/sizes/scales against `resources/island/icons/manifest.json` |
| `app_resources.{h,cc}` | Runtime resource-directory resolution (fonts, icons) |
| `font_registry.h` | Font registration seam, implemented per platform |
| `app_runtime.{h,cc}` | Runtime seam shared by the app and tests |
| `startup_options.{h,cc}` | Command-line parsing, including `--island-smoke-test`; production leaves the initial URL empty so `BrowserWindow` opens the embedded start page |
| `lifecycle_config.h` | Shutdown/lifecycle constants |
| `sidebar_state.{h,cc}`, `collapsed_rail.h` | CEF-free expand/collapse state (persistent choice in prefs `sidebar_collapsed`, transient expansion while editing the address from the icon rail) and the icon rail's metrics plus the space-dot raster |
| `favicon_policy.h` | CEF-free favicon rules: web pages only, 16 DIP downloads, stale-result and clear-on-navigation checks (bookkeeping lives on `Tab`) |
| `tab.{h,cc}`, `tab_id.h` | Move-only tab model identified by `TabId`, holding the per-tab `CefBrowserView`/`CefBrowser` seam and the Settings-document-ready bit that clears on navigation/view replacement |
| `space.{h,cc}`, `space_icon.h` | Named, colored space with an optional validated Lucide icon id, ordered tabs, active selection, `SplitPairing`, and the space's `CefRequestContext`. `space_icon.h` owns `kSpaceIconChoices` (24 ids) and `SpaceIconPaletteCommands()` |
| `space_icon_cache.h` | Template cache of rasterized space icons keyed by id, size and color (`SpaceIconCache<Image>`); a failed load is retried and `Clear()` drops entries. Used by `IconCatalog::LoadSpaceIcon` so switchers do not reread PNGs on each update |
| `session_store.{h,cc}` | Session JSON round-trip with a `SessionError` taxonomy; not yet called by any lifecycle code. Schema stays version 1; 0.10 adds optional space members `web_data_key`, `import_key` and `routes` (written only when non-empty; a wrong-typed value loads as absent; unknown members are ignored) |
| `session_save_scheduler.{h,cc}` | CEF-free debounce and max-latency decision for session saves (R-S2): a save is due 2 s after the last change and at most 10 s after the first unsaved one. The clock is a parameter; `MarkDirty` returns the deadline to arm, `Due` / `SaveFinished` / `Cancel` drive the owner. A failed save stays dirty but is not due until the next change. Not yet driven by any window code (the window wiring is 0.10 T10) |
| `space_routing.{h,cc}` | CEF-free host routing rules (R-R1). `NormalizeRoutePattern` takes a bare host or http(s) URL and gives a lowercase host without port, trailing dot or `www.` (IP literals, localhost and dotless names are rejected; the last-label rule is the address bar's). `RouteForUrl` matches the host and its subdomains, never a lookalike, longest pattern wins, and reads only http(s) (`WebUrlHost`). `AssignRoute` gives a host to one space and removes it from the others; `RemoveRoute` drops it. The rules live in `Space::routes()`. Not yet applied to tabs (R-R2 is 0.10 T15). Extension points: a new rule kind is a new normalizer and a new branch in `RouteForUrl`; the window only calls these functions |
| `web_data.{h,cc}` | CEF-free per-space web data on disk (R-D3, R-D5). A space's cookies live in `<root>/Spaces/<web_data_key>`, where the key is 32 lowercase hex digits from `std::random_device`. `IsValidWebDataKey` is the only gate: `SpaceWebDataPath` returns an empty path for any other key, so a hand-edited session cannot leave `Spaces/`. `WriteWebDataTombstone` writes `.island-delete`. `SweepWebData` (startup) removes tombstoned directories whose key is not live, keeps the directories still named by the session, reports orphans (no tombstone, not live) without deleting them, and does nothing at all after a session load error. Directory names that are not keys are skipped. `EnsurePrivateDirectory` sets 0700 explicitly and returns `false` (`[[nodiscard]]`) when the directory cannot be created, is not a directory, or cannot be secured; a caller must not write cookies after a `false`. The window passes the root in (the caller owns `IslandWebDataRoot()`). Not yet called by the window (0.10 T11) |
| `data_paths.{h,cc}` | CEF-free home of the platform data rule. `IslandDataDirectory()` is the directory the session, prefs, bookmarks and reports stores write to (their `DefaultXFilePath()` are `IslandDataDirectory() / name`); `IslandWebDataRoot()` is `ISLAND_DEV_CACHE_DIR` when set, else `Web Data` under the data directory (Windows: `%LOCALAPPDATA%\Island\Web Data`). Extension points: a new store delegates to `IslandDataDirectory()`; `src/agent` keeps its own copy and a test pins the two equal |
| `window_agent_host.{h,cc}`, `devtools_bridge.{h,cc}`, `agent_navigation.{h,cc}` | `AgentBrowserHost` implementation behind the MCP tools in `src/agent/` (tabs, spaces, page text/screenshot via DevTools) |
| `query_display_ranges.h` | Generated by `scripts/gen_query_display_ranges.py` (do not edit): sorted ranges of the non-ASCII code points a search query may show in the address field; read by `search_engine.cc` |
| `pages/settings.html` | Settings shell, loaded in one ordinary tab per window: a section list (`data-pane` items, `aria-current`) over one `<section id=...>` pane at a time, sidebar search over rows, version footer. `settings.appearance.base_look` decides whether Space colors and Sidebar tint show (hidden for Native). `ShowSettingsSection` and dev `settings:<section>` focus the existing tab and carry a `focus_seq` from `SettingsFocus` (`settings_focus.h`), so a repeated section opens again; the request remains pending until that tab's current document is ready. The tab is not session-restored; only a tab whose main frame is exactly `SettingsPageUrl()` may send Settings messages, and agent tools refuse it before navigation or DevTools calls. |
| `pages/bookmarks.html` | Managed local bookmark library. `islandRender` receives `theme`, `folders[{id,name,count}]`, `bookmarks[{id,title,url,host,folder_id,folder_path,global,spaces[{id,name}]}]`, `current_space`, `selected_folder`, bookmark-only `import_sources`, durable `status` (including Retry), separate transient `page_status`, and optional `agent_naming`. `page_status` clears when another page action starts or the page closes/reopens; a durable failure never duplicates it. Its rows open from the title/Enter and keep all secondary actions in one `...` menu; it uses menuitem roles, arrow/Home/End navigation, and Escape or an outside click closes the menu and returns focus to that trigger (also after favorite toggles). Its native dispatcher is `BrowserWindow::HandleBookmarksMessage`: `bookmarks:open`, `bookmarks:open-new-tab`, `bookmarks:edit`, `bookmarks:delete`, `bookmarks:move`, `folder:create`, `folder:rename`, `folder:delete`, `favorite:toggle-global`, `favorite:toggle-space`, `import`, `retry`, and `select-folder`. Add a message by documenting it in the page's top comment/table, emitting only scalar IDs/strings, validating it before a `RunBookmarkMutation`, and adding a page/window test. |
| `local_page.{h,cc}`, `local_pages_html.h`, `pages/*.html` | Embedded HTML surfaces loaded as `data:` URLs. Settings and Start are ordinary tab documents; their messages are accepted only when the hosting tab's main frame is the exact generated URL. The agent panel, All tabs and Bookmarks remain content-slot surfaces. `pages/agent.html` renders sent page and quote chips from each user transcript item's optional `context` object above its bubble, with neutral token-only chips; sent quotes reuse the composer's one-line ellipsis span. Page→native messages travel as `\x01island:` console messages, native→page state through `islandRender(state)`. |
| `keymap.{h,cc}`, `macos/keymap_menu_mac.{h,mm}` | Configurable shortcuts: `KeyAction` ids, `Mod+Shift+K` binding text, defaults, conflicts, VK and macOS key-equivalent mapping, keycap glyphs (`FormatKeyGlyphs`) and the ten `CheatsheetRows`. An override reapplies both CEF accelerators and macOS NSMenu key equivalents immediately. |
| `cheatsheet_overlay.{h,cc}` | The ⌘/ shortcut cheatsheet: CEF-free `CheatsheetLifetime` (toggle, detach, `OutsideMouseDownCloses` → `OverlayStep`) and the 320 DIP bottom-right card, created on show and destroyed on hide (see `overlay_lifetime.h`). A mouse press outside the card closes it through `mouse_down_monitor.h`; focus and window activation never do. Escape reaches it through the stop-loading route, where the cheatsheet comes before a peek |
| `mouse_down_monitor.h`, `macos/mouse_down_mac.mm` | Reports every mouse press the app receives (an `NSEvent` local monitor, passed through unchanged), installed only while an overlay that closes on an outside click is shown; no-op elsewhere |
| `native_handle.h` | `NativeHandlePointer`: turns `cef_window_handle_t` (a pointer on macOS and Windows, an X11 id on Linux) into the `void*` the native helpers take |
| `history_recorder.{h,cc}` | Single-sequence background owner of the on-device history `SearchIndex`; records web navigations, rebuilds/flushes the bounded segment, queries, and clears history |
| `find_bar_model.{h,cc}`, `find_bar_overlay.{h,cc}` | CEF-free find-bar state (query, match case, "n of m", the `FindRequest` each gesture needs) and the overlay card shown only while the bar is open; `browser_window_find_reader.cc` wires both to `CefBrowserHost::Find` / `OnFindResult` |
| `reader_mode.{h,cc}`, `reader/` | Reader view: `reader/reader_extract.js` (injected through DevTools `Runtime.evaluate`, returns a whitelist tree) and `reader/reader_page.html` (the document, embedded like `pages/`); `reader_mode.cc` builds the data: URL. Per-tab `ReaderSession`s record the origin page and the document's URL hash; its messages (`reader_settings`, `reader_exit`) are accepted only with that document's token |
| `address_indicators.h` | CEF-free rules for the Native address-field indicators (`ComputeAddressIndicators`: shield count capped at "9999+", Reader, agent state). `BrowserWindow::RefreshAddressIndicators` recomputes them on tab/space switch, blocked request, Reader change and agent session change and calls `BrowserChrome::SetAddressIndicators`; the shield opens Settings > Privacy (no blocker popover exists), the dot toggles the agent panel. The agent state is window-wide because the agent drives the active tab |
| `browser_window.cc` (confirmations) | `ShowConfirmation`/`AnswerConfirmation` keep pending agent confirmation cards in the window (120 s timeout via a 1 s UI timer, cancelled on shutdown, Stop/new chat and when the card's tab closes, moves or its space is left; the card never opens the panel or takes focus; the HTTP handler budget `kHandlerTimeoutSeconds`=150 exceeds the card timeout); panel state key `confirmations`, panel message `confirm`, settings message `remove_trusted_site`; legacy Agent icons carry a 6 DIP `warning_text` amber badge while Native has its one readable footer status dot (`BrowserChrome::SetAgentNeedsYou`); dev action `agent_fixture:confirm` |
| `browser_window.cc` (task queue) | `agent_queue_` (`src/agent/agent_task_queue.h`) runs agent work one task at a time: `SubmitAgentPrompt` and `StartResearch` enqueue, `StartQueuedTask` (the StartFn) sends and records `in_flight_id_`, `CheckInFlightTurn`/`FinishPostedTurn` report chat turn ends (posted, since the session is inside its handler), research reports from `CompleteResearch`, `SyncQueueWaiting` is the edge-triggered OnWaiting (cards or research awaiting approval; a research waiting for approval reports no turn end, Run is the same task), `StopAgentTask` cancels then `StopCurrent` (turn open) or `StopCurrentAndAdvanceNow`, `AbandonInFlightTurn` for new chat/agent switch/teardown; panel state key `tasks`, messages `task_move`/`task_remove`/`task_stop`/`tasks_clear_finished`; `AgentButtonLabel` (browser_chrome.cc); palette command `agent_tasks`; dev action `agent_fixture:queue` |
| `agent_context.{h,cc}` | CEF-free "ask the agent about this": `BuildAgentContext` (page title, URL and selection in tags labelled as web content, selection clipped to 4096 bytes), `BuildLinkAgentContext` (a link the agent reads in its own background tab), `AgentContextMenuItems` (the page context menu's agent items and their `kAgentMenu*` offsets), `ClipUtf8` / `EscapeUntrusted` / `UntrustedTitle` |
| `prefs_store.{h,cc}` | Preferences JSON (theme, history capture, agent provider + custom command with migration from the old single command, agent panel state, agent trusted sites, shortcut overrides, update checks) |
| `updater.{h,cc}` | CEF-free updater core: SemVer precedence, GitHub releases parsing/selection (`kReleasesRepo`), per-target assets and `SHA256SUMS.txt`, URL/redirect allow-list, install detection, generated sh/cmd apply scripts, the `Updater` state machine behind the `UpdateFetcher` seam |
| `sha256.{h,cc}` | Standard-library SHA-256 used to verify update downloads |
| `cef_update_fetcher.{h,cc}` | `UpdateFetcher` over `CefURLRequest` (manual, allow-listed redirects; capped, hashed streaming) |
| `content_blocker.{h,cc}`, `content_blocker_policy.{h,cc}` | RAII wrapper for the Rust engine plus CEF-free request-type, bypass, allowlist, and four-day refresh policy |
| `cef_content_blocker.{h,cc}` | Background EasyList/EasyPrivacy cache loading, `CefURLRequest` refreshes, atomic engine publication, and request checks |
| `bookmark_import.{h,cc}`, `browser_import.{h,cc}` | Import from Chrome-family browsers (one source row imports the union of readable `Default`, `Profile N`, and `Local State`-named profiles, and reports profile parse failures after a partial import), Safari, Firefox (`[Install*] Default` wins over `[Profile*] Default=1`, otherwise newest `places.sqlite`; the temporary SQLite copy includes WAL sidecars while Firefox may be running; `.jsonlz4` backups are the fallback when optional SQLite3 is unavailable or fails), and Arc (spaces + pinned tabs). Add a source by giving it a stable `ImportSource`/id, a profile-root detector that distinguishes absent from unreadable, an importer, fixtures for every probe path, and a Welcome description. |
| `bookmark_store.{h,cc}` | CEF-free bookmark library, schema 2 in `bookmarks.json`: bookmarks with IDs, folders holding ordered bookmark IDs, one global favorite list and one favorite list per SpaceId (decimal keys). `BookmarkStore` is the single mutation seam. Each operation (`AddOrFindByUrl`, folder create/rename/move/delete, `MoveBookmark`, `EditBookmark`, `DeleteBookmark`, `AddFavorite`/`AddFavoriteByUrl`/`RemoveFavorite`/`ReorderFavorite`, `RemoveSpaceFavorites`, `Import`) applies to a copy, saves atomically and commits only on success, and returns a `BookmarkResult` (`BookmarkError`, a short message, the affected ID, the import count). A default-constructed store is in-memory. `Load()` migrates schema 1 (original bytes to `bookmarks.json.v1.bak` first; a failed write leaves the v1 file and makes the store read-only for the run). Migration collapses duplicate and case-variant URLs: the first occurrence in folder-then-item order wins, keeping its title and folder; later copies are dropped from the live library (their bytes stay in `.v1.bak`); empty and same-named folders are kept. Read-only runs carry their own reason message; edits that change nothing still succeed. Load also backs up an invalid file to `bookmarks.json.invalid.bak` before the first save; an existing file that cannot be read makes the store read-only, so it is never overwritten. `Flattened()` is the read-only (folder, title, URL) projection the palette uses; `MaxSpaceKey()` lets the window reserve SpaceIds above it. Extension points: a new record field goes into the struct, `SerializeToJson`, `ParseSchemaTwo`, `IsConsistent`; a new operation is a `Commit` lambda over the copy, so it inherits rollback and atomic save |
| `favorite_tabs.{h,cc}` | CEF-free registry of favorite virtual-tab records: `FavoriteTabKey` (scope plus bookmark ID, defaulted `<=>`), `FavoriteTabRecord` (reset URL, last URL), `Ensure`/`Find`/`SetLastUrl`/`ResetToFavorite`/`Dispose`/`DisposeBookmark`/`DisposeScope`, and `ToSessionFavoriteTabs`/`FavoriteTabsFromSession` for `session.json`. The window pairs records with live `Tab`s; see "Favorites and virtual tabs" below |
| `tab_sleep_policy.h`, `peek_state.h` | CEF-free models: tab sleep decisions (injected clock, per-tab last-active time, exclusions as inputs; `Expired` for the timed sweep, `AllEligible` for "Sleep inactive tabs now") and the peek card's state (open/replace/close/open-as-tab/Escape, generation counter for stale callbacks) |
| `link_open_policy.{h,cc}`, `peek_layout.h` | CEF-free decisions: `DecideLinkOpen(disposition, user_gesture)` → `kPeek` / `kForegroundTab` / `kReject` for every page new-tab open (table in the header); `PeekCardBounds` sizes the card from the content area (80% by 85%, centered, minimums 360 by 280 DIP) |
| `peek_overlay.{h,cc}` | The peek card: an overlay of the one `CefWindow`, created in `Open` and released in `Close` (see `overlay_lifetime.h`), covering only the card, holding a title bar and its own `CefBrowserView` with its own client (popups load in the card, Escape claimed in `OnPreKeyEvent`) |
| `overlay_lifetime.h`, `overlay_card.h` | CEF-free overlay rule (an overlay exists only while its surface is shown, because a live `CefWindow` overlay blocks mouse input on macOS) and the solid-color `OverlayFillDelegate` that re-asserts its color on theme change |
| `browser_window_research.cc`, `research_prompt.{h,cc}`, `report_store.{h,cc}` | Research mode in `BrowserWindow` (start, cancel and finish, the Plan hub, the Report tab with save/copy/continue/export); the CEF-free prompt, research space name and report extraction; and the Reports list persisted as `reports.json` (schema 2: each report has a `kind`, "research" or "collection"; version 1 loads as research) |
| `collect_prompt.{h,cc}`, `table_export.{h,cc}` | "Collect from tabs": the CEF-free prompt that lists the chosen tabs as untrusted web content and asks for one table between the Research report markers; `FirstMarkdownTable` reads a report's table (link cells become URLs, with a title column when the texts differ) and `TableToCsv` / `TableToJson` export it. CSV cells that start with `= + - @` TAB or CR get a leading quote (formula neutralization); JSON keys de-duplicate with ` 2`, ` 3`. The sheet sends `{index, url}` per checked tab and `CollectFromTabs` takes a choice only while the active space still has its URL (`CollectChoice` in `collect_prompt.h`). Sent by `CollectFromTabs` as a `kCollect` task whose context is the prompt; `FinishCollectTurn` stores the table before the queue moves on. Panel state key `collect`, message `collect`; dev actions `agent_fixture:collect` and `report_fixture:collection` |
| `plan_dev_fixture.h` | The developer capture's sample research plan (`--island-dev-actions=plan_fixture`), kept equal to the test fixture by a unit test |
| `dev_capture.{h,cc}` (agent fixtures) | Developer capture fixtures for the agent panel, each a dev action: `agent_fixture:confirm` (a confirmation card), `agent_fixture:queue` (one running and two queued tasks), `agent_fixture:quote` (a page chip and a quote chip), `agent_fixture:collect` (the Collect sheet), `agent_fixture:thread` (a sample conversation: a user message with a page chip and long quote, three tool rows (two done, one failed, one long MCP-prefixed name), a Markdown reply, and one queued message; `ShowThreadFixtureForDev` feeds the session through `AgentSession::AddDevUserPrompt` / `ApplyDevEvent`, so no agent starts). `main_mac.mm` maps the names to `IslandApp::Show*Fixture` through the `kAgentFixtures` table; add a fixture there and to this list. Captures with a fresh HOME open on the Welcome sheet, which covers the page: write a valid `prefs.json` with `"onboarding_completed":true` and a `theme` string in the capture HOME (under `Library/Application Support/Island/`) before the run, or the sheet stays on top. The status pill can read "Not running" while a fixture task shows Running; the fixtures never start an agent. The palette capture `palette_query:<text>` (`main_mac.mm` → `IslandApp::ShowCommandPaletteWithQueryForDev` → `CommandPaletteView::SetQueryForDev`) opens ⌘K with the text typed in; it is how the bookmark rows are captured with a matching query. |
| `pages/plan.html`, `pages/report.html`, `pages/markdown.js` | The Plan tab's graph and list page, the Report tab page (its view drops a first H1 that repeats the report title; Copy and Export keep it), and the shared safe Markdown renderer both use (inlined where a page carries `// @@ISLAND_MARKDOWN@@`) |
| `main_mac.mm`, `process_helper_mac.cc` | macOS main and helper-process entrypoints |
| `Info.plist.in`, `Helper-Info.plist.in` | Templated bundle plists configured per helper suffix |

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `linux/` | Linux entrypoint and Fontconfig font registry (see `linux/AGENTS.md`) |
| `macos/` | macOS CoreText font registry (see `macos/AGENTS.md`) |
| `windows/` | Windows entrypoint, GDI font registry, manifests (see `windows/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Sidebar collections (Bookmarks 0.9): `SidebarCollectionSnapshot` (`browser_chrome.h`) carries the
  global favorites, the current space's favorites, the tab rows and the failure status. To add a
  collection: add its field to the snapshot, build it in `BrowserWindow::BuildSidebarCollection`,
  render it in `BrowserChrome::AddFavoriteSections` (expanded) or `AddCollapsedFavorites` (rail), and
  route clicks through a `CollectionButtonAction` plus a `BrowserChromeHost` virtual. Pure rules
  (strip fit, names, headers, the Tabs-header placement, row heights, fills, favorite ink) live in
  `sidebar_collection.{h,cc}`. Favorite text and glyph colors repaint only on a theme change:
  `BrowserChrome::ApplyFavoriteInk` (from `ApplyCollectionTheme`, never hover) paints
  `FavoriteInkPlan`; add a themed favorite surface as a plan target plus an `ApplyFavoriteInk` case. The contract tests project the tab strip with
  `BrowserChrome::SidebarCollectionContract` (its tabs part is `TabStripContract`, shared with
  `CollectionCountContract`), the rail with `CollapsedRailOrder` and the failure row with
  `BookmarkStatusContract`. The header-row, strip, "tabs follow favorites" and rail-separator
  conditions are the functions `GlobalHeaderRowVisible`, `GlobalStripVisible`,
  `FavoritesPrecedeTabs` and `CollapsedRailSeparatorVisible`, called by both the builders and the
  projections; button names come from the same `*AccessibleName` helpers. Tab row fills (R15) go
  through `TabRowFill`, and the failure row's colors through `BookmarkStatusRowColors`. The
  new `ChromeViewId`s are `1081`–`1093` (`1093` is the global header row holding the label and the
  trailing "+"); `1052` (collapsed New Tab) is retired and never reused. The New tab row returned
  on 2026-10-26 with two new ids: `1094` (`kTabsNewTabRow`, the expanded row directly under the
  tabs header, built by `AddNewTabRow` and projected by `TabStripContract`) and `1095`
  (`kCollapsedRailNewTab`, the rail's "+" after the tab icons, built by `AddCollapsedNewTab` and
  listed last by `CollapsedRailOrder`). Both run `CollectionButtonAction::kNewTab`, which calls
  `ExecuteBrowserCommand(BrowserCommand::kNewTab)`; `ApplyNewTabVisual` paints them with the
  neutral `FooterAgentPlate` plate and is hover-tracked in `RefreshRowHover`.
  Space menu (`space_menu.h/.cc`): a click on the ACTIVE space's segment or rail icon runs
  `kActivateSpace`, which calls `BrowserChromeHost::ShowSpaceMenu` (any other space is selected
  as before). `BrowserWindow::OpenSpaceMenu` shows `SpaceMenuPaletteCommands(can_delete)` on the
  command palette in a `space_menu_open_` mode (the palette is created lazily, like Cmd+K).
  Submitting a row goes through `ParseSpaceMenuCommandId` and `ScheduleSpaceMenuCommand`, which runs
  `RunSpaceMenuCommand` on the next UI task after the palette releases its overlay. Rename calls
  `BeginSpaceRenaming`, Change icon `ShowSpaceIconPicker`, a colour `SetSpaceColorFromHex` then
  `SaveSession` and `ApplyAppearance`, New space `kNewSpace`, Delete space `kCloseSpace` (no
  confirmation, the same as the existing command). The colour presets live in
  `kSpaceMenuColorChoices`, shared with New Space. To add a row, extend `SpaceMenuAction`, the
  builder, the parser and `RunSpaceMenuCommand`. Dev capture: `space_menu` opens it (the macOS
  selector is `openSpaceMenu:`, `IslandApp::OpenSpaceMenu`).
  `BrowserChrome::SpaceSegmentClickFor` decides the routing (active → menu, other → select).
  Hover fills of the New tab row use `NewTabRowFill` (the tab row's `TabRowFill`) and
  `NewTabCornerPatch` (the tab rows' 8 DIP radius), both listed in `ComputeCornerPatches`.
  Fullscreen: `OnWindowFullscreenTransition` tracks `fullscreen_`; `ResolvedTokens` drops the
  frameless `titlebar_band_dip` (the traffic-light inset) while fullscreen, so the rail and the
  navigation row stop reserving room for hidden buttons, and restores it on exit. Tests read it
  through `TitlebarBandDipForTest`.
  The expanded New tab row sits in a wrapper panel that keeps `kNewTabFocusInsetDip` (4 DIP, the
  ring's outset) on every side, so its keyboard focus ring is not clipped by the tab list;
  `ComputeTabStrip` height counts the wrapper. The collapsed rail's "+" sits in a wrapper that
  keeps `kCollapsedRailNewTabRingInsetDip` (4 DIP) above and below it (`collapsed_rail.h`);
  `UpdateCollapsedTabListSize` adds `kCollapsedRailNewTabPadDip` to the list height.
  Dev capture: `favorite:global:<i>` and
  `favorite:space:<i>` show the i-th favorite (`IslandApp::ActivateFavoriteForDev`), and
  `focus:global_favorite:<i>` moves keyboard focus to the i-th global strip icon
  (`IslandApp::FocusGlobalFavoriteForDev`); `focus:new_tab` moves it to the New tab control that
  is drawn (`IslandApp::FocusNewTabForDev`, the expanded row or the collapsed rail's "+").
- `ChromeViewId` values `1001`–`1027` are a hard invariant. Never renumber them; allocate new IDs
  after `1027`.
- Keep genuinely CEF-free logic (`design_tokens`, `navigation_state`, `address_bar_model`,
  `address_policy`, `session_store`) free of CEF headers, so it can be exercised without a running
  CEF runtime. Note that `island_tests` *does* link CEF (`libcef_dll_wrapper` +
  `CEF_STANDARD_LIBS`); what it does not do is stage the CEF framework and resources beside the
  binary. No source in that target calls `CefInitialize` today, and none may start one — only
  `island_cef_tests` stages the runtime a CEF browser process needs. `tab.{h,cc}` and `space.{h,cc}` are no
  longer CEF-free — both now include CEF headers for their browser/request-context seams.
- Adding a `.cc` here usually means editing **two** lists: the `island_browser_core` sources in
  `CMakeLists.txt`, and the explicit source list of the matching test target in
  `tests/CMakeLists.txt`.
- The updater policy (`updater.cc`, `sha256.cc`) is CEF-free like the stores above; only
  `cef_update_fetcher.cc` and the `BrowserWindow` glue touch CEF. The startup check is deferred and
  skipped for the smoke run (`persist_session` false) and under `ISLAND_DISABLE_UPDATES=1`, so
  startup stays offline. Release-contract details live in the comment at the top of `updater.h`.
- The content blocker is native (not an extension). It never intercepts main-frame or local-page
  requests. Cached lists live under the Island support directory in `content-blocker/`, refresh
  no more often than every four days, compile on `TID_FILE_USER_BLOCKING`, and are atomically
  published for IO-thread checks. `ISLAND_ENABLE_CONTENT_BLOCKER=OFF` exercises the no-op stub.
- Currently **not** compiled into any target: `session_store.cc`. (PR #27 is in flight to add it to
  both `island_browser_core` and `island_tests`; until it merges, that file has never been
  compiled.) `space.cc` and `tab.cc` *are* in `island_browser_core` as of commit `043aa34`, and
  also in `island_tests`.
- The agent panel and Settings share one provider contract built by `BrowserWindow`
  (`AgentProvidersStateJson`): `provider`, `providers[{id,name,description,command,executable,
  available,install,install_command,docs}]`, `command` (custom), `env_override`, `env_command`.
  Page messages: panel `set_provider {id}` / `open_settings` / `start {command?}`; Settings
  `set_agent_provider {id}` / `set_agent_command {command}` / `open_agent_docs {id}`. Availability
  is re-detected at most every 5 s and whenever the panel or Settings opens.
- `HistoryRecorder` owns every `SearchIndex` call on one worker sequence. Browser navigation and
  title callbacks only enqueue work; palette queries return asynchronously to the CEF UI thread;
  periodic and clean-quit flushes never execute on it. On open, a persisted S0 segment is read back,
  capped to the newest 50,000 logical visits, and re-ingested into a fresh MemTable before rewrite,
  because S0 cannot flush a segment that predates its MemTable.
- Platform selection is `if(APPLE) / elseif(WIN32) / elseif(Linux)` with a `FATAL_ERROR` fallback;
  each branch adds its own `font_registry_*` source and platform link libraries.

### Agent extension points (window side)

The agent kernel's own extension points (gate rules, gated tools, task kinds) are in
`src/agent/AGENTS.md`, "Extension points". The three below live in the window.

**A new context-menu ask** (`agent_context.h`, `agent_context.cc`, `browser_window.cc`):
1. Add `inline constexpr int kAgentMenu<Name> = N;` to `agent_context.h`. Start at 4: offset 0 is
   "Open in peek" (`MENU_ID_USER_FIRST`), and every offset is added to `MENU_ID_USER_FIRST`.
2. Add the item to `AgentContextMenuItems`, with its condition (`has_agent`, `has_selection`,
   `has_link`). Selection items need a web page; `RunAgentMenuCommand` refuses the rest too.
3. Handle the offset in the `switch` of `BrowserWindow::RunAgentMenuCommand`. Build the prompt, and
   the context with `BuildAgentContext` or `BuildLinkAgentContext`, then call `AskAgent(prompt,
   context)`, which opens the panel and enqueues a `kAsk` task. Page text reaches the prompt only
   through `ClipUtf8`, `EscapeUntrusted` and `UntrustedTitle`.
4. To offer it in the command palette too, add an entry to `PaletteCommands()` and a branch to
   `BrowserWindow::RunPaletteCommand`.
5. Tests: `tests/agent_context_test.cpp` for the items and the escaping; `BrowserWindowPages` tests
   for the path. The CEF menu model (`OnBeforeContextMenu`, `OnContextMenuCommand`) has no unit
   test, so click the item in a real window before release.

**A new export format** (`table_export.h`, `table_export.cc`, `browser_window_research.cc`, `pages/report.html`):
1. Add a pure converter `std::string TableTo<Format>(const Table&)` to `table_export.{h,cc}`. Follow
   `TableToCsv`: RFC 4180 quoting, and formula neutralization for spreadsheet formats (`TableToJson`
   does not neutralize).
2. In `BrowserWindow::ExportReport`, accept the format name, call the converter, and use the
   extension `"." + format`. `md` goes to `SaveReportToFile`; an unknown name is an error.
3. In `pages/report.html`, add a `role="menuitem"` button with `data-format` to `export-menu`. If the
   format needs a table, add its id to the list that disables the items when `has_table` is false.
4. Tests: `tests/table_export_test.cpp` for the converter; `BrowserWindowPagesTest.ExportMessageWritesTheChosenFormat`
   for the message path (it writes through `export_path_for_test_`).

**A new task kind**: see `src/agent/AGENTS.md`. In this directory the window touches it in
`BrowserWindow::StartQueuedTask` (send the prompt, set `in_flight_is_chat_`), in
`FinishInFlightTurn` if the kind needs a turn-end hook (as `FinishCollectTurn` does), and in the
kind switch of `BrowserWindow::TasksStateJson`.

### CEF ownership seams

- **Per-space request context.** Each `Space` owns a `CefRequestContext`, created lazily by
  `Space::request_context()` on first access — not at space construction. `CreateRequestContextIfNeeded`
  takes a `cache_path`, but the only caller passes `""`, so contexts are currently created with no
  `cache_path` set and are not persisted to a per-space directory on disk.
- The context is always built with `CefRequestContext::CreateContext(settings, nullptr)`. Do not
  switch it to `CreateContext(GetGlobalContext(), ...)`: that dereferences a null global context and
  segfaults whenever CEF is not initialized, which is the case in the unit tests.
- **Per-tab browser seam.** `Tab` holds `CefRefPtr<CefBrowserView>` and `CefRefPtr<CefBrowser>`, but
  does not create them. `BrowserWindow` creates the view and calls `Tab::SetBrowserView`; the
  `CefBrowser` arrives later via `OnBrowserCreated` → `Tab::SetBrowser`. Both are null while a tab
  is not attached, so always null-check before use.
- `BrowserWindow` currently constructs exactly one hardcoded space (`SpaceId{1}`, "Default") holding
  one hardcoded tab (`TabId{1}`), and `OnWindowCreated` creates exactly one `CefBrowserView`. There
  is no code path that creates a second space or a second tab.
- **Session cookies are deliberately not persisted.** `app_runtime.cc` sets
  `settings.persist_session_cookies = false` and points `root_cache_path` at
  `/tmp/island_cef_cache`. Both exist to stop CEF encrypting cookies through Chromium Safe Storage,
  which raises a macOS keychain prompt during tests and smoke runs. Do not "fix" either without
  solving that prompt.

### Tab sleep and peek

- **Sleeping a tab** (`BrowserWindow::SleepTab`): set the model first (startup URL = committed web URL, saved scroll, `sleeping`), then `Tab::SetBrowser(nullptr)`/`SetBrowserView(nullptr)`, then `CloseBrowser(true)`. The order is load-bearing: `OnBrowserDestroyed` runs `DetachChromeAndObservers()` for any browser it can still map to a tab. The closed browser's id goes into `sleep_closing_browser_ids_` so its `OnBeforeClose` is never read as the window's final close during shutdown. Waking is `EnsureTabBrowserView`; scroll is replayed from `OnLoadEnd`.
- **The probe** (`SleepProbeScript`) answers scroll offset, unsaved input and playing media in one console message `\x01island-sleep:<token>:x,y,dirty,playing`, accepted only with a live token from the probed browser (500 ms timeout) and routed before the start-page bridge. Web Audio and iframes are not inspected.
- **Link opens** go through `DecideLinkOpen` (link_open_policy.h) in `OnBeforePopup` and `OnOpenURLFromTab`; `HandleLinkOpen` runs the result. Only an explicit gesture opens anything: Cmd/Ctrl+click or middle-click (`NEW_BACKGROUND_TAB`) → Peek (CEF reports both alike, so middle-click peeks too); Shift+click (`NEW_WINDOW`) and a plain target=_blank or a gestured window.open (`NEW_FOREGROUND_TAB`) → `OpenLinkTab(url)`, which calls `AddTabToActiveSpace(url, TabPlacement::kAfterActive)`: the tab goes after the current tab's group and is selected. Popups with window features and gesture-less opens are rejected. CEF's popup callbacks carry no modifier flags, so the disposition is the modifier.
- **Peek** entry points: the Cmd/Ctrl+click (and middle-click) decision above, the context-menu item `Open in peek` (`MENU_ID_USER_FIRST`), and the agent's "Summarize linked page" (`DeferPeek`). Opening is deferred one UI task. An open card takes the neutral overlay focus ring (`OverlayFocusRingShown`). The card's size comes from `PeekCardBounds` on every content-bounds update. Escape reaches the card through the `Stop Loading (Escape)` menu item (`CanStopLoadingWithEscape`) and the card's own key handler. Selection changes close the peek. Dev actions: `peek:<url>`, `sleep_tabs`, `sleep_report`, `wait:<s>`; with `--island-dev-keep-open`, SIGUSR1 saves another capture and SIGUSR2 logs tab/sleep/peek state.

### Favorites and virtual tabs (0.9)

- **Library first.** The constructor loads `bookmarks_` before any space exists, then calls
  `ReserveSpaceIdsUpTo(bookmarks_.MaxSpaceKey())`, so a fresh or discarded session never reuses a
  SpaceId that still owns a favorite list. A load problem is kept as the status (`kFileReadError` on
  a missing file is the normal first run and shows nothing; a read-only or invalid library shows
  why). Lists whose space does not exist are kept but not projected.
- **One mutation seam.** Every bookmark mutation goes through `RunBookmarkMutation(std::function)`,
  including the two `Import` calls. A failure stores `{message, retry}` as `failed_bookmark_mutation_`;
  a success clears it. Either way it reconciles virtual tabs with the library and reprojects.
  `retry` is kept only for I/O failures (`kFileReadError`, `kFileWriteError`): invalid input or a
  missing item fails the same way again, so its status has no Retry. `RetryLastBookmarkMutation()`
  re-runs the stored closure (an empty one returns an error); `DismissBookmarkStatus()` clears it.
  Load problems have no retry closure. A failed import is reported once, on the status row, so
  `ImportFromBrowser` returns an empty settings message for it.
- **Projection.** `BuildSidebarCollection()` (const, test-callable) returns a
  `SidebarCollectionSnapshot`: `global_favorites`, then the active space's `space_favorites`, then
  `tabs` (the existing rows), then `bookmark_status`. Each `FavoriteEntrySnapshot` carries scope,
  bookmark ID, title (the bookmark's), URL, `displayed` and the virtual tab's favicon. Identical
  favorites never collapse across scopes. `UpdateChromeCollections` pushes the result through
  `BrowserChrome::SetSidebarCollection`, which forwards `tabs` to `SetTabStripEntries`; the chrome
  keeps the snapshot for Task 3's drawing. Projection is rebuilt by every existing
  `UpdateChromeCollections` call (mutations, selection, title and favicon changes).
- **Virtual tabs.** `virtual_tabs_` (`std::map<FavoriteTabKey, Tab>`) holds live `Tab`s outside
  `spaces_`; their `TabId`s come from `NextTabId()` and are never persisted. `displayed_favorite_`
  names the one shown. `ActivateFavorite` ensures the record, creates the tab (its startup URL is
  the record's last URL when that validates, else the favorite URL) and calls
  `AttachActiveTabObservers`; the selected ordinary tab
  and its list are not touched. `AttachActiveTabBrowserView` shows a displayed favorite ahead of the
  split and ordinary paths. `FindTabById`, `FindTabByBrowser` and `FindTabByBrowserView` search the
  virtual tabs too, so CEF callbacks and view swaps resolve them. A favicon finished for a virtual
  tab reprojects only when that tab is global or in the active space (`FaviconAffectsCollections`).
- **The shown tab.** `DisplayedTab()` is the displayed virtual tab, else `active_tab()`; `shown_tab_id()`
  is its public form. Page commands (back, forward, reload, reload ignoring cache, zoom, stop, copy
  URL, Enter in the address field, the palette's URL), find, the reader state, the address shield and
  reader indicators, the agent's current page and context chip (`ActiveBrowser`), and the
  pending-confirmation check all use it. Cmd+W while a favorite is shown calls `CloseFavoriteTab`
  and returns (it never closes the hidden ordinary tab). Tab selection, sessions, sleep, split and
  the tab strip keep `active_tab()`. Only the shown tab's navigation observer is set
  (`ClearShownTabObservers` runs before any change of the shown tab), so the address field follows
  the favorite and never the hidden tab.
- **Lifecycle rules.** Any `DetachActiveTabObservers` (every selection change, space removal) ends a
  displayed favorite; re-selecting the already-selected tab or space does too (`ReturnToOrdinaryTab`).
  `ActivateFavorite` accepts global favorites and the active space's list only; another live
  space's list opens after switching to that space.
  `CloseFavoriteTab` drops the virtual tab and resets its record to the favorite URL. Removing a
  favorite (or deleting its bookmark) drops its records through `ReconcileFavoriteTabs`. Really
  deleting a space (`RemoveSpaceAtIndex`, never the reorder in `MoveSpace`) drops that scope's
  records and virtual tabs and removes its favorite list when one exists.
- **Session.** `SessionState::favorite_tabs` is optional in `session.json` (`space_id` absent means
  global); the schema version is still 1 and an old file loads with none. `BuildSessionState` writes
  each live virtual tab's current URL as its last URL. `RestoreSessionState` restores records before
  anything else (they belong to the library) and drops those whose favorite is gone. The display is
  never restored. Older builds ignore the key, so they drop these records only when they save.
- **Virtual-tab lifecycle (Task 7).**
  - *Split.* `favorite_pane_` is the focused split member (the active tab) at activation. The favorite
    fills that pane and the other pane keeps its tab. `ShownPanesFor()` is the one place that decides
    the slots: `AttachActiveTabBrowserView` and `AttachedViewWillRemain` both use it, and
    `ShownPanesForTest()` exposes it. Selecting any tab ends the display (as before), so a favorite
    moves to the other pane by selecting that pane's tab and activating the favorite again. It is
    never in both panes. Cmd+W returns only that pane.
  - *Sleep.* `CollectSleepInputs` includes the virtual tabs: the displayed one is `active` (never
    eligible), a hidden one follows `TabSleepPolicy`. `SleepTab` refuses the displayed one. The idle
    clock of the displayed tab is touched on every tick. A slept virtual tab keeps its record;
    `ActivateFavorite` reuses the same `TabId`, and its startup URL is the last URL.
  - *Invalid stored URL.* `ActivateFavorite` validates the record's last URL first. If it does not
    validate, the record is reset (`ResetToFavorite`) and a hidden existing tab's startup URL is
    reset, so the favorite opens at its favorite URL. Only an invalid favorite URL is a no-op: it
    returns false before it creates a record or a tab, and it sets `failed_bookmark_mutation_` with
    no retry. That status is written only into an empty slot, so a pending Retry or a library status
    is never replaced by an activation failure (activation is not a library write).
  - *Peek.* `FavoritePeekUrl` returns the bookmark URL of a listed favorite (global, or the active
    space's list); `PeekFavorite` opens it with `OpenPeek`. It never touches the virtual tab or the
    displayed favorite. The sidebar's Cmd/middle-click wiring is not in this layer yet.
  - *Agent.* `AgentOwnsTab(TabId)` is true only for an ordinary tab the agent opened. Virtual tabs are
    never agent-owned, so a displayed favorite is the person's tab. The agent bridge still lists
    spaces only.
  - *Removal.* `DeleteBookmark` runs through `RunBookmarkMutation`; `ReconcileFavoriteTabs` then
    drops every record of the bookmark, clears the displayed one's observer, and returns the slot to
    the selected ordinary tab. A failed mutation rolls back, so the display and its record survive.
  - *Still limited.* `RecordBlockedRequest(int)` searches the virtual tabs by browser identifier, but
    headless tests cannot create a browser, so only the `TabId` path (`RecordBlockedRequestForTab`) is
    unit-tested. `SetSidebarCollection` needs CEF and is not unit-tested. The tab strip's active entry
    is still the selected ordinary tab while a favorite is shown (the `displayed` flag is the mark).
- **Extension points.** A new favorite list kind goes into `FavoriteScope`'s users and
  `FavoriteListContains`. A new per-tab record field goes into `FavoriteTabRecord`,
  `ToSessionFavoriteTabs`/`FavoriteTabsFromSession`, and `FavoriteTabState` (session store). A new
  bookmark mutation is a lambda passed to `RunBookmarkMutation`. Any new `Tab` lookup must consult
  `virtual_tabs_` as well.

### Testing Requirements

```bash
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app --args --island-smoke-test
pgrep -fl island_browser || true
```

A default `ctest --test-dir build` run on `origin/main` reports 103 tests. None of them come from
this directory's `session_store.cc`, which is not compiled anywhere yet.

### Common Patterns

- Include guards are `ISLAND_<PATH>_H_`.
- CEF refcounted classes use `IMPLEMENT_REFCOUNTING` + `DISALLOW_COPY_AND_ASSIGN` and a private
  destructor.
- Observer seams (`NavigationObserver`, `ChromeObserver`) exist so tests can assert on snapshots
  without driving a real window.
- Owning models are move-only when they own move-only members (see `Space`).

### Command palette kinds (Native presentation)

- `kPaletteKindStyles` in `command_palette.cc` is the one table for the palette's result kinds:
  group header, sentence-case label, legacy uppercase label and row icon (`PaletteRowIcon`, mapped
  to a Lucide `ChromeIcon` in `command_palette_view.cc`). To add a kind, add its enum value, one row
  in that table, and the icon mapping; `CommandPaletteNativeStyleTest` checks each kind is listed once.
- Shortcut keycaps come from `PaletteCommandEntry::shortcut_action_id`: `BrowserWindow` resolves the
  current binding when the palette opens. Never hard-code a key in the view.
- `BrowserChrome::NativeKeycapColors` supplies the keycap plate (border_strong in Native light).

### Welcome and overlay focus

- `WelcomeFlow` and `CommandPaletteView` both report show/hide through
  `OnOverlayFocusRingChanged`; `BrowserWindow::ApplyAddressFocusColors` owns the single window
  focus color, calls `ThemeChanged()` after each transition, and restores the address treatment
  when the last overlay closes. Do not set `CEF_ColorFocusableBorderFocused` inside either view.
- The Native welcome card's 12 DIP raster caps sit in exact-size `CefPanel` delegates. Their image
  buttons must keep zero insets or CEF clips the cap into a detached strip. While welcome is shown,
  `BrowserWindow` hides the attached `CefBrowserView`s because the macOS browser compositor can
  otherwise paint start-page controls above the CEF overlay; hiding the welcome restores them.
  This applies to Native only (`WelcomeHidesPageViews` in `overlay_lifetime.h`). The decision is
  re-applied on every attach (`BrowserWindow::ApplyPageViewVisibility`, called from
  `AttachActiveTabBrowserView`), so a view attached or re-attached during the welcome follows it.
- Palette shortcut keycaps: Native keeps a fixed 104 DIP right column on every row (so kind labels
  line up) and shows the keycap plate inside it at its natural width; other looks hide the column.

## Dependencies

### Internal

- `resources/island/icons/` — manifest and PNGs consumed by `icon_catalog`
- `cmake/platform/` — `island_add_windows_browser_target()` / `island_add_linux_browser()`
- `assets/fonts/` — Geist fonts staged into the bundle by `island_stage_chrome_resources`

### External

- CEF (`libcef_dll_wrapper`, `CEF_STANDARD_LIBS`)
- Rust/Cargo (optional): builds the pinned MPL-2.0 Brave `adblock` crate as a static library; CMake
  disables the blocker by default when `cargo` is unavailable
- macOS: CoreFoundation, CoreText — Windows: gdi32 — Linux: Fontconfig

<!-- MANUAL: -->
- Content blocker startup: cached lists load and compile on a file thread after the window exists, so requests of tabs restored at launch can pass until the engine is published (a few hundred ms); list downloads use `CefURLRequest` on the UI thread but compile off it, and `CefURLRequest` callbacks hand back a different wrapper object, so never compare it by pointer.
- Research source tabs use `Tab::group_parent`. `StartResearch` makes the research space itself
  (`OpenResearchSpace`: the newest space with the question's name, else a new one whose only tab
  is the Plan hub, so the view swaps once) and the research prompt no longer asks the agent to
  create one. `OpenAgentTab` appends the page as a child and activates it
  (`AppendPlanChildTab(..., activate=true)`) so the agent's page tools find a live browser: one
  swap per MCP call. If the person has switched to another space, the page instead opens as a
  background child in the Plan's space and does not move their selection. Never swap the content
  view twice in one turn: that crashed CEF a few
  seconds later. The dev fixture appends background children (`activate=false`, no browser until
  first shown; their `Tab::DisplayTitle` is the host (the full address stays in the agent's `url`) and `EffectiveUrl` saves the startup
  URL). Children sit right after the Plan tab's last child; a pinned Plan's children start the
  unpinned tabs, so the pinned group stays whole (`Space::AppendChildTab`). `NormalizeGroups`
  re-places a parent's children behind it after moves, pinning and restore; only the child the
  person dragged away is ungrouped. An ordinary tab dropped inside a child run lands after the
  complete run, and `MoveTab` reports whether its final position changed. `BrowserWindow` maps
  the running `BuildPlanGraph` node id to the opened `TabId`. `NavigateTab` and the agent's
  back/forward/reload refuse the Plan tab
  (`IsPlanTabIndex`; `IsAgentPrivilegedTab` also refuses Settings for the agent's targets). The sidebar draws a branch kicker line (`kTabGroupKicker`) above each run;
  every row keeps the fixed row height. Closing the Plan tab while `research_` is active is
  deferred to the in-content Stop / Keep running confirmation, or stops the research at once when
  the page has no live frame; removing the Plan's space stops the research (no dialog).

- Agent tool targets (`BrowserWindow::ResolveAgentTab`, `window_agent_host.cc`). In ordinary chat the
  agent works with the window the person sees: tab indices and calls without an index refer to the
  active space and its active tab (`browser_list_tabs` lists that space). While a research runs and
  no plan waits for approval (`ResearchOwnsAgent`), the agent works in the Plan's space: indices and
  the listing refer to it, `browser_open_tab` files pages into the Plan's group, and a call without
  an index acts on `agent_tab_id_`, the agent's most recent research page. That id is recorded for
  each research child and cleared when a research starts or ends. If the page is missing or closed,
  the tool errors and never falls back to the person's tab. Page tools need a live browser and never
  activate a page. A research page without a browser gets a `NoLivePageText` error that tells the
  agent to report the source unread, because activating it would move the person's view.
  `browser_navigate` and pinning change a page in another space in place; `browser_activate_tab`
  switches the person's space on purpose; `browser_close_tab` works only in the space on screen.

- Space icons (add one): (1) add the locked Lucide SVG source (see `tools/icon_pipeline/AGENTS.md`) and its
  entry in `tools/icon_pipeline/icon-sources.lock.json`; the pipeline pins that file with `kLockHash` in
  `tools/icon_pipeline/icon_pipeline.cc`, so update the constant with the lock; (2) add its entry to
  `kSpaceIconChoices` in `space_icon.h` (the array size changes with it); (3) run
  `tools/icon_pipeline/run.sh generate` to regenerate the PNGs and `run.sh verify` to check them; (4) update the PNG count in the root
  `CMakeLists.txt` (`888` now), `resources/island/AGENTS.md` and `tools/icon_pipeline/AGENTS.md`.
  The command palette's picker (`kSpaceIconCommandPrefix`, `space_icon_picker_open_`) and the
  switchers read that list, so they need no edit. Sessions store only the id; unknown ids load as
  none.
- Palette commands: `BrowserWindow::PaletteCommands(const BookmarkCommandState&)` is the registry for
  ordinary palette action rows. `CurrentBookmarkCommandState()` supplies the space name, whether the
  page on screen can be bookmarked, and the page's position in the global and space lists, so favorite
  labels say Add or Remove and the reorder rows (`move_global_favorite_left/right`,
  `move_space_favorite_up/down`) appear only for a favorite that can move that way. Add a
  `PaletteCommandEntry` there, route its stable id in `RunPaletteCommand`, and add a window test.
  Bookmark and favorite commands set `PaletteCommandEntry::bookmark_group`: they list under the
  "Bookmarks" header (`PaletteGroupHeader`) after the general "Commands", then the bookmark rows under
  the same header. `ComposePaletteResults` keeps that order for an empty query too.
  Bookmark results remain `kBookmark`; `PaletteBookmarkEntries()` gives one row per library bookmark
  in folder order, with its folder and every presented favorite scope joined into one label
  ("Global, Work"; a removed space's list is skipped). The view prints folder and scope as muted
  text after the title (`result_details_`).
- Bookmark commands that change the page resolve it through `CurrentBookmarkablePage()` when the
  command runs: the displayed favorite's virtual tab when one is shown, else the selected tab, and
  nothing while a local page fills the slot or the URL is not http(s). The page and the add-or-remove
  or position decision are captured before `RunBookmarkMutation`, and the lambda only applies them. So
  a Retry repeats the same change to the same page, even after the displayed page has changed. A refusal
  ("Only web pages can be favorites.") reaches the status. A successful move sets `bookmark_notice_`,
  and the sidebar status row shows it as "Moved <title> to position N of M" (`BookmarkStatusSnapshot::ok`,
  neutral colors). Any later mutation or Dismiss clears it.
- Palette sub-picker: `ShowSpaceIconPicker` reopens the palette with only the icon rows
  (`SpaceIconPaletteCommands`) and a `space_icon_picker_open_` flag that makes the next submit set
  the icon. Each command submit must reach the host after the palette released its overlay, so the
  picker is scheduled with `ScheduleSpaceIconPicker` (a posted task). To add another palette command,
  append a `PaletteCommandEntry` to the list `BrowserWindow::PaletteCommands` returns and handle its
  id in `BrowserWindow::RunPaletteCommand` (a command selection reaches it through
  `OnCommandPaletteSubmitted`). Return re-applies the field's text through
  `CommandPaletteModel::SetQuery`, which is a no-op for an unchanged query: resetting the highlight
  there once made Return submit row 0 whatever the arrow keys chose (so the icon picker never
  opened).
- Web color mode: the Light and Dark themes also set the pages' `prefers-color-scheme` (and
  scrollbars) through `CefRequestContext::SetChromeColorScheme`. `BrowserWindow::ApplyWebColorScheme`
  runs from `ApplyTheme` and `Space::SetWebColorScheme` stores the mode, so a context created later
  gets it too; System passes `CEF_COLOR_VARIANT_SYSTEM`. The setter compares the current mode first,
  because the color-mode change notifies the theme path again.
- Native's Agent row (`kRailAgentButton`) has three views: the name (`agent_button_`, fitted by
  `FitAgentEntryName` to the room between the label and the status dot), the status dot and the
  keycap (`agent_keycap_`). `AgentEntryStatus` drives the dot: `AgentActivity` is `kIdle`,
  `kWorking` or `kNeedsYou`, and the status is refreshed from `RefreshAgentEntry`. The row's views
  are currently three focus stops; the meta and keycap labels should not be (review item M4, open).

- Native address controls (location glyph, shield, field holder, reload, reader, agent dot) sit in
  `address_inner_`, which takes the ring inset. While the field is focused `ComputeCornerPatches`
  rounds that panel's corners to the inner radius (8 - 2 = 6) with the accent as the outside color,
  so the 2 DIP ring keeps one width at the ends; square child corners would cut a notch into it.
- Native address focus ring: Chromium's inner textfield ring is `CEF_ColorFocusableBorderFocused`,
  clamped to a minimum contrast against `CEF_ColorWindowBackground`; a dark ring is lifted to a
  grey (94,94,94). While the Native address field is focused `BrowserWindow::ApplyAddressFocusColors`
  sets the ring to the plate fill and the window background to the opposite extreme
  (`BrowserChrome::TextfieldFocusRingColor/Backdrop`), so only the row's accent ring shows. Focus
  and blur call `SetAddressFocusRingSuppressed`, which repaints just those colors and calls
  `ThemeChanged` (the chrome reapplies its colors); it never runs a full `ApplyTheme`, and other
  looks skip the repaint because their helpers do not change.
