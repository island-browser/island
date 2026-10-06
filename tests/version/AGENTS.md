<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-06 | Updated: 2026-10-06 -->

# version

## Purpose

Tests for `scripts/version.py`: SemVer parsing and bumping, `CHANGELOG.md` rotation, the Markdown
subset the site repository renders into its changelog page (HTML-escaped, unsafe link schemes dropped), the
`show`/`bump`/`sync`/`check`/`notes` commands against a temporary repository, and the real repository's
consistency — `VERSION`, `CHANGELOG.md`, the Windows manifest, and the CMake wiring must
agree.

## Key Files

| File | Description |
|------|-------------|
| `test_version.py` | `unittest.TestCase` suites; loads `scripts/version.py` by path |

## For AI Agents

- `RepositoryTests.test_repository_is_consistent` fails whenever the Windows manifest is stale
  or the changelog lacks the version's section; the fix is `python3 scripts/version.py sync`, not editing the test.
- Run with `python3 -m pytest tests/version` or
  `python3 -m unittest discover -s tests/version -p 'test_*.py'` (what `ci.yml` runs).
