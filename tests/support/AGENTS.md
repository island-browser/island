<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-27 | Updated: 2026-10-27 -->

# support

## Purpose

Header-only helpers shared by the GoogleTest suites in `tests/`. Nothing here is compiled into a
product target.

## Key Files

| File | Description |
|------|-------------|
| `scoped_data_home.h` | `island::testing::ScopedDataHome`: a fresh temporary home for one test. It points HOME, XDG_DATA_HOME, APPDATA, LOCALAPPDATA, USERPROFILE and ISLAND_DEV_CACHE_DIR at it, restores each variable exactly on destruction (unset stays unset) and removes the directory. Its static `GetEnv` / `SetEnv` are the portable way to change one variable in a test, including on Windows |

## For AI Agents

- Use `ScopedDataHome` instead of swapping HOME by hand. It changes the process environment, so
  tests that use it must not run in parallel.
- Add a helper here only when two or more suites need it; keep single-suite helpers in the suite.
