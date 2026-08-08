# Island Browser — Phase 1 Implementation Plan

> **Status:** Completed, reviewed, and tested. This is the canonical Phase 1 implementation record.
> Historical Phase 0 plan: `docs/superpowers/plans/2026-08-07-island-browser-phase0.md`.

**Goal:** Turn the Phase 0 empty native CEF window into a minimal deterministic browser surface: one
`CefBrowserView`, one local startup page, observable navigation state, back/forward/reload commands,
popup rejection, and clean CEF shutdown without introducing browser chrome or later product systems.

**Architecture:** `IslandApp` owns the single `BrowserWindow`. `BrowserWindow` is the `CefClient`,
`CefWindowDelegate`, `CefBrowserViewDelegate`, display/load/lifespan handler, command receiver, and
close-lifecycle owner. `NavigationState` publishes small immutable snapshots for tested runtime
state. `app_runtime` centralizes `CefInitialize`, remote debugging port `9222`, sandbox flag
selection, message loop, observer release, and `CefShutdown`. Platform entry points only bootstrap the
same core.

**Tech Stack:** CEF 150 (`cef_views`), C++20, CMake ≥ 3.21, GoogleTest v1.15.2 through
`FetchContent`, Python packaging tests, native macOS/Windows/Linux entry points.

## Global constraints

- CEF and Geist are vendored by `scripts/setup_deps.sh` from `deps/dependencies.lock.json` into
  gitignored `third_party/cef/` and `assets/fonts/`.
- First CMake configure needs network access for GoogleTest; CEF and fonts must already be installed.
- Keep the app directly on CEF/native `cef_views`, not Electron or a web shell.
- Use only one top-level window and one browser view in Phase 1.
- Startup pages must be local `data:` documents and must not trigger external startup requests.
- Browser commands are limited to Back, Forward, Reload.
- Reject popups and new windows.
- Do not add browser chrome, tabs, spaces, persistence, command bar, settings, extensions, or extra
  CDP features.

---

## Completed implementation checklist

- [x] **Startup options:** Parse `--island-smoke-test`; default to fixed production data page; expose
  only deterministic local production/smoke URLs.
- [x] **BrowserWindow:** Create one `CefWindow`, attach one `CefBrowserView`, disable CEF toolbar,
  store the primary browser, and reject all popup creation.
- [x] **Navigation snapshot:** Track URL, page title, display title, load phase, back/forward
  availability, HTTP status, network error, and revision. Publish to an observer and close/detach on
  shutdown.
- [x] **Commands and shortcuts:** Implement Back, Forward, Reload; wire macOS Browser menu entries and
  accelerators for Cmd+[ / Cmd+] / Cmd+R plus reload control accelerator.
- [x] **CEF close lifecycle:** `CanClose()` delegates to `TryCloseBrowser()`, browser destruction clears
  refs, window destruction closes navigation and quits the CEF message loop, then runtime calls
  `CefShutdown()`.
- [x] **Platform bootstrap:** Keep macOS Objective-C++ entry with native menu and bundle helpers; add
  Windows entry with sandbox-off/on support; keep Linux entry/CMake platform integration.
- [x] **CMake core:** Build shared `island_browser_core` and platform executables; keep CEF sentinel
  failure and target-scoped configuration.
- [x] **Tests:** Cover lifecycle constants, startup options/data-only URLs, and navigation state with
  GoogleTest discovery. Acceptance run: 18/18 CTest passed on macOS arm64.
- [x] **Packaging/workflows:** Preserve unsigned package metadata, checksums, macOS framework/helper
  layout, Windows sandbox DLL checks, architecture checks, and native build matrix. Windows build
  matrix covers sandbox off and sandbox on.
- [x] **Package compatibility:** Phase 0 package layout compatibility was verified; package tests report
  11 passed.

---

## Verification commands

Use these from a clean checkout on macOS arm64:

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
python3 scripts/deps.py verify
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app
```

Copy-paste smoke command:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

Process cleanup check after manually quitting:

```bash
pgrep -fl island_browser || true
```

Accepted evidence:

- macOS arm64 clean build passed.
- Dependency verify passed.
- CTest passed 18/18.
- Runtime showed one window, one page, expected title, and smoke marker.
- Data-only startup made no external startup request.
- Popup was blocked.
- History back/forward worked.
- Native close and menu Quit cleaned up helper processes.
- Package tests passed 11/11.
- Cmd+R produced a decisive signal; physical Cmd+Q remains a manual caveat.

---

## Phase 2+ non-goals

Do not treat any of these as implemented by Phase 1: address bar, custom browser chrome, multiple
tabs, spaces, persistence, command bar, settings, extensions, split view, hibernation, stable signing
or notarization, or CDP/agentic capabilities beyond `remote_debugging_port = 9222`.

macOS x64 and Windows/Linux targets still require native CI evidence before support claims. Stable
releases remain blocked by signing and notarization verification.
