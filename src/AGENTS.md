<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# src

## Purpose

Container for all C++20 application source. Island is built directly on CEF and native `cef_views`;
there is no web app shell, no bundler, and no scripting runtime in this tree.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `main/` | The browser application, its chrome, and platform entrypoints (see `main/AGENTS.md`) |
| `search/` | Phase S0 local search primitives, built only under `ISLAND_ENABLE_SEARCH=ON` (see `search/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Both `src/main/` and `src/search/` have a `CMakeLists.txt`. The root reaches `src/main`
  unconditionally, but reaches `src/search` only inside `if(ISLAND_ENABLE_SEARCH)` — an option that
  defaults to `OFF`, so a default build produces no search targets.
- `src/search/` also works as its own source root
  (`cmake -S src/search -DISLAND_ENABLE_SEARCH=ON`), which is how CI builds it without CEF.
- Never add a global `include_directories()`/`link_libraries()`; use target-scoped commands.
- Use explicit `std::` qualification. No `using namespace std;`.

### Testing Requirements

C++ sources here are compiled a second time directly into the test binaries defined in
`tests/CMakeLists.txt`. Adding a `.cc` to a target does not make it tested — check whether the
matching test executable also lists it.

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

That covers `src/main/` only (103 tests on `origin/main`). To include `src/search/`, configure with
`-DISLAND_ENABLE_SEARCH=ON` (187 tests).

## Dependencies

### External

- CEF binary distribution under `third_party/cef/` (vendored by `scripts/setup_deps.sh`)

<!-- MANUAL: -->
