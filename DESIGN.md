# Island Product Site Design System

## 0. Research Log

- Concrete reference: `browser.pen` canonical frames `01 iP7WU`, `02 Sl8RT`, `03 WWVGY`, `04 ppc35`, `05 ugsnW`, `06 zXkwR`, and `09 V20eP` are the sole visual source; frames `07 fMGeC` and `08 I3cB58` are rejected and excluded.
- Interaction reference: beui.dev `shared-layout-bg` source inspected; the site adapts its restrained state-feedback principle as an opacity/transform hover treatment rather than importing a motion dependency.
- Font assets: no self-hosted Geist files are currently available to this ownership unit; robust system fallbacks are used until the repository dependency setup vendors Geist.

## 1. Atmosphere & Identity

Island is a quiet, editorial browser workbench: warm paper behind carefully layered cool interfaces.
Its signature is the product shell—an off-white, lightly translucent sidebar anchored to a calm
browser canvas—rather than a literal island motif. The identity is precise, useful, and restrained:
one small mark, a deliberate teal action color, and generous space for the product to speak.

## 2. Color

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
| H2 | `--type-h2` | `clamp(24px, 3vw, 36px)` | 500 | 1.12 | Feature headings |
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
- **Structure:** skip link, `header > nav`, restrained logo, links, primary link.
- **States:** default, hover, focus-visible; no active route decoration on the landing page.
- **Accessibility:** semantic navigation; skip link exposes `main`; all links have clear labels.
- **Motion:** 160ms opacity/transform feedback, reduced-motion safe.

### Button link
- **Structure:** anchor with `.button` and optional inline SVG arrow.
- **Variants:** primary uses the canonical `--text` background and `--surface` foreground for AA
  contrast in both themes; text secondary uses `--text`. Accent remains the focus and detail color.
- **States:** default, hover, active, focus-visible.
- **Accessibility:** visible 3px accent focus ring; native anchor semantics.
- **Motion:** 160ms transform and color transition; no script required.

### Browser shell mockup
- **Structure:** landmark-free illustrative `div` with sidebar, chrome, tabs, and canvas; children
  are `aria-hidden` because narrative content describes the same concepts.
- **Variants:** focus workspace, split workspace, command panel, empty space.
- **Accessibility:** decorative UI does not create a duplicate keyboard path.
- **Motion:** one entrance fade/translate and intentional tab/sidebar hover cues.

### Feature narrative
- **Structure:** `article` with mono label, heading, copy, and a product fragment.
- **States:** static, since these are descriptive—not controls.
- **Accessibility:** headings preserve document outline; no hidden status claims.

### Footer
- **Structure:** `footer > nav` with product, privacy, and source links.
- **States:** default, hover, focus-visible.
- **Accessibility:** named navigation and readable legal copy.

## 6. Motion & Interaction

- `--motion-fast: 160ms`; `--motion-enter: 480ms`; `--ease-out: cubic-bezier(.16, 1, .3, 1)`.
- Links and buttons use transform/opacity/color feedback to confirm targeting. The browser shell’s
  staged entrance communicates that the product is a composed workspace, not a claim of live UI.
- No layout properties animate. `prefers-reduced-motion: reduce` cancels entrance motion and
  transitions while preserving all content and states.

## 7. Depth & Surface

Strategy: **mixed, restrained.** The canonical shell material is a `--surface-2` translucent panel
with a 1px `--border` edge, 18px background blur, and a light 0 16px 48px shadow. Ordinary text
sections remain open; no card-grid repetition or glass stack is used. Browser canvas surfaces use a
single border and a soft low-opacity shadow to read as a physical product window.

## 8. Accessibility Constraints & Accepted Debt

### Constraints
- WCAG 2.2 AA target: 4.5:1 body contrast, 3:1 large text, keyboard reachability, semantic
  landmarks, visible focus, skip link, no color-only information, and no auto-playing media.
- Content supports 320px width, browser zoom, and system font fallback. Product availability uses
  explicit “planned” or “coming soon” language rather than implied availability.
- Motion is optional under `prefers-reduced-motion`; no action depends on hover, motion, or JS.

### Accepted Debt
| Item | Location | Why accepted | Owner / Exit |
| --- | --- | --- | --- |
| Geist / Geist Mono absent | `site/assets/css/site.css` font stack | Vendor outputs are owned by dependency setup and are unavailable here. | Replace fallbacks with local `@font-face` once `assets/fonts/` exists. |
| Public Pages base URL | `site/404.html` | The repository remote is known, but its conventional project Pages URL currently returns 404. The 404 page is self-contained and links to the verified repository instead of assuming a deployed Pages path. | Restore a Pages home link only after the deployed base URL responds successfully. |
