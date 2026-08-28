<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# plans

## Purpose

Implementation plans. Each sequences the work for one phase into ordered, individually verifiable
steps, against the contract fixed by the matching spec in `../specs/`.

## Key Files

| File | Description |
|------|-------------|
| `2026-08-07-island-browser-phase0.md` | Phase 0 infrastructure — historical |
| `2026-08-08-island-browser-phase1.md` | Phase 1 — the accepted plan |
| `2026-08-08-island-browser-phase2.md` | Phase 2 chrome |
| `2026-08-09-island-browser-phase3.md` | Phase 3 chrome |
| `2026-08-10-island-search-phase-s0.md` | Search S0 — code landed under `src/search/`, opt-in only |
| `2026-08-12-island-sidebar-palette.md` | Sidebar + command palette — accepted plan, not yet implemented |

## For AI Agents

### Working In This Directory

- Read the paired spec in `../specs/` first. A plan on its own does not carry the contract.
- Plans are ordered and each step is meant to end in a verifiable state. Do not batch steps to save
  a build; the verification points are the value.
- A plan for a completed phase is a record, not a live checklist. Do not edit it to reflect what was
  built instead.

## Dependencies

### Internal

- `../specs/` — the paired design specs

<!-- MANUAL: -->
