# Island Browser — Sidebar Hover-Reveal and LLM Search Palette Design

## Status and baseline

This is the canonical, decision-complete design for the sidebar/palette feature. The baseline at
write time is Phase 3 mid-execution: the Phase 1/2 contracts hold (one `CefWindow`, popup
rejection, CEF close lifecycle, port `9222`, data-only startup), Phase 3's U1 is merged
(CEF-free `tab.h`/`space.h`/`tab_id.h` model, `SessionStore` with clean-quit round-trip, PR #9),
`BrowserChrome` carries the fixed-plus-collection view-tree contract with IDs `1001–1027` and
shape-and-count collection assertions (commits `5519d1d`, `f821468`), and the `BrowserWindow`
multi-space restructuring (Phase 3 U2) is in flight but not yet landed — `BrowserWindow` still
owns the single `browser_view_`/`browser_`/`navigation_state_` triad. See the
[Phase 3 design](2026-08-09-island-browser-phase3-design.md) and
[Phase 3 plan](../plans/2026-08-09-island-browser-phase3.md) for the surrounding contracts this
feature layers on top of and does not renegotiate.

This feature adds three behaviors on that baseline: an Arc-like left sidebar that hides to the
window edge and reveals when the cursor approaches it; a keyboard-invoked search palette
(`Cmd/Ctrl+K`); and palette "providers" that navigate the active tab to an LLM/search site with a
prefilled query through plain URL navigation — no API keys, no provider-side accounts, no
embeddings or completion logic.

## Scope

- **Sidebar hover-reveal.** The existing 286-DIP rail (`ChromeViewId::kRail = 1002`,
  `rail_width_dip = 286` in `design_tokens.h`) becomes hidden-by-default: in the hidden state the
  rail occupies 0 DIP of layout width and a 1–2-DIP vertical sliver marks the left edge; hovering
  near the edge reveals the rail at its full 286 DIP. `DESIGN.md`'s "always-visible 286 DIP rail"
  is superseded **only for the default visibility state**; the rail's revealed width, tokens, and
  internal structure are unchanged.
- **Search palette.** A single overlay inside the existing `CefWindow`, created lazily on first
  `Cmd/Ctrl+K`, hidden (never destroyed, never a second top-level `CefWindow`) on Escape or
  submission. It holds one query textfield and a fixed, ordered provider list.
- **Provider navigation.** Submitting the palette navigates the **active tab's** `CefBrowser` to
  `provider_base + "?q=" + PercentEncodeQuery(query)` via `CefFrame::LoadURL`. This is pure URL
  navigation: the project never talks to a provider API, stores no credentials, and gains no
  provider-specific knowledge beyond the base URL.
- **Keyboard fallback.** Because the pinned CEF has no hover or mouse callbacks (see below), a
  `Cmd/Ctrl+B` toggle reveals/hides the sidebar deterministically on every platform, regardless of
  whether a native hover seam exists there.

## Explicit non-goals

- **No API keys, tokens, or provider accounts.** Nothing phones home from Island; the provider
  site itself performs the search.
- **No second top-level `CefWindow`.** The palette is an overlay on the one existing window; this
  is a hard invariant, not a default.
- **No web content embedded in the palette** (no `CefBrowserView`, no WebView, no preview
  iframes): the palette is `cef_views` chrome only.
- **No layout-property animation of the sidebar.** Reveal/hide is an instantaneous rail-width
  change (0 ↔ 286 DIP). If motion is added later it must be a composited transform, never an
  animated layout width; this feature ships without animation.
- **No hover-reveal requirement on Windows or Linux in this feature** (see Boundaries — the seam
  is defined, the non-macOS implementations are deferred; `Cmd/Ctrl+B` covers them meanwhile).
- **No change** to the address-bar URL policy (`AddressModel`/`cef_address_parser`/allow-list),
  startup `data:` pages, popup rejection, session restore, or the command-palette-in-Phase-3-U7
  tab/space switching scope. This palette is a *search* palette; Phase 3's command palette (tabs,
  spaces, go-to-URL) remains Phase 3 U7's scope and is not touched or preempted here.

## Architecture

### Boundaries

```text
BrowserWindow (one CefWindow)
├── BrowserChrome (kRail: width 0 hidden / 286 revealed; internal structure unchanged)
│   └── Hover-sliver overlay (kHoverSliver, window-level overlay, not a rail child)
├── Hover seam (platform-native; macOS now, Windows/Linux deferred)
│   └── posts SetSidebarRevealed(bool) onto the CEF UI thread
├── SearchPalette (window overlay, AddOverlayView + CEF_DOCKING_MODE_CUSTOM + can_activate=true)
│   ├── query CefTextfield (focus anchor, traps Tab)
│   └── provider rows projected from the static SearchProvider table
└── ActiveTabProvider seam (interface; BrowserWindow implements)
    └── palette submission → provider.ActiveBrowser()->GetMainFrame()->LoadURL(url)
```

