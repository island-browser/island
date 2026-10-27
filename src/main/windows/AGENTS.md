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
| `island.ico`, `island_browser.rc` | Generated multi-size icon and the resource script that embeds it in the exe (the Inno Setup script also installs it for shortcuts, because the sandboxed build ships CEF's icon-less bootstrap exe) |
| `island_browser.iss` | Inno Setup script for the per-user `-setup.exe` that `scripts/installers.py` builds from the release `.zip` |

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
- Known gap (follow-up, not in the 0.9 icon change): the sandboxed release build (`USE_SANDBOX=ON`,
  what `package.yml` and `ci.yml` build) ships CEF's icon-less `bootstrap.exe` as
  `island_browser.exe`, so the exe has no icon in Explorer or on its taskbar button. The installer,
  shortcuts and uninstall entry carry `island.ico`. The fix is a post-build step in the sandbox
  branch of `cmake/platform/windows.cmake` that writes the icon into the copied `bootstrap.exe`
  (`UpdateResource` or `rcedit`); it needs a Windows build to verify. Until then, `WM_SETICON` on
  the main window would cover the taskbar only.

### Testing Requirements

Evidence comes from the `native-windows` matrix job in `.github/workflows/ci.yml` and from
`windows64` / `windowsarm64` in `.github/workflows/package.yml`.

## Dependencies

### External

- `gdi32`, CEF

<!-- MANUAL: -->
