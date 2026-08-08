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
