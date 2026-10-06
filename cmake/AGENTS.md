<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# cmake

## Purpose

Container for the platform CMake modules that `src/main/CMakeLists.txt` includes on non-Apple
hosts. macOS has no module here — its bundle wiring is written inline in `src/main/CMakeLists.txt`.

## Key Files

| File | Description |
|------|-------------|
| `island_version.cmake` | `island_read_version()` parses the root `VERSION` file (SemVer, fatal on anything else) before `project()`; `island_add_version_target()` defines the INTERFACE target `island_version` with the generated `<island_version.h>` |
| `island_version.h.in` | Template for `ISLAND_VERSION_STRING` / `_MAJOR` / `_MINOR` / `_PATCH` |
| `local_page_html.cc.in` | Embeds each `src/main/pages/*.html` as a C++ string (`ISLAND_LOCAL_PAGE_SOURCES`) |

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
