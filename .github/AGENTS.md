<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# .github

## Purpose

GitHub automation. This directory matters more than usual here: the root `AGENTS.md` states that
every target except macOS arm64 requires **native GitHub Actions evidence** before its support
claim can change, so these workflows *are* the evidence chain.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `workflows/` | Every workflow and what each one proves (see `workflows/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Do not upgrade a claim in `docs/supported-platforms.md` on the basis of a workflow *existing*. A
  green run on the matching native runner is the evidence.
- Actions are pinned by commit SHA, not by tag. Keep it that way when adding a step.

## Dependencies

### Internal

- `docs/supported-platforms.md`, `docs/release-process.md`, `scripts/`, `tests/`

<!-- MANUAL: -->
