# Island Browser — Phase 2 Implementation Plan

> **Status:** Approved execution plan. It implements only the Phase 2 design in
> [the canonical specification](../specs/2026-08-08-island-browser-phase2-design.md).

**Goal:** Add one accessible native chrome surface around the existing single CEF browser view:
Frame 01's 286 DIP focus rail, contextual editable address flow, OS-theme tokens, registered
fonts, deterministic icons, and verified runtime/package delivery. The reference viewport is
1440×900 DIP; the supported minimum is 800×560 DIP.

**Implementation rule:** Work in the stated dependency order. An implementer owns only the files in
their unit. Do not modify `browser.pen`, Phase 1 documents, unrelated docs, or another unit's files.
No unit may introduce Frame 07/08, tabs, spaces, persistence, settings, command UI, extensions, or
any Phase 3+ behavior.

## Shared acceptance contract

- Retain one `CefWindow`, one `CefBrowserView`, Phase 1 startup URLs, popup rejection, Back/Forward/
  Reload commands, port `9222`, and the existing CEF close lifecycle.
- Use Frame 01 hierarchy in light and dark themes. Frame 06 contributes dark token values only.
  The rail is 286 DIP; the reference window is 1440×900 DIP and minimum is 800×560 DIP.
- Represent only one active/current-page focus item. It is not a tab list or tab implementation.
- Address submission accepts only absolute credential-free `http`/`https` URLs with a non-empty DNS
  hostname, `localhost`, `*.localhost`, `127.0.0.0/8`, or `[::1]`; it rejects relative URLs,
  malformed values, credentials, and unsupported schemes without CEF navigation.
- Pin Lucide 1.30.0 at `249af14dc6c09d846fada19455ac074ed29ee407` with SHA-256
  `c38157cb46ef10cf21782f3bf90b75a4a7dbbe973ef376ff18b71766bbc1574e`; map the `globe-2`
  semantic slot to `earth.svg`. NanoSVG/STB are approved inputs only until each archive SHA is
  independently computed and recorded in the lock.
- Accept rectangular `cef_views` browser/input hit regions; do not require cross-platform blur,
  clipping, rounded browser content, or shadow parity.

## Units

### U1 — Lock chrome dependencies and deterministic assets

**Owner:** U1 implementer
**Files:** `deps/dependencies.lock.json`, `deps/model.py`, `deps/resolver.py`, `deps/install.py`,
`scripts/setup_deps.sh`, `tests/deps/test_resolver.py`, `assets/icons/manifest.json`
**Depends on:** none

Add the pinned Lucide archive and the explicit SVG manifest, including `earth.svg` for the
`globe-2` semantic slot. Add NanoSVG/STB only after downloading their immutable archives and
independently computing their SHA-256 values; record source, commit, SHA, and extraction allowlist.
Extend the dependency model/resolver/install tests so an absent, wrong, or unallowlisted asset fails
before extraction. Keep all downloaded/extracted outputs ignored.

**Tests:** `bash -n scripts/setup_deps.sh`; `./scripts/setup_deps.sh --dry-run`;
`python3 scripts/deps.py verify`; `python3 -m pytest tests/deps/test_resolver.py -q`.

### U2 — Add typed chrome and address models

**Owner:** U2 implementer
**Files:** `src/main/chrome_tokens.h`, `src/main/chrome_tokens.cc`, `src/main/address_model.h`,
`src/main/address_model.cc`, `tests/chrome_tokens_test.cpp`, `tests/address_model_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** none

Define typed token values for both OS themes and the address model/snapshot/mode/rejection result.
Implement strict absolute URL parsing with the Phase 2 allowlist and credential rejection. Keep this
code independent of CEF views so it is unit-testable. Tests must cover valid HTTPS/DNS, HTTP
localhost, `.localhost`, `127/8`, `[::1]`, and each rejected class.

**Tests:** `cmake -B build -S .`; `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(ChromeTokens|AddressModel)' --output-on-failure`.

### U3 — Implement native `BrowserChrome` composition

**Owner:** U3 implementer
**Files:** `src/main/browser_chrome.h`, `src/main/browser_chrome.cc`,
`src/main/browser_chrome_view.h`, `src/main/browser_chrome_view.cc`, `tests/browser_chrome_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U2

Build the Frame 01-only hierarchy: a 286 DIP focus rail, navigation controls, contextual address
view, a single non-interactive active-tab focus representation, and a rectangular browser-content
host. Project immutable navigation/address snapshots into native controls; do not place CEF refs or
URL policy in chrome. Implement compact layout at the 800×560 minimum without creating another
surface or state model. Test projection, enabled state, truncation, edit/display swap, and focus
order through a view-independent seam.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R BrowserChrome --output-on-failure`.

### U4 — Wire ownership, commands, observers, and lifecycle

