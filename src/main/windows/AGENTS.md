<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# windows

## Purpose

Windows process entrypoint, the GDI implementation of the `font_registry.h` seam, and the
application/compatibility manifests embedded in the executable.

## Key Files

| File | Description |
|------|-------------|
| `main_win.cc` | Windows entrypoint (`wWinMain`) and CEF process bootstrap |
| `font_registry_win.cc` | Registers the bundled Geist faces via GDI |
| `island_browser.exe.manifest` | Application manifest for the shipped executable |
| `compatibility.manifest` | OS compatibility declarations merged into the app manifest |

## For AI Agents

### Working In This Directory

- Wired in the `elseif(WIN32)` branch of `../CMakeLists.txt`, which links `gdi32` and includes
  `cmake/platform/windows.cmake` to call `island_add_windows_browser_target()`.
- Windows builds use the static CRT. The root `CMakeLists.txt` forces
  `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>` to match CEF's
  `CEF_RUNTIME_LIBRARY_FLAG`; mixing `/MT` and `/MD` trips `LNK2005`/`LNK2038`.
- Any header here that pulls in `windows.h` alongside CEF needs `NOMINMAX` and
  `WIN32_LEAN_AND_MEAN` (see how `island_tests` sets them) so the `min`/`max` macros do not clobber
  `std::min`/`std::max`. `browser_chrome.h` also `#undef`s them defensively.
- Windows support claims require native GitHub Actions evidence; there is no local build evidence.

### Testing Requirements

Evidence comes from the `native-windows` matrix job in `.github/workflows/ci.yml` and from
`windows64` / `windowsarm64` in `.github/workflows/package.yml`.

## Dependencies

### External

- `gdi32`, CEF

<!-- MANUAL: -->