- **Chrome boundary.** `BrowserChrome` keeps the Phase 3 view-tree contract byte-for-byte. Sidebar
  reveal changes runtime *geometry* (rail width, content x-offset via the existing
  `LayoutForBounds()` computation, which already derives content from rail width), not tree shape.
  The reveal state lives on `BrowserWindow` (`sidebar_revealed_`) and is applied through the
  existing `RootPanelDelegate` layout path — no second layout implementation.
- **Intermediate-build rule.** Until the sliver and toggle land (U4), the sidebar behavior is
  exactly today's always-visible 286-DIP rail — `sidebar_revealed_` is constructed `true`, so no
  intermediate unit ever ships a window whose rail cannot be seen. The hidden/sliver state becomes
  reachable only when U4 adds the `Cmd/Ctrl+B` toggle that makes it escapable.
- **Platform-seam boundary.** The pinned CEF exposes **no** hover/mouse callbacks in
  `cef_view_delegate.h`, `cef_window_delegate.h`, or `cef_browser_view_delegate.h` (verified
  against the vendored distribution). Hover therefore enters through one platform-native object per
  OS, isolated from all chrome code: on macOS, an `NSTrackingArea`/`NSEvent` watch on the CEF
  window's `NSView` posts `BrowserWindow::SetSidebarRevealed(bool)` onto the CEF UI thread
  (all mutation stays single-threaded, CEF-UI-only). The seam's C++-visible contract —
  `SetSidebarRevealed(bool)` plus a small per-OS install hook — is fixed by this spec even for the
  deferred platforms, so later seams drop in without touching chrome or window code.
- **Palette boundary.** The palette is created by
  `window->AddOverlayView(palette_panel, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true)`
  (`cef_window.h`, overlays are positioned via the returned `CefOverlayController` and are hidden
  by default). It is lazily created on first `Cmd/Ctrl+K` and hidden thereafter — never destroyed,
  never re-parented, never a separate window.
- **Provider-nav boundary.** Palette submission goes `SearchPalette → BrowserWindow::
  SubmitSearchQuery(query, provider) → ActiveTabProvider::ActiveBrowser() → GetMainFrame()->
  LoadURL(...)` and then hides the palette. `ActiveTabProvider` is an interface with one method
  returning the active tab's `CefBrowser` (nullable): before Phase 3 U2 lands, `BrowserWindow`
  implements it from its single `browser_` member; after U2 lands, the same implementation reads
  the active space's active tab. The palette never holds a raw `CefBrowser` reference.

### ChromeViewId and ViewTreeContract compatibility

`1001–1018` (Phase 2) and `1019–1027` (Phase 3 collection regions) are hard invariants — never
renumbered, never reused. New IDs are allocated after `1027`, in this fixed allotment:

| ID | Symbol | Role |
| --- | --- | --- |
| 1028 | `kHoverSliver` | edge sliver overlay (window overlay, not a rail child) |
| 1029 | `kSearchPalette` | palette overlay root panel |
| 1030 | `kSearchPaletteQuery` | query textfield |
| 1031 | `kSearchPaletteProvider` | one provider row (repeated, identified per row) |
| 1032 | `kSearchPaletteProviderName` | provider row label |

Compatibility rules: `ViewTreeContract()` is **unchanged** — the hover sliver and the palette are
window-level overlays and are therefore not children of the rail or the root panel, so the
`kRoot → {kRail, kBrowserContent}` shape, the `children.size() == 2` assertion in
`tests/integration/browser_window_chrome_contract_test.cc`, and the shape-and-count collection
assertions (`f821468`) all remain green. The hidden default state keeps the rail present at 0 DIP
width (it is removed from layout, not from the tree). `ChromeGeometrySnapshot` gains no required
fields; if a unit records sliver/palette geometry it does so via new snapshots, never by
repurposing existing ones.

### Input, commands, and accessibility

- `Cmd/Ctrl+K` opens the palette, registered exactly like the existing accelerators:
  `window_->SetAccelerator(kOpenPaletteAccelerator, 'K', /*shift=*/false, /*ctrl=*/true,
  /*alt=*/false, /*high_priority=*/true)` (`browser_window.h` `AcceleratorId` enum, dispatched in
  `BrowserWindow::OnAccelerator`; `ctrl_pressed=true` yields Cmd on macOS and Ctrl elsewhere, per
  the existing `R`/`L` precedent). `high_priority=true` so it works while web content has focus.
- `Cmd/Ctrl+B` toggles sidebar reveal/hide using the same registration pattern. `B` is unused by
  the existing accelerator set (`Left`, `Right`, `F5`, `R`, `L`).
- The palette **traps focus while visible**: on open the query field receives focus; `Tab` cycles
  only within the palette; `ArrowUp`/`ArrowDown` move the highlighted provider; `Enter` submits the
  highlighted provider; `Escape` hides the palette with **no** navigation and restores focus to the
  invocation point (browser view by default). Key interception follows the existing
  `BrowserChrome::HandleAddressKeyEvent` pattern (`CefTextfieldDelegate::OnKeyEvent`).
