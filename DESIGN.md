# Island Design System — Graphite

## 0. Research Log

- Accepted design: **Graphite** (contract version 3, 2026-10-06), replacing Ledger at the project
  owner's request: "Linear without gradients — developer style, minimal, like Vercel".
- References: vercel.com and the Geist design language (neutral near-black/white surfaces, 1px
  hairlines, Geist + Geist Mono, mono uppercase labels), linear.app (dense, calm app chrome), and
  Arc (per-space color as identity). Gradients are explicitly out.
- Consumers: the native chrome (`src/main/design_tokens.cc`), the browser's internal pages
  (`src/main/pages/*.html`, which receive the live tokens from the native state), and the product
  site, which lives in its own repository (`island-browser/island-site`) and checks its CSS variables
  against the block below in CI.

## 1. Atmosphere & Identity

Island is a precise, quiet tool. Surfaces are neutral grays that recede; type does the work. The
only color in the interface is meaningful color: the accent for actions and active state, and the
active space's color, which tints the sidebar and the window margin around the page (Arc's idea,
strengthened on 2026-10-15: the sidebar and the margin are one `canvas` surface). The product
shell — a sidebar of exactly 286 DIP beside the page on a rounded, softly shadowed card inset 8 DIP
in a frameless window (macOS: the traffic lights sit over the sidebar) — is the signature. Every
part of that look is customizable in Settings > Appearance. Honesty still rules: no
surface depicts a feature that does not exist in the build.

## 2. Color

### Canonical token contract

The fenced `design-tokens` block below is the single machine-readable source of truth for the Island
design contract. The tables in this section document it in human-readable form; the native runtime
tokens (`src/main/design_tokens.cc`) and the drift tests (`tests/design/test_token_contract.py`,
`tests/design_tokens_test.cpp`) must stay consistent with it, and the product site
(`island-browser/site`) checks its own CSS variables against it. Any change to these values must
update the block and pass the drift tests before it ships.
Colors are opaque `#RRGGBB` hex; spacings, radii, and the rail width are DIP
values.

```design-tokens
{
  "contract": "island-design-tokens",
  "version": 3,
  "accepted_design": "Graphite",
  "colors": {
    "light": {
      "background": "#FAFAFA",
      "surface": "#FFFFFF",
      "surface_secondary": "#F2F2F2",
      "text": "#0A0A0A",
      "text_secondary": "#666666",
      "border": "#E5E5E5",
      "accent": "#0068D6"
    },
    "dark": {
      "background": "#0A0A0A",
      "surface": "#111111",
      "surface_secondary": "#1A1A1A",
      "text": "#EDEDED",
      "text_secondary": "#A1A1A1",
      "border": "#2A2A2A",
      "accent": "#3291FF"
    },
    "site_only": {
      "space_blue": "#0090FF",
      "space_coral": "#E5484D",
      "space_green": "#30A46C"
    }
  },
  "spacing": {
    "space_1": 4,
    "space_2": 8,
    "space_3": 12,
    "space_4": 16,
    "space_6": 24
  },
  "radii": {
    "radius_small": 6,
    "radius_medium": 10
  },
  "rail_width_dip": 286,
  "arc": {
    "surfaces": {
      "light": {
        "canvas": "#F2F2F2",
        "selected": "#FFFFFF",
        "hover": "#EAEAEA",
        "well": "#FAFAFA",
        "plate": "#FFFFFF"
      },
      "dark": {
        "canvas": "#1A1A1A",
        "selected": "#2A2A2A",
        "hover": "#222222",
        "well": "#242424",
        "plate": "#EDEDED"
      }
    },
    "defaults": {
      "sidebar_tint_percent": 60,
      "sidebar_side": "left",
      "card_radius_dip": 10,
      "card_radius_choices": [0, 6, 10, 14],
      "card_inset_dip": 8,
      "control_height_dip": 36,
      "compact_control_height_dip": 28,
      "row_gap_dip": 6,
      "compact_row_gap_dip": 2,
      "sidebar_padding_dip": 16,
      "compact_sidebar_padding_dip": 8,
      "collapsed_rail_width_dip": 52,
      "titlebar_band_dip": 40,
      "font": "geist",
      "card_shadow": true,
      "full_bleed": false
    }
  },
  "typography": {
    "ui_family": "Geist",
    "mono_family": "Geist Mono",
    "ui_stack": "Geist, ui-sans-serif, -apple-system, BlinkMacSystemFont, \"Segoe UI\", sans-serif",
    "mono_stack": "\"Geist Mono\", ui-monospace, SFMono-Regular, Menlo, monospace"
  },
  "motion": {
    "fast_ms": 160,
    "enter_ms": 480,
    "reveal_ms": 700,
    "ease_out": "cubic-bezier(.16, 1, .3, 1)",
    "composited_properties_only": ["opacity", "transform", "filter"]
  },
  "material": {
    "shell_material": "surface_secondary at 90% over its assigned background",
    "blur_px": 12,
    "shadow": "0 1px 2px plus 0 8px 24px, both low-opacity neutral"
  },
  "depth": {
    "product_window": "1px border plus one soft low-opacity shadow; no glass stack",
    "text_sections": "hairline-ruled grids; cards are flat surfaces with a 1px border"
  },
  "accessibility": {
    "target": "WCAG 2.2 AA",
    "body_contrast_min": 4.5,
    "large_text_contrast_min": 3.0,
    "focus_ring": "3px solid accent with 4px offset",
    "reduced_motion": "prefers-reduced-motion cancels entrance motion and transitions; content and states remain",
    "min_content_width_dp": 320
  },
  "rules": {
    "accent_semantics": "accent only signifies an action or active state",
    "light_mode_text": "all readable light-mode text uses the text token",
    "shipped_features": "tabs and the tab strip, pinned tabs, spaces and the space switcher, split view, the command palette, the search palette, the all-tabs overview, settings with custom shortcuts, the agent panel, the MCP tools endpoint, session restore, and the welcome flow exist in the shipped build; the product site may present them and must keep the honesty rule — every depicted control runs in the current build",
    "neutral_palette": "surfaces are neutral grays; color comes only from the accent and the space markers",
    "no_literal_imagery": "no photos, tropical colors, or gradients of any kind"
  }
}
```

