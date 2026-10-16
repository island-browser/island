# Native Look and Research Plan Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the "Native" look (macOS-style source-list sidebar, system font, one blue, address
indicators) as Island's default, and turn Research into a reviewable plan graph that opens as a Plan
tab and groups the pages it reads under it.

**Architecture:** Island has two layers. The CEF-free model code is testable with GoogleTest
without a window: tokens and looks, appearance prefs, address indicators, plan metadata, plan graph
layout, plan edit messages, and tab grouping. Thin wiring then connects it to the views and pages:
`browser_chrome.cc` for the native sidebar and toolbar, and `src/main/pages/*.html` for local
pages, including the new `plan.html`. Each task lands the model first, test-first, then the wiring,
then a dev-capture check.

**Tech Stack:** C++20, CEF 150 Alloy + `cef_views`, GoogleTest (`ctest`), pytest
(`tests/design`), local HTML/CSS/JS pages without external libraries, Ninja.

**Spec:** `docs/superpowers/specs/2026-10-16-island-macos-native-design.md`. Read it before any
task. Its §8 "Decisions" are binding.

## Global Constraints

- Branch base: `design/macos-native-spec`, which is on top of `release/design` (Arc window,
  Appearance knobs). Do not rebase onto `main` inside this plan.
- Native is the default look. It has **no space color**: no tint, no dot, no colored accent
  (spec §8.1).
- **No status bar.** Page state is shown by indicators inside the address field (spec §8.2).
- Plan mode is a **Plan tab in the content area**. The pages the agent reads open as tabs grouped
  under it (spec §8.3).
- Branching depth is **at most 3 levels**. A deeper request becomes a Needs-you node (spec §8.4).
- Native colors are exactly those in spec §2.1, including `text_secondary` `#636366` light and the
  separate `selection` slot (`#0A64D8` light / `#0A6FE0` dark).
- The Graphite `design-tokens` block in DESIGN.md (contract version 3) is **not changed**, because
  the site repository checks its CSS against it. The Native palette goes in a second fenced block,
  `design-tokens-native`.
- Overlays exist only while shown (`src/main/overlay_lifetime.h`). The ChromeViewId range stays
  1001–1027.
- AGENTS.md conventions: explicit `std::`, Google style with 4-space indents and 100 columns,
  target-scoped CMake, clang-format on changed lines, a CHANGELOG `[Unreleased]` entry per
  user-visible change, `/utf-8` on any new MSVC target that has non-ASCII literals, and fully
  qualified CEF handler typedefs (cross-platform pitfalls).
- Build: `cmake -G Ninja -B build -S . && cmake --build build`. Done means
  `ctest --test-dir build --output-on-failure` and `python3 -m pytest tests/design` are green, and
  for UI tasks a light and a dark dev capture have been looked at.

## Review Focus

1. **Malformed or hostile plan metadata.** `after` names a missing id, a cycle (a→b→a), duplicate
   ids, or 500 entries. The graph must still render: drop the bad edges, show the entry as a plain
   step, and never hang or crash. Task 8 and Task 9 have tests for each case.
2. **A plan that changes while the person edits it.** The agent sends a new `plan` update after
   the person has skipped a node. Skips the person made must survive the update for ids that still
   exist. Task 10 has a test.
3. **Closing the Plan tab or a grouped child tab mid-run.** Closing a child must not orphan the
   group. Closing the Plan tab asks first and stops the research only on "Stop". Task 11 has tests.
4. **Huge blocked counts and fast-changing state in the address field.** 10 000 blocked requests
   shows "9999+". A tab switch updates the indicators for the new tab and never shows the previous
   tab's numbers. Task 5 has tests.
5. **Switching looks after customizing.** Picking Native after a Custom tweak restores every Native
   knob. Tweaking one knob turns the look into Custom without losing the others, and both survive a
   restart. Task 2 has tests.

---

### Task 1: Native look tokens

**Files:**
- Modify: `src/main/design_tokens.h`, `src/main/design_tokens.cc`
- Modify: `DESIGN.md` (new fenced block `design-tokens-native` after the Graphite block)
- Test: `tests/design_tokens_test.cpp`, create `tests/design/test_native_look_contract.py`

**Interfaces:**
- Produces: `enum class LookPreset : std::uint8_t { kNative, kArc, kGraphite };`
  `ChromeTokens ChromeTokens::ForLook(LookPreset look, ChromeTheme theme) noexcept;` and the new
  `ChromeTokens` color slots `fill`, `border_strong`, `text_tertiary`, `selection`, `accent_soft`,
  `accent_text`, `warning_bg`, `warning_border`, `warning_text`, `success`, `danger`,
  `std::array<ArgbColor, 4> branch`. `ForTheme(theme)` stays and equals
  `ForLook(LookPreset::kGraphite, theme)` for the old slots.

- [ ] **Step 1: Write the failing tests** in `tests/design_tokens_test.cpp`:

