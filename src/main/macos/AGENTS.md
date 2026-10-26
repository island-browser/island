<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# macos

## Purpose

macOS implementations of native browser seams.

## Key Files

| File | Description |
|------|-------------|
| `font_registry_mac.mm` | Registers the bundled Geist faces with CoreText at startup |
| `keymap_menu_mac.{h,mm}` | Tags configurable `NSMenuItem`s by `KeyAction` and reapplies live key equivalents after a Settings shortcut change; the CEF-free action-to-equivalent mapping stays in `keymap.*` |

## For AI Agents

### Working In This Directory

- Added to `island_browser_core` only inside the `if(APPLE)` branch of `../CMakeLists.txt`, together
  with the `CoreFoundation` and `CoreText` frameworks.
- This is Objective-C++ (`.mm`); the root `CMakeLists.txt` calls `enable_language(OBJCXX)` on Apple.
- The macOS process entrypoints (`main_mac.mm`, `process_helper_mac.cc`) live one level up in
  `src/main/`, not here.

### Testing Requirements

macOS arm64 is the only target with local build/run evidence:

```bash
cmake --build build
open build/src/main/island_browser.app --args --island-smoke-test
```

## Dependencies

### External

- Cocoa, CoreFoundation, CoreText

<!-- MANUAL: -->
