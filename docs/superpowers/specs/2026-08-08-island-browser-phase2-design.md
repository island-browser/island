# Island Browser — Phase 2 Design

## Status and baseline

This is the canonical, decision-complete design for Phase 2. Phase 1 remains the implemented
baseline: a C++20 application using CEF and native `cef_views`, one `CefWindow`, one
`CefBrowserView`, deterministic `data:` startup pages, `NavigationState`, Back/Forward/Reload,
popup rejection, and the CEF close lifecycle. See the completed
[Phase 1 design](2026-08-08-island-browser-phase1-design.md) and
[Phase 1 plan](../plans/2026-08-08-island-browser-phase1.md).

Phase 2 adds native browser chrome around that same single browser view. It does not turn Island
into a multi-tab browser or change the CEF/runtime/package baseline.

## Scope

Phase 2 delivers all of the following:

- A responsive native chrome layout at a 1440×900 DIP reference size and a supported minimum
  content size of 800×560 DIP.
- A 286 DIP left rail and a browser-content region. The rail is visible at the reference size; the
  286 DIP width is invariant in either OS theme while the window is large enough to show it.
- Exactly one **active-tab focus representation**: it displays current-page identity and navigation
  state, but it is not a tab collection, selector, switcher, or persistence model.
- A contextual address control that displays the active location while idle and becomes editable
  on click or the platform focus shortcut. Enter submits; Escape restores the last displayed value;
  focus loss restores the display representation without navigating.
- Navigation only to an absolute, credential-free `http` or `https` URL whose host is either a
  normal non-empty DNS hostname, `localhost`, a suffix ending in `.localhost`, `127.0.0.0/8`, or
  `[::1]`. Unsupported schemes, relative input, malformed URLs, empty hosts, user names, and
  passwords are rejected without calling CEF navigation. The existing deterministic `data:` startup
  pages remain an internal startup exception, not address-bar input.
- OS light/dark theme selection, native font registration, deterministic bundled Lucide SVG assets,
  typed chrome tokens, CMake/package/workflow integration, unit coverage, visual evidence, and
  keyboard/accessibility acceptance.

## Explicit non-goals

The following are not Phase 2 behavior, even where `browser.pen` depicts them:

- Frames 07 and 08, including top-tabs and hybrid-tabs alternatives.
- Any additional tab, tab strip, tab switching, tab closing/creation, split view, spaces,
  sidebars with product state, downloads, history, bookmarks, settings, reader mode, sharing,
  command bar, agent actions, extensions, sync, persistence, hibernation, profiles, or account UI.
- Phase 3 and later behavior, expanded CDP/agentic integration, changes to port `9222`, signing,
  notarization, or revised platform-support claims.

The rail contains only the focus identity and supported navigation/address controls. Decorative
mock-product content in Frames 01/06 is not data or functionality to implement.

## Reference layout and rendering contract

### Frame selection

`browser.pen` is a visual reference only. Implement Frame 01 (`01 Browser — Light Main`) for the
light hierarchy and Frame 06 (`06 Browser — Dark Main`) for **dark tokens only**. Frame 06 does not
authorize its alternate sidebar hierarchy, width, dashboard, tabs, spaces, settings, or actions.

At 1440×900 DIP, the hierarchy in both themes is:

```text
BrowserWindow (1440×900 reference; minimum 800×560)
├── BrowserChrome (horizontal)
│   ├── FocusRail (286 DIP wide)
│   │   ├── WindowAndNavigationControls
│   │   │   ├── Back button
│   │   │   ├── Forward button
│   │   │   └── Reload button
│   │   ├── ContextualAddressControl
│   │   │   ├── security/location icon
│   │   │   └── AddressView (display or edit field)
│   │   └── ActiveTabFocus
│   │       ├── favicon asset
│   │       ├── page title
│   │       └── active indicator
│   └── BrowserContent (fills remaining width and height)
│       └── CefBrowserView
└── native CefWindow frame and platform controls
```

The title and favicon truncate rather than resize or create a second row. On a window width below
the reference, `BrowserContent` is reduced first; the top-level window must not be made smaller
than 800×560 DIP. If the CEF/OS minimum client area makes the full 286 DIP rail impractical at the
minimum, keep its controls reachable through the chrome's compact layout without adding state or
another navigation surface; document the exact platform result in visual evidence.

### `cef_views` constraints

`cef_views` lays out rectangular native views. Therefore `CefBrowserView` and input/button hit
testing stay rectangular. Corner radius, translucent/blurred surfaces, and shadows are styling
approximations applied only to chrome-owned controls where the platform supports them; they must
not clip, mask, overlap, or shadow the browser view. No requirement depends on pixel-exact blur,
rounded browser content, or composited shadow parity across macOS, Windows, and Linux.

## Tokens, fonts, and icon assets

### Typed token API

Define a `ChromeTokens` value type, selected from OS theme once at window creation and refreshed on
the platform theme-change callback. It has strongly typed fields, not string-key lookup:

```text
Color background, surface, surface_secondary, text, text_secondary, border, accent;
int rail_width_dip = 286, radius_small_dip = 8, radius_medium_dip = 12;
int spacing_1_dip = 4, spacing_2_dip = 8, spacing_3_dip = 12,
    spacing_4_dip = 16, spacing_6_dip = 24;
FontFamily ui = Geist, mono = Geist Mono.
```

The Frame 01 light values are: `#F3F0E9`, `#FFFEFB`, `#ECE9E2`, `#18303A`, `#687A7D`, `#D8D8D0`,
and `#168C99` respectively. Frame 06 supplies dark values only: background `#0D1B26`, surface
`#142633`, secondary surface `#1B3040`, text `#EAF3F3`, secondary text `#9CB0B5`, border
`#29414E`, and accent `#168C99`.

Register the vendored Geist and Geist Mono TTF files before any chrome view is created. Registration
failure is startup-fatal with a diagnostic naming the missing file; silently falling back to an
uncontrolled system font is not acceptable.

Bundle only the Lucide SVGs used by the implementation under a deterministic assets directory and
load them by fixed filename. Pin Lucide to version `1.30.0`, commit
`249af14dc6c09d846fada19455ac074ed29ee407`, archive SHA-256
`c38157cb46ef10cf21782f3bf90b75a4a7dbbe973ef376ff18b71766bbc1574e`. Use `earth.svg` for the
design's `globe-2` semantic slot because that exact icon name is not an asset contract. NanoSVG and
STB commits are approved inputs; their archive SHA-256 values must be independently computed before
they are written to the dependency lock. They must not be added with an unverified checksum.

## State, ownership, and lifecycle

`BrowserWindow` continues to own the sole `CefBrowserView`, browser command dispatch, and CEF close
lifecycle. It additionally owns exactly one `BrowserChrome`, created before the browser view is
attached and destroyed before CEF references are released. `IslandApp` owns one `BrowserWindow` and
does not own chrome, address, or tab state directly.

`BrowserChrome` is a native-view composition owner. It receives immutable navigation and address
snapshots, maps them to controls, owns focus traversal, and sends command/submit callbacks to
`BrowserWindow`. It never calls CEF APIs, owns no `CefRefPtr`, and contains no navigation policy.

`AddressModel` owns address-specific state:

```text
AddressSnapshot { display_text, edit_text, mode (display|editing), validation_error }
```

It derives `display_text` from `NavigationSnapshot::url`, preserves `edit_text` only while editing,
and validates a submitted UTF-8 string into either an absolute allowed URL or a typed rejection.
On a successful `BrowserWindow` submission, `AddressModel` returns to display mode and
`BrowserWindow` invokes `CefBrowser::GetMainFrame()->LoadURL()`. On rejection, it remains editable,
keeps the input intact, exposes a non-modal accessible validation message, and does not navigate.

`NavigationState` remains the single source of browser-navigation truth. `BrowserWindow` adapts its
published snapshots into `BrowserChrome`/`AddressModel`; it must detach their observers during close
before browser/window references are cleared. Browser callbacks after close are ignored. Theme and
font objects have the same window-bound lifetime and are released before `CefShutdown()`.

## Input, commands, and accessibility

- Back, Forward, and Reload retain their Phase 1 commands and accelerators. Button enabled state is
  derived only from `NavigationSnapshot::can_go_back` and `can_go_forward`; Reload is available
  while the browser exists.
- The platform location shortcut focuses and selects the editable address value (`Cmd+L` on macOS,
  `Ctrl+L` on Windows/Linux). Clicking the contextual address control has the same effect.
- Enter submits, Escape cancels to the current display value, and Tab/Shift+Tab follow a deterministic
  order: Back, Forward, Reload, address field, then active-tab focus representation. Disabled
  buttons are skipped by activation and announced as unavailable.
- Every interactive control has an accessible name and role; the active-tab representation exposes
  its title as a non-interactive current-page status. Validation errors are announced once through a
  polite live/status surface and have sufficient contrast in both token sets.

## Build, package, workflow, and acceptance

Keep target-scoped CMake and the existing CEF sentinel/setup flow. CMake copies registered fonts and
the deterministic icon set into each runtime bundle/package and fails configuration or packaging when
any declared asset is missing. Dependency setup/verification recognizes the new locked archives and
their receipts. Package checks verify every declared chrome asset is present in the executable
runtime layout and that no source-tree path is required at runtime.

The native build workflow runs configure/build/CTest and package tests on the existing target matrix;
visual/manual evidence is collected on macOS arm64 and must record the OS theme, resolution, and
compact/minimum-size result. Native CI evidence remains required before claims about other targets.

Acceptance requires focused model tests for address parsing/rejection and chrome state projection;
existing navigation/lifecycle tests remain green; build/package/dependency checks pass; both OS themes
render the Frame 01 hierarchy at 1440×900; keyboard and screen-reader semantics work; and the
browser still has one view, rejects popups, starts from data-only pages, and exits through the Phase
1 CEF lifecycle.
