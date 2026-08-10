# Island Product Site Design System

## 0. Research Log

- Accepted design: Ledger (recorded by the shared design-contract unit).
- Concrete reference: `browser.pen` canonical frames `01 iP7WU`, `02 Sl8RT`, `03 WWVGY`, `04 ppc35`, `05 ugsnW`, `06 zXkwR`, and `09 V20eP` are the sole visual source; frames `07 fMGeC` and `08 I3cB58` are rejected and excluded.
- Interaction reference: beui.dev `shared-layout-bg` source inspected; the site adapts its restrained state-feedback principle as an opacity/transform hover treatment rather than importing a motion dependency.
- Font assets: no self-hosted Geist files are currently available to this ownership unit; robust system fallbacks are used until the repository dependency setup vendors Geist.

## 1. Atmosphere & Identity

Island is a quiet, editorial browser: warm paper behind a carefully layered cool interface. Its
signature is the product shell—an off-white, lightly translucent left rail of exactly 286 DIP
anchored to a calm browser canvas—rather than a literal island motif. The identity is precise,
useful, restrained, and honest: the site presents only what the running Phase 2 build actually
does. One small mark, a deliberate teal action color used only for real actions and active state,
ledger-ruled hairline sections, and generous space for the product to speak. Nothing on the page
describes a feature that does not exist in the build.

## 2. Color

### Canonical token contract

The fenced `design-tokens` block below is the single machine-readable source of truth for the Island
design contract. The tables in this section document it in human-readable form; the native runtime
tokens (`src/main/design_tokens.cc`), the product-site stylesheet (`site/assets/css/site.css`), and
the drift tests (`tests/design/test_token_contract.py`, `tests/design_tokens_test.cpp`) must stay
consistent with it. Any change to these values must update the block and pass the drift tests before
it ships. Colors are opaque `#RRGGBB` hex; spacings, radii, and the rail width are DIP values. The
site-only marker colors are part of the contract because both the shared design surface and the
browser chrome rely on them.

```design-tokens
{
  "contract": "island-design-tokens",
  "version": 1,
  "accepted_design": "Ledger",
  "colors": {
    "light": {
      "background": "#F3F0E9",
      "surface": "#FFFEFB",
      "surface_secondary": "#ECE9E2",
      "text": "#18303A",
      "text_secondary": "#687A7D",
      "border": "#D8D8D0",
      "accent": "#168C99"
    },
    "dark": {
      "background": "#0D1B26",
      "surface": "#142633",
      "surface_secondary": "#1B3040",
      "text": "#EAF3F3",
      "text_secondary": "#9CB0B5",
      "border": "#29414E",
      "accent": "#168C99"
    },
    "site_only": {
      "space_blue": "#168C99",
      "space_coral": "#7B8588",
      "space_green": "#A4AAA9"
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
    "radius_small": 8,
    "radius_medium": 12
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
    "blur_px": 18,
    "shadow": "0 16px 48px"
  },
  "depth": {
    "product_window": "1px border plus one soft low-opacity shadow; no glass stack",
    "text_sections": "open; no card grids"
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
    "phase3_exclusions": "tabs, tab strip, spaces, split view, command palette, and session restore are Phase 3 concerns and do not exist in the shared design contract",
    "no_recolor": "this unit formalized the existing shared tokens; it did not recolor them",
    "no_literal_imagery": "no photos, tropical colors, or generic AI gradients"
  }
}
```

### Documented colors

| Role | Token | Light | Dark | Usage |
| --- | --- | --- | --- | --- |
| Background | `--bg` | `#F3F0E9` | `#0D1B26` | Page field |
| Surface | `--surface` | `#FFFEFB` | `#142633` | Browser and content surfaces |
| Surface 2 | `--surface-2` | `#ECE9E2` | `#1B3040` | Sidebar and secondary panels |
| Text | `--text` | `#18303A` | `#EAF3F3` | Headlines and body |
| Text 2 | `--text-2` | `#687A7D` | `#9CB0B5` | Retained canonical token; secondary UI detail in dark mode only |
| Border | `--border` | `#D8D8D0` | `#29414E` | Dividers and outlines |
| Accent | `--accent` | `#168C99` | `#168C99` | Links, CTA, focus, active state |
| Space blue | `--space-blue` | `#168C99` | `#168C99` | Focus space marker |
| Space coral | `--space-coral` | `#7B8588` | `#7B8588` | Research space marker |
| Space green | `--space-green` | `#A4AAA9` | `#A4AAA9` | Personal space marker |

