<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# search

## Purpose

Unit tests for the Phase S0 search primitives in `src/search/` and for the deterministic benchmark
corpus in `bench/search/`.

## Status

**Registered with CTest, but not built by default.** `src/search/CMakeLists.txt` (standalone
configuration) lists every file here, and `../CMakeLists.txt` lists a subset, both inside
`if(ISLAND_ENABLE_SEARCH)`, and that option defaults to `OFF`. The standalone list is the complete
one: `block_cache_test.cpp`, `search_index_test.cpp`, `membench_checkpoints_test.cpp` and
`search_ci_contract_test.cpp` build only there. Registered and run-by-default
are different claims: a plain `cmake -B build -S .` produces no search targets at all, so default
`ctest` output says nothing about this directory. Pass `-DISLAND_ENABLE_SEARCH=ON` to build and run
them.

## Key Files

| File | Description |
|------|-------------|
| `tokenizer_test.cpp` | Term normalization |
| `query_parser_test.cpp` | Query string → structured terms |
| `posting_codec_test.cpp` | Round-trip plus truncated/corrupt decode paths |
| `ranker_test.cpp` | BM25-lite scoring, field weights, recency boost, tie-breaking by `DocId` |
| `byte_lru_cache_test.cpp` | Byte-budget eviction behaviour |
| `mem_sampler_test.cpp` | Memory sampling, including that a mapped file's resident pages are counted |
| `memtable_test.cpp` | The arena-backed MemTable |
| `segment_test.cpp` | On-disk segment format, writer, and validation/quarantine ladder |
| `varint_crc32c_test.cpp` | Varint and CRC32C primitives |
| `block_cache_test.cpp` | Byte ceiling, strict LRU, bypass, `Put` returning the stored list, hand-written moves |
| `search_index_test.cpp` | The facade: privacy refusals, Flush equivalence, corpus-level avgdl, concurrent queries, failed flushes |
| `membench_checkpoints_test.cpp` | The memory gate's pass/fail/error verdict (`src/search/bench/membench_checkpoints.h`) |
| `search_ci_contract_test.cpp` | Drift guard: `search.yml` builds the gate and tolerates only exit 1; advertised `ctest -R SearchMemBench…` filters match a registered test |
| `benchmark_corpus_test.cpp` | Determinism and streaming (`O(1)` state) of `bench/search/` |

## For AI Agents

### Working In This Directory

- Decode tests must cover `kTruncated` and `kCorrupt` explicitly — the codec never throws, so a
  missing enum assertion silently passes.
- `benchmark_corpus_test.cpp` tests code in `bench/search/`, not `src/search/`; a new target must
  add that include directory.
- `search_ci_contract_test.cpp` reads repository files through the `ISLAND_SEARCH_REPO_ROOT` compile
  definition, which only the standalone target sets.
- Thread-safety tests are only meaningful under TSan and ASan. Build those lanes in separate
  directories, e.g. with `-DCMAKE_CXX_FLAGS="-fsanitize=thread"`, and
  `-fsanitize=address,undefined -fno-sanitize-recover=undefined` (without `-fno-sanitize-recover`,
  UBSan only prints and the test stays green).

### Testing Requirements

These tests run only under `ISLAND_ENABLE_SEARCH=ON`. Standalone (no CEF required — this is the
configuration `.github/workflows/search.yml` uses):

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

Every `*_test.cpp` file here except `posting_codec_test.cpp`, plus
`bench/search/benchmark_corpus.cc`, builds into `island_search_tests`. `posting_codec_test.cpp` is **not** part of that binary: it is a standalone
harness with its own `main()`, registered separately as the `PostingCodecStandaloneHarness` test.

Never cite default `ctest` output as evidence for a change in this directory — it does not cover
it.

## Dependencies

### Internal

- `src/search/` — the units under test
- `bench/search/` — corpus generator under test

<!-- MANUAL: -->
