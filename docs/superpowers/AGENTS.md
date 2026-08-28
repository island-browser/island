<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# superpowers

## Purpose

Container for the phase record. Every unit of work in this repository is a *phase* with exactly two
documents: a design spec that fixes the contract, and a plan that sequences the implementation.
They are paired by date and slug.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `specs/` | Design specs — what a phase must be (see `specs/AGENTS.md`) |
| `plans/` | Implementation plans — how it gets built (see `plans/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Read the spec first, then the plan, before implementing anything.
- Implement only the current accepted phase. Phase 2+ work requires its own spec and plan.
- Filenames are `YYYY-MM-DD-<slug>.md` in `plans/` and `YYYY-MM-DD-<slug>-design.md` in `specs/`.
  A spec without a matching plan is not ready to be implemented.
- Documents for completed phases are historical context. Do not edit them to match what the code
  later became.

## Dependencies

### Internal

- The root `AGENTS.md` names which phase is currently accepted.

<!-- MANUAL: -->