```cpp
TEST(NativeLookTokensTest, LightMatchesTheSpec) {
    const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    EXPECT_EQ(t.background.argb, 0xFFEBEBEFU);
    EXPECT_EQ(t.surface.argb, 0xFFFFFFFFU);
    EXPECT_EQ(t.surface_secondary.argb, 0xFFF6F6F8U);
    EXPECT_EQ(t.fill.argb, 0xFFE8E8EDU);
    EXPECT_EQ(t.text.argb, 0xFF1D1D1FU);
    EXPECT_EQ(t.text_secondary.argb, 0xFF636366U);
    EXPECT_EQ(t.text_tertiary.argb, 0xFF8E8E93U);
    EXPECT_EQ(t.border.argb, 0xFFE5E5EAU);
    EXPECT_EQ(t.border_strong.argb, 0xFFD2D2D7U);
    EXPECT_EQ(t.accent.argb, 0xFF0A64D8U);
    EXPECT_EQ(t.selection.argb, 0xFF0A64D8U);
    EXPECT_EQ(t.accent_soft.argb, 0xFFDFE9F8U);
    EXPECT_EQ(t.accent_text.argb, 0xFF0A4FA8U);
    EXPECT_EQ(t.warning_bg.argb, 0xFFFFF8E6U);
    EXPECT_EQ(t.warning_border.argb, 0xFFF5D38AU);
    EXPECT_EQ(t.warning_text.argb, 0xFF6A4A00U);
    EXPECT_EQ(t.success.argb, 0xFF248A3DU);
    EXPECT_EQ(t.danger.argb, 0xFFD70015U);
    EXPECT_EQ(t.branch[1].argb, 0xFF8E44ADU);
    EXPECT_EQ(t.canvas, t.background);  // no space tint in Native
}

TEST(NativeLookTokensTest, DarkMatchesTheSpec) {
    const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kDark);
    EXPECT_EQ(t.background.argb, 0xFF2A2A2DU);
    EXPECT_EQ(t.surface.argb, 0xFF1E1E20U);
    EXPECT_EQ(t.text_secondary.argb, 0xFF98989DU);
    EXPECT_EQ(t.accent.argb, 0xFF0A84FFU);
    EXPECT_EQ(t.selection.argb, 0xFF0A6FE0U);
    EXPECT_EQ(t.warning_text.argb, 0xFFFFD60AU);
}

TEST(NativeLookTokensTest, EveryTextPairPassesAa) {
    for (ChromeTheme theme : {ChromeTheme::kLight, ChromeTheme::kDark}) {
        const ChromeTokens t = ChromeTokens::ForLook(LookPreset::kNative, theme);
        const ArgbColor white{.argb = 0xFFFFFFFFU};
        EXPECT_GE(ContrastRatio(t.text, t.background), 4.5);
        EXPECT_GE(ContrastRatio(t.text_secondary, t.background), 4.5);
        EXPECT_GE(ContrastRatio(t.text_secondary, t.surface), 4.5);
        EXPECT_GE(ContrastRatio(white, t.selection), 4.5);
        EXPECT_GE(ContrastRatio(t.accent_text, t.accent_soft), 4.5);
        EXPECT_GE(ContrastRatio(t.warning_text, t.warning_bg), 4.5);
    }
}

TEST(NativeLookTokensTest, NativeIgnoresTheSpaceColor) {
    AppearanceSettings appearance;
    appearance.look = LookPreset::kNative;
    const ChromeTokens base = ChromeTokens::ForLook(LookPreset::kNative, ChromeTheme::kLight);
    const ChromeTokens tinted =
        base.Customized({.argb = 0xFFE5484DU}, ChromeTheme::kLight, appearance);
    EXPECT_EQ(tinted.canvas, base.background);
    EXPECT_EQ(tinted.accent, base.accent);
}
```

- [ ] **Step 2: Run them and confirm they fail to compile**
  (`cmake --build build --target island_tests`: "no member named 'ForLook'").
- [ ] **Step 3: Implement.** In `design_tokens.h`, add the enum and the slots above (default
  `ArgbColor{}`) next to the Arc runtime slots, plus `LookPreset look = LookPreset::kNative;` in
  `AppearanceSettings`. In `design_tokens.cc`, add `ForLook`: `kGraphite` returns today's
  `ForTheme` body, `kArc` returns `ForTheme` too (Arc differs only through `Customized`), and
  `kNative` returns the spec §2.1 table with `canvas = background`, `selected = selection`,
  `hover = MixColor(background, fill, 0.6)`, `well = fill`, `plate = surface`, and
  `ui_font_family = "system"`. Fill the new slots for Graphite with the Native values so every
  look has them. In `Customized`, skip `Tint(...)` when `appearance.look == LookPreset::kNative`.
- [ ] **Step 4: Run** `ctest --test-dir build -R NativeLookTokens --output-on-failure`.
  Expected: PASS.
- [ ] **Step 5: Add the DESIGN.md block and its drift test.** Add a fenced block
  ` ```design-tokens-native ` holding
  `{"contract":"island-native-look","version":1,"colors":{"light":{…},"dark":{…}}}` with every
  slot from spec §2.1 (branch colors as `branch_a`…`branch_d`). Then create
  `tests/design/test_native_look_contract.py`. It loads that block the same way
  `test_token_contract.py` loads `design-tokens`, checks the slot set and that every value is
  `#RRGGBB`, and checks that every hex in it appears in `tests/design_tokens_test.cpp` (same
  technique as the existing native-assertion drift check). Run
  `python3 -m pytest tests/design -q`. Expected: PASS, with the old Graphite tests untouched.
- [ ] **Step 6: Commit** `git commit -m "Add the Native look tokens"`.

### Task 2: Look preset in Appearance and prefs

**Files:**
- Modify: `src/main/design_tokens.{h,cc}` (`ApplyLookPreset`, `LookName`, `ParseLook`)
- Modify: `src/main/prefs_store.{h,cc}` (persist `appearance.look`)
- Modify: `src/main/pages/settings.html` (a Look pop-up at the top of Appearance)
- Modify: `src/main/browser_window.cc` (`HandleSettingsMessage`: `set_look`)
- Test: `tests/appearance_test.cpp`, `tests/prefs_store_test.cpp`

**Interfaces:**
- Consumes: `LookPreset`, `AppearanceSettings::look` (Task 1).
- Produces: `AppearanceSettings ApplyLookPreset(LookPreset look);` (Native: tint 0, radius 0,
  shadow off, full bleed on, font "system", comfortable; Arc: today's defaults; Graphite: tint
  0, radius 10, shadow on, full bleed off, font "geist"),
  `std::string_view LookName(std::optional<LookPreset>)` ("native" | "arc" | "graphite" |
  "custom"), and `std::optional<LookPreset> MatchingLook(const AppearanceSettings&)`.

- [ ] **Step 1: Write the failing tests** in `tests/appearance_test.cpp`:

```cpp
TEST(LookPresetTest, NativeKnobs) {
    const AppearanceSettings a = ApplyLookPreset(LookPreset::kNative);
    EXPECT_EQ(a.look, LookPreset::kNative);
    EXPECT_EQ(a.sidebar_tint_percent, 0);
    EXPECT_TRUE(a.full_bleed);
    EXPECT_FALSE(a.card_shadow);
    EXPECT_EQ(a.font_family, "system");
}

TEST(LookPresetTest, OneTweakMakesItCustomAndKeepsTheRest) {
    AppearanceSettings a = ApplyLookPreset(LookPreset::kNative);
    a.density = Density::kCompact;
    EXPECT_EQ(MatchingLook(a), std::nullopt);
    EXPECT_EQ(LookName(MatchingLook(a)), "custom");
    EXPECT_TRUE(a.full_bleed);  // untouched knobs stay
}

TEST(LookPresetTest, PickingNativeAfterCustomRestoresEveryKnob) {
    AppearanceSettings a = ApplyLookPreset(LookPreset::kArc);
    a.card_radius_dip = 14;
    a = ApplyLookPreset(LookPreset::kNative);
    EXPECT_EQ(a, ApplyLookPreset(LookPreset::kNative));
}
```

  In `tests/prefs_store_test.cpp`, add a test that saves `ApplyLookPreset(kNative)` with
  `density = kCompact`, reloads it, and expects the same struct back. Add a second test that a
  prefs file without `appearance.look` loads as `kArc`, so existing users keep their look, while
  `AppearanceSettings{}` still defaults to `kNative` for new profiles.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement** the three functions. `MatchingLook` compares every knob except
  `look` against each preset. Persist the look as the string `LookName(...)`. When loading, a
  missing key means `kArc` (existing users) and an unknown string means custom (keep the knobs).
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Wire Settings.** Add a "Look" pop-up (Native, Arc, Graphite, and a disabled
  "Custom" entry shown while custom) to the Appearance group in `settings.html`. It posts
  `{type:"set_look", look:"native"}`. `HandleSettingsMessage` applies `ApplyLookPreset`, saves
  prefs, and re-renders the chrome through the existing appearance path. Every other knob message
  keeps sending its knob, and the page relabels Look from the `look` field the window sends back.
  Check it with dev captures: `--island-dev-actions=settings` in light and dark, and look at the
  PNGs.
- [ ] **Step 6: Commit** `git commit -m "Add the Look preset to Appearance"`.

### Task 3: Page theme variables for the Native slots

**Files:**
- Modify: `src/main/browser_window.cc` (`ThemeJson`)
- Modify: `src/main/pages/{start,agent,settings,report,tab_overview}.html` (`applyTheme`)
- Test: `tests/browser_window_pages_test.cpp`, `tests/design/test_local_pages.py`

**Interfaces:**
- Consumes: the new `ChromeTokens` slots (Task 1).
- Produces: `ThemeJson()` keys `fill`, `border_2`, `text_3`, `selection`, `accent_soft`,
  `accent_text`, `warn_bg`, `warn_border`, `warn_text`, `success`, `danger`, `branch` (an array of
  4). Pages expose them as the CSS variables `--fill`, `--border-2`, `--text-3`, `--selection`,
  `--accent-soft`, `--accent-text`, `--warn-bg`, `--warn-border`, `--warn-text`, `--success`,
  `--danger`, `--branch-a` … `--branch-d`.

- [ ] **Step 1: Failing test** in `browser_window_pages_test.cpp`: with Native light,
  `ThemeJson()` has `"selection":"#0a64d8"` and `branch` is an array of 4 strings (use the existing
  ThemeJson test helper in that file).
- [ ] **Step 2: Run it and confirm it fails.**
- [ ] **Step 3: Implement** the keys with the existing `CssColor` helper. In each page's
  `applyTheme(theme)`, set the matching `--*` variables on `document.documentElement.style`.
- [ ] **Step 4: Add the pytest** `test_pages_map_every_theme_key` to `test_local_pages.py`. For
  every page under `src/main/pages/*.html` that defines `applyTheme`, every key listed above must
  appear in it.
- [ ] **Step 5: Run** ctest and pytest. Expected: PASS.
- [ ] **Step 6: Commit** `git commit -m "Expose the Native tokens to local pages"`.

### Task 4: Native sidebar and toolbar

**Files:**
- Modify: `src/main/browser_chrome.{h,cc}` (tab rows, section headers, toolbar layout)
- Modify: `src/main/row_raster.{h,cc}` if row painting lives there
- Test: `tests/browser_window_test.cpp` (view-tree contract), dev captures

**Interfaces:**
- Consumes: `ChromeTokens::selection`, `fill`, `text_tertiary`, `ui_font_family == "system"`.
- Produces: no new public API. The row painter takes `selected` and reads `selection` and white
  text when `tokens.look == LookPreset::kNative`. Add `LookPreset look` to `ChromeTokens`, set by
  `Customized` from the appearance.

- [ ] **Step 1: Failing contract test.** In `browser_window_test.cpp`, assert that with Native
  tokens the selected tab row's resolved fill is `tokens.selection` and its title color is
  `0xFFFFFFFF`, and that an unselected row's title is `tokens.text`. Use the existing helper that
  resolves a row's colors from tokens; if there is none, extract `ResolveRowColors(const
  ChromeTokens&, bool selected, bool hovered, bool slept) -> RowColors{fill, title}` from the
  painter into `row_raster.h` first and test that.
- [ ] **Step 2: Run it and confirm it fails.**
- [ ] **Step 3: Implement** `ResolveRowColors` for Native: selected → `{selection, white}`;
  hovered → `{hover, text}`; slept → title `text_tertiary`. Section headers become sentence case,
  11 px, weight 600, `text_secondary`. The toolbar puts back and forward as 28 DIP icon buttons
  on the left, then an address field that fills the rest (`fill`, radius 8, height 30), then share
  and agent. Address text is centered while unfocused (`SetHorizontalAlignment(CEF_HORIZONTAL_
  ALIGNMENT_CENTER)`) and left-aligned on focus. Re-assert the colors in the theme-reset path.
- [ ] **Step 4: Run ctest.** Then take dev captures:
  `--island-dev-actions=theme:light,navigate:https://example.com` and the same with
  `theme:dark`. Open both PNGs and compare them with canvas row 2 (S2-Start, S2-Agent).
