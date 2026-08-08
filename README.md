# Island Browser

Island is a Phase 1 C++20 desktop browser skeleton built directly on Chromium Embedded Framework
(CEF) and native `cef_views`. The implemented app opens one native top-level `CefWindow` containing
one `CefBrowserView` with a deterministic local data page. Browser chrome, tabs, spaces,
persistence, command bar, settings, extensions, and expanded CDP features are not implemented yet.

## Status

- Intended desktop targets: `macosx64`, `macosarm64`, `windows64`, `windowsarm64`, `linux64`,
  `linuxarm64`.
- Local evidence: macOS arm64 clean build passed; dependency verify passed; CTest passed 18/18; the
  runtime showed one window/page/title/marker, blocked popups, handled history back/forward, and
  cleaned up after native close/menu Quit; package tests passed 11/11.
- Needed evidence: macOS x64 plus Windows/Linux targets require native GitHub Actions build,
  test, and package evidence before they are treated as verified.
- Windows build matrix covers both sandbox off and sandbox on.
- Public stable release is blocked until signing and notarization verification exist.
- Static Pages/site visual QA is pending because Chrome was unavailable during review.

## Dependencies

CEF 150 and Geist are pinned in `deps/dependencies.lock.json` and installed outside git:

- `third_party/cef/` for the CEF binary distribution.
- `assets/fonts/` for Geist and Geist Mono files.

The first CMake configure also needs network access for GoogleTest v1.15.2 through CMake
`FetchContent`.

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
./scripts/setup_deps.sh --force
python3 scripts/deps.py verify
python3 scripts/deps.py check-updates
```

Setup syntax is `./scripts/setup_deps.sh [--dry-run] [--force] [--target <target>]`. Use
`--target <target>` with `setup_deps.sh` or `scripts/deps.py` when resolving a non-host target.

## Build, test, and run

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app
```

On macOS the app bundle is `build/src/main/island_browser.app`. It should show one Island window with
one local data page. Smoke mode uses a deterministic marker page:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

Startup should remain data-only with no external requests. Popups should be blocked, history
back/forward should work on the smoke history entry, and close/menu Quit should leave no
`island_browser` helper processes after quitting. Cmd+R has a decisive accepted signal; physical
Cmd+Q remains a manual caveat.

```bash
pgrep -fl island_browser || true
```

## Package an unsigned candidate

```bash
python3 scripts/package.py --target macosarm64 --build-dir build --version 0.1.0 --output-dir dist
```

Packaging writes `island_browser-<version>-<target>.zip` for macOS/Windows targets,
`island_browser-<version>-<target>.tar.gz` for Linux targets, and `SHA256SUMS.txt`. Each package
contains `build-metadata.json` and `THIRD_PARTY_NOTICES.txt`; current metadata is unsigned:
`signed=false`, `notarized=false`, and `publicReleaseEligible=false`.

## More docs

- `docs/dependency-update-process.md`
- `docs/supported-platforms.md`
- `docs/release-process.md`
- Current Phase 1 design/plan: `docs/superpowers/specs/2026-08-08-island-browser-phase1-design.md`
  and `docs/superpowers/plans/2026-08-08-island-browser-phase1.md`
- Historical Phase 0 design/plan: `docs/superpowers/`
