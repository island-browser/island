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
| `browser_chrome.{h,cc}` | Native chrome layout; declares the `ChromeViewId` enum (`kRoot = 1001` …) |
| `chrome_snapshot.h` | Observable snapshot struct + `ChromeObserver` seam used by tests |
| `browser_command.h` | The Back / Forward / Reload command enum |
| `navigation_state.{h,cc}` | Back/forward/reload availability and navigation snapshot |
| `address_bar_model.{h,cc}` | Address field text/editing state |
| `address_policy.{h,cc}` | Decides how typed input is treated (navigate vs. search vs. reject) |
| `cef_address_parser.{h,cc}` | CEF-dependent URL parsing behind the policy seam |
| `design_tokens.{h,cc}` | `ArgbColor` and the token palette shared with `tests/design/` |
| `icon_catalog.{h,cc}` | Resolves icon names/sizes/scales against `resources/island/icons/manifest.json` |
| `app_resources.{h,cc}` | Runtime resource-directory resolution (fonts, icons) |
| `font_registry.h` | Font registration seam, implemented per platform |
| `app_runtime.{h,cc}` | Runtime seam shared by the app and tests |
| `startup_options.{h,cc}` | Command-line parsing, including `--island-smoke-test` |
| `lifecycle_config.h` | Shutdown/lifecycle constants |
| `tab.{h,cc}`, `tab_id.h` | Move-only tab model identified by `TabId`, holding the per-tab `CefBrowserView`/`CefBrowser` seam |
| `space.{h,cc}` | Named, colored space owning ordered tabs, active selection, `SplitPairing`, and the space's `CefRequestContext` |
| `session_store.{h,cc}` | Session JSON round-trip with a `SessionError` taxonomy; not yet called by any lifecycle code |
| `window_agent_host.{h,cc}`, `devtools_bridge.{h,cc}`, `agent_navigation.{h,cc}` | `AgentBrowserHost` implementation behind the MCP tools in `src/agent/` (tabs, spaces, page text/screenshot via DevTools) |
| `local_page.{h,cc}`, `local_pages_html.h`, `pages/*.html` | In-window HTML pages (agent panel, Settings, All tabs) loaded as `data:` URLs; page→native messages travel as `\x01island:` console messages, native→page state through `islandRender(state)` |
| `keymap.{h,cc}` | Configurable shortcuts: `KeyAction` ids, `Mod+Shift+K` binding text, defaults, conflicts, VK and macOS key-equivalent mapping |
| `prefs_store.{h,cc}` | Preferences JSON (theme, agent provider + custom command with migration from the old single command, agent panel state, shortcut overrides, update checks) |
| `updater.{h,cc}` | CEF-free updater core: SemVer precedence, GitHub releases parsing/selection (`kReleasesRepo`), per-target assets and `SHA256SUMS.txt`, URL/redirect allow-list, install detection, generated sh/cmd apply scripts, the `Updater` state machine behind the `UpdateFetcher` seam |
| `sha256.{h,cc}` | Standard-library SHA-256 used to verify update downloads |
| `cef_update_fetcher.{h,cc}` | `UpdateFetcher` over `CefURLRequest` (manual, allow-listed redirects; capped, hashed streaming) |
| `bookmark_import.{h,cc}`, `browser_import.{h,cc}` | Import from Chrome-family browsers, Safari, Firefox (`.jsonlz4` bookmark backups) and Arc (spaces + pinned tabs) |
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
- macOS: CoreFoundation, CoreText — Windows: gdi32 — Linux: Fontconfig

<!-- MANUAL: -->
