# Island Search — Phase S0 Implementation Plan

> **Status:** Approved execution plan. It implements only the Phase S0 design in
> [the canonical specification](../specs/2026-08-10-island-search-phase-s0-design.md).

**Goal:** Ship a self-contained, on-device C++20 static library `island_search` under `src/search/`
— tokenizer/normalization, LEB128 delta+block posting codec, MemTable + one immutable mmap segment
with a versioned header and corruption quarantine, a byte-bounded LRU block cache, a BM25-lite+recency
ranker, and an `Ingest`/`Query`/`Flush` facade — plus a `search_membench` memory-gate binary that
samples per-OS RSS and asserts a CI ceiling, **without** adding any vendored dependency, CEF wiring,
network egress, chrome/palette surfacing, or `SessionStore` coupling.

**Implementation rule:** Work in the stated dependency order. An implementer owns only the files in
their unit and does not modify another unit's files, `src/main/**` (except to **read** the existing
`ValidatedAddress` header — no edits), CMake outside the guarded `src/search` subtree and the single
guarded root hook, the Phase 0–3 documents, `browser.pen`, or tool state. No unit may open a socket,
add a `third_party/`/FetchContent/`deps` entry, link CEF, or surface search in any UI. Root CMake gains
exactly one guarded hook (`option(ISLAND_ENABLE_SEARCH OFF)` + conditional `add_subdirectory`) and
nothing else.

## Shared acceptance contract

- `island_search` and `island_search_tests` build and run **without** a CEF distribution present.
- With `ISLAND_ENABLE_SEARCH=OFF` (default), the existing Phase 0–3 configure/build/test/package flow
  is byte-for-byte unchanged.
- Zero new vendored dependencies: no `third_party/` additions, no new FetchContent, no
  `deps/dependencies.lock.json` change. GoogleTest (already fetched) is reused.
- `DocId` is its own monotonic `std::uint64_t`, never derived from URL hash, `TabId`, `SpaceId`, or any
  browser identity; `DocId(0)` is the reserved null.
- No exceptions cross the facade; fallible operations return `std::expected<T, SearchError>` (or the
  agreed in-repo shim). `Query` is total and never fails.
- Every segment-byte decode path is bounds-checked and total; a corrupt segment is **quarantined** to
  `.corrupt-*`, never deleted, and never causes OOB/crash/wrong results.
- `data:` and credentialed URLs are refused at `Ingest` with `kRefusedForPrivacy`.
- The only shared seam with `src/main` is reading the `island::ValidatedAddress` value type by value;
  `island_search` links no `src/main` object and no networking facility.
- `search_membench` asserts **RSS ≤ 32 MB at 100k docs** via a non-zero exit on ceiling breach.

## Units

### W1 — Leaf primitives: types, tokenizer/normalization, LEB128

**Owner:** W1 implementer (route to `executor`)
**Files:** `src/search/types.h`, `src/search/text/tokenizer.h`, `src/search/text/tokenizer.cc`,
`src/search/codec/leb128.h`, `src/search/codec/leb128.cc`, `src/search/CMakeLists.txt`,
`tests/search/tokenizer_test.cpp`, `tests/search/leb128_test.cpp`
**Depends on:** none

Define the shared value types (`DocId` enum-class, `DocumentInput`, `StoredDocument`, `Term`,
`Posting`, `Query`, `SearchHit`, `SearchResult`, `SearchError`/`SearchErrorKind`) in `types.h`.
Implement the single `Tokenize(std::string_view, Field)` pure function (URL/title split, percent-decode,
ASCII casefold, 1..64-byte UTF-8-boundary clamp) reused by ingest and query. Implement unsigned
LEB128 encode plus a **total** decode returning `std::optional<std::uint64_t>` on truncation/overflow.
Stand up `src/search/CMakeLists.txt` declaring the `island_search` static lib (target-scoped, C++20,
no CEF, no networking) with just these sources for now; later units append. Establish
`island_search_tests` (GoogleTest via `gtest_discover_tests`, links `island_search`, no CEF).

**Tests:** `cmake -B build-search -S . -DISLAND_ENABLE_SEARCH=ON`;
`cmake --build build-search --target island_search_tests`;
`ctest --test-dir build-search -R '(Tokenizer|Leb128)' --output-on-failure`.

### W2 — Posting codec (delta + block layout)

**Owner:** W2 implementer (route to `executor`)
**Files:** `src/search/codec/posting_codec.h`, `src/search/codec/posting_codec.cc`,
`src/search/codec/crc32c.h`, `src/search/codec/crc32c.cc`, `src/search/CMakeLists.txt`,
`tests/search/posting_codec_test.cpp`, `tests/search/CMakeLists.txt` hooks
**Depends on:** W1

