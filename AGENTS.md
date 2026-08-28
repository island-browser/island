# AGENTS.md

## Current state

- Phase 1 is implemented on top of the Phase 0 infrastructure: the app opens one native CEF window
  containing exactly one `CefBrowserView`, starts from deterministic local `data:` pages, exposes
  shared `BrowserWindow`/`IslandApp`/runtime/navigation snapshot seams, supports back/forward/reload
  commands and shortcuts, rejects popups, and shuts down through the CEF close lifecycle.
- Read `docs/superpowers/specs/2026-08-08-island-browser-phase1-design.md`, then
  `docs/superpowers/plans/2026-08-08-island-browser-phase1.md` before implementing anything. The
  Phase 0 design and plan remain historical context only.
- Implement only the current accepted phase. Phase 2+ work requires its own spec and plan; browser
  chrome, tabs, spaces, persistence, command bar, settings, extensions, and expanded CDP/agentic
  features are currently out of scope.

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

The Verification commands above build and run the default suite only — **103 tests** on `main`. The
Phase S0 search kernel under `src/search/` is guarded by `ISLAND_ENABLE_SEARCH`, which is declared
`OFF`, so none of it is configured, compiled, or run by those commands.

To build and test the search kernel on its own, without CEF and without `scripts/setup_deps.sh`
(this is what `.github/workflows/search.yml` runs on all six targets):

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

That reports 84 tests. To build the browser and the search kernel together instead:

```bash
cmake -B build-search-root -S . -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search-root
ctest --test-dir build-search-root --output-on-failure
```

That reports 187 tests (103 + 84). The Python suites are separate from `ctest` entirely:

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
