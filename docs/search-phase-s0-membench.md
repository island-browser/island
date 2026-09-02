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

## Current result: the S0 budget is NOT met

The design targets **≤ 32 MB delta at 100,000 documents**. Measured on macOS arm64:

| Documents | after ingest | after flush | delta (peak − baseline) | bytes/doc |
|---:|---:|---:|---:|---:|
| 10,000 | 11.6 MB | 18.8 MB | 18.2 MB | ~1,817 |
| 25,000 | 26.1 MB | 42.7 MB | 42.1 MB | ~1,684 |
| 50,000 | 50.3 MB | 80.9 MB | 80.6 MB | ~1,612 |
| 100,000 | 97.3 MB | 159.7 MB | 159.7 MB | ~1,597 |

Growth is **linear and stable at roughly 1.6 KB per document**, against a budget that allows about
320 B per document. The gate is therefore over by ~5×, and the 32 MB ceiling is currently met only
up to roughly 20,000 documents.

Two distinct contributors, visible in the checkpoint split:

1. **The MemTable itself costs ~1 KB per document** (97 MB at 100k). Per document it holds two
   `std::string`s, an `optional<std::string>` tag, and a `vector<TermFrequencyEntry>`, each with its
   own allocation and its own growth slack; the per-term posting vectors add more. The stored bytes
   (url + title) are on the order of 100 B per document, so most of this is container and allocator
   overhead rather than data.
2. **`Flush` adds a further ~60%** (97 MB → 160 MB). `EncodeSegment` materializes the entire segment
   image in one `std::vector<std::uint8_t>` before writing, and the freed pages are not returned to
   the OS, so the peak persists in RSS. The re-mapped segment's resident pages add to this.

Neither is a defect in the gate; the gate is doing its job by reporting them. Closing the budget is
its own unit of work — plausible directions are arena-backed document storage instead of per-document
`std::string`, `reserve()` on posting vectors to remove doubling slack, and a streaming segment writer
that never holds the whole image in memory.

## CI

The search workflow runs the gate on all six native targets and prints its line. **The step is
currently non-blocking**, because the budget above is not met and a blocking gate would make every
pull request red for a reason unrelated to its own changes. Flip it to blocking in the same change
that brings the delta under 32 MB — that is the point of having recorded the number here.