Rules: accent only signifies an action or active state. In light mode, all readable text—including
14–16px body copy, navigation, captions, and labels—uses `--text` (`#18303A`). `--text-2`
(`#687A7D`) remains a canonical token for dark-theme secondary detail but is not used by the
light-theme product site because it does not meet the 4.5:1 contrast floor on all assigned surfaces.
No literal imagery, photos, tropical colors, or generic AI gradients. The only non-token
transparency is the shell material: `--surface-2` at 90% over its assigned background.

## 3. Typography

Primary is `Geist, ui-sans-serif, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif`; mono
is `Geist Mono, ui-monospace, SFMono-Regular, Menlo, monospace`. Geist is intentionally not fetched
by this product-site unit. The fallback stack keeps the site legible until repository setup provides
local font files.

| Level | Token | Size | Weight | Line height | Usage |
| --- | --- | --- | --- | --- | --- |
| Display | `--type-display` | `clamp(40px, 6vw, 72px)` | 500 | 0.98 | Hero |
| H1 | `--type-h1` | `clamp(32px, 4vw, 52px)` | 500 | 1.04 | Major headings |
| H2 | `--type-h2` | `clamp(24px, 3.2vw, 40px)` | 500 | 1.12 | Feature headings |
| Body | `--type-body` | 16px | 400 | 1.55 | Default copy |
| Body small | `--type-small` | 14px | 400 | 1.5 | Supporting copy |
| Mono label | `--type-mono` | 12px | 600 | 1.4 | Labels and keys |

## 4. Spacing & Layout

Base unit is 4px. Tokens: `--space-1: 4px`, `--space-2: 8px`, `--space-3: 12px`,
`--space-4: 16px`, `--space-6: 24px`. Section cadence uses responsive `clamp()` mechanics with
these steps as its intent. Content caps at `--content: 1200px`; gutters are `--gutter:
clamp(16px, 4vw, 48px)`. The desktop browser mockup follows the canonical 286px sidebar / flexible
canvas grammar. At <768px it becomes a single readable canvas and the sidebar is represented as a
compact strip; at <480px secondary navigation yields to the primary CTA.

Radii: `--radius-sm: 8px`, `--radius-md: 12px`; browser product windows are intentionally a larger
composition only where needed, not a new reusable radius scale.

## 5. Components

### Header and navigation
- **Structure:** skip link, `header > nav`, restrained logo, links, primary action link.
- **States:** default, hover, focus-visible; the current page's own link carries `aria-current="page"` and a static accent underline (site index and privacy pages).
- **Accessibility:** semantic navigation; skip link exposes `main`; all links have clear labels.
- **Motion:** 160ms opacity/transform feedback, reduced-motion safe. No scroll-progress indicator.

### Button link
- **Structure:** anchor with `.button` and optional inline SVG arrow.
- **Variants:** primary uses the canonical `--text` background and `--surface` foreground for AA contrast in both themes; text secondary uses `--text`. Accent remains the focus and active-state color only.
- **States:** default, hover, active, focus-visible.
- **Accessibility:** visible 3px accent focus ring with 4px offset; native anchor semantics.
- **Motion:** 160ms transform and color transition; no script required.

### Browser rail mockup (hero)
- **Structure:** illustrative `div.browser` rendering the real `BrowserChrome::ViewTreeContract` — a `286px` left rail beside a single browser canvas. The rail holds, in order: window controls, a navigation row (Back, Forward), an address row (Location, Address, Reload), a divider, and one active-page row. Children are `aria-hidden` because the surrounding copy describes the same working controls.
- **Glyphs:** the four locked Lucide source glyphs — `chevron-left` (Back), `chevron-right` (Forward), `rotate-cw` (Reload), and `globe-2` (Location and the active-page fallback favicon) — drawn with `stroke="currentColor"` from an inline SVG sprite.
- **Content fidelity:** the address row shows the current page (the data page, display text "Island"); the canvas shows the real local starting page (`Island Browser`). No tabs, spaces, split view, command surface, or dashboard are depicted.
- **Accessibility:** decorative shell does not create a duplicate keyboard path; it is `aria-hidden="true"` with rendering detail kept off the accessibility tree.
- **Motion:** one soft fade-up entrance (`--motion-enter`); no tilt, no glow, no hover coupling.

