# Changelog

All notable changes to Island are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Island uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html): while the version is
`0.x`, a minor bump marks a new feature set and may change behavior.

The version lives in the `VERSION` file. Bump it with
`python3 scripts/version.py bump minor` (or `major` / `patch`), which moves the
Unreleased notes under the new version and refreshes the site. Merging a
version bump to `main` tags the commit `vX.Y.Z` automatically. Versions before
0.4.0 were never tagged; their entries are reconstructed from the git history.

## [Unreleased]

### Added

- Automatic builds: every push to `main` that changes the app builds, tests, and packages all
  six targets and publishes them as unsigned GitHub prereleases — a rolling `nightly`, plus a
  `vX.Y.Z` prerelease with the changelog notes the first time a version builds. Each release
  carries the six archives and one `SHA256SUMS.txt`.
- `scripts/version.py notes` prints one version's changelog section.
- In-browser updates (Settings > Updates): checks the GitHub releases of `island-browser/island`
  shortly after startup (at most once a day, `ISLAND_DISABLE_UPDATES=1` to turn off) or on demand,
  downloads the platform archive, verifies it against `SHA256SUMS.txt`, and installs it on
  "Restart to update" with a backup and automatic rollback. SemVer pre-releases are opt-in; build
  trees and read-only installs only report new versions.

### Changed

- New "Graphite" design language across the browser: neutral near-black and white surfaces,
  1px hairlines, a single blue accent, tighter radii (6/10), and a refined space palette; the
  space tint is now a faint cast on the sidebar instead of a colored wash.

## [0.4.0] - 2026-10-06

### Added

- Built-in AI agent: an Agent Client Protocol (ACP) agent runs in a sidebar panel
  (`Cmd/Ctrl+J`) with streaming replies, permission prompts, cancel, and new chat.
- Browser tools for any agent over MCP at `http://127.0.0.1:9223/mcp`: tabs, spaces,
  navigation, page text, screenshots, JavaScript evaluation, console, and pinning.
  Bearer-token auth, loopback-only, plus the `island_mcp_bridge` stdio bridge.
- All tabs (`Cmd/Ctrl+Shift+A`): every space's tabs as searchable cards, with keyboard
  navigation, pin/close, and drag to reorder or move between spaces.
- Settings (`Cmd/Ctrl+,`): theme, agent command, a copyable MCP client config,
  keyboard shortcuts, import, and About.
- Configurable keyboard shortcuts: record new keys, remove or reset them; conflicts are
  flagged and the macOS menu follows the keymap.
- Import from Firefox (bookmark backups) and Arc (spaces with their colors and pinned
  tabs), next to Chrome-family browsers and Safari.
- Versioning: one `VERSION` file feeds the build, the app's About page, the agent
  protocols, the macOS bundle, and the site; `scripts/version.py` bumps and checks it.
- The site gains a changelog page and redeploys on every push to `main` that touches it.

### Changed

- Arc-style chrome: the sidebar is tinted with the active space's color (with WCAG
  contrast guarantees), pinned tabs sit in a tray, and the page floats on a card.
- GitHub Actions run on manual dispatch only, except the site deploy and version tag.

### Fixed

- A web page could crash the browser while an agent read it, by returning deeply nested
  JSON to the page tools; JSON nesting is now capped.
- The agent process no longer inherits the MCP server's sockets or other descriptors.
- The MCP endpoint caps concurrent connections, the stdio bridge stops reading at the
  response's `Content-Length`, and the discovery file is created owner-only and is
  removed on quit only if it still belongs to this instance.
- Permission prompts close when an agent turn fails; oversized function-key names such
  as `F4294967301` are rejected instead of wrapping to a real key.
- Split view was bound to `Shift+S` without `Cmd/Ctrl`.
- Next/previous tab matched ASCII brackets instead of virtual key codes.

## [0.3.0] - 2026-09-22

### Added

- Tabs and colored spaces with a tab strip, space switcher, rename (`F2`), and reorder.
- Split view with a keyboard-adjustable divider.
- Command palette (`Cmd/Ctrl+K`) and web search palette (`Cmd/Ctrl+Shift+K`).
- Session restore after a clean quit, including split pairs.
- First-run welcome with an appearance choice and bookmark import from Chrome-family
  browsers and Safari.
- Hideable sidebar (`Cmd/Ctrl+B`) with hover-reveal on macOS.
- Search S0/S1 kernel behind `ISLAND_ENABLE_SEARCH`: MemTable, on-disk segments, block
  cache, Ingest/Query/Flush facade, and a memory benchmark.
- Hybrid native/container Linux build lane.

## [0.2.0] - 2026-08-12

### Added

- Native chrome restyle in the Ledger design system with a floating-canvas layout.
- Per-space `CefRequestContext` and per-tab browser ownership seams.
- Session store with schema validation, design-token drift guard, and Phase 3
  acceptance checklists.

## [0.1.0] - 2026-08-10

### Added

- One native CEF window with a `CefBrowserView`, a local start page, Back / Forward /
  Reload, popup rejection, and a clean shutdown through the CEF close lifecycle.
- Pinned dependency vendoring (`scripts/setup_deps.sh`), packaging, and the product site.

[Unreleased]: https://github.com/island-browser/island/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/island-browser/island/releases/tag/v0.4.0
[0.3.0]: https://github.com/island-browser/island/commits/main
[0.2.0]: https://github.com/island-browser/island/commits/main
[0.1.0]: https://github.com/island-browser/island/commits/main
