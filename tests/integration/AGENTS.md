<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# integration

## Purpose

CEF-linked integration coverage that drives a real `BrowserWindow` and asserts on the chrome
contract end to end, rather than against the CEF-free models.

## Status

**Tracked, but in no CMake target.** `../CMakeLists.txt` does not reference this directory, so
`ctest` never builds or runs it under any flag — unlike `tests/search/`, there is no option that
turns it on. Treat it as work in progress.

## Key Files

| File | Description |
|------|-------------|
| `browser_window_chrome_contract_test.cc` | Exercises `BrowserWindow` + `BrowserChrome` together |

## For AI Agents

### Working In This Directory

- Registering this file means adding a CEF-linking target modelled on `island_cef_tests`: a macOS
  `MACOSX_BUNDLE` executable with the CEF framework copied into `Contents/Frameworks`, and on
  Linux/Windows a `COPY_FILES` of `CEF_BINARY_FILES` and `CEF_RESOURCE_FILES` beside the binary.
  Without both, `CefInitialize` fails on missing binaries or missing `icudtl.dat`.
- Do not add it to `island_tests`. That target does link CEF, but it does not stage the CEF
  framework or resource files beside the binary, so a test that calls `CefInitialize` there fails
  on missing binaries or a missing `icudtl.dat`. No source in `island_tests` calls it today;
  `island_cef_tests` is the target that stages the runtime.

### Testing Requirements

None wired today. Verify by compiling explicitly; do not report `ctest` output as evidence for this
directory.

## Dependencies

### Internal

- `src/main/browser_window.{h,cc}`, `browser_chrome.{h,cc}`

### External

- CEF (`libcef_dll_wrapper`, `CEF_STANDARD_LIBS`)

<!-- MANUAL: -->