Implement the `kBlockPostings = 128` delta+block posting encode/decode: per-block header (first
absolute `DocId` skip-pointer + block byte length), intra-block `DocId` deltas + `term_frequency`, and
a term-dictionary entry shape (term bytes, posting count, first-block offset, block count). Implement
CRC32C (standard table, ~20 LOC, no external lib) here since both the codec and the segment need it.
Every decode is bounds-checked and returns an error rather than reading OOB. Append sources to
`island_search`.

**Tests:** `ctest --test-dir build-search -R '(PostingCodec|Crc32c)' --output-on-failure`.

### W3 — MemTable + Ranker

**Owner:** W3 implementer (route to `executor`)
**Files:** `src/search/store/memtable.h`, `src/search/store/memtable.cc`,
`src/search/rank/ranker.h`, `src/search/rank/ranker.cc`, `src/search/CMakeLists.txt`,
`tests/search/memtable_test.cpp`, `tests/search/ranker_test.cpp`
**Depends on:** W1

Implement the mutable in-RAM index: dense `DocId -> StoredDocument` store, `Term -> vector<Posting>`
inverted index kept id-ascending by construction, and aggregate stats (doc count, total tokens,
per-doc length, running average doc length). Mint monotonic `DocId` (never `0`). Implement the
`Ranker` with the documented BM25-lite (`k1=1.2`, `b=0.75`) + recency (`HALFLIFE_DAYS=14`,
`RECENCY_WEIGHT=0.5`, multiplicative) formula, taking `query_now_ms` explicitly (no hidden clock), with
deterministic `DocId`-ascending tie-break. W3 runs in parallel with W2 (both depend only on W1).

**Tests:** `ctest --test-dir build-search -R '(MemTable|Ranker)' --output-on-failure`.

### W4 — Segment: versioned header, writer, mmap reader, corruption quarantine

**Owner:** W4 implementer (route to `executor`, `model=opus` — highest-risk unit)
**Files:** `src/search/store/segment_header.h`, `src/search/store/segment.h`,
`src/search/store/segment.cc`, `src/search/store/segment_writer.h`,
`src/search/store/segment_writer.cc`, `src/search/CMakeLists.txt`, `tests/search/segment_test.cpp`
**Depends on:** W2, W3

Implement the on-disk segment layout from the spec: fixed versioned `SegmentHeader` (magic `ISS0`,
`format_version=1`, section offsets/lengths, `next_doc_id` high-water mark, `avg_doc_len_q16`,
`header_crc32c`), doc-store block, term-dictionary block, posting-block region, and trailer CRC32C.
`SegmentWriter` serializes a MemTable to `path + ".tmp"`, `fsync`s, and atomically `rename`s over
`segment_path`. `Segment::Open` maps read-only and runs the full validation ladder (size, magic,
version, header CRC, section-bounds/non-overlap, trailer CRC); **any** failure returns `SegmentError`,
**quarantines** the file to `<name>.corrupt-<timestamp>` (never delete), and reports so the facade can
run MemTable-only. Use a per-OS mmap seam (`mmap`/`MapViewOfFile`) with a thin compile-time
abstraction; no third-party lib. Every reader path is total/bounds-checked.

**Tests:** `ctest --test-dir build-search -R Segment --output-on-failure` — includes flipped-magic,
unknown-version, truncated-file, bit-flipped-body, and overlapping-offset cases each quarantining and
leaving queries answerable.

### W5 — Block cache + SearchIndex facade (Ingest/Query/Flush) + privacy refusals

**Owner:** W5 implementer (route to `executor`, `model=opus`)
**Files:** `src/search/cache/block_cache.h`, `src/search/cache/block_cache.cc`,
`src/search/index/search_index.h`, `src/search/index/search_index.cc`, `src/search/CMakeLists.txt`,
`tests/search/block_cache_test.cpp`, `tests/search/search_index_test.cpp`
**Depends on:** W4

Implement the byte-bounded LRU `BlockCache` over decoded posting blocks (hard byte ceiling
`kBlockCacheBytes` default 4 MB, strict LRU eviction, oversized-block bypass, correctness independent
of cache contents). Implement `SearchIndex::Open/Ingest/Query/Flush`: `Open` maps any existing segment
(quarantining a bad one via W4) with an empty MemTable; `Ingest` mints `DocId`, tokenizes, updates the
MemTable, and enforces the privacy refusals (`data:` scheme and credentialed URLs → `kRefusedForPrivacy`),
reading the caller's `island::ValidatedAddress` by value at the boundary (header-only include of
`src/main/address_policy.h`, no link to `src/main`); `Query` merges disjoint segment/MemTable id ranges
per term, ranks via W3, returns top-k, and is total (broken segment → MemTable-only, never throws);
`Flush` writes via W4's atomic writer then re-opens the segment read-only under the single-writer
precondition. All fallible ops return `std::expected<T, SearchError>` (or the agreed shim).