- The sidebar has a keyboard path on every platform (`Cmd/Ctrl+B`), so hover-reveal is an
  enhancement, not an accessibility dependency. Every palette row is keyboard-reachable and gets an
  accessible name ("Search with <provider> for <query>") matching the Phase 2 rail accessibility
  bar.
- Sidebar reveal/hide edge behavior (macOS seam): reveal when the cursor enters the leftmost
  12-DIP band of the window while hidden; hide again when the cursor leaves the revealed rail by
  more than a 16-DIP grace band while it was hover-revealed. A `Cmd/Ctrl+B`-pinned reveal stays
  revealed until toggled off; hover never overrides an explicit toggle (toggle state wins, hover
  only applies to the unpinned hidden state).

## Providers

The provider table is static, in-code, ordered, and contains no secrets. The five entries and
their query-URL formats are **inline assumptions of this spec** (manually spot-checked at U7 QA
time; unit-locked once adopted):

| Provider | Query URL (submitted = base + `q=<PercentEncodeQuery(query)>`) |
| --- | --- |
| ChatGPT | `https://chatgpt.com/?q=` |
| Perplexity | `https://www.perplexity.ai/search?q=` |
| Claude | `https://claude.ai/new?q=` |
| Gemini | `https://gemini.google.com/app?q=` |
| Google | `https://www.google.com/search?q=` |

All five are HTTPS. A provider entry is `(id, display name, base URL)`; the composed URL is always
`base + "?q=" + PercentEncodeQuery(query)`. Provider order is the table order above and is part of
the testable contract. Adding/removing providers is a table edit plus a unit update — no chrome
change. Empty/whitespace-only queries are rejected in the palette (no navigation, no error page);
this is the only validation the palette performs — it deliberately does **not** reuse
`AddressModel`/`cef_address_parser`, which govern *address* submission, not provider query
composition.

## Percent-encoding rules

The repository has no percent-encoder; this spec defines one. `island::PercentEncodeQuery(
std::string_view) -> std::string`:

1. Input is UTF-8 (`CefTextfield` text is UTF-8); each UTF-8 byte is encoded independently.
2. RFC 3986 *unreserved* characters pass through verbatim: `A–Z`, `a–z`, `0–9`, `-`, `.`, `_`,
   `~`.
3. Every other byte is encoded as `%XX` with **uppercase** hex digits (e.g. `/` → `%2F`, never
   `%2f`); spaces become `%20`, never `+`.
4. Encoding is total and deterministic for any byte sequence (including invalid UTF-8), and its
   output consists only of unreserved characters and `%`, so it is always safe to embed directly in
   a `https:` URL with no further escaping.

Composition never hand-concatenates untrusted text into a URL elsewhere: submission builds exactly
`base`, `"?q="`, and the encoded query, then hands the string to `CefFrame::LoadURL`.

## Startup/network posture

"Startup must not request external network resources" remains in force unchanged. Provider
navigation is an **explicit, user-requested exception**: the first external request for a provider
URL can only occur after the user opens the palette, types, and submits. No startup path, prewarm,
or prefetch of provider URLs exists or may be added by this feature.

## Testable acceptance requirements

1. `PercentEncodeQuery` unit tests: unreserved passthrough; reserved and multibyte UTF-8 inputs
   produce exact expected `%XX` sequences (uppercase); empty input → empty output.
2. Provider-table tests: exactly five providers in the order above; for a fixed query
   (including spaces and Unicode), each composed URL equals the spec's base + encoded query
   byte-for-byte.
3. Palette model tests: lazy create, Escape hides without navigation, submission produces the
   single expected `(provider, URL)` request and hides, empty query rejects, null
   `ActiveBrowser()` is a defined no-op (palette stays open, nothing navigates).
4. Window invariant: at no point does the feature create a second `CefWindow`; the palette and
   sliver are overlays reported through `CefOverlayController`.
5. Contract regression: `ViewTreeContract()` unchanged;
   `tests/integration/browser_window_chrome_contract_test.cc` and the `BrowserChrome` contract
   suite pass unmodified.
6. Sidebar (after U4 lands): hidden state = rail 0 DIP + visible sliver; revealed state = rail
   286 DIP, sliver hidden; `Cmd/Ctrl+B` toggles both states; `LayoutForBounds` content x-offset
   tracks rail width; before U4 lands the rail is constructively revealed (intermediate-build
   rule).
7. `Cmd/Ctrl+K` registration matches the stated `SetAccelerator` parameters; palette focus trap
   (Tab containment, focus restore on close) per the accessibility rules.
8. Theme: after a `window->ThemeChanged()` cascade, palette surfaces re-assert their token
   backgrounds (U6 guard), matching PR #10's `SurfacePanelDelegate::OnThemeChanged` re-assertion
   pattern in `browser_chrome.cc`.
9. All Phase 1/2 tests and merged Phase 3 units remain green; startup remains data-only.