### Native look contract

```design-tokens-native
{
  "contract": "island-native-look",
  "version": 2,
  "colors": {
    "light": {
      "background": "#EBEBEF",
      "surface": "#FFFFFF",
      "surface_secondary": "#F6F6F8",
      "fill": "#E8E8ED",
      "text": "#1D1D1F",
      "text_secondary": "#636366",
      "text_tertiary": "#8E8E93",
      "border": "#E5E5EA",
      "border_strong": "#D2D2D7",
      "accent": "#0A64D8",
      "selection": "#0A64D8",
      "accent_soft": "#DFE9F8",
      "accent_text": "#0A4FA8",
      "warning_bg": "#FFF8E6",
      "warning_border": "#F5D38A",
      "warning_text": "#6A4A00",
      "success": "#248A3D",
      "danger": "#D70015",
      "segment_selected": "#E3E3E8",
      "branch_a": "#0A64D8",
      "branch_b": "#8E44AD",
      "branch_c": "#C25400",
      "branch_d": "#1F8A70"
    },
    "dark": {
      "background": "#2A2A2D",
      "surface": "#1E1E20",
      "surface_secondary": "#252527",
      "fill": "#3A3A3D",
      "text": "#F5F5F7",
      "text_secondary": "#98989D",
      "text_tertiary": "#6E6E73",
      "border": "#38383A",
      "border_strong": "#48484A",
      "accent": "#0A84FF",
      "selection": "#0A6FE0",
      "accent_soft": "#16324F",
      "accent_text": "#64B5FF",
      "warning_bg": "#3A2E12",
      "warning_border": "#6B5420",
      "warning_text": "#FFD60A",
      "success": "#30D158",
      "danger": "#FF453A",
      "segment_selected": "#3A3A3D",
      "branch_a": "#0A84FF",
      "branch_b": "#BF5AF2",
      "branch_c": "#FF9F0A",
      "branch_d": "#40C8A0"
    }
  }
}
```

`segment_selected` is shared by Native, Arc and Graphite. A selected segment is a neutral raised
plate with the normal `text` color; blue remains reserved for actions and `:focus-visible` rings.
In dark theme the selected plate equals the track fill, so the chrome draws a `border_strong`
outline there (`BrowserChrome::NativeSegmentColors`), and the web pages draw the same hairline as an
inset `--segment-outline` (`#48484A`; `transparent` in light). Blue is still not used for selection.

0.7.2 applies the same rule to the other Native selections. The command palette's highlighted row is
the `segment_selected` plate with `text`, not the blue `selection` fill (the palette redesign itself
is later work). The open Agent entry is the same plate with normal text. On the selected (blue)
tab row, the close × is white at 70% and the generic globe is white. The address field's focus ring
is one 2 DIP accent ring that follows the 8 pt radius: the controls sit in an inner panel whose
corners take the 6 pt inner radius. Arc and Graphite draw one ring too (0.8.0): the pill's outer
accent outline, with the textfield's own Chromium ring painted in the plate fill so it vanishes.
The field sits 2 DIP inside the pill there, so that ring never covers the outline. The Plan's
instruction field is 30 pt tall with radius 6 and `border_strong`. Web pages follow the Light or
Dark theme's color mode; System follows the OS.

