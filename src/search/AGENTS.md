<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# search

## Purpose

Phase S0 local-search primitives: tokenization, query parsing, posting-list encoding, BM25-lite
ranking, a byte-budgeted LRU cache, and a memory sampler. All code is CEF-free and header-first so
it can be unit-tested and benchmarked in isolation.

## Status

**Built and tested only behind an opt-in flag.** `src/search/CMakeLists.txt` defines the
`island_search` static library, and the root `CMakeLists.txt` adds this directory with
`add_subdirectory(src/search)` — but only inside `if(ISLAND_ENABLE_SEARCH)`, and that option is
declared `OFF` by default. A plain `cmake -B build -S .` therefore builds **none** of this code.

`.github/workflows/search.yml` does exercise it on every pull request and every push to `main`,
across all six targets, using the standalone configuration described under Testing Requirements.

Read `docs/superpowers/specs/2026-08-10-island-search-phase-s0-design.md` and
`docs/superpowers/plans/2026-08-10-island-search-phase-s0.md` before changing anything here, and do
not treat these interfaces as a settled contract.

## Key Files

| File | Description |
|------|-------------|
| `tokenizer.{h,cc}` | Normalizes text into ranking/index terms |
| `query_parser.{h,cc}` | Parses a user query string into structured terms |
| `posting_codec.{h,cc}` | LEB128 delta encoding in 128-id blocks; non-throwing decode via `PostingCodecError` |
| `ranker.{h,cc}` | BM25-lite with `RankingWeights`, `CorpusStats`, title/url field weighting, and a half-life recency boost |
| `byte_lru_cache.h` | Header-only LRU cache bounded by total byte size |
| `mem_sampler.{h,cc}` | Process memory sampling used by the memory-budget benchmarks |

## For AI Agents

### Working In This Directory

- Everything lives in `namespace island::search`.
- The decode hot path must not throw; report all failures through the result enums
  (`PostingCodecError::{kOk,kTruncated,kCorrupt}`).
- `kPostingBlockSize = 128` is bounded by the single-byte block header (7 usable bits, `0` reserved
  as corrupt). Do not raise it without changing the header format.
- CEF 150 memory-compatibility constraints apply to anything that samples or budgets memory — check
  the project wiki page on that topic before tuning `mem_sampler`.

### Testing Requirements

Unit tests live in `tests/search/`. They **are** registered with CTest, but only when
`ISLAND_ENABLE_SEARCH` is `ON` — which is not the default. `ctest` output from a default build says
nothing about this directory.

Standalone (no CEF, no `scripts/setup_deps.sh` needed — this is what CI runs):

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

Or from the browser root, which builds the browser and the search kernel together:

```bash
cmake -B build-search-root -S . -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search-root
ctest --test-dir build-search-root --output-on-failure
```

Configuring `src/search` standalone without the flag is a hard `FATAL_ERROR`, not a silent skip.

## Dependencies

### Internal

- `bench/search/` — deterministic corpus generator used by the memory/perf benchmarks
- `tests/search/` — the unit suite for every file here

### External

- C++20 standard library only (`<span>`, `<unordered_map>`, …). No CEF, no third-party libraries.

<!-- MANUAL: -->
