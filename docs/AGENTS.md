<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# docs

## Purpose

Process documentation and the phase spec/plan archive. Two different kinds of document live here:
the flat files below are standing policy, while `superpowers/` holds the dated, immutable record of
each accepted phase.

## Key Files

| File | Description |
|------|-------------|
| `supported-platforms.md` | Which targets are claimed supported and what evidence backs each claim |
| `dependency-update-process.md` | How a lock-file bump is proposed, verified, and accepted |
| `release-process.md` | Release gating, including the signing/notarization blocker |
| `phase2-visual-acceptance.md` | Phase 2 visual acceptance criteria and evidence template |
| `phase3-visual-acceptance.md` | Phase 3 visual acceptance criteria and evidence template |

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `superpowers/` | Dated phase specs and plans (see `superpowers/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- `supported-platforms.md` is a claims document. Only macOS arm64 has local build/run/package
  evidence; every other target needs native GitHub Actions evidence before its row changes. Do not
  upgrade a claim because the code looks portable.
- The visual acceptance documents define the evidence template that the checklists in
  `tests/manual/` are walked against. Keep the pairs in sync.
- `release-process.md` owns the signing/notarization blocker that gates any stable release claim.
  Keep it consistent with the same statement in the root `AGENTS.md`.

### Testing Requirements

No automated checks run against these files. The enforcement lives in the code they describe —
`tests/deps/` for the dependency process, `tests/design/` for the design-token contract.

## Dependencies

### Internal

- `deps/`, `scripts/`, `tests/manual/`, `.github/workflows/`

<!-- MANUAL: -->
