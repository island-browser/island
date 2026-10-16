# Island Browser — "Native" Look and the Research Plan Graph

## Status and baseline

- Proposed 2026-10-16. The project owner picked direction 2 ("macOS native") out of eight
  directions on the design canvas (https://claude.ai/artifact/424P54xVNumcF8ySPkizs6, row 2: Start,
  Agent at work, ⌘K, Settings → Appearance, Plan mode, Settings → Shortcuts). The owner then asked
  for Plan mode to be a graph, so that research can branch.
- Baseline: branch `release/design` (8d65e77). It already has the frameless macOS window with
  traffic lights over the sidebar, the space-tinted canvas, the rounded page card painted by
  `macos/page_card_mac.mm`, and live Settings → Appearance controls: space color, tint intensity,
  sidebar side, page radius, density, interface font (Geist / System / any family), shadow, and
  full bleed. This spec reuses that machinery. Most of the Native look is a set of defaults for
  those controls, plus a few new pieces.
- This document is a spec, not an accepted plan. Per AGENTS.md, implementation starts only after
  the owner accepts this spec and a plan written from it.

## Goals

1. Island looks like a well-made Mac app on macOS and stays neutral and native-feeling on Windows
   and Linux: system font, a grey source-list sidebar, system blue, and no decorative color.
2. People and the agent share the same surfaces. Every agent action shows up in a place a person
   could have acted in: tab rows, the address field, ⌘K rows, and Settings controls.
3. Research is planned before it runs, as a graph the person can prune, extend and approve. Running
   research shows progress on that same graph.

## Non-goals

- Real vibrancy (`NSVisualEffectView` behind the sidebar). CEF views cannot host it today, so the
  sidebar uses an opaque grey that matches the light and dark source-list colors. This can be
  revisited later as a macOS-only overlay, like the page card.
- Any space color in the Native look: no tint, no dot, no colored accent (decided 2026-10-16).
  Spaces are told apart by name in the switcher. Selection and focus stay system blue. People who
  want Arc-style color switch the "Look" preset (see Settings).
- A status bar. Page state is shown by small indicators inside the address field instead (§3).
- Store extensions, more top-level windows, or a new agent protocol. The plan graph rides on ACP
  plan updates through an optional `_meta` extension (see "Agent protocol").

## 1. Principles

- **Chrome recedes, page leads.** The sidebar is grey, the toolbar is light, and the page is white
  and full bleed. Color appears only for selection, focus, agent state and approval.
