<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# tests

## Purpose

Two independent test systems live side by side here:

- **C++ / GoogleTest**, driven by `CMakeLists.txt` and run through `ctest`.
- **Python / pytest**, for the dependency, packaging, and design-token tooling. These are
  discovered from the repository root and locate the code under test by path
  (`Path(__file__).resolve().parents[2]`) plus package-relative imports. There is no root
  `conftest.py` on `main` and none is needed.

## Key Files

| File | Description |
|------|-------------|
| `CMakeLists.txt` | Fetches GoogleTest v1.15.2 and defines the three C++ test executables |
| `address_bar_model_test.cpp` | Address field text/editing state |
| `address_policy_test.cpp` | Typed-input policy decisions (CEF-free) |
| `cef_address_parser_test.cpp` | CEF-linked URL parsing (`island_cef_tests`) |
| `design_tokens_test.cpp` | Token palette values |
| `address_input_test.cpp`, `search_engine_test.cpp` | Address-bar URL-vs-search heuristic and the search engine table |
| `lifecycle_config_test.cpp` | Shutdown/lifecycle constants |
| `navigation_state_test.cpp` | Back/forward/reload availability |
| `space_test.cpp` | `Space`, ordering, active selection, `SplitPairing` |
| `startup_options_test.cpp` | Command-line parsing |
| `tab_test.cpp` | Move-only `Tab` model |
| `keymap_test.cpp` | Shortcut parsing, defaults, overrides, conflicts, platform key mapping |
| `browser_import_test.cpp` | mozLz4 decoding, Firefox bookmark trees, Arc sidebar parsing |
| `bookmark_store_test.cpp` | Schema 2 `BookmarkStore`: v1 migration and its backup and write-failure policy, invalid-file backup, unreadable-file read-only, atomic save rollback, folders (including atomic delete-and-move/delete), favorites, import de-duplication, schema validation |
| `browser_window_pages_test.cpp` | Headless Settings / All-tabs / Bookmarks-page state and message-dispatch, including partial Chrome-import result feedback / shortcut / agent-provider seams of `BrowserWindow`; Native ThemeJson look and palette command icon contracts |
| `history_recorder_test.cpp` | History URL/privacy filtering, deduplication and visit ranking, cap compaction, reopen rebuild, and clear |
| `local_page_test.cpp` | Local page `data:` URLs and the console-message bridge |
| `updater_test.cpp` | Updater core: SHA-256 vectors, SemVer precedence, release/asset/checksum selection, URL allow-list, install detection, apply-script text (and a real run of the Linux script), the `Updater` state machine with a fake fetcher |
| `prefs_store_test.cpp` | Preferences round-trip, optional keys (history, agent provider, shortcuts, update checks), agent-command migration, schema errors |
| `session_store_test.cpp` | Session JSON round-trip, including optional `favorite_tabs` records (old files load with none), the 0.10 optional space members, the 0.9.0 fixture in `tests/fixtures/session/session-0.9.0.json` (`ISLAND_TEST_FIXTURE_DIR`), and unknown members being ignored |
| `session_save_scheduler_test.cpp` | Debounce, max latency, clean after success, cancel after close, and retry after a failed save, with explicit `time_point`s (no real clock) |
| `space_routing_test.cpp` | Host routing: subdomain and longest match, one space per host, non-web URLs, pattern normalization (URL, port, trailing dot, `www.`), rejected host forms, and no lookalike matches |
| `data_paths_test.cpp`, `support/scoped_data_home.h` | Data directory and web-data root rules. `ScopedDataHome` gives a test a fresh temp HOME, XDG_DATA_HOME, APPDATA, LOCALAPPDATA, USERPROFILE and `ISLAND_DEV_CACHE_DIR` (`<home>/web`), restores each variable exactly and removes the directory; use it instead of a local HOME swap. It changes the process environment, so such tests must not run in parallel |
| `favorite_tabs_test.cpp` | CEF-free favorite virtual-tab registry: ensure, reset-on-close, disposal by key, bookmark and scope, and the session form |
| `address_indicators_test.cpp` | Address-field shield count, its 9999+ cap and tooltip |
| `appearance_test.cpp` | Appearance prefs round-trip and the Native look written by default |
| `content_blocker_policy_test.cpp`, `content_blocker_test.cpp`, `content_blocker_stub_test.cpp` | Blocker request types, bypasses and refresh policy; Rust adblock matching; the disabled build that always allows requests |
| `favicon_policy_test.cpp`, `find_bar_model_test.cpp`, `peek_state_test.cpp`, `reader_mode_test.cpp`, `start_tiles_test.cpp`, `tab_sleep_policy_test.cpp` | Favicon rules, find-bar state, the peek card state, Reader eligibility and settings, start-page speed-dial tiles, and tab sleep decisions |
| `overlay_lifetime_test.cpp`, `settings_focus_test.cpp`, `report_store_test.cpp`, `research_prompt_test.cpp` | Overlay create/destroy rules, repeated Settings section focus, the Reports list, and the research prompt, space name and report extraction |
| `chrome/collapsed_rail_test.cpp` | Icon rail metrics: buttons and scrollbar fit 52 DIP, list height with gaps and separator |
| `agent/plan_graph_test.cpp`, `agent/plan_edits_test.cpp` (fixture `agent/plan_fixture.h`) | Plan graph building and layout (branches, joins, depth), and the plan edit messages |
| `agent/action_gate_test.cpp` | Action gate classification: password and card fields, consequential words, submits, Enter in forms, scripts, foreign navigation and tab close, host and origin matching |
| `design/test_native_look_contract.py`, `design/test_reader_extract.py` | Native look color drift guard (pytest); Reader extraction heuristic run against `reader_extract.js` (pytest) |
| `design/test_segment_outline.py` | Selected segments on Settings, Start and Plan pair the dark fill with an inset `--segment-outline` (pytest) |
| `space_icon_cache_test.cpp` | `SpaceIconCache` keys (id, size, color), reuse, failed-load retry and `Clear()` |
| `icon_catalog_test.cpp` | `IconCatalog::LoadSpaceIcon` reads each icon once per key and rejects unknown ids before the reader |

