# Changelog

All notable changes to Island are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Island uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html): while the version is
`0.x`, a minor bump marks a new feature set and may change behavior.

The version lives in the `VERSION` file. Bump it with
`python3 scripts/version.py bump minor` (or `major` / `patch`), which moves the
Unreleased notes under the new version. Merging a
version bump to `main` tags the commit `vX.Y.Z` automatically. Versions before
0.4.0 were never tagged; their entries are reconstructed from the git history.

## [Unreleased]

### Added
- Start page **Research** mode: Enter sends the question to the configured agent with a prompt to research it with Island's browser tools (a new space named after the question, several sources in tabs, compared) and to finish with a Markdown report that cites every claim. The final report opens in a new **Report** tab (local page, shared safe Markdown renderer, links open in new tabs) with Copy Markdown, Save as .md... and Continue in agent panel. Clear messages for a missing agent (with an Open Settings button), a busy agent, and Cancel.
- Reports list: persisted in `reports.json` under the support directory, listed in Settings > Reports and in Cmd/Ctrl+K as REPORT rows.
- Dev capture actions `research:<question>` and `open_report`.

- Cmd/Ctrl+B now collapses the sidebar to a 52 DIP icon rail instead of hiding it: one square
  button per tab of the active space (pinned tabs first, the active tab highlighted like the
  selected row, the page title as tooltip and accessible name), a "+" new-tab button and the
  spaces as colored dots at the bottom, and a scrolling list when the tabs outgrow the window.
  The choice is saved and restored on the next launch. Cmd/Ctrl+L from the icon rail expands the
  sidebar for the length of the edit. The macOS hover reveal and the edge sliver are gone, since
  the rail is always there. Favicons are fetched at 16 DIP only for web pages, ignored when
  stale, and cleared when a tab leaves the site.
- More keyboard shortcuts, all listed and rebindable in Settings: Ctrl+Tab and Ctrl+Shift+Tab
  switch tabs (Control is now its own modifier on macOS), Cmd+9 selects the last tab, Cmd+Shift+T
  reopens the last closed tab, Cmd+=, Cmd+-, and Cmd+0 zoom the page, Cmd+Shift+R reloads ignoring
  the cache, Cmd+. and Escape stop loading (Escape only when no overlay is open), Cmd+Option+Down
  and Cmd+Option+Up move between spaces, and Cmd+Shift+C copies the tab's URL (macOS only for now).
- Island has its own start page, shown on launch and in every new tab: one prominent input with a
  Search / Ask agent switch. Search opens the results on your search engine in that tab; Ask agent
  opens the agent panel and sends the text as a task, exactly as if typed there. The last-used
  mode is remembered. The page is a local document and requests nothing until you submit.
- The address bar searches when the text is not an address: `google`, `weather in oslo` or
  `what is 2+2` open a Google results page instead of showing "Enter an absolute http or https
  address." Bare hosts (`example.com`) open over https. Explicit schemes are
  validated exactly as before. localhost and IP addresses open over http (dev servers rarely speak
  TLS); domain names open over https.
- Settings has a Search section to choose the engine: Google (default), DuckDuckGo or Yandex.
- Agent replies in the agent panel render as Markdown: headings, bold/italic, inline code, fenced
  code blocks, lists, links, blockquotes and tables. The renderer escapes everything the model
  sends, only links to http, https and mailto, and falls back to plain text.

### Fixed

- Clicking a tab or space button no longer destroys the pressed button from inside its own
  callback (a use-after-free in CEF event dispatch that could crash the browser); the action now
  runs right after the callback returns.
- Island no longer crashes with "quit unexpectedly" when you switch to another app or window (and on
  quit). The window and every browser view now use CEF's Alloy runtime style; Island hosts several
  browser views in one window, which CEF supports only for Alloy style views.
- The sidebar no longer shows every control as its own light box: CEF wipes custom view colors
  when a theme change lands, and only some chrome views repainted afterwards. Every chrome view now
  reports the reset and the chrome repaints from the design tokens, which also removes the white
  panel behind the tab list.

### Changed

