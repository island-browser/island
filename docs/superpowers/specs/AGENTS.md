<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# specs

## Purpose

Design specs. Each fixes the contract for one phase — scope, seams, invariants, and what is
explicitly out of scope — before any code is written.

## Key Files

| File | Description |
|------|-------------|
| `2026-08-07-island-browser-phase0-design.md` | Phase 0 infrastructure — historical |
| `2026-08-08-island-browser-phase1-design.md` | Phase 1: single window, single `CefBrowserView` — the accepted contract |
| `2026-08-08-island-browser-phase2-design.md` | Phase 2 chrome |
| `2026-08-09-island-browser-phase3-design.md` | Phase 3 chrome |
| `2026-08-10-island-search-phase-s0-design.md` | Search S0 primitives — code landed under `src/search/`, opt-in only |
| `2026-08-12-island-sidebar-palette-design.md` | Sidebar + command palette — accepted spec, not yet implemented |

## For AI Agents

### Working In This Directory

- `2026-08-08-island-browser-phase1-design.md` is the document the root `AGENTS.md` points at as
  authoritative; Phase 0 is historical context only.
- A spec is read-only once its phase is accepted. Corrections to a shipped phase go in a new spec,
  not as edits to the old one.
- The sidebar/palette spec is accepted but **not implemented**: no code on `main` corresponds to
  it, and `browser_chrome.h` still stops at `ChromeViewId` `1027`. It is being implemented on a
  separate lane — do not start on it, and do not describe it as existing behaviour. Three decisions
  in it are worth knowing: the palette is
  `AddOverlayView(panel, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true)` on the one existing
  window, explicitly **not** a second top-level `CefWindow`; the pinned CEF exposes no hover or
  mouse callbacks, so hover enters through one platform-native seam (macOS now, Windows/Linux
  deferred) with `Cmd/Ctrl+B` as the cross-platform keyboard path; and it allocates
  `ChromeViewId` `1028` (`kHoverSliver`) and `1029` (`kSearchPalette`) — after the existing
  `1001`–`1027` range, leaving `ViewTreeContract()` unchanged because both are window-level
  overlays rather than children of the rail or root panel.
- Every spec here must have a matching plan in `../plans/` before implementation starts.

## Dependencies

### Internal

- `../plans/` — the paired implementation plans

<!-- MANUAL: -->
