# AGENTS.md

## Current state

- Island 0.6.0 through 0.6.2 shipped on 2026-10-15 as **unsigned GitHub prereleases**. The
  shipped feature set: Alloy runtime style (the Chrome style crashed on deactivate/close, and Island
  handles downloads itself); `--use-mock-keychain` in the app and the CEF test harness; overlays
  that exist only while shown (`src/main/overlay_lifetime.h`, because a live `CefWindow` overlay
  blocks all mouse input on macOS); tabs and spaces (tab strip, space switcher, rename via F2/menu,
  reorder via menu); split view (`Cmd/Ctrl+Shift+S` pairs the active tab with its adjacent tab; the
  divider is keyboard-adjustable); keyboard shortcuts; the command palette on `Cmd/Ctrl+K` and the
  search palette on `Cmd/Ctrl+Shift+K`; a hideable sidebar (`Cmd/Ctrl+B` collapses it to a 52 DIP
  icon rail of tab favicons; no hover reveal); popup rejection; shutdown through the CEF close
  lifecycle; a search start page with Search / Ask agent / Research modes and Markdown agent
  replies; Research mode with Report tabs; developer capture flags
  (`--island-dev-actions`, `--island-dev-capture`, `--island-dev-delay`); native ad and tracker
  blocking on Brave's Rust `adblock` engine (building it needs a Rust toolchain); on-device history
  search; tab sleep and the Peek card (see `src/main/AGENTS.md`); Reader view and find in page
  (`Cmd/Ctrl+F`).
- Documented macOS deviations: `CefWindow::SetAccelerator` never dispatches (the NSMenu owns the
  command keys), and the split divider is not drag-adjustable because the pinned CEF distribution
  exposes no mouse events on custom views; the divider moves through the menu items and
  `kMoveDividerLeft/Right` commands instead.
- Phase 3 U10 (manual/visual acceptance) remains outstanding; it needs a human with display access
  per `docs/phase3-visual-acceptance.md`.
- Browser-store extensions are out of scope. They only work in CEF's Chrome runtime style, and
  Island uses Alloy.