- Downloads are saved straight into your Downloads folder under the suggested name, without a
  dialog; an existing file is never overwritten (`name (1).ext`, `name (2).ext`, ...). Alloy
  style cancels downloads nobody handles, so Island now handles them itself.
- Site permission requests (camera, microphone, location, notifications) are declined, because
  Alloy style has no built-in prompt and Island does not show one yet.
- Sidebar redesigned to the Graphite spec: ghost back/forward and footer buttons, the address as
  one outlined pill (site glyph, field, reload) that takes the accent outline while focused, tabs
  as rows on the sidebar with a hairline-outlined selected row, one 32 DIP control height and an
  even 8 DIP rhythm, Geist instead of mono in the address field.
- The first-run welcome card is a real card: title and lede, a segmented System/Light/Dark control,
  outlined import rows, a wrapped note for unreadable browsers instead of an ellipsized line, and
  Import / Start browsing buttons.
- The start page is styled with the design tokens and the Geist stack (still a local `data:`
  document with no external requests).

## [0.5.1] - 2026-10-06

### Added

- Installers next to every release archive: a macOS `.dmg` to drag Island onto Applications, a
  Debian/Ubuntu `.deb` (installs to `/opt/island-browser`, adds the `island-browser` command and
  a menu entry), and a Windows `-setup.exe` that installs per user without administrator rights,
  with a Start menu entry and an uninstaller.

### Fixed

- macOS: a downloaded Island no longer opens as "damaged and can't be opened". The app bundle is
  now ad-hoc signed, so Gatekeeper offers "Open Anyway" in Privacy & Security instead.

## [0.5.0] - 2026-10-06

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
- Agent providers: choose the sidebar agent from the panel's title or Settings > Agent & tools —
  Claude Code, Codex, OpenCode, Gemini CLI, Qwen Code, Goose, or a custom command. Each shows
  whether its program was found on the `PATH`, with an install hint and docs link when it was not;
  switching stops a running agent and starts a new chat with the new one. `ISLAND_AGENT_COMMAND`
  still overrides the choice.

### Changed

- The product site moved to its own repository,
  [island-browser/site](https://github.com/island-browser/site), and is served at
  https://island-browser.github.io/site/. It reads the version and these notes from this
  repository when it builds; `scripts/version.py sync` no longer touches site pages.
- The default Claude Code agent now runs `npx -y @agentclientprotocol/claude-agent-acp`, the
  renamed successor of the deprecated `@zed-industries/claude-code-acp`. A saved command equal to
  the old default switches to the Claude Code provider; any other saved command becomes the
  custom provider.
- New "Graphite" design language across the browser: neutral near-black and white surfaces,
  1px hairlines, a single blue accent, tighter radii (6/10), and a refined space palette; the
  space tint is now a faint cast on the sidebar instead of a colored wash.

### Fixed

- An agent command with unbalanced quotes is refused with "check its quotes" instead of being
  handed to the login shell.
- Native builds on every target again: a GCC `-Werror` warning in the command palette, an MSVC
  parse error in a JSON test, the Windows arm64 runner's Visual Studio generator, and a macOS-only
  false positive in the agent descriptor-leak test.
- Windows builds: the agent tests compile with MSVC's conforming preprocessor and UTF-8 literals,
  and the macOS/Linux update script always writes `/`-separated paths, also when generated on a
  Windows host.
- Windows packaging recognizes the sandbox build again: CEF's bootstrap `island_browser.exe`
  does not name `island_browser.dll`, so the package step now checks that the DLL exports
  `RunWinMain` instead.
- Windows arm64 packaging no longer requires `dxcompiler.dll` and `dxil.dll`, which CEF ships
  only for x64.

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

[Unreleased]: https://github.com/island-browser/island/compare/v0.5.1...HEAD
[0.5.1]: https://github.com/island-browser/island/releases/tag/v0.5.1
[0.5.0]: https://github.com/island-browser/island/releases/tag/v0.5.0
[0.4.0]: https://github.com/island-browser/island/releases/tag/v0.4.0
[0.3.0]: https://github.com/island-browser/island/commits/main
[0.2.0]: https://github.com/island-browser/island/commits/main
[0.1.0]: https://github.com/island-browser/island/commits/main