- **One blue.** `accent`/`selection` (#0A64D8 light; #0A84FF / #0A6FE0 dark) mark the selected tab row, focus rings, the
  primary button and the active segment. No other blue appears in the chrome.
- **Amber means "you decide".** Anything that waits for a person (sign-in, an approval, a
  shortcut conflict) uses the warning trio and nothing else does.
- **Same component, two actors.** A row the agent is reading gets a small status dot. An approval is
  an inline card, not a modal, and plan steps are ordinary buttons.

## 2. Tokens

The `design-tokens` contract in DESIGN.md moves to version 4 with a second accepted design,
`"Native"`, next to `"Graphite"`. Graphite stays for the site and as an option. The drift tests
(`tests/design/test_token_contract.py`, `tests/design_tokens_test.cpp`) cover both. New slots are
added to `ChromeTokens` and to the CSS variables of the local pages (`src/main/pages/*.html`).

### 2.1 Color

| Slot | Light | Dark | Use |
|------|-------|------|-----|
| `background` (canvas, sidebar) | `#EBEBEF` | `#2A2A2D` | Window ground, sidebar, source list |
| `surface` | `#FFFFFF` | `#1E1E20` | Page, panels, cards, palette |
| `surface_secondary` | `#F6F6F8` | `#252527` | Toolbar, inspector, agent panel |
| `fill` *(new)* | `#E8E8ED` | `#3A3A3D` | Address field, segmented track, chips, code |
| `text` | `#1D1D1F` | `#F5F5F7` | Labels, titles |
| `text_secondary` | `#636366` | `#98989D` | Meta, section headers, icons |
| `text_tertiary` *(new)* | `#8E8E93` | `#6E6E73` | Placeholders, disabled, skipped |
| `border` | `#E5E5EA` | `#38383A` | Hairlines |
| `border_strong` *(new)* | `#D2D2D7` | `#48484A` | Field and button outlines, keycaps |
| `accent` | `#0A64D8` | `#0A84FF` | Focus ring, links, active segment, primary button (light) |
| `selection` *(new)* | `#0A64D8` | `#0A6FE0` | Selected tab row, selected palette row, primary button (dark); white text on it |
| `accent_soft` *(new)* | `#DFE9F8` | `#16324F` | Agent button, "Paused" pill, question node |
| `accent_text` *(new)* | `#0A4FA8` | `#64B5FF` | Text on `accent_soft` |
| `warning_bg` *(new)* | `#FFF8E6` | `#3A2E12` | Approval cards, nodes that need sign-in, conflicts |
| `warning_border` *(new)* | `#F5D38A` | `#6B5420` | Their outline |
| `warning_text` *(new)* | `#6A4A00` | `#FFD60A` | Their text |
| `success` *(new)* | `#248A3D` | `#30D158` | Done steps, "will do" ticks |
| `danger` *(new)* | `#D70015` | `#FF453A` | "Will not do", failed steps, destructive buttons |
| `branch_a` … `branch_d` *(new)* | `#0A64D8` `#8E44AD` `#C25400` `#1F8A70` | `#0A84FF` `#BF5AF2` `#FF9F0A` `#40C8A0` | Plan graph branch strokes and labels |

The traffic lights keep the system colors (`#FF5F57`, `#FEBC2E`, `#28C840`); they are drawn by
AppKit and are not tokens. Contrast (computed): `text_secondary` on `background` is 5.0:1 light
and 5.0:1 dark; white on `selection` 5.5:1 light and 4.8:1 dark (white on the dark `accent`
#0A84FF is only 3.7:1, which is why dark selection fills use `selection`, not `accent`);
`warning_text` on `warning_bg` 7.7:1 and 9.4:1; `accent_text` on `accent_soft` 6.4:1 and 6.0:1.
`text_tertiary` is only for placeholders, disabled controls and skipped nodes (exempt from AA,
3.3:1). The drift test checks AA for every other text/ground pair listed above.

### 2.2 Type

- **Interface font = "System"** by default. On macOS it is SF Pro Text below 20 px and SF Pro
  Display at 20 px and up; on Windows, Segoe UI Variable; on Linux, the fontconfig `sans-serif`
  match. Geist stays bundled and selectable. The mono font is SF Mono / Cascadia Mono / fontconfig
  `monospace`, falling back to Geist Mono.
- Scale (px, weight): 34/700 page title (Plan hero, Start greeting) · 26/700 Settings section
  title · 17/700 panel title · 15/700 toolbar title · 13/400 body and controls (the base) · 12/400
  secondary rows, keycaps · 11/600 section headers (sentence case, `text_secondary`, no
  uppercase) · 10.5/400 chips · 10/700 node kicker ("Branch A").
- Line height 1.45 for body text and 1.25 for node titles.

### 2.3 Shape, spacing and sizes

- Radii: 4 (favicon plate, small chips) · 5 (chips, keycaps) · 6 (rows, buttons, fields) · 8
  (cards, segmented track, approval card) · 10 (plan nodes, palette rows) · 12 (window corners,
  palette, popovers). Pills (`999`) only for switches.
- Spacing on a 4 px grid: 4, 6, 8, 10, 12, 14, 16, 18, 24.
- Sizes: sidebar 248 (200 compact) · tab row 28 (34 comfortable) · toolbar 52 · address indicator 20 ·
  button 28 · small button 24 · field 30 (address) and 44 (start-page search) · segmented 24 ·
  switch 22×38 · keycap 24 min-width · inspector 316 · agent panel 380 · icon rail 52 (unchanged).
- Shadows: buttons `0 1px 3px rgba(0,0,0,.12)` (active segment), cards
  `0 1px 2px rgba(0,0,0,.06)`, palette `0 16px 48px rgba(0,0,0,.22)`, selected node focus
  `0 0 0 3px accent @ 35%`. The page card has no shadow and no inset (full bleed).

## 3. Window anatomy

```
┌ sidebar 248 ─────────┬ toolbar 52: ‹ ›  [ 🛡14  example.com   ≡  • ]  share  agent ┐
│ ● ● ●                │                                                                 │
│ Favorites            │                                                                 │
│  ▢ Mail              │                         page (full bleed)                       │
│ Work · Tabs          │                                                                 │
│ ▐▢ island / island ▌ │                                                                 │
│  ▢ CEF Forum      •  │                                                                 │
│  …                   │                                                                 │
│ ✦ Agent              │                                                                 │
└──────────────────────┴─────────────────────────────────────────────────────────────────┘
```

- **Sidebar.** Traffic lights sit at 12/12 inside it (the existing frameless window). Below them
  are section headers ("Favorites", "<Space> · Tabs"), then rows: a 16 px favicon on a 4 px-radius
  plate, the title, and an optional trailing status dot (`accent` = the agent is reading this tab,
  `warning_text` = it is waiting for you). The selected row is filled `selection` with white text, as
  in Finder. Hover is `fill` at 60%. The agent entry sits at the bottom (`accent_soft`, sparkle
  icon). The space switcher stays where it is today, restyled as a segmented control.
- **Toolbar** (`surface_secondary`, hairline below): back and forward as 28 px icon buttons, then
  the address field (`fill`, radius 8, 30 tall) whose text is centered while idle and left-aligned
  while editing, then share and the agent toggle.
- **Address indicators** (instead of a status bar, decided 2026-10-16): 20 px glyph buttons inside
  the address field, at its edges, in `text_secondary`; each shows only when it has something
  to say:
  - left: a shield with the number of blocked requests (click → blocker popover for this site);
  - right: Reader available (click → Reader view), and an agent dot (`accent` = the agent is
    reading this tab, `warning_text` = it is waiting for you; click → agent panel).
  A slept tab is shown in its sidebar row (title at `text_tertiary`), not in the address field.
  Every indicator has an `aria-label` and a tooltip with the full text ("14 trackers blocked").
- **Page:** white, square, full bleed.

The Appearance defaults for this look: tint 0 %, page radius 0, shadow off, full bleed on, font
System, density comfortable, space tint off. "Look" in Appearance becomes a preset menu, Native (default) / Arc
/ Graphite, that sets these knobs. Changing any knob afterwards shows "Custom".

## 4. Screens

### 4.1 Start (new tab)

Centered column, 560 wide: a segmented control **Search · Ask Agent · Research**, then a 44 px
search field with a 4 px `accent` focus halo, then six 56 px speed-dial tiles (radius 12) with
11 px labels. Tab and Shift+Tab cycle the segments while the field has focus. The placeholder
follows the mode: "Search with Google" / "Ask about this tab or anything" / "Research a question
— you'll see the plan first".

### 4.2 Agent at work (panel, 380, right)

The header has the title "Agent", a step counter ("Step 3 of 4"), and Stop. Below it are the
question card (`accent_soft`), then the step list: state glyph (done = `success` tick, running =
spinner in `accent`, waiting = `warning_text` dot, queued = hollow `text_tertiary`) and a one-line
label. Tool calls are 12 px rows, `tool` in mono plus its argument. An **approval** is an inline
card (`warning_*`) with "Allow the agent to sign in on github.com?", one line of why, and three
buttons: Skip this step · Allow once (primary) · Always for this site. The message field is
pinned to the bottom. While the agent reads a tab, its sidebar row shows the dot and the status
bar says "Agent is reading".

### 4.3 ⌘K command palette

640 wide, radius 12, centered at 18 % from the top over a 20 % black scrim. A 52 px input row
holds the query and a mode chip, followed by groups (Agent, Tabs, History, Actions) with 11 px
headers and 36 px rows: icon, title, meta in `text_secondary`, and right-aligned keycaps. The
selected row is `accent` with white text. ⌘↵ sends the query to the agent and ↵ runs the row.

### 4.4 Settings → Appearance

A local page in the content slot. On the left is a 200 px section list (General, Appearance,
Privacy & blocking, Tabs, Reader, Agent, Shortcuts, About) in the sidebar style. It has a search
field on top and the version at the bottom. On the right are grouped forms (Theme, Color, Layout,
Text), each a white card of 34–76 px rows: a label, an optional 12 px description, and the control
on the right. Controls are native-looking: segmented controls (Theme: Light / Dark / Match
system), switches, pop-up buttons with a ⌄ chevron, and color swatches (22 px circles, the
selected one ringed in `accent`). "Reset to defaults" is a plain button at the top right. Changes
apply live, as they do on `release/design` today.

### 4.5 Settings → Shortcuts

The same shell. Groups: Tabs, Navigation, Agent, Spaces, Window. Each row has an action name and
right-aligned keycaps (24 px min width, radius 5, `surface` with a `border_strong` outline and a 1 px
bottom shadow, 12 px glyphs ⌘ ⇧ ⌃ ⌥ ↵ ⇥ esc). Clicking a row puts it in **recording**: the keycaps
are replaced by an `accent` outline and "Press keys…". A **conflict** shows a `warning_*` banner
under the search field, "⌘B is already used by Toggle sidebar.", with Replace and Cancel. Unbound
actions show a ghost "Add shortcut" button. Top right has "Reset all". The note "On macOS the menu
bar picks up changes after a restart" stays, matching the current behavior. **⌘/** opens a
cheatsheet: a 320 px popover in the bottom right with the ten most-used bindings.

### 4.6 Plan mode: the research plan graph

This is the screen the owner singled out. Plan mode opens **in the content area, as its own tab**
(a Plan tab, pinned at the top of the space's tab list while research runs) when a Research query
is submitted, or when the agent proposes a plan with two or more branches. Nothing runs until the
person approves. Once it runs, the agent **opens the pages it reads as ordinary tabs**, grouped
under the Plan tab in the sidebar (indented one level, with the branch label as a small kicker).
Selecting a node with an open page switches to that tab, and the Result opens as a Report tab. The
Plan tab stays the hub: closing it asks whether to stop the research.

**Layout.** The toolbar (52) holds a graph icon, "Research plan", the pill "Paused · nothing runs
until you approve", the Graph / List segmented control, and "Tidy up". Under it, the graph canvas
(`#FBFBFD` with a 20 px dot grid) and the **inspector** (316, `surface_secondary`) sit side by
side. A legend floats bottom-left and zoom (− 100% +) bottom-right.

**Node kinds** (148 × 92, radius 10, padding 8/10):

| Kind | Look | Meaning |
|------|------|---------|
| Question | `accent_soft` fill, `accent_text` | The prompt. The root, always first. |
| Step | `surface`, `border_strong` outline, 3 px branch stripe on the left | One tool call or reading task |
| Needs you | `warning_bg`/`warning_border`, chip "Asks first" | Pauses that branch for sign-in or approval |
| Join | `surface`, chip "Joins A + B" | Waits for every live incoming branch |
| Result | `surface`, kicker "Result" | The report; opens as a Report tab |
| Skipped | `surface_secondary`, dashed `text_tertiary` outline, struck title, chip "Skipped by you" | A pruned branch; kept so it can be restored |
| Add | dashed outline, "+ Add a branch" | Ghost target hanging off the branching step |

Every node shows a kicker ("Branch A", "Step 1", "Join", "Result"), a title clamped to two lines,
and a chip row: tool in mono plus the site or state.

**Edges.** Cubic Béziers from the right middle of a node to the left middle of the next (control
points ±60 px), 2 px, colored by the branch they lead into; trunk edges are `text_tertiary`. A
3.5 px dot marks each arrival, and edges into skipped nodes are dashed. Join → Result is a straight
vertical edge.

**Lanes.** Each branch gets a soft band (its color at 3.5–4 % alpha, radius 12) behind its nodes, so
a branch reads as a group even after many sub-branches.

**Layout algorithm.** Columns follow depth, and the column pitch is node width + 40. Rows: each
branch gets its own band, and sub-branches split their parent's band. Branching is limited to
**3 levels** (decided 2026-10-16): an agent that wants to go deeper must put a Needs-you node in
the plan and ask, and "Branch from here" is disabled on level-3 nodes with a tooltip saying why. A Join sits in the column
after its deepest input, vertically centered on its inputs. Result goes below Join when the
canvas is narrower than depth × pitch, otherwise to its right. "Tidy up" reruns the layout, and
dragging a node only moves it visually; the order still comes from the edges.

**Inspector** (for the selected node): kicker ("Branch A · step 2"), a 17 px title, a key/value
list (Tool in mono, Site, Runs after, Feeds), a `warning_*` note when the step needs the person,
an editable "Instruction for this step" field, then the actions **Branch from here**, **Skip**
(or **Restore** on a skipped node) and **Ask agent to expand**. The bottom half summarizes the
whole plan: "8 steps · 2 live branches · 1 skipped · about 4 min", a permission list (✓ reads
pages on …, ! signs in to … after asking you, ⊘ never purchases, posts or deletes, ⊘ never types
passwords or payment details), and the buttons **Run plan** (primary), **Run A only** (runs the
selected branch and its trunk), and **Cancel**.

**While it runs** (proposed, to be drawn next on the canvas): the pill turns into "Running · 3 of
8". Queued nodes are drawn at 60 % opacity. The running node gets an `accent` ring and a
spinner in its kicker, done nodes get a `success` tick, and failed ones get a `danger` outline
with Retry and Skip in the inspector. A node waiting on the person pulses its amber outline once
and shows the approval card in the inspector. Pausing one branch never stops the others, and
Join waits. The edge into the running node animates its dash (off under Reduce motion).

**Keyboard and accessibility.** The graph is a `role="tree"`: the arrow keys move between parent,
children and siblings, ↵ edits the instruction, ⌫ skips (or restores), B branches from the
selection, ⌘↵ runs the plan, and esc returns focus to the canvas. Every node is a real `<button>`.
**List view** shows the same plan as an indented outline (branches as disclosure groups) and is
the screen-reader-first form; Graph and List stay in sync. Branch colors always come with the
"Branch A/B" text, so color is never the only cue.

## 5. Agent protocol for branching plans

ACP `plan` updates are flat (`content`, `status`, `priority`), and Island stores them as
`AcpPlanEntry`. Branching rides in each entry's `_meta`, which a plain ACP agent can omit:

```json
{"content": "Known problems in open issues", "status": "pending", "priority": "medium",
 "_meta": {"island": {"id": "a2", "after": ["a"], "branch": "A", "kind": "step",
                      "tool": "browser_open_tab", "site": "github.com", "needs_you": "sign-in"}}}
```

- `id` is unique within the plan. `after` lists the ids this entry waits for, so several ids make
  it a Join. `kind` is one of `question`, `step`, `join`, `result`. `branch` is a free label and the
  UI assigns colors in order.
- If no entry carries `_meta.island`, the plan is a straight chain, and Plan mode opens in List
  view with Graph available.
- The person's edits (skip, restore, branch from here, edited instruction, Run A only) go back to
  the agent as one user message carrying the edited plan in the same shape. "Ask agent to expand"
  sends "Expand step <id>" and expects new entries whose `after` points at that id.
- `AcpPlanEntry` gains an optional `IslandPlanMeta` (parsed in `src/agent/`, CEF-free and unit
  tested). Unknown `_meta` keys are kept and passed through unchanged.

## 6. Where each piece lives

- **Native chrome** (`src/main/browser_chrome.cc`, `design_tokens.*`): sidebar rows and selection
  fill, toolbar layout with the centered address field and its indicators, and the new token slots.
  Each new view re-asserts its colors through the theme-reset mechanism. The ChromeViewId range
  stays 1001–1027; the indicator buttons are children of the address view and take no new ids.
- **Local pages** (`src/main/pages/`): `start.html`, `agent.html`, `settings.html` (Appearance,
  Shortcuts, Look preset), and a new `plan.html` for the graph. The graph is an inline SVG with
  absolutely positioned `<button>` nodes, rendered from the plan JSON pushed in by the agent bridge,
  with no external libraries. Pages read the tokens as CSS variables injected by the browser.
- **⌘K** stays native (`command_palette_view.cc`) and gets the new row style and keycaps.
- **Overlays** keep `overlay_lifetime.h`: the palette and the cheatsheet exist only while shown.

## 7. Acceptance

1. With the default prefs on macOS, a dev capture of the window (`--island-dev-capture`, light and
   dark) matches canvas row 2 in structure and token colors. Every text/ground pair passes AA, as
   checked by the token drift test.
2. Appearance → Look switches Native / Arc / Graphite live. Changing any knob afterwards shows
   "Custom", and the choice survives a restart.
3. Shortcuts: recording, a conflict with Replace, and Reset all work. The ⌘/ cheatsheet opens and
   closes, and no overlay lingers after it closes (the mouse-input regression test).
4. Plan mode: a fixture agent that emits the `_meta.island` plan from §5 renders 3 branches, 1 join
   and 1 result. Skip, Restore, Branch from here, Run A only and Run plan send the expected messages
   (unit tests on the bridge plus a pytest against `plan.html`). A plan without `_meta` opens in
   List view. Plan opens as a tab; pages the agent reads open as tabs grouped under it; a 4th
   branching level is refused with a Needs-you node.
6. Address indicators: the shield shows the blocked count and hides at 0, Reader shows only when
   available, the agent dot follows agent state; all have accessible names.
5. Windows and Linux: the system font resolves and the toolbar and sidebar keep their metrics.
   Evidence comes from the six-target CI run, not from macOS.

## 8. Decisions (owner, 2026-10-16)

1. No space color in the Native look: no tint, no dot.
2. No status bar; page state is a set of small indicators in the address field.
3. Plan mode lives in the content area as a Plan tab; the agent then opens the pages it reads as
   tabs grouped under it.
4. Branching is limited to 3 levels before the agent must ask.
