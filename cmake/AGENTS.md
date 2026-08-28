<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# cmake

## Purpose

Container for the platform CMake modules that `src/main/CMakeLists.txt` includes on non-Apple
hosts. macOS has no module here — its bundle wiring is written inline in `src/main/CMakeLists.txt`.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `platform/` | Per-platform executable creation and CEF runtime staging (see `platform/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Modules here are included *after* `find_package(CEF)` and after `libcef_dll_wrapper` and
  `island_browser_core` exist. Each entrypoint asserts those preconditions with `FATAL_ERROR`
  rather than failing later with a confusing link error.
- Use target-scoped commands only; the project convention forbids global `include_directories()`
  and `link_libraries()`.

## Dependencies

### Internal

- `src/main/CMakeLists.txt` (the only consumer)

<!-- MANUAL: -->