### Ledger-ruled section (the rail)
- **Structure:** `.rail-list` of `.lift` rows ruled top and bottom by 1px `--border` hairlines. Each row pairs a small mono index with a heading beside a short paragraph and a status chip.
- **States:** static, descriptive; the row hover lifts its background with the translucent `--surface-2` shell material to confirm targeting only.
- **Accessibility:** rows are `article`s; headings preserve the document outline; status chips state "working in the build" or "planned" in text, not by color alone.
- **Honesty rule:** every row describes a control that exists and works in the current build; nothing here is a concept or a promise.

### Status chip
- **Structure:** pill with a 6px marker dot and mono uppercase label.
- **Variants:** `--active` (accent dot) means working in the current build; `--planned` (muted `--space-coral` dot) means planned, not yet available.
- **Accessibility:** the marker dot is redundant; the label carries the meaning.

### Footer
- **Structure:** `footer > nav` with product, privacy, and source links.
- **States:** default, hover, focus-visible.
- **Accessibility:** named navigation and readable legal copy.

## 6. Motion & Interaction

- `--motion-fast: 160ms`; `--motion-enter: 480ms`; `--motion-reveal: 700ms`; `--ease-out: cubic-bezier(.16, 1, .3, 1)`.
- Motion is **meaningful only**. Links and buttons use transform/opacity/color feedback to confirm targeting. Section content gets a single soft fade-up entrance (`reveal`) so the page composes calmly rather than popping in. Every animation maps to a real state change or entrance; nothing moves for decoration alone.
- **Removed as slop / dishonest motion:** 3D tilt (`rotateY`/`rotateX`), radial hero glow, ghost numerals, scroll-progress bar, and any hover that changes nothing. These implied a product and a polish the build does not have.
- Only composited properties animate (`transform`, `opacity`, `filter`); no layout properties animate.
- `prefers-reduced-motion: reduce` cancels entrance motion and transitions while preserving all content and states.

## 7. Depth & Surface

Strategy: **mixed, restrained, honest.** The canonical shell material is a `--surface-2` translucent panel (90% over its assigned background) with a 1px `--border` edge, 18px background blur, and a light 0 16px 48px shadow. Ordinary text sections stay open; there are no card grids and no glass stack. The hero browser mock reads as a single physical product window — one border plus one soft low-opacity shadow — with no tilt or layered glow. Ledger sections are separated by flat 1px hairlines alone; depth is reserved for the one place it means something, the product window.

## 8. Accessibility Constraints & Accepted Debt

### Constraints
- WCAG 2.2 AA target: 4.5:1 body contrast, 3:1 large text, keyboard reachability, semantic
  landmarks (`header`/`nav`/`main`/`footer`), visible focus, a skip link, no color-only
  information, and no auto-playing media.
- The current page's navigation link carries `aria-current="page"` on each page. The decorative
  hero browser shell is `aria-hidden="true"`; its real controls are described in adjacent copy.
- Status is stated in text ("working in the build" / "planned"), never by color alone.
- Content supports 320px width, browser zoom, and system font fallback. Product availability uses
  explicit "in development" or "planned" language rather than implied availability.
- Motion is optional under `prefers-reduced-motion`; no action depends on hover, motion, or JS.

### Accepted Debt
| Item | Location | Why accepted | Owner / Exit |
| --- | --- | --- | --- |
| Geist / Geist Mono absent | `site/assets/css/site.css` font stack | Vendor outputs are owned by dependency setup and are unavailable here. | Replace fallbacks with local `@font-face` once `assets/fonts/` exists. |
| CEF theme parity gap | `src/main/browser_window.cc` dark detection | The browser chrome derives its light/dark tokens from the OS via the CEF window theme, which has no automated visual verification yet (`docs/phase2-visual-acceptance.md`). Values themselves are drift-guarded by `tests/design_tokens_test.cpp` and `tests/design/test_token_contract.py`. | Close the gap with the native visual acceptance evidence once the manual checklist is run on real hardware. |
| Product-window corner rounding | `site/assets/css/site.css` browser mockup | The hero browser product window intentionally uses a rounded corner treatment as an illustrative composition, not as an entry in the reusable radius scale. | Promote one only when the contract explicitly needs it. |
| Public Pages base URL | `site/404.html` | The repository remote is known, but its conventional project Pages URL currently returns 404. The 404 page is self-contained and links to the verified repository instead of assuming a deployed Pages path. | Restore a Pages home link only after the deployed base URL responds successfully. |
