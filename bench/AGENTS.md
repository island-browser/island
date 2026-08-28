<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# bench

## Purpose

Container for benchmark support code. Nothing here is part of the shipped browser: no target that
builds by default references this tree.

`search/benchmark_corpus.cc` *is* referenced by CMake, but only behind the opt-in
`ISLAND_ENABLE_SEARCH` flag — `tests/CMakeLists.txt` compiles it into `island_search_tests`, and
`src/search/CMakeLists.txt` does the same in its standalone configuration. It is never linked into
`island_browser_core` or any browser executable.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `search/` | Deterministic synthetic corpus generator for search benchmarks (see `search/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Benchmark code must stay deterministic and CEF-free so results are reproducible and comparable
  across runs and machines.
- Do not link benchmark helpers into `island_browser_core`.

## Dependencies

### Internal

- `src/search/` — the code being benchmarked

<!-- MANUAL: -->