- [ ] **Step 5: Commit** `git commit -m "Draw the Native sidebar and toolbar"`.

### Task 5: Address indicators

**Files:**
- Create: `src/main/address_indicators.h` (CEF-free, header-only)
- Modify: `src/main/browser_chrome.{h,cc}` (three child buttons of the address view)
- Modify: `src/main/browser_window.cc` (feed state and handle clicks)
- Test: create `tests/address_indicators_test.cpp`, add it to `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
enum class AgentTabState : std::uint8_t { kIdle, kReading, kWaitingForYou };
struct AddressIndicators {
    bool show_shield = false;
    std::string shield_label;      // "14", "9999+"
    std::string shield_tooltip;    // "14 trackers blocked"
    bool show_reader = false;
    AgentTabState agent = AgentTabState::kIdle;
    bool operator==(const AddressIndicators&) const = default;
};
[[nodiscard]] AddressIndicators ComputeAddressIndicators(
    std::uint64_t blocked, bool reader_available, AgentTabState agent);
```

- [ ] **Step 1: Failing tests:**

```cpp
TEST(AddressIndicatorsTest, HidesTheShieldAtZero) {
    EXPECT_FALSE(ComputeAddressIndicators(0, false, AgentTabState::kIdle).show_shield);
}
TEST(AddressIndicatorsTest, CapsHugeCounts) {
    const AddressIndicators a = ComputeAddressIndicators(10000, false, AgentTabState::kIdle);
    EXPECT_EQ(a.shield_label, "9999+");
    EXPECT_EQ(a.shield_tooltip, "10000 trackers blocked");
}
TEST(AddressIndicatorsTest, SingularTooltip) {
    EXPECT_EQ(ComputeAddressIndicators(1, false, AgentTabState::kIdle).shield_tooltip,
              "1 tracker blocked");
}
TEST(AddressIndicatorsTest, ReaderAndAgentPassThrough) {
    const AddressIndicators a = ComputeAddressIndicators(0, true, AgentTabState::kWaitingForYou);
    EXPECT_TRUE(a.show_reader);
    EXPECT_EQ(a.agent, AgentTabState::kWaitingForYou);
}
```

  In `browser_window_test.cpp`, add a test that selecting another tab recomputes the indicators
  from that tab's `blocked_requests()`: tab A has 5, tab B has 0, and after selecting B the shield
  is hidden.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement** the function. Then wire it: `BrowserWindow` recomputes the indicators
  on tab switch, on each blocked request, on Reader availability changes, and on agent
  state/approval changes, and passes them to `BrowserChrome::SetAddressIndicators`. The chrome
  shows or hides three 20 DIP image buttons inside the address view, each with an accessible name
  and tooltip. The shield click opens the existing blocker popover for the site, the reader click
  runs `ToggleReader`, and the agent dot click opens the agent panel. The agent dot is `accent`
  when reading and `warning_text` when waiting.
- [ ] **Step 4: Run ctest and take dev captures** of example.com with the blocker on (the shield
  must show), then look at them.
- [ ] **Step 5: Commit** `git commit -m "Show page state as address-field indicators"`.

### Task 6: Shortcuts page, recording, conflicts and Reset all

**Files:**
- Modify: `src/main/pages/settings.html` (Shortcuts section)
- Modify: `src/main/browser_window.cc` (`HandleSettingsMessage`: `record_shortcut`,
  `replace_shortcut`, `reset_shortcuts`)
- Modify: `src/main/keymap.{h,cc}` if a helper is missing
- Test: `tests/keymap_test.cpp`, `tests/browser_window_pages_test.cpp`

**Interfaces:**
- Consumes: the existing `Keymap` with `Conflicts()`.
- Produces: `std::optional<KeyAction> Keymap::ConflictFor(KeyAction action, const Accelerator&
  accel) const;`, which returns the other action already bound to `accel`, if any.

- [ ] **Step 1: Failing tests** in `keymap_test.cpp`: binding ⌘B to the blocker toggle
  reports `ConflictFor == kToggleSidebar`; binding a free chord reports `nullopt`; binding an
  action to its own current chord reports `nullopt`. In `browser_window_pages_test.cpp`: the message
  `record_shortcut` with a conflicting chord returns
  `{conflict:"Toggle sidebar"}` and leaves the keymap unchanged; `replace_shortcut` moves the chord
  and unbinds the old action; `reset_shortcuts` restores the defaults.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement** `ConflictFor` and the three messages. In `settings.html`, clicking a
  row enters recording (an `accent` outline and "Press keys…"), and the next keydown with a
  modifier posts `record_shortcut`. A conflict shows the `--warn-*` banner with Replace and
  Cancel, and esc cancels recording. Keycaps are 24 px min-width, radius 5, with a
  `--border-2` outline. Unbound actions show "Add shortcut". Keep the macOS restart note.
- [ ] **Step 4: Run** ctest and pytest, then take a dev capture with `settings` and look at it.
- [ ] **Step 5: Commit** `git commit -m "Record, replace and reset shortcuts in Settings"`.

### Task 7: ⌘/ cheatsheet overlay

**Files:**
- Create: `src/main/cheatsheet_overlay.{h,cc}`
- Modify: `src/main/browser_command.h` (`kShowCheatsheet`), `src/main/keymap.cc` (default ⌘/,
  Ctrl+/ elsewhere), `src/main/browser_window.cc`, `src/main/main_mac.mm` (menu item under
  Help), `src/main/CMakeLists.txt`, `src/main/dev_capture.cc` (action `cheatsheet`)
