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

## [0.7.3] - 2026-10-23

### Fixed

- The Windows builds compile again after the expanded icon set exceeded the platform's command-line
  limit during post-build resource staging.

## [0.7.2] - 2026-10-19

### Added

- Spaces can use one of 24 bundled Lucide icons, chosen from Browser > Icon on macOS or from the
  command palette ("Change space icon…") on every platform. The choice persists in the session
  (saved at once) and appears in the Native, Arc and Graphite switchers and the collapsed rail,
  where the active space keeps a selected plate. A non-string `icon` in a session is ignored.

### Changed

- Command palette rows on Native: the selected row is the neutral raised plate with normal text,
  not blue. Blue stays for the active tab and focus rings.
- Native's Agent entry shows an open panel as the neutral raised plate with normal text, not a blue
  label.
- Native space segments with an icon show only that centered icon in equal-width segments (the
  name stays in the tooltip, the accessible name and the sidebar header). Spaces without an icon
  keep their name when it fits, else their initial, never a lone ellipsis.
- Polished the Native sidebar footer into one aligned, hairline-separated control group with a
  raised space track, same-height ghost actions, and a quieter tinted Agent row.
- Native's Back and Forward buttons share one continuous toolbar surface, removing the thin
  rail-colored seams beside the ghost-button group.
- Selected segments use a neutral raised plate with normal text in every look; blue is reserved
  for actions and keyboard focus.
- Native's Agent entry shows the provider name with a status dot (idle or working) and the
  shortcut keycap, on the sidebar's bottom row.
- Streaming agent status updates use the session's permission count instead of repeatedly scanning
  the full transcript.
- macOS rounded-corner patches stop their quick polling after the chrome settles, wake on layout,
  pointer, tab, space and sidebar changes, and follow scrolling within about a second.
- Downgrade note: 0.7.1 ignores a space's `icon` and rewrites the session without it, so going
  0.7.2 → 0.7.1 → 0.7.2 drops every chosen space icon.

### Fixed

- Native's selected tab row draws its close × and its generic globe in white (the × at 70%).
- Arc and Graphite space chips show their full names when every chip fits the sidebar. A chip that
  does not fit shows its icon alone, never an icon beside a lone initial.
- The Native address field's focus ring is one even 2 DIP accent ring that follows the 8 pt radius,
  with no step at the globe and reload ends.
- With Island's Light or Dark theme, web pages get the same prefers-color-scheme (and scrollbars);
  System follows the OS.
- Plan child rows show the page title, or the host before the page loads, not the raw address. The
  Plan's instruction field spans the inspector at 30 pt with a 6 pt radius.
- In ordinary chat, agent tools act on the window the person sees: calls without a tab index use the
  active tab, and indices refer to its space. While a research runs, they act on the agent's own
  research page in the Plan's space; a plan waiting for approval is not a running research. When
  that page is missing or closed, the tool reports it instead of using the page on screen.
- Choosing "Change space icon…" from the command palette with the keyboard opens the icon picker.
  Return re-applied the unchanged query, which reset the arrowed row, so it submitted the first row
  instead.
- Focusing or blurring the Native address field repaints only its focus colors, not the whole
  window.
- Selected segments on the Settings, Start and Plan pages have an outline in dark theme, so the
  selected option is distinguishable.
- The Native footer's Agent name fills the room between "Agent" and its status dot, measured from
  the laid-out row, so "Claude Code" shows in full and ellipsizes only when it does not fit.
- Arc and Graphite space chips keep their natural width; the equal-width track and centered labels
  apply to Native only.
- Opening the agent panel no longer hides the page and the toolbar.
- The Native address field shows one clear accent focus ring in either theme instead of a second
  inner ring.
- Focusing the address field on an Island page no longer exposes its embedded data URL; the start
  page stays empty and other built-in pages use short `island://` addresses.
- Settings uses each look's selected-segment color instead of forcing Native's dark gray.
- Agent-opened research pages stay grouped in the Plan's space without interrupting another space.
- Moving a regular tab into a research group places it after the group without reporting a
  phantom move.
- Closing a split-view window detaches both page panes and the divider cleanly.
- Windows prefers Segoe UI Variable for Native chrome and falls back to Segoe UI.
- Settings and other internal pages respect the same young-browser swap guard as ordinary tabs.
- The research prompt asks the agent not to purchase, post or delete on a site or type passwords or
  payment details, which the Plan's limits and the README state; a step that needs them asks first.

### Known limitations

