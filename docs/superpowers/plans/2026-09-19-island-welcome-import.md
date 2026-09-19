# Island — Welcome flow, browser import, appearance preference (plan)

Spec: `docs/superpowers/specs/2026-09-19-island-welcome-import-design.md`

## W1 — Persistence and import model (headless, fully unit-tested)

1. Extract the JSON value tree/writer/parser from `session_store.cc` into
   `src/main/json_util.h` (header-only, CEF-free); `session_store.cc` includes it. No behavior
   change; existing session tests stay green.
2. `src/main/prefs_store.{h,cc}`: `PrefsState`, `PrefsError`, `PrefsStore::Load/Save`,
   `DefaultPrefsFilePath`. Round-trip, defaults, corrupt-file fallback tests
   (`tests/prefs_store_test.cpp`).
3. `src/main/bookmark_store.{h,cc}`: `BookmarkItem`, `BookmarkFolder`, `BookmarkState`,
   `BookmarkStore::Load/Save/MergeImported` (de-dup by case-insensitive URL),
   `DefaultBookmarksFilePath`. Round-trip + de-dup + corrupt-file tests
   (`tests/bookmark_store_test.cpp`).
4. `src/main/bookmark_import.{h,cc}`: source ids/names, `DetectInstalledSources(base_home)`,
   `ParseChromiumBookmarks`, per-source import entry points; 500-entry cap. Fixture-based parser
   tests + detection tests with a temp base home (`tests/bookmark_import_test.cpp`). macOS Safari
   plist read behind `src/main/macos/bookmark_import_mac.mm` + `bookmark_import_stub.cc`
   (compiled per-platform in `tests/CMakeLists.txt` like the hover seam).

**Gate:** `cmake --build build && ctest --test-dir build -R '(Prefs|Bookmark|Session)'`.

## W2 — Window wiring and chrome (W1 done)

5. `BrowserWindow`: load prefs at construction; `ResolvedChromeTheme()` consults the preference;
   `SetThemePreference(theme)` persists and re-applies; welcome overlay show/dismiss seams
   (headless no-ops); first-run trigger after `OnWindowCreated`; "Show Welcome…" and
   Appearance menu items (macOS NSMenu + `SetAccelerator`-free menu route); bookmarks snapshot
   for the palette; `OpenBookmark(url)` navigates via the existing draft-submission path.
6. `welcome_overlay.{h,cc}` following `search_palette`: overlay panel, segmented theme choice,
   source checklist with counts, two completion actions, Escape dismiss, focus trap, theme
   re-assertion.

**Gate:** build + full `ctest`; manual render items recorded for U10.

## W3 — Site and contract refresh (independent of W1/W2)

7. `DESIGN.md` token contract → version 2: replace `phase3_exclusions` with
   `shipped_features` (tabs, tab strip, spaces, split view, command palette, search palette,
   session restore, welcome flow now exist and the site may present them); all color/spacing/
   motion values unchanged. Update `tests/design/test_token_contract.py` rules test to match v2.
8. Rebuild `site/`: landing presenting the shipped feature set with the real chrome anatomy
   (tab strip, space switcher, split divider, palettes), `docs.html` with quick start (build /
   run / test sequences), `sitemap.xml`, `robots.txt`, JSON-LD, canonical URLs. Pages deploy
   already runs on push via `.github/workflows/pages.yml`.

**Gate:** `python3 -m pytest tests/design` green; site screenshotted via the local browser tool
(headless Chromium — no display permission needed).
