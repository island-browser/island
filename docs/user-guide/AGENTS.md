<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-17 | Updated: 2026-10-17 -->

# user-guide

## Purpose

The end-user guide, laid out for GitBook: `README.md` is the landing page and `SUMMARY.md` is the
table of contents GitBook reads. Pages live in topic folders and are written second person,
task-first, one fact per sentence.

## Key Files

| File | Description |
|------|-------------|
| `README.md` | Landing page and conventions |
| `SUMMARY.md` | The full table of contents; every page in the tree must be listed |
| `getting-started/`, `basics/`, `search/`, `agent/`, `settings/`, `reference/` | Written pages |
| `../../.gitbook.yaml` | GitBook Git Sync config at the repo root: `root: ./docs/user-guide/`, readme `README.md`, summary `SUMMARY.md`; keep it in step with this folder |
| `privacy/`, `reading/`, `tabs/`, `troubleshooting.md`, `faq.md` | Planned pages: a heading and a "What this page will cover" list |

## For AI Agents

### Working In This Directory

- Take names, labels and shortcuts from the code (`src/main/keymap.cc`, `src/main/pages/*.html`),
  never from memory, and do not describe features that are not built.
- The shortcut tables in `settings/shortcuts.md` and `reference/keyboard-shortcuts.md` mirror
  `kActions` in `src/main/keymap.cc`; update both when a default changes.
- Mark macOS-only behavior **macOS only** and unwritten pages **Planned**.
- Use GitBook hints (`{% hint style="info" %}`) only where they help.

### Testing Requirements

No automated checks run against these files.

<!-- MANUAL: -->