Native's sidebar footer uses a 28 DIP control height on an 8 DIP row rhythm
(`NativeSwitcherMetrics`). Its Agent row shows the provider name, a status dot (idle, working or
needs you) and the shortcut keycap. A space with an icon shows only that icon in its switcher
segment; the name stays in the tooltip and the accessible name.

### Documented colors

| Role | Token | Light | Dark | Usage |
| --- | --- | --- | --- | --- |
| Background | `--bg` | `#FAFAFA` | `#0A0A0A` | Window field, page background |
| Surface | `--surface` | `#FFFFFF` | `#111111` | Page card, inputs, cards |
| Surface 2 | `--surface-2` | `#F2F2F2` | `#1A1A1A` | Sidebar, secondary panels, hover |
| Text | `--text` | `#0A0A0A` | `#EDEDED` | Headlines and body |
| Text 2 | `--text-2` | `#666666` | `#A1A1A1` | Secondary text, meta, labels |
| Border | `--border` | `#E5E5E5` | `#2A2A2A` | Hairlines and outlines |
| Accent | `--accent` | `#0068D6` | `#3291FF` | Links, focus, active state |
| Space blue | `--space-blue` | `#0090FF` | `#0090FF` | Default space marker |
| Space coral | `--space-coral` | `#E5484D` | `#E5484D` | Space marker |
| Space green | `--space-green` | `#30A46C` | `#30A46C` | Space marker |

The browser cycles new spaces through `#0090FF`, `#30A46C`, `#FFB224`, `#E5484D`, `#8E4EC6`.
Rules: accent only signifies an action or active state. Every text token, including `--text-2`,
meets 4.5:1 on every surface in both themes (asserted by `test_token_contract.py`), so secondary
text is usable everywhere. No gradients of any kind, no photos, no glow. Space tinting is a runtime
cast: the `canvas` (sidebar + window margin) mixes the space color into the neutral surface by up
to 62% light / 42% dark at the default 60% intensity (Settings > Appearance > Sidebar tint, 0–100),
and `selected`, `hover`, `well` and `plate` are derived from it. Tinting never changes the text
tokens' values; instead the cast eases off whenever primary text would drop below 4.5:1 on the
canvas or a row fill, and `text_secondary` walks toward the text color until it holds 4.5:1 on
every one of those surfaces (asserted for pure white/black spaces at every intensity in
`tests/appearance_test.cpp`). The `arc` block of the contract pins the neutral defaults.

## 3. Typography

Primary is `Geist, ui-sans-serif, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif`; mono
is `Geist Mono, ui-monospace, SFMono-Regular, Menlo, monospace`. The native chrome registers the
vendored Geist files at startup; the product site self-hosts them. Headings are set tight
(negative tracking), labels and metadata in uppercase Geist Mono at 11–12px with wide tracking,
keys and code in Geist Mono.

| Level | Size | Weight | Line height | Usage |
| --- | --- | --- | --- | --- |
| Display | `clamp(40px, 6vw, 72px)` | 600 | 1.0 | Hero |
| H1 | `clamp(32px, 4vw, 48px)` | 600 | 1.05 | Page titles |
| H2 | `clamp(22px, 2.6vw, 32px)` | 600 | 1.15 | Section headings |
| Body | 15–16px | 400 | 1.55 | Default copy |
| Small | 13–14px | 400 | 1.5 | Supporting copy, UI |
| Mono label | 11–12px | 500 | 1.4 | Labels, keys, meta |

## 4. Spacing & Layout

Base unit is 4px: `--space-1: 4px`, `--space-2: 8px`, `--space-3: 12px`, `--space-4: 16px`,
`--space-6: 24px`. Radii: `--radius-sm: 6px` (controls, chips, tabs) and `--radius-md: 10px`
(cards, panels). The sidebar is 286 DIP (52 DIP collapsed); the page sits on a card inset by
`--space-2` (8 DIP) on its top, outer side and bottom and flush with the sidebar, radius 0/6/10/14
(default 10), with the sidebar on the left or right. Density sets the sidebar row height: 32 DIP
comfortable, 26 compact. Full bleed removes inset, radius and shadow.

## 5. Components

- **Frameless window (macOS):** no title bar and no title text. The first 40 DIP of the sidebar
  is the titlebar band: the traffic lights sit in it at the left, back/forward start 92 DIP in, and
  the band (minus those buttons) plus the canvas strip above the page card drag the window
  (`CefWindow::SetDraggableRegions`, `BrowserChrome::DragRegionsForBounds`). With the sidebar on
  the right, or collapsed to the 52 DIP rail, the lights sit over the page column, so the page
  card starts below the band. Windows/Linux keep the platform frame.
