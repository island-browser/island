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
| `lifecycle_config_test.cpp` | Shutdown/lifecycle constants |
| `navigation_state_test.cpp` | Back/forward/reload availability |
| `space_test.cpp` | `Space`, ordering, active selection, `SplitPairing` |
| `startup_options_test.cpp` | Command-line parsing |
| `tab_test.cpp` | Move-only `Tab` model |
| `keymap_test.cpp` | Shortcut parsing, defaults, overrides, conflicts, platform key mapping |
| `browser_import_test.cpp` | mozLz4 decoding, Firefox bookmark trees, Arc sidebar parsing |
| `browser_window_pages_test.cpp` | Headless Settings / All-tabs / import / shortcut seams of `BrowserWindow` |
| `local_page_test.cpp` | Local page `data:` URLs and the console-message bridge |
| `updater_test.cpp` | Updater core: SHA-256 vectors, SemVer precedence, release/asset/checksum selection, URL allow-list, install detection, apply-script text (and a real run of the Linux script), the `Updater` state machine with a fake fetcher |
| `prefs_store_test.cpp` | Preferences round-trip, optional keys (agent, shortcuts, update checks), schema errors |
| `session_store_test.cpp` | Session JSON round-trip — **not currently listed in `CMakeLists.txt`**, so it has never run (PR #27 in flight) |

## Test targets

| Target | Sources | Registration |
|--------|---------|--------------|
| `island_tests` | `address_bar_model`, `address_policy`, `chrome/*`, `design_tokens`, `lifecycle_config`, `navigation_state`, `space`, `startup_options`, `tab` | `gtest_discover_tests` |
| `island_resource_tests` | `resources/app_resources_test.cc` | `add_test(NAME ResourcePaths)` |
| `island_cef_tests` | `cef_address_parser_test.cpp` (+ `address_policy.cc`) | `gtest_discover_tests`; macOS bundle on Apple, CEF runtime staged beside the binary |
| `island_search_tests` | `search/*_test.cpp` (+ `bench/search/benchmark_corpus.cc`) | `gtest_discover_tests`, **only under `-DISLAND_ENABLE_SEARCH=ON`** |
| `island_search_posting_codec_harness` | `search/posting_codec_test.cpp` | `add_test(NAME PostingCodecStandaloneHarness)`, same flag; own `main()`, not a GoogleTest binary |

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
| `search/` | Phase S0 search unit tests, built only under `ISLAND_ENABLE_SEARCH=ON` (see `search/AGENTS.md`) |
| `deps/` | pytest suite for `deps/` (see `deps/AGENTS.md`) |
| `package/` | pytest suite for `scripts/package*.py` (see `package/AGENTS.md`) |
| `design/` | pytest design-token drift guard (see `design/AGENTS.md`) |
| `version/` | pytest suite for `scripts/version.py` and the repository's version consistency (see `version/AGENTS.md`) |
| `manual/` | Human visual acceptance checklists (see `manual/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Adding a `*_test.cpp` file does **not** register it. Every C++ test source must be added by hand
  to a target in `CMakeLists.txt`, along with the `src/main/*.cc` files it exercises.
- Two source sets are on disk but in no target at all: `session_store_test.cpp` (PR #27 in flight)
  and `integration/`. A default `ctest` run covers neither.
- `search/` is a third case, and a different one: it *is* registered, but only inside
  `if(ISLAND_ENABLE_SEARCH)`, and that option defaults to `OFF`. Registered is not the same as run
  by default — do not cite a default `ctest` run as evidence for anything under `search/`.
- GoogleTest is built with `gtest_force_shared_crt OFF` so it matches CEF's static CRT on Windows.
  Do not flip this.
- The first configure downloads GoogleTest and therefore needs network access.

### Testing Requirements

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R <TestName> --output-on-failure
```

On `origin/main` that reports **103 tests**. Adding `-DISLAND_ENABLE_SEARCH=ON` at configure time
raises it to **187** by including the two search targets.

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