**Owner:** U4 implementer
**Files:** `src/main/browser_window.h`, `src/main/browser_window.cc`, `src/main/island_app.h`,
`src/main/island_app.cc`, `src/main/browser_command.h`, `tests/browser_window_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U2, U3

Make `BrowserWindow` own one `BrowserChrome` and the sole `CefBrowserView`; retain `IslandApp` as
the sole `BrowserWindow` owner. Adapt navigation snapshots to chrome/address snapshots, dispatch
validated submissions to the main frame, wire click/Cmd-or-Ctrl+L/Enter/Escape behavior, and detach
observers before browser references are cleared. Preserve Phase 1 popup and close behavior. Test
that rejected input never reaches the navigation seam and callbacks after close do nothing.

**Tests:** `cmake --build build --target island_browser_tests`;
`ctest --test-dir build -R '(BrowserWindow|NavigationState|Lifecycle)' --output-on-failure`.

### U5 — Register fonts and select OS theme

**Owner:** U5 implementer
**Files:** `src/main/font_registry.h`, `src/main/font_registry.cc`, `src/main/platform_theme.h`,
`src/main/platform_theme.cc`, `src/main/main_mac.mm`, `src/main/main_win.cc`,
`src/main/main_linux.cc`, `tests/font_registry_test.cpp`, `tests/platform_theme_test.cpp`,
`tests/CMakeLists.txt`
**Depends on:** U1, U2, U4

Register required Geist/Geist Mono assets before chrome creation, fail startup with a useful
diagnostic if a declared font is missing, select the OS light/dark token set, and refresh chrome on
the native theme-change path. Platform entry points provide only platform hooks; common policy stays
in the shared implementation. Test registration input/diagnostics and theme-to-token projection.

**Tests:** `cmake --build build`; `ctest --test-dir build -R '(FontRegistry|PlatformTheme)' --output-on-failure`.

### U6 — Integrate CMake/runtime/package assets

**Owner:** U6 implementer
**Files:** `CMakeLists.txt`, `src/main/CMakeLists.txt`, `src/main/Info.plist.in`,
`scripts/package.py`, `tests/package/test_package.py`
**Depends on:** U1, U3, U5

Add target-scoped sources and copy only manifest-declared fonts/icons into each runtime layout.
Make configure/package fail on missing declared assets, and assert package contents do not rely on
source-tree paths. Preserve the CEF sentinel, GoogleTest discovery, architecture checks, unsigned
metadata, and existing bundle/helper contracts.

**Tests:** `cmake -B build -S .`; `cmake --build build`; `ctest --test-dir build --output-on-failure`;
`python3 -m pytest tests/package/test_package.py -q`; `python3 scripts/package.py --help`.

### U7 — Extend native CI and dependency/package evidence

**Owner:** U7 implementer
**Files:** `.github/workflows/build.yml`, `.github/workflows/package.yml`,
`.github/workflows/dependency-check.yml`, `docs/supported-platforms.md`
**Depends on:** U1, U6

Run dependency verification, configure/build/CTest, and package checks with the Phase 2 assets on
the existing native matrix. Keep target support claims unchanged until native evidence exists; do
not add signing/notarization claims. Document the required macOS arm64 visual evidence rather than
pretending a workflow screenshot proves it.

**Tests:** validate workflow YAML with the repository's available YAML checker; run
`python3 scripts/deps.py verify`; run the same local configure/build/CTest/package commands used by
U6.

### U8 — Capture visual, keyboard, and accessibility acceptance

**Owner:** U8 implementer
**Files:** `docs/phase2-visual-acceptance.md`, `tests/manual/phase2_chrome_checklist.md`
**Depends on:** U4, U5, U6

Record macOS arm64 evidence at 1440×900 and 800×560 in light and dark OS themes. Verify the Frame
01 hierarchy, exact 286 DIP rail at reference size, one active focus representation, focus order,
Cmd+L, Enter, Escape, disabled navigation semantics, validation announcement, and no browser-view
clipping/shadow requirement. Include manual close/process-cleanup and data-only-startup regressions.

**Tests:** `open build/src/main/island_browser.app`; `open build/src/main/island_browser.app --args --island-smoke-test`;
after manual quit, `pgrep -fl island_browser || true`.

### U9 — Run end-to-end regression and targeted rework

**Owner:** U9 integrator
**Files:** only failing unit-owned files, plus `tests/startup_options_test.cpp`,
`tests/navigation_state_test.cpp`, and `tests/lifecycle_config_test.cpp` when a regression test is
required
**Depends on:** U1–U8

Run the clean-checkout sequence and repair only demonstrated Phase 2 integration failures. Rework
is targeted: URL-policy defects return to U2/U4; hierarchy/theme/focus defects return to U3/U5;
runtime asset/package defects return to U1/U6; CI evidence defects return to U7; manual acceptance
gaps return to U8. Do not use this unit for refactors or Phase 3 features.

**Tests:**

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
python3 scripts/deps.py verify
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest tests/deps/test_resolver.py tests/package/test_package.py -q
```

## Dependencies and handoff order

```text
U1 ─┬─ U5 ─┬─ U6 ─┬─ U7
    │      │      └─ U8 ─┐
U2 ─┴─ U3 ─┴─ U4 ────────┴─ U9
```

U1 and U2 may run in parallel. U3 follows U2; U4 follows U2/U3; U5 follows U1/U2/U4. U6 follows
U1/U3/U5. U7 follows U1/U6 and U8 follows U4/U5/U6. U9 is the only integration/rework unit.

## Per-implementer commit policy

Each implementer creates commits only for their owned files after focused checks pass. Keep an
implementation file and its direct tests in the same commit; do not stage generated CEF/fonts/icon
output, `browser.pen`, tool state, or another implementer's files. Use the repository's plain
imperative English style. A unit may use multiple atomic commits when its owned changes are
independently reversible; each commit includes the required project attribution footer and co-author
trailer. U9 may commit only the minimal regression fix and its direct test after identifying the
responsible unit. No implementer pushes or rewrites history as part of this plan.