- **Sidebar (native):** one `canvas` surface (the space color, tinted by the intensity setting)
  shared with the window margin. Controls are ghost by default at one control height (32 DIP,
  26 compact) and 16px icons on a 4/8/12 DIP rhythm: back/forward, then the address as one soft
  `well` pill (the outline turns `--accent` while the field has focus), then the tab list — rows
  on the canvas, the selected tab a soft `selected` fill, the hovered one `hover`, no outline — and
  a footer with a quiet space chip and icon buttons. A "+ New tab" row sits directly under the
  "<Space> · Tabs" header, before the first tab row, in every look (owner decision 2026-10-26; it
  supersedes the 0.9 spec §9-4 removal). It is a row of the tab list (the same height and neutral
  `hover` plate as a tab row, no accent) and opens a tab exactly like ⌘T. Its keyboard focus ring
  sits in a 4 DIP inset around the row (a wrapper panel; the ring is drawn about 4 DIP outside the
  row, so a 2 DIP inset clipped its sides), so the ring is neutral, rounded and not clipped by the
  tab list; the collapsed rail's "+" sits after the tab icons in a wrapper with 4 DIP above and
  below it, so its ring keeps air from the favicon above. Clicking the active
  space's segment (or its rail icon) opens the space menu: Rename, Change icon, the five colour
  presets, New space and, with another space present, Delete space. It is a palette listing shown
  only while open, not a persistent overlay. In Arc and Graphite the footer's Agent button is a
  neutral plate at the row radius (8 DIP) and the control height, centered with the icon buttons and
  spaced like them: `hover` on hover, `segment_selected` while the panel is open, the label `text`
  and the sparkle `text_2`, never an accent fill or label.
  Favicons sit on a 22 DIP rounded plate (`plate`) so dark favicons such as GitHub's stay visible on
  a dark sidebar. Chrome text uses the interface font (Geist, the system font, or an installed
  family). CEF `cef_views` has no corner-radius API and clips button images to the button's insets,
  so the soft rounded fills are square fills whose four corners a native macOS overlay repaints in
  the canvas color, or in the patch's own `outside` color (the Native toolbar around the address
  plate) (`CornerPatch` / `ApplyCornerPatches`, macos/page_card_mac.mm; the chrome pushes
  them on selection, hover, relayout and scroll). Elsewhere the fills stay square. CEF resets
  custom view colors on every theme change, so each chrome view reports the reset and the chrome
  repaints from the resolved tokens (`BrowserChrome::ReapplyThemeColors`, `TabRowColors`,
  `SpaceChipColors`).
  - *Keyboard focus (R16):* every sidebar control (the global strip icons, `+N`, `+`, favorite and tab
    rows, rail icons, footer and status buttons) shows a rounded neutral 2 DIP `text_secondary` ring
    (`BrowserChrome::TextfieldFocusRingColor`, `address_focused=false`), in every look and theme. Only the
    address field keeps the accent focus ring. Tests: `BrowserChromeFocusRingStyleTest`.
- **Collapsed rail, Arc and Graphite:** the rail shows only tabs and spaces (no Back, Forward or
  address controls). This is the intended toolbar-less rail for those looks, not a defect.
- **Collapsed icon rail (native):** Cmd/Ctrl+B swaps the 286 DIP sidebar for a 52 DIP rail on the
  same `canvas` surface, never hiding the tabs. One 32 DIP square button per tab of the
active space (16 DIP favicon on its plate, globe fallback), pinned tabs first with a hairline
between the groups; the active tab is the same soft `selected` fill as the selected row. A quiet "+" and the spaces (round dots; the active one wears a ring, so state is a
  shape and never color alone) sit at the bottom. The selected generic globe is white on its blue
  plate. The tab title is the tooltip and the accessible
  name, buttons are keyboard focusable, and the list scrolls vertically (buttons plus a classic
  scrollbar always fit the 52 DIP). There is no edge sliver or hover reveal. Cmd/Ctrl+L expands
  the sidebar for the length of the address edit. Every rail view re-asserts its colors through
  the same theme-reset delegates (`BrowserChrome::ApplyCollapsedRailTheme`).
