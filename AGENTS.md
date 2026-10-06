# AGENTS.md

## Current state

- Phases 1–3 are landing unit-by-unit on top of the Phase 0 infrastructure: the app opens one
  native CEF window with a `CefBrowserView` content slot, starts from deterministic local `data:`
  pages (or a clean-quit-restored session), and supports back/forward/reload, tabs and spaces (tab
  strip, space switcher, rename via F2/menu, reorder via menu), split view (`Cmd/Ctrl+Shift+S`
  pairs the active tab with its adjacent tab; the divider is keyboard-adjustable), the command
  palette on `Cmd/Ctrl+K`, the search palette on `Cmd/Ctrl+Shift+K`, the hideable sidebar
  (`Cmd/Ctrl+B` everywhere, macOS hover-reveal bands), popup rejection, and shutdown through the
  CEF close lifecycle. Phase 3 units U4/U5, U6, U7, and U8 have landed; U9's locally completable
  part is done (workflows audited, docs updated — the green CI/package runs land with the next
  push), U11's clean-checkout regression is green locally (fresh configure/build, 242 default +
  150 search + 361 combined ctest, pytest, smoke run), and U10 (manual/visual acceptance) remains
  outstanding — it needs a human with display access per docs/phase3-visual-acceptance.md. Documented deviations: on macOS `CefWindow::SetAccelerator` never dispatches (the
  NSMenu owns the command keys), and the split divider is not drag-adjustable because the pinned
  CEF distribution exposes no mouse events on custom views — the divider moves through the menu
  items and `kMoveDividerLeft/Right` commands instead.
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
- Agentic integration is in scope as of 2026-10-06 at the project owner's request: the CEF-free
  `src/agent/` kernel provides an MCP tools endpoint (`http://127.0.0.1:<port>/mcp`, bearer token,
  discovery file) and an ACP client that runs an agent in the sidebar. GitHub Actions triggers
  are paused (manual `workflow_dispatch` only); build, test, and look at changes locally before
  committing.
- The Phase S0 search kernel under `src/search/` stays gated behind `ISLAND_ENABLE_SEARCH`
  (declared `OFF`), and the hybrid native/container build lane (`docker/`, `tests/container/`) is
  present on `main`.

## Phase 1 contract

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
| `CMakeLists.txt` | Top-level build: CEF sentinel check, font/icon resource gates, `island_stage_chrome_resources`, the `ISLAND_ENABLE_SEARCH` option, then `tests/` and `src/main/` |
| `CLAUDE.md` | Loader that pulls this file in via `@AGENTS.md` |
| `DESIGN.md` | Long-form product/visual design narrative |
| `README.md` | Human-facing project overview |
| `.clang-format` | Google base, 4-space indent, 100 columns, left-aligned pointers |

### Search build (opt-in)

The Verification commands above build and run the default suite only — **242 tests** on `main`
as of 2026-09-19 (the count grows as units land; rerun `ctest -N` after adding tests). The
Phase S0 search kernel under `src/search/` is guarded by `ISLAND_ENABLE_SEARCH`, which is declared
`OFF`, so none of it is configured, compiled, or run by those commands.

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

That reports 150 tests (as of 2026-09-19). To build the browser and the search kernel together instead:

```bash
cmake -B build-search-root -S . -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search-root
ctest --test-dir build-search-root --output-on-failure
```

That reports 361 tests on `main` (as of 2026-09-19; the combined target discovers a slightly different set than the sum of the standalone lanes). The Python suites are separate from `ctest` entirely:

```bash
python3 -m pytest tests/deps tests/package tests/design
```

### Excluded from this documentation tree

No `AGENTS.md` is generated under: `third_party/` and `assets/` (both vendored by
`scripts/setup_deps.sh` and gitignored), any `build*/` or `dist*/` directory, `site/`, `.omc/`,
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