- Research reads each source while its space is shown. If you switch to another space, the
  research's pages that have not been shown yet have no browser: the agent reports those sources
  as unread instead of reading them, and does not activate them. Background reading is not in 0.7.2.

## [0.7.1] - 2026-10-17

### Fixed

- The Linux builds compile again: native window handles are converted the same way on every
  platform (0.7.0 failed to build for linux64 and linuxarm64).

## [0.7.0] - 2026-10-17

### Changed

- The start page is restyled to the Native mockup: a segmented Search · Ask Agent · Research
  control, a 44 px search field with an accent focus halo, and a row of six speed-dial tiles for
  the most visited sites (from history, one per host, opened in the current tab). Tab and
  Shift+Tab step through the modes while the field has focus. Clearing history clears the tiles.
  Visits are grouped by host before the six-tile limit applies, so one busy site with many pages
  no longer hides the others. IP-address sites get a neutral globe instead of a digit, and the
  old keyboard-shortcut list under the start page is removed.
- Settings is restyled to the Native mockups: a section list (General, Appearance, Privacy &
  blocking, Tabs, Reader, Agent & tools, Shortcuts, About) with search over settings and the
  version at the bottom, and one section shown at a time. Appearance groups its controls into
  Theme, Color, Layout and Text cards; Shortcuts lists its groups in two columns with keycaps,
  an amber conflict banner and Reset all. Native hides Space colors and Sidebar tint, because
  Native has no space color. Every existing setting keeps its behavior. Search finds sections by
  name (Privacy, Shortcuts, About) and shows the whole pane for them, and also finds the Import
  sources and the About list. A section that is already selected opens again when it is requested
  again (for example, the privacy shield after a click on another section), and an explicit
  request clears the search first. The search fields use the interface font, and the selected
  section in the list is filled with a semibold label instead of the accent.
- The window does not re-lay out the page in a loop while idle, and the Native sidebar and
  toolbar stay visible when a page swap is deferred, for example when pages open in the
  background.

### Added

- Native is the default look for fresh profiles: the system font (SF Pro on macOS), one
  system-blue accent for selection and focus, a source-list sidebar and no space color. Released
  profiles without Appearance settings use Native; Arc-era profiles with Appearance settings but
  no saved look keep Arc. Settings > Appearance starts with a Look preset menu (Native, Arc,
  Graphite), and changing any other control there shows "Custom".
- The Native sidebar bottom is redesigned: one full-width Agent button (sparkle, 32 DIP)
  sits at the very bottom of the sidebar and is tinted while the agent panel is open; the toolbar
  Agent button is gone under Native (the address-field agent dot shows agent state).
- Native spaces are a segmented control: a 24 DIP track with one segment per space (name only,
  no color dot), a "+" button for a new space, and overview and settings icon buttons at its
  right end. With more than four spaces the inactive segments show only their initial.
- Native's "New Tab" row moves below the last tab. Arc and Graphite keep their sidebar.
- Research opens a Plan tab, pinned first in the space, that shows the agent's plan as a graph
  (or a list) of steps, branches, a join and a result. Nothing runs until you approve: Run plan,
  Run one branch only, or Cancel. You can skip and restore steps, write an instruction for a
  step, branch from a step (at most 3 levels deep) or ask the agent to expand one. A research
  turn that proposes a plan and stops now shows "Waiting for approval" instead of failing.
  Plain ACP plans open in List view with stable p0, p1, ... step ids, and each research reuses
  its single Plan tab without closing spaces or adding cleanup to Reopen Closed Tab. Plan graphs
  with a cycle keep valid dependencies from that cycle to downstream steps.
