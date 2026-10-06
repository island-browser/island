<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-06 | Updated: 2026-10-06 -->

# version

## Purpose

Tests for `scripts/version.py`: SemVer parsing and bumping, `CHANGELOG.md` rotation, the Markdown
subset rendered into `site/changelog.html` (HTML-escaped, unsafe link schemes dropped), the
`show`/`bump`/`sync`/`check` commands against a temporary repository, and the real repository's
consistency — `VERSION`, `CHANGELOG.md`, the site, the Windows manifest, and the CMake wiring must
agree.

## Key Files

| File | Description |
|------|-------------|
| `test_version.py` | `unittest.TestCase` suites; loads `scripts/version.py` by path |

## For AI Agents

- `RepositoryTests.test_repository_is_consistent` fails whenever a site page or the changelog
  page is stale; the fix is `python3 scripts/version.py sync`, not editing the test.
- Run with `python3 -m pytest tests/version` or
  `python3 -m unittest discover -s tests/version -p 'test_*.py'` (what `ci.yml` runs).
