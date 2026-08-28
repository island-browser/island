<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# search

## Purpose

Unit tests for the Phase S0 search primitives in `src/search/` and for the deterministic benchmark
corpus in `bench/search/`.

## Status

**Registered with CTest, but not built by default.** `../CMakeLists.txt` lists every file here
inside `if(ISLAND_ENABLE_SEARCH)`, and that option defaults to `OFF`. Registered and run-by-default
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
| `mem_sampler_test.cpp` | Memory sampling |
| `benchmark_corpus_test.cpp` | Determinism and streaming (`O(1)` state) of `bench/search/` |

## For AI Agents

### Working In This Directory

- Decode tests must cover `kTruncated` and `kCorrupt` explicitly — the codec never throws, so a
  missing enum assertion silently passes.
- `benchmark_corpus_test.cpp` tests code in `bench/search/`, not `src/search/`; a new target must
  add that include directory.

### Testing Requirements

These tests run only under `ISLAND_ENABLE_SEARCH=ON`. Standalone (no CEF required — this is the
configuration `.github/workflows/search.yml` uses):

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

Five of the six `*_test.cpp` files here plus `bench/search/benchmark_corpus.cc` build into
`island_search_tests`. `posting_codec_test.cpp` is **not** part of that binary: it is a standalone
harness with its own `main()`, registered separately as the `PostingCodecStandaloneHarness` test.

Never cite default `ctest` output as evidence for a change in this directory — it does not cover
it.

## Dependencies

### Internal

- `src/search/` — the units under test
- `bench/search/` — corpus generator under test

<!-- MANUAL: -->
