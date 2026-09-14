<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-09-01 | Updated: 2026-09-01 -->

# search/cache

## Purpose

The byte-bounded LRU cache of decoded posting lists that sits between
`SearchIndex::Query` and the memory-mapped segment.

## Status

Unit W5 of the Phase S0 plan. Built only behind `ISLAND_ENABLE_SEARCH`; see `../AGENTS.md`.

## Key Files

| File | Description |
|------|-------------|
| `block_cache.{h,cc}` | `BlockCache` plus `kBlockCacheBytes` (4 MB default ceiling) |

## For AI Agents

### Working In This Directory

- **`BlockCache` does not reuse `../byte_lru_cache.h`, and that is deliberate.** `ByteLruCache`
  stores byte *sizes*, not values, and evicts internally with no callback naming what it dropped. A
  cache built on it would keep the decoded blocks in a side map the ceiling never bounds — the
  accounting would stay under budget while real memory grew without limit. `BlockCache` therefore
  owns its recency list, key map, and running byte total. Do not "simplify" it back onto
  `ByteLruCache` without solving that.
- **Known deviation from the design.** The S0 design keys entries on
  `(term_dict_index, block_index)`. The committed `Segment` reader exposes only `PostingsFor(term)`,
  which decodes a term's whole posting body, so entries are whole posting lists keyed by term. A
  future unit that adds a per-block reader can narrow the key without changing this class's
  contract.
- **Correctness must never depend on cache contents.** An entry larger than the whole ceiling is
  bypassed rather than cached, and the caller must still produce the same answer. There is a test
  that runs the same query against a 4 MB cache and a 1-byte cache and compares results.
- Not internally synchronized. S0 promises no reader/writer concurrency, so there is no mutex and no
  atomics here; `SearchIndex` holds it `mutable` because an LRU promotes on read.

### Testing Requirements

```bash
cmake -G Ninja -B build-ninja -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-ninja
ctest --test-dir build-ninja -R BlockCache --output-on-failure
```

Use Ninja. A Unix Makefiles build directory for this tree reports stale results, because compile,
archive and link land in the same wall-clock second and `make`'s timestamp comparison then treats the
older binary as current. `touch` does not cure it.