- Test: `tests/overlay_lifetime_test.cpp`, `tests/keymap_test.cpp`, `tests/dev_capture_test.cpp`

**Interfaces:**
- Consumes: `OverlayLifetime` from `overlay_lifetime.h` and `Keymap`.
- Produces: `std::vector<CheatsheetRow> CheatsheetRows(const Keymap&)`, which returns the ten
  rows in the spec order (Command palette, Ask agent, Toggle sidebar, Split view, New tab, Close
  tab, Find in page, Reader view, Settings, Cheatsheet). Each row is `{label, keys}`, and the keys
  are the current binding.

- [ ] **Step 1: Failing tests.** The default keymap binds ⌘/ to `kShowCheatsheet`.
  `CheatsheetRows(defaults)` has 10 rows, and its first is `{"Command palette","⌘K"}`. Rebinding
  the palette to ⌘P shows "⌘P". The overlay lifetime test proves the cheatsheet overlay is
  destroyed on hide, so no live overlay is left after esc.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement** a 320 DIP bottom-right card that follows the `find_bar_overlay`
  pattern: created on show, destroyed on hide, and closed by esc or a click outside.
- [ ] **Step 4: Run ctest.** Then take a dev capture with `--island-dev-actions=cheatsheet`,
  look at it, and check by hand that clicks work after closing it (the mouse-input regression).
- [ ] **Step 5: Commit** `git commit -m "Add the ⌘/ shortcut cheatsheet"`.

### Task 8: Plan metadata in the ACP client

**Files:**
- Modify: `src/agent/acp_client.{h,cc}`
- Test: `tests/agent/acp_client_test.cpp`

**Interfaces:**
- Produces:

```cpp
struct IslandPlanMeta {
    std::string id;
    std::vector<std::string> after;
    std::string branch;     // free label, "" for the trunk
    std::string kind;       // "question" | "step" | "join" | "result"; "" → "step"
    std::string tool;
    std::string site;
    std::string needs_you;  // "", "sign-in", "approval", "depth"
    bool operator==(const IslandPlanMeta&) const = default;
};
struct AcpPlanEntry {
    std::string content;
    std::string status;
    std::optional<IslandPlanMeta> island;  // new; absent for plain ACP agents
    std::string raw_meta;                  // the entry's _meta re-serialized, "" if none
    bool operator==(const AcpPlanEntry&) const = default;
};
```

- [ ] **Step 1: Failing tests:**

```cpp
TEST_F(AcpClientTest, PlanEntriesCarryIslandMeta) {
    Handshake(true);
    events_.clear();
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"plan","entries":[{"content":"Known problems","status":"pending","_meta":{"island":{"id":"a2","after":["a"],"branch":"A","kind":"step","tool":"browser_open_tab","site":"github.com","needs_you":"sign-in"},"x-other":1}}]}}})");
    ASSERT_EQ(events_.size(), 1U);
    const AcpPlanEntry& e = events_[0].plan.at(0);
    ASSERT_TRUE(e.island.has_value());
    EXPECT_EQ(e.island->id, "a2");
    EXPECT_EQ(e.island->after, std::vector<std::string>{"a"});
    EXPECT_EQ(e.island->needs_you, "sign-in");
    EXPECT_NE(e.raw_meta.find("x-other"), std::string::npos);  // unknown keys kept
}

TEST_F(AcpClientTest, PlainPlanEntriesHaveNoIslandMeta) {
    Handshake(true);
    events_.clear();
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"plan","entries":[{"content":"Find docs","status":"pending"}]}}})");
    EXPECT_FALSE(events_.at(0).plan.at(0).island.has_value());
}

TEST_F(AcpClientTest, MalformedIslandMetaIsIgnoredNotFatal) {
    Handshake(true);
    events_.clear();
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"plan","entries":[{"content":"x","status":"pending","_meta":{"island":{"id":7,"after":"a"}}}]}}})");
    ASSERT_EQ(events_.at(0).plan.size(), 1U);
    EXPECT_FALSE(events_.at(0).plan.at(0).island.has_value());  // id must be a non-empty string
}
```

- [ ] **Step 2: Run** `ctest --test-dir build -R AcpClientTest --output-on-failure` and confirm
  it fails.
- [ ] **Step 3: Implement** the parsing in the `kind == "plan"` branch. Read `_meta.island` only
  when it is an object with a non-empty string `id`. `after` accepts only an array of strings;
  anything else becomes an empty array. Each string field defaults to "". Keep `raw_meta` as the
  serialized `_meta`. Existing callers that build `{content, status}` still compile, because the
  new members have defaults. Fix the brace-init sites that break.
- [ ] **Step 4: Run** the agent tests (standalone `cmake -B build-agent -S src/agent` too).
  Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "Parse Island plan metadata from ACP plans"`.

### Task 9: Plan graph model and layout

**Files:**
- Create: `src/agent/plan_graph.{h,cc}` (CEF-free; add to `island_agent` in
  `src/agent/CMakeLists.txt`)
- Test: create `tests/agent/plan_graph_test.cpp` and add it to `island_agent_tests`

**Interfaces:**
- Consumes: `AcpPlanEntry` and `IslandPlanMeta` (Task 8).
- Produces:

```cpp
enum class PlanNodeKind : std::uint8_t { kQuestion, kStep, kNeedsYou, kJoin, kResult };
enum class PlanNodeState : std::uint8_t { kQueued, kRunning, kDone, kFailed, kSkipped };
struct PlanNode {
    std::string id, title, branch, tool, site, needs_you;
    PlanNodeKind kind = PlanNodeKind::kStep;
    PlanNodeState state = PlanNodeState::kQueued;
    std::vector<std::string> after;   // only edges that resolve
    int depth = 0;                    // branching depth, 0 = trunk
    int column = 0, row = 0;          // layout grid
};
struct PlanGraph {
    std::vector<PlanNode> nodes;      // topological order
    bool branching = false;           // false → open in List view
    std::vector<std::string> dropped; // diagnostics: "a→b: cycle", "x: unknown after"
};
inline constexpr int kMaxBranchDepth = 3;
[[nodiscard]] PlanGraph BuildPlanGraph(const std::vector<AcpPlanEntry>& entries,
                                       const std::set<std::string>& skipped_ids);
