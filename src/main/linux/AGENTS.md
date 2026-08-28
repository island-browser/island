<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# linux

## Purpose

Linux process entrypoint and the Linux implementation of the `font_registry.h` seam.

## Key Files

| File | Description |
|------|-------------|
| `main_linux.cc` | Linux `main()`; drives `CefInitialize` / `CefRunMessageLoop` / `CefShutdown` |
| `font_registry_linux.cc` | Registers the bundled Geist faces through Fontconfig |

## For AI Agents

### Working In This Directory

- Wired in the `elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")` branch of `../CMakeLists.txt`, which
  does `find_package(Fontconfig REQUIRED)` and then includes `cmake/platform/linux.cmake` to call
  `island_add_linux_browser(island_browser)`.
- The root `CMakeLists.txt` forces `PROJECT_ARCH=arm64` on aarch64 Linux because CEF's
  `cef_variables.cmake` only recognizes the literal `arm64`.
- Linux support claims require native GitHub Actions evidence — there is no local build evidence for
  this platform.

### Testing Requirements

Evidence comes from the `native-linux` job in `.github/workflows/ci.yml`. That job is the only
Linux evidence on `main`; there is no local Linux build path and no container lane in this
repository today. Do not upgrade a Linux support claim without a green run of that job.

## Dependencies

### External

- Fontconfig (`Fontconfig::Fontconfig`), CEF

<!-- MANUAL: -->
