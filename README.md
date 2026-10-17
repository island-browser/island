# Island

Island is a desktop web browser with a built-in AI agent. It is a C++20 application built directly
on the Chromium Embedded Framework (CEF) and CEF's native `cef_views` toolkit. It is not Electron
and not a web-based shell: the sidebar, tabs, palettes and overlays are native views.

> **Status:** Island is pre-1.0. Every build is an **unsigned GitHub prerelease**. Only macOS
> arm64 has local build, run and package evidence. See [Status and limits](#status-and-limits).

## What you get

**Looks and sidebar**
- Three looks, switchable live in Settings > Appearance: **Native** (the default: a macOS-style
  source list, system font, one blue accent), **Arc** (space-tinted canvas and a rounded page
  card) and **Graphite**.
- A sidebar on the left or right with pinned tabs, tabs and a space switcher. `Cmd/Ctrl+B`
  collapses it to a 52 DIP icon rail of tab favicons.
- Light, dark or system theme; density, corner radius, font and shadow are adjustable.

**Tabs, spaces and split view**
- Spaces group tabs (work, travel, research). Rename, reorder and switch them from the sidebar,
  menu or keyboard.
- Pin tabs, reopen closed tabs, and see every space's tabs as searchable cards in **All tabs**
  (`Cmd/Ctrl+Shift+A`).
- Split view shows two tabs side by side (`Cmd/Ctrl+Shift+S`) with a keyboard-adjustable divider.

**Search and palettes**
- The command palette (`Cmd/Ctrl+K`) finds open tabs, spaces, bookmarks, on-device history,
  research reports and commands. The search palette (`Cmd/Ctrl+Shift+K`) sends a query to Google,
  ChatGPT, Perplexity, Claude or Gemini.
- The start page has three modes: Search, Ask Agent and Research, plus six speed-dial tiles.

**Agent and research**
- An [Agent Client Protocol](https://agentclientprotocol.com) agent runs in the sidebar panel
  (`Cmd/Ctrl+J`): Claude Code (default), Codex, OpenCode, Gemini CLI, Qwen Code, Goose or your own
  command.
- **Research** mode asks the agent for a branching plan first. A pinned **Plan** tab shows it as a
  graph; nothing runs until you approve. Pages the agent opens become grouped child tabs, and the
  answer lands in a **Report** tab.
- Island serves browser tools to external agents over **MCP** at `http://127.0.0.1:<port>/mcp`
  (bearer token). Settings > Agent & tools has a ready-to-paste config.

**Privacy**
- Native ad and tracker blocking on Brave's Rust `adblock` engine with EasyList and EasyPrivacy,
  with separate controls and a per-site allowlist.
- On-device history search. History never leaves your computer and can be turned off or cleared.

**Reading and pages**
- Reader view (`Cmd/Ctrl+Alt+R`) and find in page (`Cmd/Ctrl+F`).
- **Tab sleep** frees memory from inactive tabs. **Peek** shows a link in a floating card
  (`Cmd/Ctrl+click`).

**Settings, shortcuts and updates**
- Settings (`Cmd/Ctrl+,`) has General, Appearance, Privacy & blocking, Tabs, Reader, Agent &
  tools, Shortcuts and About sections, with search.
- Every shortcut is rebindable. `Cmd/Ctrl+/` shows a cheatsheet of the ten most used.
- Import bookmarks from Chrome-family browsers, Safari and Firefox, and spaces with pinned tabs
  from Arc. Nothing leaves the computer.
- The built-in updater checks GitHub releases, verifies the SHA-256 checksum and swaps the app in
  on "Restart to update".

## Install

Download a build from [GitHub Releases](https://github.com/island-browser/island/releases):

- `nightly` is a rolling prerelease of the latest `main`.
- `vX.Y.Z` is one prerelease per version, titled `Island X.Y.Z (unsigned)`.

Each release has a portable archive per target (`island_browser-<version>-<target>.zip` for macOS
and Windows, `.tar.gz` for Linux), installers (`.dmg`, `.deb`, `-setup.exe`) and `SHA256SUMS.txt`.
Targets are `macosarm64`, `macosx64`, `windows64`, `windowsarm64`, `linux64` and `linuxarm64`.

Builds are not code-signed or notarized, so macOS Gatekeeper refuses to open them at first. Either
allow the app in System Settings > Privacy & Security, or remove the quarantine flag:

```bash
xattr -dr com.apple.quarantine /Applications/Island.app
```

Windows SmartScreen may also warn. While this repository is private, its releases are visible only
to people with access to it. The [user guide](docs/user-guide/getting-started/install.md) has the
step-by-step version.

## Build from source

Requirements: CMake, a C++20 compiler, Python 3, network access for the first configure
(GoogleTest), and a stable Rust toolchain for the ad blocker (without `cargo`, a no-op blocker is
built).

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

`scripts/setup_deps.sh` vendors the pinned CEF distribution into `third_party/cef/` and the Geist
fonts into `assets/fonts/`; both stay out of git. Smoke run on macOS:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
pgrep -fl island_browser || true   # after quitting: no helper processes should remain
```

The AI-agent kernel and the search kernel also build standalone, without CEF:

```bash
cmake -B build-agent -S src/agent && cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
python3 -m pytest tests/deps tests/package tests/design tests/version
```

Build-time detail: [AGENTS.md](AGENTS.md), [docs/release-process.md](docs/release-process.md).

## Project layout

| Path | Contents |
|------|----------|
| `src/main/` | The browser: CEF app and window, native chrome, tabs, spaces, local pages (`pages/`) |
| `src/agent/` | CEF-free agent kernel: MCP tools endpoint and ACP client |
| `src/search/` | CEF-free search kernel behind on-device history search |
| `src/content_blocker_ffi/` | Rust crate wrapping Brave's `adblock` engine |
| `tests/`, `bench/` | GoogleTest and pytest suites; benchmark support |
| `scripts/`, `deps/`, `cmake/` | Dependency vendoring, packaging, versioning, platform CMake |
| `docs/` | Process docs, specs and plans, and the [user guide](docs/user-guide/README.md) |
| `DESIGN.md` | The product and visual design narrative |

## Status and limits

- **Unsigned prereleases only.** Stable public release is blocked until signing and notarization
  verification exist.
- **Platform evidence.** Only macOS arm64 is built, tested and run locally. The other five targets
  need native GitHub Actions evidence before they count as supported; see
  [docs/supported-platforms.md](docs/supported-platforms.md).
- **Visual acceptance** (Phase 3 U10) still needs a human with display access.
- **Not in scope:** browser extensions (they need CEF's Chrome runtime; Island uses Alloy), sync,
  profiles and accounts, and more than one top-level window.
- **macOS deviations:** the split divider moves through menu items and shortcuts, not by dragging.

## Documentation

- [User guide](docs/user-guide/README.md): task-first help, written to be published on GitBook.
- [CHANGELOG.md](CHANGELOG.md): every release and the unreleased notes.
- [DESIGN.md](DESIGN.md): why Island looks and behaves the way it does.
- [docs/supported-platforms.md](docs/supported-platforms.md) and
  [docs/release-process.md](docs/release-process.md).

## Contributing

- Read [AGENTS.md](AGENTS.md) first. Only units of an accepted spec and plan are implemented.
- Work on a branch and open a pull request. Build and test locally before committing; GitHub
  Actions triggers are mostly paused.
- Add your change to `## [Unreleased]` in `CHANGELOG.md`. Do not edit `VERSION` by hand: the
  project owner bumps it with `python3 scripts/version.py bump <part>` and merges the PR. Run
  `python3 scripts/version.py check` before committing version changes.
- Style: Google-based, 4-space indent, 100 columns, explicit `std::`, target-scoped CMake.

Licensed under the terms in [LICENSE](LICENSE).