[[nodiscard]] std::string PlanGraphJson(const PlanGraph& graph);  // payload for plan.html
```

- [ ] **Step 1: Failing tests** in `plan_graph_test.cpp`. Use a helper
  `E(id, after, branch, kind="step")` that builds an `AcpPlanEntry` with `island` set.

```cpp
TEST(PlanGraphTest, FlatPlanIsAChainAndNotBranching) {
    const PlanGraph g = BuildPlanGraph({{"a","pending"},{"b","pending"}}, {});
    ASSERT_EQ(g.nodes.size(), 2U);
    EXPECT_FALSE(g.branching);
    EXPECT_EQ(g.nodes[1].after, std::vector<std::string>{g.nodes[0].id});  // synthesized ids
}
TEST(PlanGraphTest, SpecExampleLaysOutThreeBranchesOneJoin) {
    const PlanGraph g = BuildPlanGraph({E("q",{},"","question"), E("s",{"q"},""),
        E("a",{"s"},"A"), E("b",{"s"},"B"), E("c",{"s"},"C"), E("a1",{"a"},"A"),
        E("a2",{"a"},"A"), E("b1",{"b"},"B"), E("m",{"a1","a2","b1"},"","join"),
        E("r",{"m"},"","result")}, {"c"});
    EXPECT_TRUE(g.branching);
    EXPECT_EQ(Find(g,"m").kind, PlanNodeKind::kJoin);
    EXPECT_EQ(Find(g,"c").state, PlanNodeState::kSkipped);
    EXPECT_EQ(Find(g,"a1").column, 3);
    EXPECT_EQ(Find(g,"m").column, 4);
    EXPECT_GT(Find(g,"b").row, Find(g,"a").row);
}
TEST(PlanGraphTest, CyclesAreBrokenAndReported) {
    const PlanGraph g = BuildPlanGraph({E("a",{"b"},""), E("b",{"a"},"")}, {});
    EXPECT_EQ(g.nodes.size(), 2U);
    EXPECT_FALSE(g.dropped.empty());
}
TEST(PlanGraphTest, UnknownAfterIsDropped) {
    const PlanGraph g = BuildPlanGraph({E("a",{"nope"},"")}, {});
    EXPECT_TRUE(g.nodes[0].after.empty());
    EXPECT_EQ(g.dropped.size(), 1U);
}
TEST(PlanGraphTest, DuplicateIdsKeepTheFirst) {
    const PlanGraph g = BuildPlanGraph({E("a",{},""), E("a",{},"")}, {});
    EXPECT_EQ(g.nodes.size(), 1U);
}
TEST(PlanGraphTest, DepthBeyondThreeBecomesNeedsYou) {
    // s → a(A) → a1(A.1) → a11(A.1.1) → a111(A.1.1.1): the 4th branching level asks.
    const PlanGraph g = BuildPlanGraph({E("s",{},""), E("a",{"s"},"A"), E("x",{"s"},"B"),
        E("a1",{"a"},"A.1"), E("a2",{"a"},"A.2"), E("a11",{"a1"},"A.1.1"),
        E("a12",{"a1"},"A.1.2"), E("a111",{"a11"},"A.1.1.1"), E("a112",{"a11"},"A.1.1.2")}, {});
    EXPECT_EQ(Find(g,"a111").kind, PlanNodeKind::kNeedsYou);
    EXPECT_EQ(Find(g,"a111").needs_you, "depth");
}
TEST(PlanGraphTest, FiveHundredEntriesStayFast) {
    std::vector<AcpPlanEntry> many;
    for (int i = 0; i < 500; ++i)
        many.push_back(E("n" + std::to_string(i), i ? std::vector<std::string>{"n" +
                         std::to_string(i - 1)} : std::vector<std::string>{}, ""));
    const auto start = std::chrono::steady_clock::now();
    const PlanGraph g = BuildPlanGraph(many, {});
    EXPECT_EQ(g.nodes.size(), 500U);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
}
TEST(PlanGraphTest, StatusMapsToState) {
    AcpPlanEntry run = E("a",{},""); run.status = "in_progress";
    AcpPlanEntry done = E("b",{"a"},""); done.status = "completed";
    const PlanGraph g = BuildPlanGraph({run, done}, {});
    EXPECT_EQ(Find(g,"a").state, PlanNodeState::kRunning);
    EXPECT_EQ(Find(g,"b").state, PlanNodeState::kDone);
}
```

- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement.**
  - Entries without `island` get ids `"p0"`, `"p1"`, … and each one is `after` the previous one.
  - Keep the first entry for each id and drop unknown `after` ids into `dropped`.
  - Run a Kahn topological sort. Whatever is left in a cycle loses its back edges, and each lost
    edge goes into `dropped`.
  - `branching` = some node has two or more children.
  - `depth` = the number of branch points on the path from the root, where a branch point is a
    node with two or more children. A node at depth > `kMaxBranchDepth` becomes `kNeedsYou` with
    `needs_you = "depth"`.
  - `column` = the longest path from a root. Rows: walk the tree depth-first, give each leaf the
    next row, give each parent the middle of its children's rows rounded down, and give a join the
    middle of its inputs' rows.
  - Status mapping: "in_progress" → `kRunning`, "completed" → `kDone`, "failed" → `kFailed`. An id
    in `skipped_ids` → `kSkipped`.
  - `kind` comes from the meta, and `needs_you` != "" → `kNeedsYou`.
  - `PlanGraphJson` writes `{branching, nodes:[{id,title,kind,state,branch,tool,site,needs_you,
    after,column,row,depth}]}` with `json::Value`.
- [ ] **Step 4: Run them.** Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "Build and lay out the research plan graph"`.

