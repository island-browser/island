# Search Phase S0 — memory gate (`search_membench`)

## What it measures

`search_membench` is a standalone executable that drives `SearchIndex` through a full lifecycle on a
deterministic synthetic corpus and samples process RSS at four checkpoints:

| Checkpoint | When |
|---|---|
| `baseline_bytes` | `SearchIndex` constructed, no documents |
| `ingest_bytes` | after ingesting the whole corpus |
| `flush_bytes` | after `Flush()` writes and re-maps the segment |
| `query_bytes` | after a representative query mix |

The gate is on **`delta_bytes` = peak − baseline**, not on absolute RSS. Absolute RSS includes the
executable, the C++ runtime, and the allocator's arenas, none of which are attributable to search.
The segment on disk is not counted; the resident pages of its mapping are, which is why the budget
targets RSS rather than allocated bytes.

RSS is sampled per platform as the design's measurement methodology specifies, and every sampler
counts resident file-backed pages, including the mapped segment's:

| Platform | Sampler |
|---|---|
| macOS | `task_info(MACH_TASK_BASIC_INFO)` → `resident_size` |
| Linux | `/proc/self/statm` resident pages × page size |
| Windows | `GetProcessMemoryInfo` → `WorkingSetSize` |

macOS previously used `TASK_VM_INFO` `phys_footprint`, which leaves out clean file-backed pages, so
the mapped segment's resident pages were never counted and the macOS delta read about 24 MB low.
`MemSampler.CountsTheResidentPagesOfAMappedFile` pins this.

It links `island_search` and the bench corpus generator only — no CEF, no GoogleTest — so it builds
anywhere the search kernel does.

## Running it

```bash
cmake -G Ninja -B build-ninja -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-ninja
./build-ninja/bench/search_membench                        # default: 100k docs, 32 MB ceiling
./build-ninja/bench/search_membench --documents 10000
./build-ninja/bench/search_membench --ceiling-bytes 33554432
```

The gate is **not** a CTest test: it would fail on every run until the budget is met. CTest runs
only `SearchMemBenchSmoke`, which runs a 2,000-document lifecycle with an unbounded ceiling. It
fails when the binary cannot complete the lifecycle or an RSS sample fails, and says nothing about
the budget:

```bash
ctest --test-dir build-ninja -R SearchMemBenchSmoke --output-on-failure
```

Use Ninja for this tree. A Unix Makefiles build directory reports stale results, because compile,
archive and link land in the same wall-clock second.

Flags: `--documents N`, `--ceiling-bytes N`, `--seed N`, `--segment-path PATH`.

Exit codes: `0` within ceiling (`result=pass`), `1` over ceiling (`result=fail`), `2` bad arguments
or a failed index operation, `3` a failed RSS sample or no sampler on this platform
(`result=error`). Every checkpoint is validated before the line is printed: a sample of 0 used to
read as zero growth and print `result=pass` for a budget nothing measured.

Output is one machine-readable line with a stable field order, so CI can assert on it without
parsing prose:

```
membench documents=100000 ingested=100000 refused=0 hits=160 baseline_bytes=1441792 \
  ingest_bytes=58572800 flush_bytes=84770816 query_bytes=85770240 peak_bytes=85770240 \
  delta_bytes=84328448 ceiling_bytes=33554432 result=fail
```

The query mix is sampled from titles the run actually ingested. The corpus vocabulary is generated
from a hash, so hard-coded query terms would match nothing and the post-query checkpoint would
measure an untouched index — which is exactly the memory the gate exists to observe.

## Current result: the S0 budget is NOT met