- **Welcome card (native overlay):** a radius-12 `--surface` card with a 1px `--border` outline,
  spacing_6 padding and system type. Appearance and Import are sentence-case 11/600 section
  headers. The appearance control matches the sidebar switcher: a `fill` track with a 2 DIP inset
  and 24 DIP neutral `segment_selected` plates, without a square inner hairline.
  Its focus ring is neutral and follows only the focused segment. A 20% black scrim fills the
  short-lived overlay; the page compositor is hidden while the Native welcome is shown so start-page
  controls cannot paint through it. Readable browsers remain outlined toggle rows, followed by a
  right-aligned secondary Import button and the black/white Start browsing primary button. Arc and
  Graphite keep their original square hairline card and segmented control.
- **Command palette (native overlay):** Cmd/Ctrl+K opens a 640 DIP card over a 20% black scrim.
  Sentence-case group headers divide the results; each 36 DIP row has a 16 DIP favicon or a
  meaning-specific Lucide icon, a sentence-case kind label, and a shortcut keycap resolved from the
  current keymap. The keycap plate is a 20 DIP rounded plate at its natural width (caps plus label),
  right-aligned in a fixed 104 DIP Native column so kind labels line up on every row. The selected
  row is one radius-10 neutral `segment_selected` plate, never blue. The focused query field uses
  the shared neutral overlay ring. Arc and Graphite keep the earlier card-sized overlay without a
  full-window click blocker.
- **Palette bookmark rows and the Bookmarks group:** a bookmark row keeps its title in `--text`. Its
  folder path and favorite scope ("Global, Work") follow as secondary text in `--text-secondary`
  (12 DIP Native, 13 DIP Arc/Graphite), never in the title color. The bookmark and favorite commands
  sit under one sentence-case "Bookmarks" header, after the general commands and before the bookmark
  rows, and use the same kind label as a bookmark ("Bookmark"). An empty query keeps that order. Page-changing rows are hidden when the page cannot be bookmarked.
- **Start page:** `src/main/pages/start.html`, a local page loaded as the tab's own `data:`
  document: `--surface` page, one `--bg` input with a 1px `--border` (outline turns `--accent`
  on focus), a two-segment Search / Ask agent switch (selected = `--surface` with a hairline),
  a `--text` submit button, mono uppercase label and keycap hints. It follows the app theme once
  the native side sends it (system light/dark before that) and requests nothing until a search
  is submitted.
- **Page card (native):** the page sits on the canvas with an 8 DIP inset on its top, outer side
  and bottom (flush with the sidebar), radius from the Appearance setting and one soft shadow.
  CEF cannot round or shadow the `CefBrowserView` and the page is composited by the browser, so on
  macOS a mouse-transparent native frame view lays a canvas-colored ring (the hole carries the
  rounded corners) and the shadow over the card (`ApplyPageCardStyle`); other platforms keep a
  square card.
- **Address-field placeholder:** an empty, resting address field shows "Search or enter address". In
  Native it is drawn through the label in `text_secondary`. Arc and Graphite use the textfield's
  placeholder in CEF's default colour: the pinned CEF has no placeholder colour setter
  (`SetPlaceholderTextColor` is behind `CEF_API_REMOVED(15000)`), so this is an accepted limit. It
  is never the field's text. A search engine's results page shows its query at rest instead of the
  URL, unless the query would read as an address, holds a control, is not valid UTF-8, or has a
  non-ASCII character that is not a letter, mark, number or space separator (only letters, numbers
  and spaces outside ASCII; anything else shows the URL);
  focusing the field keeps the query.
- **Address-field indicators (Native):** 20 DIP glyph buttons inside the 30 DIP address field, at
  its edges, in `text_secondary`, each shown only when it has something to say (spec §3).
  Left: a shield with the count of blocked requests, hidden at 0 and capped at `9999+`; click opens
  Settings > Privacy. Right: Reader, only on pages that have a reader view, and the agent dot,
  `accent` while the agent reads the tab and `warning_text` while it waits for the person; click
  opens the agent panel. The dot is never the only cue: each indicator has an accessible name and
  a tooltip with the full text ("14 trackers blocked"). A slept tab shows its state in its sidebar
  row, not in the field (`address_indicators.h`, `BrowserChrome::SetAddressIndicators`). The field
  fill is flush with its focus plate; Arc and Graphite idle fields begin at the URL start.
