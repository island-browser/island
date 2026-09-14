<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-09-01 | Updated: 2026-09-01 -->

# search/index

## Purpose

`SearchIndex`, the Ingest / Query / Flush facade — the only type a caller outside this module needs.
It owns the MemTable, the active read-only `Segment`, and the `BlockCache`.

## Status

Unit W5 of the Phase S0 plan. Built only behind `ISLAND_ENABLE_SEARCH`; see `../AGENTS.md`.

## Key Files

| File | Description |
|------|-------------|
| `search_index.{h,cc}` | The facade, the privacy refusals, and the segment/MemTable merge |

## For AI Agents

### Working In This Directory

- **Never include `src/main/address_policy.h`.** `ValidatedAddress::is_valid()` is defined in
  `address_policy.cc`, which this CEF-free library must not link — calling it is a link error on all
  six targets. `IsPrivacyRefusedUrl` implements the `data:`-scheme and userinfo refusals directly on
  the raw string, which is also what makes the library safe when called without pre-validation.
- **The MemTable is retained across `Flush`.** It therefore still holds every flushed document while
  the segment holds them too. Duplicate *hits* are prevented by the candidate set; the id-range split
  at `segment_boundary_` is what keeps the **corpus size** honest, and getting that wrong distorts
  idf and every score rather than producing a visibly duplicated result. The regression that pins it
  is `FlushThenQueryReturnsExactlyTheSameHits`, which compares scores, not just ids.
- **`Flush` refuses when the active segment predates the MemTable.** S0 has no segment merge, so
  writing a MemTable that does not contain the segment's documents would silently discard them. This
  is a real functional limit of S0, surfaced as `kInvalidInput` rather than hidden: an index reopened
  on an existing segment can be queried and ingested into, but not flushed. Removing the guard
  without adding a merge reintroduces silent data loss.
- **Term frequencies are derived at query time** by re-tokenizing the stored url and title. The
  segment's document record carries field *lengths* but not frequencies. `Tokenize` is deterministic
  over the same stored bytes that ingest indexed, so this reproduces the indexed view exactly.
  Widening the on-disk record is a `store/` format change with its own `format_version` bump.
- `Query` is `const` and total: it never throws and never fails. A broken segment yields
  MemTable-only results.
- Single-writer: `Ingest` and `Flush` from one thread. Nothing here is synchronized.

### Testing Requirements

```bash
cmake -G Ninja -B build-ninja -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-ninja
ctest --test-dir build-ninja -R SearchIndex --output-on-failure
```

Use Ninja — a Makefiles build directory for this tree reports stale results (see
`../cache/AGENTS.md`). Guards here are mutation-verified: replace a guard with a no-op and confirm
its test fails. Several plausible-looking tests in this module are upheld by a neighbouring
mechanism rather than the guard they name, so a green test alone proves nothing.