- Island opens the research space itself (the question's name; a space with that name is reused)
  with the Plan hub as its pinned first tab, so the agent no longer creates one. Pages the agent
  opens during research become indented child rows under the Plan tab, each run of a branch under
  its own "Branch A" kicker line, and open active (a freshly created page stays visible until its
  browser is at least 100 ms old; an older page swaps immediately); selecting their plan node
  shows the page. Children of the pinned Plan tab start the Tabs
  section, so Favorites stays whole, and they stay together behind their parent when tabs are
  moved or a favourite is unpinned. The final Result opens as a grouped Report tab and the
  sidebar keeps the grouping. Plan and Report tabs are not restored after a restart; their pages
  come back as ordinary tabs. The agent's navigate, back, forward and reload refuse
  the Plan tab with a clear error, and so do its page tools (read, snapshot, click, type,
  evaluate, screenshot): the plan can only be approved by you.
- Closing the Plan hub asks whether to stop or keep the research running (Escape keeps it
  running); with no live page to ask it stops the research and closes the tab. Deleting the
  space that holds the Plan tab stops the research. Failed plan steps can be retried from the
  inspector while the run goes on or after it ended, never while the plan awaits approval or
  after you cancelled it before approving.
- Pages the research opened but you have not shown yet keep their address in the saved session
  and show it as their name in the sidebar and in the agent's tab list.
- ⌘/ (Ctrl+/ on Windows and Linux, also Help > Keyboard Shortcuts) opens a shortcut cheatsheet:
  a 320 DIP card in the bottom-right corner of the page listing the ten most-used bindings
  (Command palette, Ask agent, Toggle sidebar, Split view, New tab, Close tab, Find in page,
  Reader view, Settings, Cheatsheet) with their current keys. ⌘/ again, Esc, its close button
  or a click outside (macOS only) closes it, and the shortcut is configurable in Settings like
  the others.
- Settings > Keyboard shortcuts records a new shortcut when you click a row: the row shows an
  accent outline and "Press keys…", and Esc cancels. A chord another action already uses is not
  taken silently: an amber banner says "⌘B is already used by Toggle sidebar." with Replace
  (moves the chord and unbinds the other action) and Cancel. Bindings show as keycaps, unbound
  actions offer "Add shortcut", and "Reset all" restores every default.
- The Native address field shows page state as 20 DIP indicators instead of a status bar: a
  shield with the blocked-request count at the left edge (capped at "9999+", hidden at 0; click
  opens Settings > Privacy), Reader at the right edge only on pages that have a reader view, and
  an agent dot (blue while the agent works, orange while it waits for an answer; click opens the
  agent panel). Each has an accessible name and a full-text tooltip, and the indicators follow
  the active tab.
- The Native look now uses a 248 DIP source-list sidebar and a 52 DIP page toolbar with 28 DIP
  navigation buttons, a centered idle address field, a Share action, sentence-case
  section labels, system-blue selected rows with white text, and space switchers without space
  colors. Arc and Graphite keep their existing chrome. The Native address field is a rounded
  (radius 8) fill plate without an outline, and Back/Forward stay clear of the window buttons
  when the sidebar is on the right or collapsed to the icon rail.
- Local pages now receive every Native look color slot, including selection, warning, status,
  and plan-branch colors, as live CSS variables in light and dark themes.
- Native look color tokens: neutral source-list surfaces, system blue selection, accessible
  secondary/warning text pairs, and four plan-branch colors in light and dark themes. Native
  customization ignores space color so its canvas and accent stay fixed.
- Arc-style look. On macOS the window is frameless: no title bar or title text, the traffic lights
  sit over the top-left of the sidebar, and the sidebar's top band (and the canvas strip above the
  page) drags the window. The sidebar and the window margin are one canvas tinted in the active
  space's color (stronger than before), the page sits on a card inset 8 DIP with rounded corners
  and one soft shadow, the selected and hovered tab rows and the address pill are soft rounded
  fills, and favicons sit on a small rounded plate so dark ones stay visible on a dark sidebar, in
  the 52 DIP icon rail as well. CEF views cannot round or shadow anything, so on macOS a native
  overlay (`macos/page_card_mac.mm`) paints the corners and the shadow; other platforms keep
  square corners and the platform frame.
- Settings > Appearance, applied live and saved in the prefs: per-space color (ten presets or a
  custom hex), sidebar tint intensity, sidebar on the left or right, page corner radius
  (0/6/10/14), density (comfortable/compact), interface font (Geist, System, or any installed
  family), the page card's shadow and "full bleed" (no inset, no rounding, no shadow). Theme stays
  System/Light/Dark.
- Research mode asks the agent for a branching plan first: each step is an ACP plan entry with
  `_meta.island` (`id`, `after`, `branch`, `kind`), at most 3 levels of branching, and nothing runs
  until you approve the plan.

### Fixed

- Opening Settings or All tabs right after a new tab, or switching split panes quickly, no longer
  risks a crash.
- Research no longer swaps the page view several times in one turn (which crashed CEF a few
  seconds later): the space is made with the Plan tab as its only tab, reusing a space, opening
  the Report and selecting a plan node from another space change the model first and swap the
  view once. An attached page view is removed only after its browser exists and is at least 100 ms
  old; generation-guarded age and fallback tasks cannot release a newer view, the startup view is
  gated too, and the 2 s cap applies only when a browser is never created. Pages the agent already
  moved past load when you show them. A browser whose tab was closed while it was being created no
  longer keeps the app from quitting.

## [0.6.2] - 2026-10-15

### Fixed

