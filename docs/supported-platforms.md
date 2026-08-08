# Supported platforms

Phase 1 is intended to cover six desktop dependency/build targets:

| Target | Platform | Architecture | Evidence status |
| --- | --- | --- | --- |
| `macosarm64` | macOS | Apple Silicon arm64 | Locally built, tested 18/18, run, smoke-verified, and packaged. |
| `macosx64` | macOS | Intel x64 | Requires native GitHub Actions evidence. |
| `windows64` | Windows | x64 | Requires native GitHub Actions evidence. |
| `windowsarm64` | Windows | ARM64 | Requires native GitHub Actions evidence. |
| `linux64` | Linux | x64 | Requires native GitHub Actions evidence. |
| `linuxarm64` | Linux | ARM64 | Requires native GitHub Actions evidence. |

Phase 1 application behavior is intentionally small: one native CEF window, one `CefBrowserView`, a
fixed local data startup page, back/forward/reload commands, popup rejection, navigation snapshots,
and remote debugging port `9222` configured. Browser chrome, tabs, spaces, persistence, command bar,
settings, extensions, or additional CDP integration should not be documented as available.

## macOS local verification

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app
```

Smoke mode:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

The smoke page should show the `ISLAND_PHASE1_SMOKE_OK` marker, make no external startup request,
block popups, and allow history back/forward on its local history link. After quitting the app,
verify there are no remaining `island_browser` helper processes. The macOS app bundle is
`build/src/main/island_browser.app`.

```bash
pgrep -fl island_browser || true
```

## Cross-platform verification

The build workflow defines native jobs for all six targets and restores/verifies cached CEF and Geist
dependencies before configure, build, and CTest. Windows jobs cover sandbox off and sandbox on. Treat
macOS x64, Windows, and Linux support as pending until their native GitHub Actions jobs provide
passing evidence for the relevant change.