- **Plan graph nodes (Native, `pages/plan.html`):** 148 × 92 DIP, radius 10, drawn as real
  buttons over an inline SVG of edges. Kinds: **Question** (`accent_soft` fill, the root),
  **Step** (`surface`, `border_strong` outline, 3 DIP stripe in its branch color), **Needs you**
  (`warning_bg`/`warning_border`, "Asks first" chip), **Join** ("Joins A + B" chip), **Result**
  (kicker "Result"; opens as a Report tab). Skipped is a state of any node, not a kind:
  `surface_secondary`, dashed `text_tertiary` outline, struck title, "Skipped by you" chip. Run
  states: queued at 60 % opacity; running with an `accent` ring; done with a `success` tick in the
  kicker; waiting with one pulse of the `warning_border` outline; failed with a `danger` outline.
  The dashed "+ Add a branch" ghost hangs off a branching step. Edges are cubic Béziers in the
  branch color (trunk `text_tertiary`), dashed into skipped nodes. Branches stop at 3 levels.
  Branch labels ("Branch A") always accompany the color. Node kickers and the legend use the
  system font at 10/700 in sentence case; a join deduplicates its incoming branch letters and reads
  "Joins A + B". Report pages use the same Native sentence-case treatment for their kicker and
  table headers; Arc and Graphite retain their mono uppercase tone.
- **Bookmarks page:** a local content-slot library with a labelled search field and a compact Import /
  New folder toolbar. A neutral raised folder row marks the current folder (`aria-current`) and keeps
  its `...` action inside the folder column. Each bookmark row shows a favicon plate, title, host and
  scope badges (`Global` and space names); its title and row Enter open the bookmark, while one `...`
  menu contains Open in new tab, favorite, move, edit and delete actions. Escape closes that menu and
  returns focus to its trigger. It collapses from folders-and-results columns into one reading-order
  column. Failed native saves remain an inline live status directly below the toolbar with Retry;
  separate transient page outcomes never replace that retryable status. Deleting a non-empty folder
  offers Move bookmarks to Unfiled and requires an in-page confirmation before permanent deletion.
  It uses ThemeJson tokens only: neutral raised hover, selection and non-address focus states, with
  the danger token reserved for destructive confirmation.
- **Internal surfaces (agent panel, Settings tab, All tabs):** flat `--surface` cards with 1px hairlines,
  `--radius-md`; controls at `--radius-sm`; section labels in mono uppercase `--text-2`; primary
  buttons use `--text` as background with `--surface` foreground. The accent is used only for the
  active tab, the address focus ring, the agent's own message bubble and its send button; selected
  states (a selected row, segment, node, radio, swatch or "on" switch) are neutral: a `--text`
  mark or a `--fill` plate, in every look. Settings' range slider takes `--text` too. The agent panel's focus ring is a neutral
  2 px `--text-2` ring (see below), never the accent.
- **Settings tab:** Settings is one ordinary, closable and reorderable tab per window, opened in
  the active space. Reopening it focuses that tab and routes the requested section through the
  existing `focus_seq` contract. Its title and address label are `Settings`, its sidebar fallback
  is the gear icon, and only the exact embedded Settings document may use its native message
  bridge. Agent tools refuse the Settings tab before navigation or page actions reach it. Form
  fields and other controls use a neutral 2 px `--text-2` focus ring that follows each control's
  own shape. The tab is omitted from session persistence and is not restored after restart.
- **Sidebar density:** in every look, Comfortable uses 36 DIP rows, 6 DIP row gaps and 16 DIP rail
  padding; Compact uses 28 DIP rows, 2 DIP gaps and 8 DIP rail padding. Density is structural, not
  a color-preset detail, so the same metrics apply to Native, Arc and Graphite.