- The Arc-style look (2026-10-15, project owner's request) is in: a frameless macOS window with
  the traffic lights over the sidebar, a space-tinted `canvas` shared by the sidebar and window
  margin, a rounded/shadowed page card, soft rounded row fills and favicon plates, plus Settings >
  Appearance (per-space color, tint, sidebar side, radius, density, font, shadow, full bleed;
  prefs `appearance`). CEF views cannot round or shadow, so `src/main/macos/page_card_mac.mm`
  overlays native corner/shadow layers on macOS only; see DESIGN.md §5 and its Accepted Debt.
- The "Native" look is in and is the default (2026-10-16 spec, project owner's request): a
  macOS-style source list, system font, one blue accent, the 52 DIP toolbar with the address field
  and its indicators, and no space color. Settings > Appearance has a Look preset (Native, Arc,
  Graphite); Arc and Graphite are kept. Research shows an editable plan graph in a pinned Plan tab,
  with grouped child tabs and at most 3 branch levels. Spec:
  `docs/superpowers/specs/2026-10-16-island-macos-native-design.md`; plan:
  `docs/superpowers/plans/2026-10-16-island-macos-native.md`.
- Read `docs/superpowers/specs/2026-08-09-island-browser-phase3-design.md` and
  `docs/superpowers/plans/2026-08-09-island-browser-phase3.md` (plus the 2026-08-12 sidebar/palette
  spec and plan) before implementing anything. The Phase 0/1/2 design and plan documents remain
  historical context only, and the "Phase 1 contract" section below describes Phase 1, not the
  current feature set.
- Every phase still requires its own accepted spec and plan, and only units of the currently
  accepted plans may be implemented. Extensions, sync, profiles/accounts, and multiple top-level
  `CefWindow`s remain out of scope (see each design's non-goals). A Settings page (theme, agent
  command, MCP config, configurable shortcuts, browser import) and an All-tabs overview were added
  on 2026-10-06 at the project owner's request; both are local HTML pages shown in the content
  slot (`src/main/pages/`). Shortcut overrides persist in prefs; on macOS they reach the NSMenu
  on the next launch.
- Tab sleep (inactive background tabs release their `CefBrowser`; Settings → Tabs, palette command) and
  Peek (a floating single-window card with its own browser, opened from Cmd/middle-click, the
  context menu or new-tab opens) were added on 2026-10-15; see `src/main/AGENTS.md`.
- Spaces can use one of 24 bundled Lucide icons. The optional id is stored in the session; the
  sidebar switcher and collapsed rail fall back to their previous color mark/initial when absent.
- Agentic integration is in scope as of 2026-10-06 at the project owner's request: the CEF-free
  `src/agent/` kernel provides an MCP tools endpoint (`http://127.0.0.1:<port>/mcp`, bearer token,
  discovery file) and an ACP client that runs an agent in the sidebar. GitHub Actions triggers
  are paused (manual `workflow_dispatch` only) except `build-release.yml` and
  `version-tag.yml` on pushes to `main`; build, test, and look at changes locally before
  committing. At the project owner's request (2026-10-06), `build-release.yml` builds all six
  targets on every push to `main` that changes the app and publishes them as **unsigned GitHub
  prereleases** (`nightly`, plus `vX.Y.Z` once per version) for the in-browser updater.
- Agent actions (spec `docs/superpowers/specs/2026-10-17-island-agent-actions-design.md`, 2026-10-17):
  every consequential page or browser action the agent takes passes the CEF-free action gate
  (`src/agent/action_gate.*`). Password fields are never typed into; card fields, submits,
  consequential words, scripts, navigation to another origin in the person's tab and closing the
  person's tab ask first through a confirmation card in the agent panel (Allow once, Allow for this
  task, Trust this site, Cancel; 120 s). Trusted sites (Settings > Agent & tools) skip those
  prompts. Agent work runs from a visible task queue (`src/agent/agent_task_queue.*`, the Tasks
  strip). The agent can be asked about a selection, a link or the page, and Collect from tabs
  gathers a table that exports as CSV or JSON. Extension points: `src/agent/AGENTS.md` and
  `src/main/AGENTS.md`, "Extension points".
- Native ad and tracker blocking is in scope as of 2026-10-15 at the project owner's request. It
  uses Brave's Rust `adblock` engine with runtime-cached EasyList and EasyPrivacy data; browser
  extensions remain out of scope.
- Research mode (start page's third mode, 2026-10-15): `research_prompt.*` (prompt, space name,
  report extraction, turn outcome), `report_store.*` (`reports.json`), `browser_window_research.cc`
  (start/cancel/finish, Report tab, save/copy/continue), `pages/report.html`. The shared safe Markdown
  renderer lives in `pages/markdown.js`; the root `CMakeLists.txt` inlines it where a page carries the
  `// @@ISLAND_MARKDOWN@@` line. Report tabs are `data:` documents (`#<id>` fragment) and are not
  restored after a restart (the report stays in the Reports list).
- Versioning: `VERSION` is the only place the version is written. Bump it with
  `python3 scripts/version.py bump <part>` (never by hand-editing derived files), keep notes under
  `## [Unreleased]` in `CHANGELOG.md` as you land changes, and run
  `python3 scripts/version.py check` before committing version changes. A `VERSION` change
  on `main` is tagged `vX.Y.Z` by `version-tag.yml`.
  Every release goes through a PR that also bumps `VERSION`. Once `vX.Y.Z` is tagged, a follow-up
  fix needs a new patch version, because the release workflow only publishes `vX.Y.Z` for the
  tagged commit. Merging PRs is done by the project owner.
- The product site moved to its own repository, `island-browser/site` (served at
  https://island-browser.github.io/site/), on 2026-10-06 at the project owner's request. Its build
  reads `VERSION` and `CHANGELOG.md` from this repository's `main` (with its `ISLAND_READ_TOKEN`
  secret, since this repository is private) and imports
  `scripts/version.py` (`read_version`, `check`, `render_changelog_html`, the changelog markers),
  so keep those names stable; its CSS is checked against the `DESIGN.md` token contract there.
- The Phase S0 search kernel under `src/search/` is part of the default browser build and backs
  local history search. `ISLAND_ENABLE_SEARCH` remains the opt-in switch for configuring
  `src/search/` as a standalone project. The hybrid native/container build lane (`docker/`,
  `tests/container/`) is present on `main`.

## Phase 1 contract

Historical: this section describes Phase 1 only. The current feature set is under
"Current state" above.

- Island is a C++20 desktop browser built directly on CEF and native `cef_views`, not Electron or a
  web-based app shell. Phase 1 creates one top-level `CefWindow` with one `CefBrowserView`; there is
  no address bar, tab strip, sidebar, settings surface, or extension UI.
- The intended desktop target set is `macosx64`, `macosarm64`, `windows64`, `windowsarm64`,
  `linux64`, and `linuxarm64`. Only macOS arm64 has local build/run/package evidence; macOS x64
  and Windows/Linux architectures require native GitHub Actions evidence before support claims.
- `scripts/setup_deps.sh` must vendor the CEF binary distribution into `third_party/cef/` and
  Geist fonts into `assets/fonts/` from `deps/dependencies.lock.json`; both outputs stay
  gitignored. Do not replace this with FetchContent or commit the binaries.
- GoogleTest is the exception: fetch v1.15.2 through CMake `FetchContent` and discover tests with
  `gtest_discover_tests`. A first configure therefore requires network access.
- Treat `third_party/cef/cmake/cef_macros.cmake` as the CEF-installed sentinel. Configuration must
  fail clearly and direct the user to `scripts/setup_deps.sh` when it is absent.
- If the planned CEF CMake or macOS bundle integration disagrees with the vendored distribution,
  follow `third_party/cef/tests/cefsimple/`; it is authoritative for the pinned CEF version.
- Set `CefSettings.remote_debugging_port` to `9222`, but add no other CDP or agentic integration.
- Production and smoke startup pages are fixed local `data:text/html` documents. Startup must not
  request external network resources.
- Browser commands are limited to Back, Forward, and Reload. Popups/new windows are rejected.

## Conventions

- Use explicit `std::` qualification; do not add `using namespace std;`.
- Use target-scoped CMake commands, not global `include_directories` or `link_libraries`.
- Formatting is Google-based with 4-space indentation, 100-column lines, left-aligned pointers,
  and sorted includes; the plan adds the exact `.clang-format` before source code.

## Cross-platform pitfalls

- macOS clang accepts code that GCC and MSVC reject, so only the six-target CI run is evidence for
  Linux and Windows.
- GCC: `BrowserWindow` inherits several CEF handlers that each declare the same typedef (for
  example `WindowOpenDisposition` in both `CefLifeSpanHandler` and `CefRequestHandler`), so
  unqualified use is ambiguous. Use the C enum type (`cef_window_open_disposition_t`) or qualify it.
- MSVC: sources are compiled with `/utf-8` (PUBLIC on `island_browser_core`, PRIVATE on
  `island_tests`); any new target with non-ASCII literals needs the same option.

## Verification

Use these copy-pasteable commands from a clean checkout:

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
python3 scripts/deps.py verify
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R <TestName> --output-on-failure
open build/src/main/island_browser.app
```

Copy-paste smoke run on macOS:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

After manually quitting the app, verify that no `island_browser` helper processes remain:

```bash
pgrep -fl island_browser || true
```

Stable public release remains blocked until signing and notarization verification are implemented.

<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

## Repository map

Every directory below owns an `AGENTS.md` carrying a `Parent:` HTML comment that points back up this
tree. This root file is the only one without such a tag. The sections above this marker are
hand-maintained project instructions and take precedence over anything a nested `AGENTS.md` says.

| Directory | Purpose |
|-----------|---------|
| `src/` | C++20 application sources (see `src/AGENTS.md`) |
| `tests/` | GoogleTest and pytest suites (see `tests/AGENTS.md`) |
| `bench/` | Deterministic benchmark support code (see `bench/AGENTS.md`) |
| `scripts/` | Dependency and packaging entrypoints (see `scripts/AGENTS.md`) |
| `deps/` | Dependency lock model, installer, and update checks (see `deps/AGENTS.md`) |
| `cmake/` | Platform CMake modules included by `src/main` (see `cmake/AGENTS.md`) |
| `docs/` | Process docs, phase specs, and phase plans (see `docs/AGENTS.md`) |
| `resources/` | Committed runtime icon resources (see `resources/AGENTS.md`) |
| `tools/` | Offline icon generation pipeline (see `tools/AGENTS.md`) |
| `.github/` | CI, packaging, and release automation (see `.github/AGENTS.md`) |

### Root files

| File | Description |
|------|-------------|
| `CMakeLists.txt` | Top-level build: CEF sentinel check, font/icon resource gates, bounded-directory staging in `island_stage_chrome_resources`, the `ISLAND_ENABLE_SEARCH` option, then `tests/` and `src/main/` |
| `CLAUDE.md` | Loader that pulls this file in via `@AGENTS.md` |
| `DESIGN.md` | Long-form product/visual design narrative |
| `README.md` | Human-facing project overview |
| `VERSION` | The single source of the version (SemVer); read by CMake and `scripts/version.py` |
| `CHANGELOG.md` | Keep-a-Changelog release notes; the site repository renders them into its changelog page at build time |
| `.gitbook.yaml` | GitBook Git Sync: serves `docs/user-guide/` (README.md, SUMMARY.md) |
| `.clang-format` | Google base, 4-space indent, 100 columns, left-aligned pointers |

### Search build

The Verification commands above build the Phase S0 search kernel, link it into
`island_browser_core`, and run its tests as part of the default root suite. The option
`ISLAND_ENABLE_SEARCH` is retained for compatibility with the standalone search configuration;
it no longer excludes search from a browser-root build.

The AI-agent kernel under `src/agent/` (MCP tools endpoint, ACP client) is CEF-free and always
part of the root build; it also builds and tests standalone without `scripts/setup_deps.sh`:

```bash
cmake -B build-agent -S src/agent
cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
```

To build and test the search kernel on its own, without CEF and without `scripts/setup_deps.sh`
(this is what `.github/workflows/search.yml` runs on all six targets):

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

The browser-root build includes the search kernel without an extra option:

```bash
cmake -B build-search-root -S .
cmake --build build-search-root
ctest --test-dir build-search-root --output-on-failure
```

The Python suites are separate from `ctest` entirely:

```bash
python3 -m pytest tests/deps tests/package tests/design tests/version
```

### Excluded from this documentation tree

No `AGENTS.md` is generated under: `third_party/` and `assets/` (both vendored by
`scripts/setup_deps.sh` and gitignored), any `build*/` or `dist*/` directory, `.omc/`,
`.omo/`, `.opencode/`, `.playwright-mcp/`, `.claude/`, the `.codegraph` symlink, and the `.cache/` /
`.pytest_cache/` / `.ruff_cache/` / `__pycache__/` caches.

Three further leaf directories are covered by a note in their parent instead of their own file,
because they hold only generated output or vendored inputs: `resources/island/icons/{source,png}/`
and `tools/icon_pipeline/{licenses,vendor}/`.

Two directories that exist in some working trees are **not** documented here because they are not
present on `main`: `docker/` and `tests/container/`, which belong to an uncommitted hybrid
native/container build lane. An `AGENTS.md` for each should land in the same change as the code.

Treat the two lists above as the declared scope when checking this tree for completeness.

<!-- MANUAL: Notes added below this line are preserved on regeneration -->
