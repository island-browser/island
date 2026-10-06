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
active space's marker, which casts a faint tint over the sidebar and hairlines (Arc's idea, kept
minimal). The product shell — a sidebar of exactly 286 DIP beside the page on a floating card —
is the signature, drawn with 1px hairlines rather than shadows or glass. Honesty still rules: no
surface depicts a feature that does not exist in the build.

## 2. Color

### Canonical token contract

The fenced `design-tokens` block below is the single machine-readable source of truth for the Island
design contract. The tables in this section document it in human-readable form; the native runtime
tokens (`src/main/design_tokens.cc`) and the drift tests (`tests/design/test_token_contract.py`,
`tests/design_tokens_test.cpp`) must stay consistent with it, and the product site checks its own
CSS variables against it. Any change to these values must update the block and pass the drift tests
before it ships. Colors are opaque `#RRGGBB` hex; spacings, radii, and the rail width are DIP
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
cast — 4–5% on the background, 7–8% on the sidebar, 12–14% on hairlines — and never changes text.

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
(cards, panels, the page card). The sidebar is 286 DIP; the page sits on a card inset by
`--space-3` from the window edge.

## 5. Components

- **Sidebar (native):** `--surface-2` with the space cast; compact address field, pinned-tab tray,
  tab list (active tab = `--surface` with a hairline), footer with space dots and icon buttons.
- **Page card (native):** `--surface` with a 1px `--border` and radius `--radius-md`.
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

Flat by default. Depth comes from 1px hairlines and surface steps (`--bg` → `--surface-2` →
`--surface`). The only shadow is the product window / page card: `0 1px 2px` plus `0 8px 24px`,
both low-opacity neutral. No glass stacks, no glow, no gradients.

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
| Native Graphite pass unverified | `src/main/browser_chrome.cc` | The Graphite recolor reaches the native chrome through tokens only; it has not been looked at in a running build (CEF downloads are blocked in the authoring environment). | Visual pass on the first native build. |
