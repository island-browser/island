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
- **Concurrent `Query` calls are safe; Ingest and Flush are not.** Queries share the `BlockCache`,
  which mutates on every read, so `cache_mutex_` guards each term's cache lookup *and* the
  `CollectRange` scan of the list it returns. A posting list larger than the cache goes into
  per-query storage, never into a member. Ingest and Flush remain single-writer and must not overlap
  any other call. `cache()` bypasses the mutex: read it only while no query is in flight.
  `ConcurrentQueriesShareATinyCacheSafely` is the regression; without the lock it reports races
  under TSan and fails under ASan.
- **avgdl is a corpus statistic** (`CorpusAverageDocLength`), never an average over the candidates.
  With a segment mapped at `Open`, which predates the MemTable, it combines the segment's Q16
  average and the MemTable's token total, weighted by document count. Otherwise the retained MemTable
  holds every document, and its exact average is used. Using the segment's rounded average after an
  in-session flush would move scores and break `FlushThenQueryReturnsExactlyTheSameHits`.
- **`Ingest` leaves the cache alone.** The cache holds only segment posting lists, which are
  immutable.
- **`Flush` releases the segment before writing.** Windows refuses to rename over a file with a live
  mapping. `segment_boundary_` must drop to 0 together with `segment_`, so the MemTable answers every
  id while nothing is mapped. A failed write re-maps the previous file, which the atomic rename left
  intact. POSIX permits the rename either way, so the Windows failure itself is unverified locally.
- **Privacy refusals parse the URL the way WHATWG does.** First strip C0 controls and spaces at both
  ends and skip tabs and newlines. Then read the scheme. For the special schemes, skip any run of `/`
  or `\` and read the authority; for any other scheme, read an authority only after `//`. The
  authority ends at `/ ? #`, plus `\` for special schemes. The function is `noexcept`, so it works
  on the `string_view` without allocating.

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