### Task 10: plan.html, the Plan tab and the plan edit messages

**Files:**
- Create: `src/main/pages/plan.html`
- Create: `src/agent/plan_edits.{h,cc}` (CEF-free) and `tests/agent/plan_edits_test.cpp`
- Modify: `src/main/local_page.h` (`LocalPageKind::kPlan`), `src/main/local_pages_html.h`,
  `src/main/browser_window.{h,cc}`, `src/main/browser_window_research.cc`
- Test: `tests/browser_window_pages_test.cpp`, `tests/design/test_local_pages.py`

**Interfaces:**
- Consumes: `BuildPlanGraph` and `PlanGraphJson` (Task 9), and `ThemeJson` keys (Task 3).
- Produces:

```cpp
struct PlanEdits {
    std::set<std::string> skipped;
    std::map<std::string, std::string> instructions;  // id → edited instruction
};
// One user message to the agent describing the person's edits and the run choice.
[[nodiscard]] std::string BuildPlanRunMessage(const std::vector<AcpPlanEntry>& plan,
                                              const PlanEdits& edits,
                                              std::optional<std::string> only_branch);
[[nodiscard]] std::string BuildExpandMessage(std::string_view node_id);
[[nodiscard]] std::string BuildBranchFromMessage(std::string_view node_id);
// Keeps the person's skips across a fresh plan update: drops ids no longer present.
[[nodiscard]] PlanEdits CarryEditsOver(const PlanEdits& edits,
                                       const std::vector<AcpPlanEntry>& next_plan);
```

  The `plan.html` ↔ window messages are `{type:"select",id}`, `{type:"skip",id}`,
  `{type:"restore",id}`, `{type:"instruction",id,text}`, `{type:"branch_from",id}`,
  `{type:"expand",id}`, `{type:"run"}`, `{type:"run_branch",branch}`, `{type:"cancel"}`, and
  `{type:"view",mode:"graph"|"list"}`. The window pushes
  `islandRender({theme, graph, edits, status, selected})`.

- [ ] **Step 1: Failing tests** in `plan_edits_test.cpp`:

```cpp
TEST(PlanEditsTest, RunMessageListsSkipsAndInstructions) {
    PlanEdits edits{.skipped = {"c"}, .instructions = {{"a2", "Only bug issues"}}};
    const std::string m = BuildPlanRunMessage(SpecPlan(), edits, std::nullopt);
    EXPECT_NE(m.find("Skip: c"), std::string::npos);
    EXPECT_NE(m.find("a2: Only bug issues"), std::string::npos);
    EXPECT_NE(m.find("Run the whole plan"), std::string::npos);
}
TEST(PlanEditsTest, RunBranchNamesTheBranch) {
    EXPECT_NE(BuildPlanRunMessage(SpecPlan(), {}, "A").find("Run only branch A"),
              std::string::npos);
}
TEST(PlanEditsTest, SkipsSurviveAPlanUpdateForIdsThatStillExist) {
    PlanEdits edits{.skipped = {"c", "gone"}};
    const PlanEdits next = CarryEditsOver(edits, SpecPlan());
    EXPECT_EQ(next.skipped, (std::set<std::string>{"c"}));
}
TEST(PlanEditsTest, ExpandAndBranchMessages) {
    EXPECT_EQ(BuildExpandMessage("a2"), "Expand step a2");
    EXPECT_EQ(BuildBranchFromMessage("a"), "Add a branch after step a");
}
```

  Also put `SpecPlan()` (the 10-entry plan from spec §5 and Task 9) in a shared test header
  `tests/agent/plan_fixture.h`. In `browser_window_pages_test.cpp`, test that `StartResearch`
  opens a tab whose URL is the plan page and that it is pinned at the top of the active space.
  Test that a `skip` message re-renders with the node in `edits.skipped`. Test that `run` sends
  exactly one agent message, built by `BuildPlanRunMessage`.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement `plan_edits`.**
- [ ] **Step 4: Implement `plan.html`** to the spec §4.6 layout:
  - a toolbar (title, status pill, the Graph/List segmented control, Tidy up);
  - a graph canvas with a 20 px dot grid, one inline `<svg>` for edges (cubic, ±60 px control
    points, branch colors from `--branch-*`, dashed edges into skipped nodes), and absolutely
    positioned `<button>` nodes at `left = 16 + column * 188`, `top = 16 + row * 112`, 148×92;
  - lanes per branch, an inspector, a legend and zoom.
  The List view is a nested `<ul role="tree">` with the same data. Keyboard: arrows move along
  edges, ↵ focuses the instruction field, ⌫ skips or restores, B runs `branch_from`, ⌘↵ runs, and
  esc returns focus to the canvas. Branch from here is disabled at `depth == 3` with the tooltip
  "Plans branch at most 3 levels deep". Running states follow spec §4.6 "While it runs". Use only
  CSS variables from Task 3.
- [ ] **Step 5: Wire the window.** `StartResearch` opens the Plan tab (pinned, first in the
  space) instead of only the agent panel. Each `kPlan` ACP event re-renders the page through
  `BuildPlanGraph(entries, CarryEditsOver(...).skipped)`. Messages map to the functions above.
  `cancel` calls `CancelResearch`.
- [ ] **Step 6: Add a pytest** in `test_local_pages.py`. `plan.html` must contain no external
  URLs, must define `islandRender`, and every `<button>` it builds must get an accessible name.
  Check this with a regex on its template strings, the same way the existing page tests work.
- [ ] **Step 7: Run ctest and pytest.** Take a dev capture with a fixture plan: add a dev action
  `plan_fixture` in `dev_capture.cc` that renders `SpecPlan()` into a Plan tab. Look at the light
  and dark PNGs.
