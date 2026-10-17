<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

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
| `browser_chrome.{h,cc}` | Native chrome layout; declares the `ChromeViewId` enum (`kRoot = 1001` …). Native's sidebar bottom is a footer with a switcher row (segments, `+`, overview, settings; `kRailSwitcherRow`) above the full-width Agent entry (`kRailAgentButton`); the toolbar has no Agent button under Native |
| `chrome_snapshot.h` | Observable snapshot struct + `ChromeObserver` seam used by tests |
| `browser_command.h` | The Back / Forward / Reload command enum |
| `navigation_state.{h,cc}` | Back/forward/reload availability and navigation snapshot |
| `address_bar_model.{h,cc}` | Address field text/editing state |
| `address_policy.{h,cc}` | Validates an already-parsed absolute http/https address (allowed schemes, hosts, ports) |
| `address_input.{h,cc}` | CEF-free address-bar heuristic: explicit scheme / host-like text becomes a URL (bare hosts get `https://`), everything else is a web search. Used only by `BrowserWindow::ResolveAddressInput` |
| `search_engine.{h,cc}` | CEF-free table of web search engines (Google default, DuckDuckGo, Yandex), `BuildSearchUrl`, and the start page's remembered mode |
| `start_tiles.{h,cc}` | CEF-free speed-dial model for the start page: `BuildStartTiles` turns the most-visited history into at most six `{title, url, host}` tiles (http/https only, no credentials, one per host, host as the label fallback); `IsWebTileUrl` guards `open_tile`. `BrowserWindow` refreshes them on the page's `ready` message from `HistoryRecorder::MostVisitedAsync`, which groups visits by `WebUrlHost` before its limit of six applies |
| `cef_address_parser.{h,cc}` | CEF-dependent URL parsing behind the policy seam |
| `design_tokens.{h,cc}` | `ArgbColor`, the token palette shared with `tests/design/`, the Arc `canvas/selected/hover/well/plate` surfaces, `AppearanceSettings` and `ChromeTokens::Customized` (tint, density, radius, inset, shadow, side, font) |
| `page_card.h`, `macos/page_card_mac.mm` | Native macOS overlay that rounds and shadows the page card and repaints row/pill corners (`PageCardStyle`, `CornerPatch`); no-op elsewhere |
| `row_raster.{h,cc}` | CEF-free rounded-rect raster used for favicon plates (and tested) |
| `icon_catalog.{h,cc}` | Resolves icon names/sizes/scales against `resources/island/icons/manifest.json` |
| `app_resources.{h,cc}` | Runtime resource-directory resolution (fonts, icons) |
| `font_registry.h` | Font registration seam, implemented per platform |
| `app_runtime.{h,cc}` | Runtime seam shared by the app and tests |
| `startup_options.{h,cc}` | Command-line parsing, including `--island-smoke-test`; production leaves the initial URL empty so `BrowserWindow` opens the embedded start page |
| `lifecycle_config.h` | Shutdown/lifecycle constants |
| `sidebar_state.{h,cc}`, `collapsed_rail.h` | CEF-free expand/collapse state (persistent choice in prefs `sidebar_collapsed`, transient expansion while editing the address from the icon rail) and the icon rail's metrics plus the space-dot raster |
| `favicon_policy.h` | CEF-free favicon rules: web pages only, 16 DIP downloads, stale-result and clear-on-navigation checks (bookkeeping lives on `Tab`) |
| `tab.{h,cc}`, `tab_id.h` | Move-only tab model identified by `TabId`, holding the per-tab `CefBrowserView`/`CefBrowser` seam |
| `space.{h,cc}` | Named, colored space owning ordered tabs, active selection, `SplitPairing`, and the space's `CefRequestContext` |
| `session_store.{h,cc}` | Session JSON round-trip with a `SessionError` taxonomy; not yet called by any lifecycle code |
| `window_agent_host.{h,cc}`, `devtools_bridge.{h,cc}`, `agent_navigation.{h,cc}` | `AgentBrowserHost` implementation behind the MCP tools in `src/agent/` (tabs, spaces, page text/screenshot via DevTools) |
| `pages/settings.html` | Settings shell: a section list (`data-pane` items, `aria-current`) over one `<section id=...>` pane at a time, sidebar search over rows, version footer. `settings.appearance.base_look` decides whether Space colors and Sidebar tint show (hidden for Native). Native-side focus requests (`ShowSettingsSection`, dev `settings:<section>`) carry a `focus_seq` from `SettingsFocus` (`settings_focus.h`), so the page opens a repeated section again; they wait until the page has loaded (`LocalPage::loaded()`) |
| `local_page.{h,cc}`, `local_pages_html.h`, `pages/*.html` | In-window HTML pages (agent panel, Settings, All tabs) loaded as `data:` URLs; `pages/start.html` (`LocalPageKind::kStart`) is the exception: it is the document each new tab loads, and `BrowserWindow::OnConsoleMessage` accepts its `search` / `ask_agent` / `research` / `set_start_mode` / `open_tile` / `ready` messages only from a tab whose main frame is exactly that URL; page→native messages travel as `\x01island:` console messages, native→page state through `islandRender(state)` |
| `keymap.{h,cc}` | Configurable shortcuts: `KeyAction` ids, `Mod+Shift+K` binding text, defaults, conflicts, VK and macOS key-equivalent mapping, keycap glyphs (`FormatKeyGlyphs`) and the ten `CheatsheetRows` |
| `cheatsheet_overlay.{h,cc}` | The ⌘/ shortcut cheatsheet: CEF-free `CheatsheetLifetime` (toggle, detach, `OutsideMouseDownCloses` → `OverlayStep`) and the 320 DIP bottom-right card, created on show and destroyed on hide (see `overlay_lifetime.h`). A mouse press outside the card closes it through `mouse_down_monitor.h`; focus and window activation never do. Escape reaches it through the stop-loading route, where the cheatsheet comes before a peek |
| `mouse_down_monitor.h`, `macos/mouse_down_mac.mm` | Reports every mouse press the app receives (an `NSEvent` local monitor, passed through unchanged), installed only while an overlay that closes on an outside click is shown; no-op elsewhere |
| `history_recorder.{h,cc}` | Single-sequence background owner of the on-device history `SearchIndex`; records web navigations, rebuilds/flushes the bounded segment, queries, and clears history |
| `find_bar_model.{h,cc}`, `find_bar_overlay.{h,cc}` | CEF-free find-bar state (query, match case, "n of m", the `FindRequest` each gesture needs) and the overlay card shown only while the bar is open; `browser_window_find_reader.cc` wires both to `CefBrowserHost::Find` / `OnFindResult` |
| `reader_mode.{h,cc}`, `reader/` | Reader view: `reader/reader_extract.js` (injected through DevTools `Runtime.evaluate`, returns a whitelist tree) and `reader/reader_page.html` (the document, embedded like `pages/`); `reader_mode.cc` builds the data: URL. Per-tab `ReaderSession`s record the origin page and the document's URL hash; its messages (`reader_settings`, `reader_exit`) are accepted only with that document's token |
| `address_indicators.h` | CEF-free rules for the Native address-field indicators (`ComputeAddressIndicators`: shield count capped at "9999+", Reader, agent state). `BrowserWindow::RefreshAddressIndicators` recomputes them on tab/space switch, blocked request, Reader change and agent session change and calls `BrowserChrome::SetAddressIndicators`; the shield opens Settings > Privacy (no blocker popover exists), the dot toggles the agent panel. The agent state is window-wide because the agent drives the active tab |
| `browser_window.cc` (confirmations) | `ShowConfirmation`/`AnswerConfirmation` keep pending agent confirmation cards in the window (120 s timeout via a 1 s UI timer, cancelled on shutdown); panel state key `confirmations`, panel message `confirm`, settings message `remove_trusted_site`; the Agent button icon carries a 6 DIP accent dot (`BrowserChrome::SetAgentNeedsYou`); dev action `agent_fixture:confirm` |
| `prefs_store.{h,cc}` | Preferences JSON (theme, history capture, agent provider + custom command with migration from the old single command, agent panel state, agent trusted sites, shortcut overrides, update checks) |
| `updater.{h,cc}` | CEF-free updater core: SemVer precedence, GitHub releases parsing/selection (`kReleasesRepo`), per-target assets and `SHA256SUMS.txt`, URL/redirect allow-list, install detection, generated sh/cmd apply scripts, the `Updater` state machine behind the `UpdateFetcher` seam |
| `sha256.{h,cc}` | Standard-library SHA-256 used to verify update downloads |
| `cef_update_fetcher.{h,cc}` | `UpdateFetcher` over `CefURLRequest` (manual, allow-listed redirects; capped, hashed streaming) |
| `content_blocker.{h,cc}`, `content_blocker_policy.{h,cc}` | RAII wrapper for the Rust engine plus CEF-free request-type, bypass, allowlist, and four-day refresh policy |
| `cef_content_blocker.{h,cc}` | Background EasyList/EasyPrivacy cache loading, `CefURLRequest` refreshes, atomic engine publication, and request checks |
| `bookmark_import.{h,cc}`, `browser_import.{h,cc}` | Import from Chrome-family browsers, Safari, Firefox (`.jsonlz4` bookmark backups) and Arc (spaces + pinned tabs) |
| `tab_sleep_policy.h`, `peek_state.h` | CEF-free models: tab sleep decisions (injected clock, per-tab last-active time, exclusions as inputs; `Expired` for the timed sweep, `AllEligible` for "Sleep inactive tabs now") and the peek card's state (open/replace/close/open-as-tab/Escape, generation counter for stale callbacks) |
| `peek_overlay.{h,cc}` | The peek card: an overlay of the one `CefWindow`, created in `Open` and released in `Close` (see `overlay_lifetime.h`), covering only the card, holding a title bar and its own `CefBrowserView` with its own client (popups load in the card, Escape claimed in `OnPreKeyEvent`) |
| `overlay_lifetime.h`, `overlay_card.h` | CEF-free overlay rule (an overlay exists only while its surface is shown, because a live `CefWindow` overlay blocks mouse input on macOS) and the solid-color `OverlayFillDelegate` that re-asserts its color on theme change |
| `browser_window_research.cc`, `research_prompt.{h,cc}`, `report_store.{h,cc}` | Research mode in `BrowserWindow` (start, cancel and finish, the Plan hub, the Report tab with save/copy/continue); the CEF-free prompt, research space name and report extraction; and the Reports list persisted as `reports.json` |
| `plan_dev_fixture.h` | The developer capture's sample research plan (`--island-dev-actions=plan_fixture`), kept equal to the test fixture by a unit test |
| `pages/plan.html`, `pages/report.html`, `pages/markdown.js` | The Plan tab's graph and list page, the Report tab page, and the shared safe Markdown renderer both use (inlined where a page carries `// @@ISLAND_MARKDOWN@@`) |
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
- **Peek** entry points: `OnBeforePopup` (new-tab dispositions with a user gesture; popups and windows stay rejected), `OnOpenURLFromTab`, and the context-menu item (`MENU_ID_USER_FIRST`). Opening is deferred one UI task. Escape reaches the card through the `Stop Loading (Escape)` menu item (`CanStopLoadingWithEscape`) and the card's own key handler. Selection changes close the peek. Dev actions: `peek:<url>`, `sleep_tabs`, `sleep_report`, `wait:<s>`; with `--island-dev-keep-open`, SIGUSR1 saves another capture and SIGUSR2 logs tab/sleep/peek state.

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
  swap per MCP call. Never swap the content view twice in one turn: that crashed CEF a few
  seconds later. The dev fixture appends background children (`activate=false`, no browser until
  first shown; their `Tab::DisplayTitle` is the address and `EffectiveUrl` saves the startup
  URL). Children sit right after the Plan tab's last child; a pinned Plan's children start the
  unpinned tabs, so the pinned group stays whole (`Space::AppendChildTab`). `NormalizeGroups`
  re-places a parent's children behind it after moves, pinning and restore; only the child the
  person dragged away is ungrouped. A hub outside the active space (an agent that made its own
  space) gets no group. `BrowserWindow` maps the running `BuildPlanGraph` node id to the opened
  `TabId`. `NavigateTab` and the agent's back/forward/reload refuse the Plan tab
  (`IsPlanTabIndex`). The sidebar draws a branch kicker line (`kTabGroupKicker`) above each run;
  every row keeps the fixed row height. Closing the Plan tab while `research_` is active is
  deferred to the in-content Stop / Keep running confirmation, or stops the research at once when
  the page has no live frame; removing the Plan's space stops the research (no dialog).
