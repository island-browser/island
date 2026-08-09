# Supported platforms

Island targets six desktop dependency/build targets:

| Target | Platform | Architecture | Evidence status |
| --- | --- | --- | --- |
| `macosarm64` | macOS | Apple Silicon arm64 | Locally built, tested 55/55, run, smoke-verified, and packaged. Also green on native CI. |
| `macosx64` | macOS | Intel x64 | Native CI: configure/build/CTest (55/55) green (`Island CI` run 31336257036, commit `aca36ce`). Not yet locally run/smoke-verified/packaged. |
| `windows64` | Windows | x64 | Native CI: configure/build/CTest (55/55) green (`Island CI` run 31336257036, commit `aca36ce`). Not yet locally run/smoke-verified/packaged. |
| `windowsarm64` | Windows | ARM64 | Native CI: configure/build/CTest (55/55) green (`Island CI` run 31336257036, commit `aca36ce`). Not yet locally run/smoke-verified/packaged. |
| `linux64` | Linux | x64 | Native CI: configure/build/CTest (55/55) green under Xvfb (`Island CI` run 31336257036, commit `aca36ce`). Not yet locally run/smoke-verified/packaged. |
| `linuxarm64` | Linux | ARM64 | Native CI: configure/build/CTest (55/55) green under Xvfb (`Island CI` run 31336257036, commit `aca36ce`). Not yet locally run/smoke-verified/packaged. |

Packaging evidence (`package.yml`, unsigned candidates) is tracked separately and was not yet
re-verified against `aca36ce` as of this writing; do not infer packaging success from the CI evidence
above. Re-check `package.yml`'s latest run before citing packaging status for a target.

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

`.github/workflows/ci.yml` defines native jobs for all six targets and restores/verifies cached CEF
and Geist dependencies before configure, build, and CTest. Linux jobs run tests under `xvfb-run`
(`CefInitialize` needs an X11 display even for tests that never show a window). As of commit
`aca36ce`, all six targets pass configure/build/CTest on native GitHub Actions runners
(`Island CI` run 31336257036) — treat this as build/test evidence only; local run, smoke-test, and
packaging evidence for non-macOS-arm64 targets is still outstanding and should not be assumed from
this alone.