- **Agent panel (native, `pages/agent.html`):**
  - *Header:* a 52 px row with a hairline bottom border. The title (the agent's name, 15 px 600)
    opens the provider menu; a status pill sits beside it. The pill reads "Waiting for approval"
    (`--warn-bg` fill, `--warn-border` 1 px border, `--warn-text`) while a confirmation card is
    pending, "Working" (neutral `--fill`, `--text-2`, a small pulsing dot) while a turn runs, "Idle"
    (neutral, no dot) when the agent is running with nothing in flight, and otherwise keeps the
    existing wording ("Not running", "Connecting...", "Stopped") as a neutral pill. New chat and
    Hide are quiet icon buttons (`--text-2`, a `--fill` plate on hover).
  - *Thread:* 14 px padding and a 10 px gap. Your messages are bubbles (`--accent-soft` fill,
    `--accent-text` text, radius 8). Agent replies are plain `--text` with no bubble. A tool call is
    one compact 12 px line in `--text-2`: a check in `--success` (a cross in `--danger` on failure,
    a spinner while it runs), the tool name in mono 11.5 px `--text`, then the host or detail.
    An MCP tool's `mcp__<server>__` prefix is dropped from the name; the full name is the row's
    tooltip. Each row also speaks its status.
  - *Composer:* one card with a 1 px `--border-2` outline, radius 10, `--surface`, and a 10 px
    inset. The textarea sits above a bottom row: the page chip (22 px tall, radius 5, `--fill`,
    `--text-2`, 11 px, ellipsis) and the quote chip on the left, and a round 24 px send button
    (`--accent` fill, `#FFF` arrow; 40 % opacity when disabled) on the right. The placeholder is
    "Ask about this tab or anything". The card's border shows focus; the textarea draws no ring.
    Keyboard hints sit below in `--text-3`, 11 px.
  - *Focus:* buttons take a neutral 2 px ring, `box-shadow: 0 0 0 2px var(--text-2)`, which follows
    each element's radius. A Collect checkbox rings its whole row, because a native checkbox may not
    paint the ring itself. There is no outline and no blue focus ring, and the ring never appears
    on mouse focus. Selected and pressed states use neutral raised plates (`--fill`,
    `--segment-selected`), never blue.
  - *Confirmation card:* a `.card.confirm-card` on the composer's 12 px inset (the same left and
    right edges as the composer, never clipped) on a `--surface` plate with a full 1 px
    `--warn-border` hairline and a `--warn-text` kicker. In Native the kicker is sentence case
    system text ("Allow this action?"), not mono capitals. It shows the host in mono `--text-2` and
    the summary, and has buttons for Allow once (the primary), Allow for this task, Trust this site
    and Cancel, on 26 px buttons with 6 px radius, in two even columns so Cancel is never alone. A card that
    cannot be trusted (card fields, password and unclassified fields) offers
    only Allow once and Cancel. Esc declines the oldest card. The card never takes focus.
  - *Tasks strip:* a `--surface` block with a `--border` outline and `--radius` corners, above the
    conversation, with a mono uppercase "Tasks" heading and a "Clear finished" link. Each row shows
    its state as text in compact 12 px rows: "Running · 3/7", "Queued · #1", "Needs you" (with the
    count when known), Done, Failed, Stopped.
    A running or waiting row takes a neutral `--fill` plate, and "Needs you" is `--warn-text`.
    Queued rows have Move up, Move down and Remove buttons; running ones have Stop. A queued chat
    message sits under the conversation as a bubble with a 1 px dashed `--border-2` border at
    65 % opacity and the tag "Queued · #n".
  - *Chips in the composer's bottom row:* the page chip (one 22 px plate: the share toggle, with
    the ✕ that removes the page context inside the same `--fill` plate) and, for a selection, a
    quote chip. Both share the 22 px chip style and ellipsize on one line. Once sent, the same
    neutral page and quote chips appear above that user's bubble in the thread; they are transcript
    metadata, not repeated prompt text. Thread quotes use the same one-line text span as composer
    quotes, including long selections.
  - *Collect sheet:* a 12 px-radius dialog over a 20 % scrim, with one text field labelled in
    sentence case at 11 px 600, a list of tab rows (checkbox, title and mono URL on a 1px
    `--border-2` list) and a Collect button that is disabled until something is typed. Checkboxes
    and buttons are neutral.
- **Settings > Agent & tools > Trusted sites:** a `--surface` card listing each host with a Remove
  button, which names the host in its accessible label; an empty state says how to add one.
- **Keyboard keys:** Geist Mono 11–12px on `--surface` with a 1px border and a 2px bottom border.
- **Status chips:** mono uppercase label plus a 6px dot; meaning is carried by the label, never by
  color alone.

### Sidebar favorites (Bookmarks 0.9)

- Order: global strip, `<Space> · Favorites` rows (Graphite: "Favorites"), the tabs header, tab
  rows, fixed footer. The tabs header sits before the first tab row (pinned or not) in Native; Arc
  and Graphite show a small "Tabs" label only when favorites come first. No header trails an empty
  list. The collapsed rail uses the same order with 32 DIP icon buttons and one hairline after the
  globals.
- Global strip: exactly `min(6, count)` icons then `+N`, never more. The "+" (add current page) is
  not in the strip: it trails the "Global favorites" header row. `ComputeGlobalStripLayout` fits the
  strip into the inner width (`rail_width − 2·spacing_3 − 10`): the plate shrinks from 28 toward
  24 DIP and the gap from 4 toward 2 DIP. Native 248 (inner 214) shows six icons with a 28-DIP plate
  and a 3-DIP gap. Arc/Graphite 286 (inner 252) keep 28/4. Native compact (200, inner 166) cannot
  hold six icons and "+N" at the 24/2 floors, so it shows five icons and "+N" (ruling R14, accepted:
  Native compact shows 5 icons plus `+N`).
- Space favorite rows: Native 28 DIP compact, 34 comfortable; Arc and Graphite 32 DIP in both.
- A displayed favorite uses `segment_selected` (neutral raised), never `accent`. Hover uses `hover`.
- While a favorite is on screen, the space's selected ordinary tab is not the tab the person sees
  (ruling R15). It takes the neutral `segment_selected` plate with `text` (no accent fill, selection
  or white glyphs) until the favorite closes; the accent returns when the ordinary tab is shown
  again. `DisplayedTabModel` is the single mapping from the stored selection/split to the pane IDs
  on screen; `TabRowFill` and `TabStripEntrySnapshot::behind_favorite` carry its visual rule.
