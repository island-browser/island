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
      "control_height_dip": 32,
      "compact_control_height_dip": 26,
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
  `well` pill (the outline turns `--accent` while the field has focus), then a quiet New Tab row,
  then the tab list — rows on the canvas, the selected tab a soft `selected` fill, the hovered
  one `hover`, no outline — and a footer with a quiet space chip and icon buttons. Favicons sit
  on a 22 DIP rounded plate (`plate`) so dark favicons such as GitHub's stay visible on a dark
  sidebar. Chrome text uses the interface font (Geist, the system font, or an installed family).
  CEF `cef_views` has no corner-radius API and clips button images to the button's insets, so the
  soft rounded fills are square fills whose four corners a native macOS overlay repaints in the
  canvas color (`CornerPatch` / `ApplyCornerPatches`, macos/page_card_mac.mm; the chrome pushes
  them on selection, hover, relayout and scroll). Elsewhere the fills stay square. CEF resets
  custom view colors on every theme change, so each chrome view reports the reset and the chrome
  repaints from the resolved tokens (`BrowserChrome::ReapplyThemeColors`, `TabRowColors`,
  `SpaceChipColors`).
- **Collapsed icon rail (native):** Cmd/Ctrl+B swaps the 286 DIP sidebar for a 52 DIP rail on the
  same `canvas` surface, never hiding the tabs. One 32 DIP square button per tab of the
active space (16 DIP favicon on its plate, globe fallback), pinned tabs first with a hairline
between the groups; the active tab is the same soft `selected` fill as the selected row. A quiet "+" and the spaces (round dots; the active one wears a ring, so state is a
  shape and never color alone) sit at the bottom. The tab title is the tooltip and the accessible
  name, buttons are keyboard focusable, and the list scrolls vertically (buttons plus a classic
  scrollbar always fit the 52 DIP). There is no edge sliver or hover reveal. Cmd/Ctrl+L expands
  the sidebar for the length of the address edit. Every rail view re-asserts its colors through
  the same theme-reset delegates (`BrowserChrome::ApplyCollapsedRailTheme`).
- **Welcome card (native overlay):** a `--surface` card with a 1px `--border` outline and
  spacing_6 padding: Geist title and lede, mono uppercase section labels (APPEARANCE, IMPORT), a
  three-segment appearance control (selected = `--surface-2` fill, `--accent` text), one outlined
  toggle row per readable browser (checked state is a check mark as well as the fill), a wrapped
  muted note for unreadable browsers, and a right-aligned footer with a secondary Import button
  and the primary Start browsing button (`--text` fill, `--surface` text).
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
- **Internal pages (agent, Settings, All tabs):** flat `--surface` cards with 1px hairlines,
  `--radius-md`; controls at `--radius-sm`; section labels in mono uppercase `--text-2`; primary
  buttons use `--text` as background with `--surface` foreground; the accent marks focus and the
  active/selected state only.
- **Keyboard keys:** Geist Mono 11–12px on `--surface` with a 1px border and a 2px bottom border.
- **Status chips:** mono uppercase label plus a 6px dot; meaning is carried by the label, never by
  color alone.

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
  keyboard reachability, semantic landmarks, visible 2–3px accent focus rings, no color-only
  information, no auto-playing media.
- Status is stated in text, never by color alone. Decorative product mockups are `aria-hidden`.
- Motion is optional under `prefers-reduced-motion`; no action depends on hover, motion, or JS.

### Accepted Debt
| Item | Location | Why accepted | Owner / Exit |
| --- | --- | --- | --- |
| CEF theme parity gap | `src/main/browser_window.cc` dark detection | The chrome derives light/dark from the OS via the CEF window theme, which has no automated visual verification yet. Values are drift-guarded by `tests/design_tokens_test.cpp` and `tests/design/test_token_contract.py`. | Close with native visual acceptance evidence on real hardware. |
| Native Graphite pass partly verified | `src/main/browser_chrome.cc`, `src/main/welcome_flow.cc`, `src/main/pages/start.html` | The macOS light-theme sidebar was inspected in a running build through an in-process window capture (the sandbox cannot use screen recording), up to the theme-reset repaint. The final address-field holder, space-chip widths, the restyled welcome card, and the styled start page were built and unit-tested but not looked at; dark theme, Windows, and Linux were verified only through the token tests. | Visual pass of the welcome card, start page, dark theme, and the other platforms. |
| Arc corners are native patches, not view radii | `src/main/macos/page_card_mac.mm`, `src/main/browser_chrome.cc` | CEF 150 `cef_views` exposes no corner radius, clips button images to the button insets, and the Views compositor is opaque, so rounded fills and the rounded page card are drawn by a native macOS overlay (corner patches and a ring frame) on top of square views. Windows/Linux keep square corners and the platform frame. The interface-font choice applies to the sidebar and the internal pages; the palettes and the welcome card keep Geist. | Close if CEF exposes view corner radii or custom-drawn views. |
| Focus ring on the address pill stays square | `src/main/browser_chrome.cc` | The 1 px accent ring is a panel behind the pill, so its square corners show beside the rounded fill while the field has focus. | Same exit. |
