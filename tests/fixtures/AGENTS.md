<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-27 | Updated: 2026-10-27 -->

# fixtures

## Purpose

Recorded input files for the GoogleTest suites. Tests reach them through `ISLAND_TEST_FIXTURE_DIR`,
which CMake defines as `tests/fixtures` for `island_tests`. This directory and its `session/`
subdirectory are covered by this file.

## Key Files

| File | Description |
|------|-------------|
| `session/session-0.9.0.json` | A session in the 0.9 shape: two spaces, an icon, pinned, grouped and split tabs, and two favorite records, with none of the 0.10 optional members. Loaded by `SessionStore.Loads090SessionFixture`. It was written by hand from the 0.9 writer, not by a 0.9.0 binary; replace it with a real capture when one is available |

## For AI Agents

- Keep fixtures small and do not put real user data in them. Use `example.test` hosts.
- A fixture that describes an older format stays as it was written. Add a new file for a new
  format rather than editing the old one.