- Compiled the Windows build as UTF-8, so window titles and the command palette no longer
  show garbled dashes and non-ASCII download names keep their bytes in the tests.

## [0.6.1] - 2026-10-15

### Fixed

- Fixed the Linux build by using CEF's unambiguous window-open disposition type in popup and
  new-tab callback signatures.

## [0.6.0] - 2026-10-15

### Added

- Start page **Research** mode: Enter sends the question to the configured agent with a prompt to research it with Island's browser tools (a new space named after the question, several sources in tabs, compared) and to finish with a Markdown report that cites every claim. The final report opens in a new **Report** tab (local page, shared safe Markdown renderer, links open in new tabs) with Copy Markdown, Save as .md... and Continue in agent panel. Clear messages for a missing agent (with an Open Settings button), a busy agent, and Cancel.
  Reports list: persisted in `reports.json` under the support directory, listed in Settings > Reports and in Cmd/Ctrl+K as REPORT rows.
  Dev capture actions `research:<question>` and `open_report`.
- Native ad and tracker blocking powered by Brave's `adblock` Rust engine. Island refreshes
  EasyList and EasyPrivacy in the background at most every four days, keeps working from its
  cached lists when an update fails, supports independent controls and a per-site allowlist in
  Settings > Privacy, and reports per-tab blocked-request counts in All tabs.
- Tab sleep: a tab you have not used for 15, 30 (default) or 60 minutes (Settings → Tabs, or
  Never) releases its browser to free memory. It keeps its URL, title, favicon and scroll
  position, shows dimmed in the sidebar and the icon rail, and reloads where you left off when you
  select it. Pinned tabs, the tab on screen, split-view partners, tabs playing audio and tabs with
  unsaved form input never sleep (a page script checks the last three just before closing). The
  command palette gains "Sleep inactive tabs now".
- Peek: Cmd/Ctrl+click or middle-click on a link, the "Open in peek" context-menu item, and
  links the page opens in a new tab show the page in a floating card over the current one, with
  the address, "Open as tab" (reopens it as a real tab of the active space) and "Close"; Escape
  closes it. The card is an overlay that exists only while it is open and covers only the card.
- Find in page (Cmd/Ctrl+F, next/previous with Enter / Shift+Enter or Cmd/Ctrl+G / +Shift+G, Escape
  closes): a small bar at the top-right of the page card with the "n of m" count and a match-case
  toggle, built on `CefBrowserHost::Find` and `CefFindHandler::OnFindResult`. The bar is an overlay
  that exists only while open, and it closes when the tab changes.
- Reader view (Cmd/Ctrl+Alt+R, or the "Aa" button in the address pill): a compact Readability-style
  script reads the article out of the page, and Island shows it in its own reader document (Geist,
  Graphite tokens; size, width and Auto/Light/Sepia/Dark controls, remembered in prefs). Toggling
  again returns to the original page. Nothing external is fetched; all four find/reader actions
  appear in Settings and the macOS Browser menu, and Settings > Reader configures the default
  text size, page width and color theme.
- On-device browsing history now records http/https page addresses and titles (never local,
  credentialed, data, or future private-mode pages), keeps the newest 50,000 visits, and searches
  them from Cmd/Ctrl+K alongside open tabs and bookmarks. The last typed word matches as a prefix. Results are URL-deduplicated, combine
  BM25/recency with repeat-visit frequency, and stay entirely on the device. Settings can stop
  remembering new visits or clear the persisted history.
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

[Unreleased]: https://github.com/island-browser/island/compare/v0.7.3...HEAD
[0.7.3]: https://github.com/island-browser/island/releases/tag/v0.7.3
[0.7.2]: https://github.com/island-browser/island/releases/tag/v0.7.2
[0.7.1]: https://github.com/island-browser/island/releases/tag/v0.7.1
[0.7.0]: https://github.com/island-browser/island/releases/tag/v0.7.0
[0.6.2]: https://github.com/island-browser/island/releases/tag/v0.6.2
[0.6.1]: https://github.com/island-browser/island/releases/tag/v0.6.1
[0.6.0]: https://github.com/island-browser/island/releases/tag/v0.6.0
[0.5.1]: https://github.com/island-browser/island/releases/tag/v0.5.1
[0.5.0]: https://github.com/island-browser/island/releases/tag/v0.5.0
[0.4.0]: https://github.com/island-browser/island/releases/tag/v0.4.0
[0.3.0]: https://github.com/island-browser/island/commits/main
[0.2.0]: https://github.com/island-browser/island/commits/main
[0.1.0]: https://github.com/island-browser/island/commits/main
