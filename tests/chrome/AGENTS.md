<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# chrome

## Purpose

Contract and style tests for the native `cef_views` chrome — the assertions that keep view IDs,
layout structure, and token-derived styling stable across changes to `src/main/browser_chrome.*`.

## Key Files

| File | Description |
|------|-------------|
| `browser_chrome_contract_test.cpp` | Structural contract: view hierarchy and `ChromeViewId` assignments |
| `browser_chrome_style_test.cpp` | Styling derived from `design_tokens` (colors, insets, tint) |

## For AI Agents

### Working In This Directory

- Both files are compiled into `island_tests` (listed as `chrome/...` in `../CMakeLists.txt`).
- These tests pull in CEF headers, which is why `island_tests` defines `NOMINMAX` and
  `WIN32_LEAN_AND_MEAN` on Windows.
- The `ChromeViewId` range `1001`–`1027` is a hard invariant. A failure here after renumbering is a
  real regression, not a stale expectation — fix the source, not the test.

### Testing Requirements

```bash
ctest --test-dir build -R BrowserChrome --output-on-failure
```

## Dependencies

### Internal

- `src/main/browser_chrome.{h,cc}`, `chrome_snapshot.h`, `design_tokens.{h,cc}`

<!-- MANUAL: -->
