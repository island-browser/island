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

It links `island_search` and the bench corpus generator only — no CEF, no GoogleTest — so it builds
anywhere the search kernel does.

## Running it

```bash
cmake -G Ninja -B build-ninja -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-ninja
./build-ninja/bench/search_membench                        # default: 100k docs, 32 MB ceiling
./build-ninja/bench/search_membench --documents 10000
./build-ninja/bench/search_membench --ceiling-bytes 33554432
ctest --test-dir build-ninja -R SearchMemBench --output-on-failure
```

Use Ninja for this tree. A Unix Makefiles build directory reports stale results, because compile,
archive and link land in the same wall-clock second.

Flags: `--documents N`, `--ceiling-bytes N`, `--seed N`, `--segment-path PATH`.

Exit codes: `0` within ceiling, `1` over ceiling, `2` bad arguments or a failed index operation,
`3` no RSS sampler on this platform (it refuses to report a budget result nothing measured).

Output is one machine-readable line with a stable field order, so CI can assert on it without
parsing prose:

```
membench documents=100000 ingested=100000 refused=0 hits=160 baseline_bytes=950584 \
  ingest_bytes=98025928 flush_bytes=160973352 query_bytes=161956392 peak_bytes=161956392 \
  delta_bytes=161005808 ceiling_bytes=33554432 result=fail
```

The query mix is sampled from titles the run actually ingested. The corpus vocabulary is generated
from a hash, so hard-coded query terms would match nothing and the post-query checkpoint would
measure an untouched index — which is exactly the memory the gate exists to observe.

## Current result: improved ~2.7x, the S0 budget is still NOT met

The design targets **≤ 32 MB delta at 100,000 documents**. Measured on macOS arm64 after the
arena-backed MemTable and the streaming segment writer:

| Documents | after ingest | after flush | delta (peak − baseline) | bytes/doc |
|---:|---:|---:|---:|---:|
| 100,000 | ~59 MB | ~60 MB | ~60 MB | ~604 |

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
term dictionary ~0.5 MB -- about 41 MB structural, with the rest of the measured 60 MB in allocator
churn and per-term vector slack.

The remaining gap to 32 MB is dominated by two things, neither a quick fix:

- **Raw text is 202 B/doc.** The 320 B/doc budget leaves ~118 B/doc for postings, records, the
  dictionary, and all allocator overhead, so the gate cannot close on this corpus without
  compressing or capping stored text (a store/ format change with a version bump).
- **Postings are `uint64` per entry.** A `uint32` pool (DocIds are far below 2^32 in practice) or a
  flush-time CSR rebuild would remove most of the ~12 MB posting cost plus its vector slack, at the
  cost of type ripple through the codec and ranker seams.

The gate stays **non-blocking** until a change actually brings the delta under 32 MB -- flip it in
the same change, per the original contract above.

## CI

The search workflow runs the gate on all six native targets and prints its line. **The step is
currently non-blocking**, because the budget above is not met and a blocking gate would make every
pull request red for a reason unrelated to its own changes. Flip it to blocking in the same change
that brings the delta under 32 MB — that is the point of having recorded the number here.
