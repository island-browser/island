<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# search

## Purpose

Deterministic synthetic corpus generation for the search memory and performance benchmarks. For a
fixed seed and configuration it always produces the same record sequence, so runs are reproducible.

## Key Files

| File | Description |
|------|-------------|
| `benchmark_corpus.h` | Streaming generator API (`Next` / `ForEach`) and configuration |
| `benchmark_corpus.cc` | Implementation |

## For AI Agents

### Working In This Directory

- The streaming API keeps **O(1)** generator state with respect to document count — a 100k-document
  corpus must never be materialized in memory. Any change that accumulates records defeats the
  purpose of this file.
- Generated URLs are absolute `https://example-<bounded>.test/...` documents with no credentials and
  no control characters; document ids and timestamps are deterministic and monotonically increasing.
  `tests/search/benchmark_corpus_test.cpp` asserts these properties.
- Pure C++20, no CEF, no third-party dependencies.

### Testing Requirements

`tests/search/benchmark_corpus_test.cpp` covers this code and is registered with CTest, but only
under `ISLAND_ENABLE_SEARCH=ON`; it does not run in a default build. This file is compiled into
`island_search_tests` rather than into a benchmark target of its own.

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search --output-on-failure
```

Do not cite default `ctest` output as evidence for a change here.

## Dependencies

### Internal

- `src/search/` — consumer of the generated corpus
- `tests/search/benchmark_corpus_test.cpp`

<!-- MANUAL: -->