## Test targets

| Target | Sources | Registration |
|--------|---------|--------------|
| `island_tests` | `address_bar_model`, `address_policy`, `chrome/*`, `design_tokens`, `lifecycle_config`, `navigation_state`, `space`, `startup_options`, `tab` | `gtest_discover_tests` |
| `island_resource_tests` | `resources/app_resources_test.cc` | `add_test(NAME ResourcePaths)` |
| `island_cef_tests` | `cef_address_parser_test.cpp` (+ `address_policy.cc`) | `gtest_discover_tests`; macOS bundle on Apple, CEF runtime staged beside the binary |
| `island_search_tests` | `search/*_test.cpp` (+ `bench/search/benchmark_corpus.cc`) | `gtest_discover_tests`; included in root and standalone search builds |
| `island_search_posting_codec_harness` | `search/posting_codec_test.cpp` | `add_test(NAME PostingCodecStandaloneHarness)`; own `main()`, not a GoogleTest binary |

`island_tests` and `island_cef_tests` both link CEF. The difference is staging: only
`island_cef_tests` copies the CEF framework/resources next to the binary, and it is the only target
with a source that calls `CefInitialize` (`cef_address_parser_test.cpp`). Keep it that way — a
`CefInitialize` added to `island_tests` would fail on the missing runtime.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `chrome/` | Native chrome contract and style tests (see `chrome/AGENTS.md`) |
| `resources/` | Runtime resource path resolution (see `resources/AGENTS.md`) |
| `integration/` | CEF-linked chrome integration test, not in any CMake target (see `integration/AGENTS.md`) |
| `search/` | Phase S0 search unit tests, built in the default root suite and standalone search lane (see `search/AGENTS.md`) |
| `deps/` | pytest suite for `deps/` (see `deps/AGENTS.md`) |
| `package/` | pytest suite for `scripts/package*.py` (see `package/AGENTS.md`) |
| `design/` | pytest design-token drift guard (see `design/AGENTS.md`) |
| `version/` | pytest suite for `scripts/version.py` and the repository's version consistency (see `version/AGENTS.md`) |
| `manual/` | Human visual acceptance checklists (see `manual/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Adding a `*_test.cpp` file does **not** register it. Every C++ test source must be added by hand
  to a target in `CMakeLists.txt`, along with the `src/main/*.cc` files it exercises.
- One source set is on disk but in no target at all: `integration/`. A default `ctest` run does not
  cover it.
- `search/` is registered in both the default root build and the standalone search project. Keep
  both source lists complete when adding a test.
- GoogleTest is built with `gtest_force_shared_crt OFF` so it matches CEF's static CRT on Windows.
  Do not flip this.
- The first configure downloads GoogleTest and therefore needs network access.

### Testing Requirements

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R <TestName> --output-on-failure
```

Use `ctest --test-dir build -N` when an exact discovered-test count is needed; the root suite
includes both the browser and search targets.

Python suites run from the repository root:

```bash
python3 -m pytest tests/deps tests/package tests/design tests/version
```

The Python suites are `unittest.TestCase` classes executed through pytest; they are not wired into
`ctest`, so a green `ctest` run says nothing about them.

## Dependencies

### Internal

- `src/main/` — the C++ units under test, compiled directly into the test executables
- `scripts/`, `deps/` — the Python modules under test

### External

- GoogleTest v1.15.2 (CMake `FetchContent`), CEF, pytest

<!-- MANUAL: -->