**Tests:** `ctest --test-dir build-search -R '(BlockCache|SearchIndex)' --output-on-failure` —
includes Flush-then-Query equivalence, merge correctness, error kinds, and privacy refusals.

### W6 — Memory gate (`search_membench`) + guarded root CMake hook + CI + docs

**Owner:** W6 implementer (route to `executor`)
**Files:** `src/search/bench/search_membench.cc`, `src/search/bench/corpus_gen.h`,
`src/search/bench/corpus_gen.cc`, `src/search/bench/rss_sampler.h`,
`src/search/bench/rss_sampler_mac.cc`, `src/search/bench/rss_sampler_linux.cc`,
`src/search/bench/rss_sampler_windows.cc`, `src/search/bench/CMakeLists.txt`, `CMakeLists.txt`
(the single guarded hook only), `.github/workflows/ci.yml`, `docs/search-phase-s0-membench.md`
**Depends on:** W5

Build the `search_membench` executable (links `island_search` only; no CEF, no GoogleTest). Implement
the deterministic 100k-doc corpus generator (fixed PRNG seed, Zipf-ish token distributions, no vendored
corpus). Implement `SampleResidentBytes()` per OS (macOS `task_info` `resident_size`, Linux
`/proc/self/statm`, Windows `GetProcessMemoryInfo` `WorkingSetSize`) behind a compile-time seam. Sample
RSS at baseline/post-ingest/post-flush/post-query checkpoints, report the **peak**, print a
machine-readable line, and **exit non-zero** when peak RSS exceeds `--ceiling-bytes` (default
`32*1024*1024`, pinned in CI). Add the single guarded root hook to `CMakeLists.txt`:
`option(ISLAND_ENABLE_SEARCH "Build the on-device search kernel" OFF)` and, only when `ON`,
`add_subdirectory(src/search)` — nothing else in root CMake changes. Add an S0 CI job (guarded by
`ISLAND_ENABLE_SEARCH=ON`) that builds the lib+tests+membench on each native runner and asserts the
membench exit code. Document the methodology and ceiling in `docs/search-phase-s0-membench.md`.

**Tests:**

```bash
cmake -B build-search -S . -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search -R Search --output-on-failure
build-search/src/search/bench/search_membench --ceiling-bytes 33554432   # exit 0 required
# guard proof: default OFF leaves the existing flow untouched
cmake -B build-offdefault -S .
cmake --build build-offdefault
ctest --test-dir build-offdefault --output-on-failure
```

## Dependencies and handoff order

```text
W1 ─┬─ W2 ─┐
    │      ├─ W4 ─ W5 ─ W6
    └─ W3 ─┘
```

W1 has no dependencies and must land first (types + tokenizer + LEB128 + the `island_search`/test
skeleton). **W2 and W3 both depend only on W1 and may run in parallel.** W4 depends on both W2 (codec +
CRC32C) and W3 (MemTable to serialize, stats for the header). W5 depends on W4. W6 depends on W5. W4,
W5, and W6 are strictly sequential and are the highest-risk units (W4 corruption handling, W5 facade
correctness, W6 memory ceiling).

## Verification (clean-checkout, search-enabled)

```bash
cmake -B build-search -S . -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search -R Search --output-on-failure
build-search/src/search/bench/search_membench            # RSS <= 32MB at 100k docs, exit 0
# default-off guard: existing flow unchanged
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

Where a sanitizer build is available, run the segment corruption cases under ASan/UBSan to prove no
OOB read on any quarantined file.

## Per-implementer commit policy

Each implementer creates commits only for their owned files after their focused checks pass. Keep an
implementation file and its direct tests in the same commit; do not stage another unit's files,
generated CEF/font/icon output, `browser.pen`, tool state, or any `third_party/`/`deps` change (there
are none in S0). Use the repository's plain imperative English style. A unit may use multiple atomic
commits when its owned changes are independently reversible; each commit carries the required project
attribution footer and co-author trailer. No implementer pushes or rewrites history as part of this
plan.

## Open items carried from the design

1. **One index per space vs one tagged index** — S0 stores an opaque `partition_tag` and reserves a
   tag query filter but implements neither; the decision is deferred to the wiring phase and must not
   require a `format_version` bump if it only uses the reserved field.
2. **`add_subdirectory` guard default** — S0 ships `ISLAND_ENABLE_SEARCH OFF`; whether a later phase
   flips it to `ON` is open and owned by the techlead.
3. **Future network egress (UNRESOLVED USER DECISION)** — on-device only (A) vs federated/hybrid (B)
   vs crawler (C). S0 forecloses none and builds toward none; revisiting requires its own spec and an
   explicit user decision.