The design targets **≤ 32 MB delta at 100,000 documents**. Per-platform results, default seed,
Release build, from `search.yml` run
[37470780227](https://github.com/island-browser/island/actions/runs/37470780227) on 2026-10-06
(`after ingest` and `after flush` are absolute RSS; MB = 10^6 bytes):

| Target | Sampler | after ingest | after flush | delta (peak − baseline) | bytes/doc |
|---|---|---:|---:|---:|---:|
| macosarm64 | `resident_size` | 53.4 MB | 79.6 MB | **78.9 MB** (78,888,960) | 789 |
| macosx64 | `resident_size` | 54.9 MB | 79.7 MB | **79.7 MB** (79,749,120) | 797 |
| linux64 | `statm` resident | 50.3 MB | 75.3 MB | **71.5 MB** (71,512,064) | 715 |
| linuxarm64 | `statm` resident | 49.7 MB | 74.7 MB | **71.3 MB** (71,315,456) | 713 |
| windows64 | `WorkingSetSize` | 49.2 MB | 74.2 MB | **71.6 MB** (71,598,080) | 716 |
| windowsarm64 | `WorkingSetSize` | 69.9 MB | 95.0 MB | **92.2 MB** (92,246,016) | 922 |

Every target reports `result=fail`, 2.1–2.9x over the ceiling. On every target, flush adds about 25
MB, which is the mapped segment's resident pages. A local macOS arm64 run on the same day measured a
slightly higher delta: 84.3–85.0 MB over 3 runs. The windowsarm64 ingest high-water is about 20 MB
above the other targets. This run does not explain why; it is a lead for whoever works on the budget.

The macOS delta rose from ~60 MB to ~79–85 MB on 2026-10-06. The kernel did not change; the
sampler did. The extra ~24 MB is the mapped segment's resident pages after `Flush`, which
`phys_footprint` never counted (compare `after flush` with `after ingest`). The earlier ~60 MB figure
(re-verified 2026-09-22, `delta_bytes` 61,112,464) is therefore an under-count, not a regression
baseline.

For reference, the pre-arena numbers this replaced: ~1.6 KB per document, delta ~160 MB.

What changed:

1. **MemTable document storage is arena-backed** (`DocumentArena`, fixed 1 MiB chunks): url/title/
   tag bytes live in one append-only store with 48-byte offset records instead of two `std::string`
   allocations plus a `vector<TermFrequencyEntry>` per document. Per-field term frequencies are no
   longer retained at all -- the ranker re-tokenizes the stored text (it already did), so the only
   cost of dropping them is ingest-time scratch. Fixed chunks also remove the vector-doubling
   reallocation, whose transient old+new coexistence was itself a large RSS high-water.
2. **`Flush` streams** (`WriteSegment`): a size pass computes the header's section lengths, then a
   write pass streams per-record and per-term encodings through a 1 MiB buffer. The whole segment
   image is never materialized; the write produces bytes identical to `EncodeSegment` (the reference
   encoder, kept for format tests), and the flushed segment passes `Segment::Open` unchanged.

Structural accounting at 100k documents (from a direct MemTable probe on the membench corpus):
document text is 20.2 MB (202 B/doc), posting lists 11.6 MB (1.45 M DocIds at 8 B), records 4.8 MB,
term dictionary ~0.5 MB -- about 41 MB structural, with the rest of the ~58 MB measured after
ingest in allocator churn and per-term vector slack. Flush then adds the mapped segment's resident
pages (~25 MB on every target).

The remaining gap to 32 MB is dominated by two things, neither a quick fix:

- **Raw text is 202 B/doc.** The 320 B/doc budget leaves ~118 B/doc for postings, records, the
  dictionary, and all allocator overhead, so the gate cannot close on this corpus without
  compressing or capping stored text (a store/ format change with a version bump).
- **Postings are `uint64` per entry.** A `uint32` pool (DocIds are far below 2^32 in practice) or a
  flush-time CSR rebuild would remove most of the ~12 MB posting cost plus its vector slack, at the
  cost of type ripple through the codec and ranker seams.

The budget verdict stays **non-blocking** until a change actually brings the delta under 32 MB --
make it blocking in the same change, per the original contract above. Now that the segment's
mapped pages are counted, a third contributor is visible: the segment stays mapped and resident
next to the retained MemTable, which holds the same documents.

## CI

The search workflow builds `search_membench` explicitly (`--target search_membench`) and runs the
gate on all six native targets, printing its line. **Only the budget verdict is non-blocking**:

- Exit `1` (over the ceiling) becomes a warning annotation. The budget above is not met, and a
  blocking gate would make every pull request red for a reason unrelated to its own changes.
- Every other failure fails the job: a missing binary, exit `2`, or exit `3` (`result=error`). None
  of these is a measurement.

Drop the exit-1 allowance in the same change that brings the delta under 32 MB; that is the point
of recording the number here. `SearchCiContract.*` in `tests/search/` pins both halves: the build
target, and that nothing but exit 1 is tolerated.
