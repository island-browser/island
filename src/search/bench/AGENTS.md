<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-09-02 | Updated: 2026-09-02 -->

# search/bench

## Purpose

`search_membench`, the Phase S0 memory gate: it drives `SearchIndex` through ingest, flush and a
query mix over a deterministic synthetic corpus and reports process RSS growth against a ceiling.

## Status

Unit W6 of the Phase S0 plan. Built only behind `ISLAND_ENABLE_SEARCH`; see `../AGENTS.md`.

**The gate currently reports a failure**, and that is the honest state of the kernel rather than a
defect in the benchmark. The design targets a 32 MB RSS delta at 100,000 documents; the measured delta
is about 84 MB on macOS arm64. The other platforms have not been measured yet. Per-platform numbers
and the contributors are recorded in `docs/search-phase-s0-membench.md`.

## Key Files

| File | Description |
|------|-------------|
| `search_membench.cc` | The gate: argument parsing, the lifecycle run, the machine-readable output line |
| `membench_checkpoints.h` | Header-only checkpoint arithmetic and the pass/fail/error verdict, unit-tested in `tests/search/membench_checkpoints_test.cpp` |
| `CMakeLists.txt` | The `search_membench` target and the `SearchMemBenchSmoke` CTest test |

## For AI Agents

### Working In This Directory

- **The gate is not a CTest test, on purpose.** It is currently red, and registering it would turn
  the whole unit-test suite red for a reason unrelated to any individual change. Only
  `SearchMemBenchSmoke` is registered: a 2,000-document run with an unbounded ceiling that fails on
  an error and never on the budget. Register the gate with CTest, and drop the exit-1 allowance in
  the CI step in `.github/workflows/search.yml`, in the same change that brings the delta under the
  ceiling.
- **A failed sample is an error, never a pass.** `MemSampler::ResidentBytes()` returns 0 when it
  cannot sample. `Judge` checks every checkpoint before anything is printed and reports
  `result=error` (exit 3). Do not reintroduce a verdict computed from the delta alone: a 0 sample
  reads as zero growth.
- **The gate is on the delta from baseline, not absolute RSS.** Absolute RSS includes the executable,
  the runtime and allocator arenas, none of which are attributable to search.
- **Do not reimplement RSS sampling or the corpus generator here.** `../mem_sampler.{h,cc}` already
  samples per OS behind a compile-time seam, and `bench/search/benchmark_corpus.{h,cc}` at the
  repository root already generates the deterministic corpus. The S0 plan lists `rss_sampler.*` and
  `corpus_gen.*` as W6 files because it was written before those landed in earlier units.
- **Query terms must be sampled from the ingested corpus.** The vocabulary is generated from a hash,
  so hard-coded terms match nothing and the post-query checkpoint would measure an untouched index —
  which is exactly the memory the gate exists to observe. An early version of this benchmark had
  that bug and reported `hits=0`.
- The output line has a stable field order so CI can assert on it without parsing prose. Add fields
  at the end; do not reorder existing ones.

### Testing Requirements

```bash
cmake -G Ninja -B build-ninja -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-ninja
./build-ninja/bench/search_membench --ceiling-bytes 33554432
```

Exit codes: `0` within ceiling, `1` over, `2` bad arguments or a failed index operation, `3` a failed
RSS sample or no sampler on this platform (`result=error`). CI tolerates only `1`.

```bash
ctest --test-dir build-ninja -R "SearchMemBenchSmoke|MembenchCheckpoints|SearchCiContract" --output-on-failure
```
 Use Ninja — a Makefiles build directory for this tree reports stale
results.
