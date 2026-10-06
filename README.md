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

## Features at a glance

- Arc-style sidebar tinted by each space's color, pinned tabs, compact address field, floating
  page card, command palette (`Cmd/Ctrl+K`) and search palette (`Cmd/Ctrl+Shift+K`).
- **All tabs** (`Cmd/Ctrl+Shift+A`): every space's tabs as searchable cards; arrow keys, Enter,
  Delete, `P` to pin, drag a card onto another space to move it.
- **Settings** (`Cmd/Ctrl+,`): theme, agent command, MCP client config, keyboard shortcuts (click a
  shortcut and press new keys; conflicts are flagged), and import.
- **Import** from Chrome-family browsers, Safari and Firefox (bookmarks), and Arc (spaces with their
  colors and pinned tabs). Nothing leaves the computer.
- **AI agents**: an Agent Client Protocol agent runs in the sidebar (`Cmd/Ctrl+J`), and Island
  serves browser tools over MCP at `http://127.0.0.1:9223/mcp` (bearer token in
  `agent-endpoint.json`, or the `island_mcp_bridge` stdio bridge). Settings shows a ready-to-paste
  `mcpServers` config.
- **Updates** (Settings > Updates): Island checks its GitHub releases shortly after startup (at
  most once a day) or on demand, downloads the archive for your platform, verifies it against the
  release's `SHA256SUMS.txt`, and swaps it in on "Restart to update", keeping the previous version
  as a backup until the new one starts. Pre-releases are opt-in. Builds run from a CMake build
  directory never update themselves; `ISLAND_DISABLE_UPDATES=1` turns updates off.

## Versions

The version lives in `VERSION` (SemVer) and feeds the build, the app's About page, the agent
protocols, the macOS bundle, and the site. Release notes go under `## [Unreleased]` in
`CHANGELOG.md`; cut a release with:

```bash
python3 scripts/version.py bump minor   # or major / patch; --pre beta for a pre-release
python3 scripts/version.py check
```

When the bump lands on `main`, the commit is tagged `vX.Y.Z` automatically and the site (with its
changelog page) redeploys to GitHub Pages.

## Downloads / releases

Every push to `main` that changes the app builds all six targets and publishes them on
[GitHub Releases](https://github.com/island-browser/island/releases) (`build-release.yml`):

- `nightly` — a rolling prerelease that always holds the latest successful build of `main`.
- `vX.Y.Z` — one prerelease per version, titled `Island X.Y.Z (unsigned)`, with that version's
  `CHANGELOG.md` notes.

Each release carries `island_browser-<version>-<target>.zip` (macOS, Windows) or `.tar.gz` (Linux)
and a `SHA256SUMS.txt` to verify them against. **All builds are unsigned prereleases**: they are not
code-signed or notarized, so macOS Gatekeeper refuses to open the app until you allow it in System
Settings > Privacy & Security (or run `xattr -dr com.apple.quarantine island_browser.app`), and
Windows SmartScreen may warn. While the repository is private, its releases (like its Pages site)
are visible only to people with access to the repository, and downloading assets needs an
authenticated GitHub request.

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
