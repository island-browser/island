<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# manual

## Purpose

Human-run visual acceptance checklists. These are not automated and are not registered with `ctest`
or pytest; they are the record of what a person must look at before a phase is accepted.

## Key Files

| File | Description |
|------|-------------|
| `phase2_chrome_checklist.md` | Phase 2 chrome visual acceptance steps |
| `phase3_chrome_checklist.md` | Phase 3 chrome visual acceptance steps |

## For AI Agents

### Working In This Directory

- Never mark a checklist item as passed on your own. These require screenshots or a human observing
  the running app; a code reading is not evidence.
- Keep these in sync with `docs/phase2-visual-acceptance.md` and `docs/phase3-visual-acceptance.md`,
  which define the evidence template the checklists feed.

### Testing Requirements

```bash
open build/src/main/island_browser.app
```

Then walk the checklist by hand and attach the evidence the matching `docs/` template asks for.

## Dependencies

### Internal

- `docs/phase2-visual-acceptance.md`, `docs/phase3-visual-acceptance.md`

<!-- MANUAL: -->
