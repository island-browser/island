<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# platform

## Purpose

The Linux and Windows executable definitions, factored out of `src/main/CMakeLists.txt` so the
macOS bundle logic and the non-Apple logic do not interleave.

## Key Files

| File | Description |
|------|-------------|
| `linux.cmake` | `island_add_linux_browser(target)` — creates the Linux executable and stages the CEF runtime beside it |
| `windows.cmake` | `island_add_windows_browser_target()` — creates the Windows executable, embeds the manifests, stages the CEF runtime |

## For AI Agents

### Working In This Directory

- Both modules are included only from the matching branch of `src/main/CMakeLists.txt`, after
  `find_package(CEF)`. Each guards its preconditions (`OS_LINUX`, `TARGET libcef_dll_wrapper`,
  `TARGET island_browser_core`) with `FATAL_ERROR`.
- CEF runtime staging is mandatory: without both `CEF_BINARY_FILES` and `CEF_RESOURCE_FILES` beside
  the binary, the process either fails to start (missing `.so` / `STATUS_DLL_NOT_FOUND`) or starts
  and immediately fails to load ICU data (`icudtl.dat`, `*.pak`).
- Linux executables need `BUILD_WITH_INSTALL_RPATH TRUE` with `INSTALL_RPATH "$ORIGIN"` so the
  staged CEF library is found.
- Neither platform has local build evidence; changes here must be validated by GitHub Actions.

### Testing Requirements

Verified through `native-linux` and `native-windows` in `.github/workflows/ci.yml`, and through the
matching targets in `.github/workflows/package.yml`.

## Dependencies

### Internal

- `src/main/CMakeLists.txt`, `src/main/windows/*.manifest`

### External

- CEF CMake macros from `third_party/cef/cmake/`

<!-- MANUAL: -->