- [ ] **Step 8: Commit** `git commit -m "Open research as a Plan tab with an editable graph"`.

### Task 11: Research tabs grouped under the Plan tab

**Files:**
- Modify: `src/main/tab.{h,cc}` (`group_parent`), `src/main/space.{h,cc}` (ordering),
  `src/main/agent_navigation.cc` (tabs the agent opens during research), `src/main/browser_chrome.cc`
  (indent + kicker), `src/main/browser_window.cc` (close confirmation), `src/main/session_store.cc`
- Test: `tests/space_test.cpp`, `tests/tab_test.cpp`, `tests/session_store_test.cpp`,
  `tests/browser_window_test.cpp`

**Interfaces:**
- Produces: `void Tab::SetGroupParent(std::optional<TabId>)`,
  `std::optional<TabId> Tab::group_parent() const`, `void Tab::SetGroupLabel(std::string)` (the
  branch kicker), and `TabId Space::AppendChildTab(TabId parent, Tab tab)`, which inserts the tab
  after the parent's last child.

- [ ] **Step 1: Failing tests:**

```cpp
TEST(SpaceTest, ChildTabsFollowTheirParent) {
    Space space = MakeSpace({"plan", "other"});
    const TabId plan = space.tabs()[0].id();
    space.AppendChildTab(plan, MakeTab("c1"));
    space.AppendChildTab(plan, MakeTab("c2"));
    EXPECT_EQ(Titles(space), (std::vector<std::string>{"plan", "c1", "c2", "other"}));
}
TEST(SpaceTest, ClosingAChildKeepsTheGroup) {
    Space space = MakeSpace({"plan"});
    const TabId plan = space.tabs()[0].id();
    const TabId c1 = space.AppendChildTab(plan, MakeTab("c1"));
    space.AppendChildTab(plan, MakeTab("c2"));
    space.RemoveTab(c1);
    EXPECT_EQ(space.tabs()[1].group_parent(), plan);
}
TEST(SpaceTest, RemovingTheParentUngroupsChildren) {
    Space space = MakeSpace({"plan"});
    const TabId plan = space.tabs()[0].id();
    space.AppendChildTab(plan, MakeTab("c1"));
    space.RemoveTab(plan);
    EXPECT_FALSE(space.tabs()[0].group_parent().has_value());
}
```

  In `session_store_test.cpp`, test that `group_parent` and `group_label` round-trip. In
  `browser_window_test.cpp`, test that closing the Plan tab while research runs shows the
  confirmation ("Stop the research?" with Stop / Keep running). Keep running leaves the tab open
  and the research running, and Stop calls `CancelResearch` and closes it. A test hook replaces the
  dialog with a canned answer.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement** the model. While `research_` is active, tabs opened by agent
  navigation call `AppendChildTab(plan_tab_id, …)` with the label of the plan node being run.
  Sidebar rows with a parent are indented 16 DIP and show the label as a 10 px `text_secondary`
  kicker. Selecting a plan node whose step opened a tab selects that tab (the window keeps an
  `id → TabId` map). The Result opens as a Report tab in the same group.
- [ ] **Step 4: Run ctest.** Take a dev capture with the `plan_fixture` action plus two fake child
  tabs and look at it.
- [ ] **Step 5: Commit** `git commit -m "Group the pages research opens under its Plan tab"`.

### Task 12: Agent prompt for branching plans

**Files:**
- Modify: `src/main/research_prompt.cc`
- Test: `tests/research_prompt_test.cpp`

**Interfaces:**
- Consumes: the `_meta.island` shape (spec §5), `kMaxBranchDepth` (Task 9).

- [ ] **Step 1: Failing test.** `BuildResearchPrompt("q")` mentions `_meta.island`, the keys
  `id`, `after`, `branch` and `kind`, the rule "at most 3 levels of branching", and "wait for
  approval before running".
- [ ] **Step 2: Run it and confirm it fails.**
- [ ] **Step 3: Implement.** Extend the prompt with one compact paragraph and one JSON example
  entry (copied from spec §5).
- [ ] **Step 4: Run it.** Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "Ask research agents for branching plans"`.

### Task 13: Docs and changelog

**Files:**
- Modify: `CHANGELOG.md` (`[Unreleased]`), `AGENTS.md` (Current state: the Native default,
  the Plan tab), `src/main/AGENTS.md`, `src/agent/AGENTS.md` (plan metadata and graph),
  `tests/AGENTS.md` (new test files), `DESIGN.md` §5 Components (address indicators, plan nodes)

- [ ] **Step 1:** Write the entries: Native look as default with the Look preset; address
  indicators; Shortcuts recording and the cheatsheet; the research plan graph in a Plan tab with
  grouped tabs; branching depth 3.
- [ ] **Step 2:** Run `python3 -m pytest tests/design tests/version -q` and
  `python3 scripts/version.py check`. Expected: PASS.
- [ ] **Step 3: Commit** `git commit -m "Document the Native look and the plan graph"`.

---

## Self-review notes

- Spec coverage: §2 tokens → T1/T3; §3 anatomy and the Look preset → T2/T4/T5; §4.1–4.3 Start,
  Agent and ⌘K use the restyled tokens (T3/T4; the ⌘K rows pick up `selection` in T4); §4.4
  Appearance → T2; §4.5 Shortcuts and the cheatsheet → T6/T7; §4.6 Plan → T9/T10/T11; §5 protocol
  → T8/T10/T12; §6 placement is followed throughout; §7 acceptance 1–6 map to the dev captures in
  T4/T5/T10/T11, the T2 tests, the T6/T7 tests, the T9/T10 tests, and the six-target CI run after
  push.
- Types: `LookPreset`, `AppearanceSettings::look`, `ChromeTokens::{selection,…}`,
  `AcpPlanEntry::island`, `PlanGraph`, `PlanEdits` and `Tab::group_parent` are used with the same
  names in every task that touches them.