- Status row: `warning_bg` fill with `warning_text`, directly above the footer, hidden when no
  bookmark change failed.
- No new color tokens were added: the feature reuses `canvas`, `hover`, `segment_selected`, `text`,
  `warning_bg` and `warning_text`, which already carry light and dark values.

## 6. Motion & Interaction

- `--motion-fast: 160ms`; `--motion-enter: 480ms`; `--motion-reveal: 700ms`; `--ease-out:
  cubic-bezier(.16, 1, .3, 1)`.
- Motion is meaningful only: hover/press feedback and one soft entrance. Only `transform`,
  `opacity`, and `filter` animate. `prefers-reduced-motion: reduce` cancels entrance motion and
  transitions while preserving all content and states.

## 7. Depth & Surface

Flat by default. Depth comes from surface steps (`canvas` → `selected` / `well` → `--surface`)
and 1px hairlines. The only shadow is the page card's single soft shadow (an ~8 DIP blur cast into
the window margin on macOS, optional in Settings). No glass stacks, no glow, no gradients.

## 8. Accessibility Constraints & Accepted Debt

### Constraints
- WCAG 2.2 AA: 4.5:1 body contrast (every text token on every surface), 3:1 large text and UI,
  keyboard reachability, semantic landmarks, visible 2–3px focus rings (accent on the address field
  only; neutral `text_secondary` on sidebar controls, see Sidebar above), no color-only
  information, no auto-playing media.
- Status is stated in text, never by color alone. Decorative product mockups are `aria-hidden`.
- Motion is optional under `prefers-reduced-motion`; no action depends on hover, motion, or JS.

### Accepted Debt
| Item | Location | Why accepted | Owner / Exit |
| --- | --- | --- | --- |
| CEF theme parity gap | `src/main/browser_window.cc` dark detection | The chrome derives light/dark from the OS via the CEF window theme, which has no automated visual verification yet. Values are drift-guarded by `tests/design_tokens_test.cpp` and `tests/design/test_token_contract.py`. | Close with native visual acceptance evidence on real hardware. |
| Native selected segment has no shadow | `src/main/browser_chrome.cc` `ComputeCornerPatches` | CEF views cannot draw a shadow; the selected segment gets its `surface` fill and rounded corners only (spec §3 asks for `0 1px 3px` shadow). `src/main/macos/page_card_mac.mm` is the only native shadow path. | Close when the Native switcher gets a macOS-native shadow layer (same approach as the page card). |
| Native Graphite pass partly verified | `src/main/browser_chrome.cc`, `src/main/welcome_flow.cc`, `src/main/pages/start.html` | The macOS light-theme sidebar was inspected in a running build through an in-process window capture (the sandbox cannot use screen recording), up to the theme-reset repaint. The final address-field holder, space-chip widths, the restyled welcome card, and the styled start page were built and unit-tested but not looked at; dark theme, Windows, and Linux were verified only through the token tests. | Visual pass of the welcome card, start page, dark theme, and the other platforms. |
| Arc corners are native patches, not view radii | `src/main/macos/page_card_mac.mm`, `src/main/browser_chrome.cc` | CEF 150 `cef_views` exposes no corner radius, clips button images to the button insets, and the Views compositor is opaque, so rounded fills and the rounded page card are drawn by a native macOS overlay (corner patches and a ring frame) on top of square views. Windows/Linux keep square corners and the platform frame. The interface-font choice applies to the sidebar and the internal pages; the palettes and the welcome card keep Geist. | Close if CEF exposes view corner radii or custom-drawn views. |
| Focus ring on the address pill stays square | `src/main/browser_chrome.cc` | The 1 px accent ring is a panel behind the pill, so its square corners show beside the rounded fill while the field has focus. | Same exit. |

### Peek card (0.9)

- The card is sized from the content area: 80% of its width and 85% of its height, centered, with
  minimums of 360 by 280 DIP (`peek_layout.h`), and it follows window resizes while open.
- Its two title-bar buttons sit on neutral `fill` plates (one step off the `surface_secondary`
  title bar, as the address well is): "Open as tab" carries a `plus` icon in the text tone with
  its label, and the close is an icon-only `×` named "Close Peek". They use the native hover and
  pressed ink and a neutral focus ring (the overlay ring, never the accent). No button in the card
  is filled with `text` or `accent`. CEF buttons cannot round their plate, which is accepted debt.
