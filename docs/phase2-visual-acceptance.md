# Phase 2 visual acceptance evidence

Tracks the manual/visual sign-off required by
[Unit U8 of the Phase 2 plan](superpowers/plans/2026-08-08-island-browser-phase2.md) and the
[Phase 2 design's acceptance section](superpowers/specs/2026-08-08-island-browser-phase2-design.md).
The design requires recorded macOS arm64 evidence at 1440x900 and 800x560 DIP, in both OS themes,
before Phase 2 can be called visually accepted.

## Status

No visual evidence has been captured yet. This file is the place to attach it once someone runs
[`tests/manual/phase2_chrome_checklist.md`](../tests/manual/phase2_chrome_checklist.md) on real
hardware with a display - that checklist cannot be completed from a headless/automated session.

Automated coverage that *is* in place and does not require a human:

- `tests/chrome/browser_chrome_contract_test.cpp` - view-tree hierarchy, rail width, layout math at
  reference and minimum bounds, stable view IDs.
- `tests/address_bar_model_test.cpp` - display/edit mode transitions, draft handling, Enter/Escape/
  blur semantics.
- `tests/cef_address_parser_test.cpp` - URL allow/reject rules (absolute http/https, localhost,
  `.localhost`, `127.0.0.0/8`, `[::1]`, credential/relative/malformed rejection).
- `tests/chrome_tokens_test.cpp` / `design_tokens_test.cpp` - light/dark token values.

What those tests cannot verify: that the rendered rail is visually exactly 286 DIP on a real
compositor, that dark-mode repaint actually fires on a live OS theme change, that focus order and
screen-reader announcements work through the real accessibility tree, or that nothing clips/shadows
the live `CefBrowserView`. That gap is exactly what the manual checklist exists to close.

## Recording evidence

When you run the checklist, attach screenshots/recordings here per target, for example:

```
### macosarm64 - <OS build> - <date>

- Light, 1440x900: <screenshot>
- Dark, 1440x900: <screenshot>
- Light, 800x560 (minimum): <screenshot>
- Dark, 800x560 (minimum): <screenshot>
- Notes: <anything that deviated from the design, e.g. rail behavior at the floor width>
```

Keep prose to what was actually observed. If a box in the checklist could not be verified (missing
hardware, no screen reader available, etc.), say so explicitly rather than leaving it implied as
passed.
