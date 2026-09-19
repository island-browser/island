# Island — Welcome flow, browser import, and appearance preference (design)

- **Status:** Accepted (user-directed unit, 2026-09-19)
- **Precedes:** Phase 3 U9/U10/U11 completion work
- **Non-goal here:** full settings surface, bookmark management UI (edit/rename/folders beyond
  import), sync, profiles — all remain out of scope per the Phase 3 non-goals.

## Goals

1. A first-run **welcome flow**: on a fresh install (no preferences file) the window shows a
   dismissible welcome overlay that offers (a) importing bookmarks from browsers already
   installed on the machine and (b) an appearance choice. It never blocks browsing: Escape and
   "Just start browsing" both dismiss it.
2. **Bookmark import** from Chromium-family browsers (Chrome, Edge, Brave, Chromium, Vivaldi,
   Opera — shared `Bookmarks` JSON schema) and best-effort Safari on macOS (plist via
   CoreFoundation; graceful "unavailable" when the file cannot be read). Firefox stores bookmarks
   in a SQLite database this unit cannot read without a new dependency, so it is detected and
   reported as unavailable rather than faked.
3. Imported bookmarks are persisted in a validated local store and become **usable immediately**
   through the command palette (a bookmarks group; selecting one navigates the active tab through
   the single existing address-validation path — no second URL parser).
4. An **appearance preference** (System / Light / Dark) chosen in the welcome flow (and changeable
   later from the Browser menu), persisted and applied at startup. `System` keeps today's
   behavior: classify the OS theme from the window's primary background color.

## Non-goals

- No bookmark editing UI, no bookmarks bar, no reading-list/history import.
- No network access: import reads local files only; welcome content is local.
- No new CefWindow, no web content in the overlay (same rules as the Phase 3 palettes).
- The site (`site/`) is a separate unit with its own contract update (DESIGN.md tokens v2).

## Design

### Persistence

- `prefs.json` (`PrefsStore`, same directory as `session.json`): `{"version":1,
  "onboarding_completed":false,"theme":"system"}`. `theme` ∈ `system|light|dark`. Missing file,
  unreadable file, or schema-invalid file behaves as fresh-install defaults (log and fall back —
  identical policy to `SessionStore`).
- `bookmarks.json` (`BookmarkStore`): `{"version":1,"folders":[{"name":"Imported","items":[
  {"title":"…","url":"…"}]}]}`. Import appends into the `Imported` folder, de-duplicating
  (case-insensitive URL match) against existing entries. No automatic pruning.

### Import

- Detection is path-based and test-injectable: `DetectInstalledSources(base_home)` probes the
  well-known per-browser `Bookmarks` file paths (Chromium family) and the Safari bookmarks plist
  on macOS. Detection returns availability + a human-readable name; it never reads file contents.
- `ParseChromiumBookmarks(text)` walks the shared Chromium schema
  (`roots.bookmark_bar|other|synced`, recursive `type:"url"` nodes carrying `name`/`url`),
  keeps traversal order, caps at 500 entries, and drops entries whose `url` is empty. URL
  *policy* (allow-list) is applied later, at click time, by the existing address path — import is
  permissive so a future policy change does not lose data.
- Safari parsing lives behind the platform seam (`src/main/macos/`), mirroring
  `sidebar_hover_mac.mm`/`sidebar_hover_stub.cc`; other platforms compile the stub.

### UI

- The welcome overlay is a single `cef_views` overlay on the one existing window, built and
  governed exactly like `SearchPalette` (created on demand, shown/hidden, never a second
  top-level window, focus trapped while shown, Escape dismisses).
- Layout: title, one-line lede, a System/Light/Dark segmented choice (applies immediately on
  click), the detected-browser checklist with per-source bookmark counts after import, and two
  actions: "Import & start" (runs checked imports, then completes) and "Just start browsing".
- First-run trigger: after `OnWindowCreated` shows the window, `PrefsStore` is loaded; if
  onboarding has not been completed the overlay shows once. A "Show Welcome…" Browser-menu item
  reopens it later without resetting anything.

### Theme preference

- `BrowserWindow` resolves the effective theme through the preference: `system` classifies the
  window background (current behavior); `light`/`dark` force `ChromeTokens::ForTheme`. OS theme
  changes only re-classify while the preference is `system`.

## Testing

- Headless GoogleTest: prefs round-trip/defaults/corruption; bookmark store round-trip, folder
  de-duplication, schema fallback; Chromium parser fixtures (nested folders, dupes, empty URLs,
  malformed JSON); detection with an injected base path; welcome overlay headless no-op paths;
  theme preference resolution.
- The overlay's live rendering, focus trapping, and the Safari seam are manual-pass items
  (recorded in the U10 checklist), same as the other overlays.
