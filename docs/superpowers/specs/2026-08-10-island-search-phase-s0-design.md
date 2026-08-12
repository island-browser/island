# Island Search — Phase S0 Design (Architecture A: On-Device Search Kernel)

## Status and baseline

This is the canonical, decision-complete design for **Phase S0**, the first phase of Island's search
work stream (the "S" series, parallel to the numbered browser phases). It selects **Architecture A**:
a fully **on-device** search kernel with **no network egress**. S0 is the storage-and-ranking core
only; it does not wire search into CEF, chrome, the command palette, or `SessionStore`.

S0 is deliberately self-contained. It ships one new C++20 static library, `island_search`, under a
new `src/search/` directory, plus one memory-gate executable, `search_membench`. It links **no CEF**,
opens **no sockets**, reads **no credentials**, and adds **zero new vendored dependencies**. The only
integration seam it shares with the rest of Island is the existing read-only value type
`island::ValidatedAddress` (declared in `src/main/address_policy.h`), consumed by value, never
mutated, and never linked back into `island_search` as a build dependency (see
[Ingestion and privacy](#ingestion-and-privacy)).

Read this design before the [Phase S0 plan](../plans/2026-08-10-island-search-phase-s0.md). The
numbered-phase designs (Phase 0–3) remain the browser baseline and are historical context for S0.

## Goals

- A minimal, correct, **on-device** full-text index over browsing history entries (`url` + `title`),
  built as a self-contained static library with a small, stable facade (`Ingest` / `Query` / `Flush`).
- A **byte-bounded** memory profile: **target ≤ 32 MB resident** for **100,000 documents** (each an
  `url` + `title` pair), proven by an automated memory-gate binary with a CI ceiling assertion.
- A **durable, versioned** on-disk representation: one write-ahead in-memory table (MemTable) plus one
  immutable memory-mapped segment, with a versioned header and **corruption quarantine** so a damaged
  segment can never crash or silently corrupt a query.
- A **compact posting codec** (LEB128 delta + block layout) that keeps both the in-memory and on-disk
  index small enough to hit the memory budget.
- A deterministic, explainable ranker: **BM25-lite + recency**, with a documented, testable scoring
  formula.
- Test surfaces (unit + property + round-trip + memory gate) that make every contract in this document
  falsifiable, plus acceptance criteria an implementer can check without inventing behavior.

## Non-goals (explicitly out of scope for S0)

- **No CEF wiring.** `island_search` does not link CEF, does not observe `NavigationState`, and is not
  instantiated by `BrowserWindow`, `IslandApp`, or any chrome class. Wiring is a later "S" phase with
  its own spec and plan.
- **No chrome / command-palette / address-bar surfacing.** No UI, no results rendering, no keyboard
  entry point. S0 is a library plus a benchmark, nothing user-visible.
- **No `SessionStore` coupling.** S0 does not read or write `session.json`, does not share its schema,
  and defines its own on-disk format and directory. Persistence models are independent.
- **No network egress of any kind.** No sockets, no HTTP client, no telemetry, no remote index, no
  crawler, no federation. Architecture A is on-device only. The future-egress question is recorded as
  an **unresolved open item** ([Open items](#open-items)), not decided here.
- **No new vendored dependencies.** No FetchContent additions, no new `third_party/` entries, no
  new entry in `deps/dependencies.lock.json`. S0 uses only the C++20 standard library and the OS
  facilities needed for `mmap` and RSS sampling. GoogleTest (already fetched by the repo) is reused
  for tests.
- **No incremental/background compaction, no multi-segment merge, no deletion/tombstones, no ranking
  personalization, no query language (boolean operators, phrase search, field qualifiers), no
  stemming/language detection, no Unicode collation beyond the normalization defined here.** These are
  future-phase scope. S0 has exactly one MemTable and at most one immutable segment.
- **No thread-safety guarantee for concurrent writers.** The facade is single-writer; see
  [Concurrency and lifecycle](#concurrency-and-lifecycle).

## Assumptions

- The consumer (a future phase) supplies, per document, a normalized absolute `url` string and a
  `title` string, and owns the policy of what is allowed to be indexed. S0 additionally enforces its
  own hard privacy refusals ([Ingestion and privacy](#ingestion-and-privacy)) as a defense-in-depth
  boundary, not as the primary policy owner.
- Corpus scale for the S0 budget is **100k documents**; url+title average lengths are drawn from the
  measurement corpus described in [Memory budget and measurement](#memory-budget-and-measurement).
- One logical index per instance. Whether the browser runs **one index per space** or **one tagged
  index** is an [open item](#open-items) and is deliberately **not** baked into the on-disk format
  beyond an optional opaque `partition_tag` field reserved in the document record.
- The host provides `mmap`/`MapViewOfFile` and a way to sample process RSS per OS (macOS
  `mach_task_basic_info`, Linux `/proc/self/statm`, Windows `GetProcessMemoryInfo`). No third-party
  library is needed for either.

## Constraints

- C++20, Google-based formatting with 4-space indentation, 100-column lines, left-aligned pointers,
  sorted includes, explicit `std::` qualification, no `using namespace std;` — identical to the rest
  of the repo (`AGENTS.md` Conventions).
- Target-scoped CMake only. No global `include_directories`/`link_libraries`.
- Files obey the repository 250 pure-LOC ceiling; oversized units are split by responsibility.
- The library must build and its unit tests must run **without** a CEF distribution present, so that
  `island_search` and its tests do not depend on `scripts/setup_deps.sh` having run. (The memory-gate
  binary likewise needs no CEF.)
- Root-CMake integration is **deferred or guarded**: S0 adds `option(ISLAND_ENABLE_SEARCH OFF)` and
  only `add_subdirectory(src/search)` when it is `ON`. The default `OFF` keeps the existing build,
  CI, and package flow byte-for-byte unchanged. See [Build integration](#build-integration).

## Architecture overview

```text
island_search (static lib, src/search/)
├── text/
│   ├── Tokenizer          normalize(url|title) -> ordered tokens
│   └── Normalizer         casefold, strip, split, length-clamp
├── codec/
│   ├── Leb128             varint encode/decode (unsigned)
│   └── PostingCodec       delta + block posting list encode/decode
├── store/
│   ├── MemTable           mutable in-RAM inverted index + doc store
│   ├── Segment            immutable mmap'd segment (versioned header)
│   ├── SegmentHeader      magic, version, sizes, checksums, doc count
│   └── SegmentWriter      MemTable -> on-disk segment (atomic rename)
├── cache/
│   └── BlockCache         byte-bounded LRU over decoded posting blocks
├── rank/
│   └── Ranker             BM25-lite + recency scoring
├── index/
│   └── SearchIndex        the Ingest/Query/Flush facade (owns the above)
└── (types) DocId, Term, Posting, Document, SearchHit, SearchError

search_membench (executable, src/search/bench/)
└── drives SearchIndex through a synthetic 100k-doc corpus,
    samples per-OS RSS, asserts against the CI ceiling.
```

### Ownership and dependency direction

Dependencies point **inward, one way**: `text` and `codec` are leaf modules with no intra-library
dependencies. `store` depends on `codec` and `text`. `cache` depends on `codec` (block layout) only.
`rank` depends on the doc/posting value types only. `SearchIndex` (the `index` module) owns and
composes `MemTable`, at most one `Segment`, a `BlockCache`, a `Ranker`, the `Tokenizer`, and the
`SegmentWriter`; nothing depends on `SearchIndex`. No cycles. No module reaches back into `src/main`
except to `#include` the read-only `ValidatedAddress` value type at the facade boundary, and that
inclusion is header-only and compiled into the library without linking `src/main`.

## Data contracts

All types live in namespace `island::search`.

### Identity

```cpp
// A monotonic, process-and-persistence-stable document id, distinct from
// TabId/SpaceId and from any browser identity. Never reused after assignment.
enum class DocId : std::uint64_t {};
```

`DocId` is its **own** monotonic `std::uint64_t`, minted by `SearchIndex`, **not** derived from URL
hash, `TabId`, `SpaceId`, or history row id. The generator persists its high-water mark in the segment
header so ids remain monotonic across a flush/reload cycle. `DocId(0)` is reserved as the invalid/null
id and is never assigned.

### Document and ingestion input

```cpp
struct DocumentInput {
    std::string url;             // absolute, already-normalized URL
    std::string title;           // display title (may be empty)
    std::uint64_t visited_at_ms; // epoch milliseconds; the recency signal
    std::optional<std::string> partition_tag;  // reserved; opaque; see open item
};

struct StoredDocument {
    DocId id;
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms;
    std::optional<std::string> partition_tag;
};
```

`visited_at_ms` is supplied by the caller (S0 does not read the clock during ingest, keeping ingest
deterministic and testable). `partition_tag` is stored but **not** interpreted by S0; it exists so the
one-index-per-space-vs-tagged decision can be made later without a format break.

### Term and posting

```cpp
using Term = std::string;   // normalized token, UTF-8, length-clamped

struct Posting {
    DocId doc;
    std::uint32_t term_frequency;  // occurrences of the term in the doc
};
```

A **posting list** for a term is the ascending-`DocId` sequence of its postings. On disk and in the
block cache, postings are stored as **LEB128 delta of DocId + LEB128 term_frequency**, grouped into
fixed-count **blocks** (default 128 postings/block) each prefixed by its first `DocId` (block
skip-pointer) so a query can skip whole blocks without decoding them. See
[Posting codec](#posting-codec).

### Query and result

```cpp
struct Query {
    std::string text;            // raw user text; tokenized internally
    std::size_t max_results;     // hard cap on returned hits (e.g. 20)
    std::optional<std::string> partition_tag;  // reserved filter; see open item
};

struct SearchHit {
    DocId doc;
    std::string url;
    std::string title;
    double score;                // BM25-lite + recency, higher is better
};

struct SearchResult {
    std::vector<SearchHit> hits; // sorted by score desc, then DocId asc, stable
    std::size_t scanned_terms;   // observability: query terms after tokenization
};
```

Ranking is **AND-lite**: a document matches if it contains **at least one** query term; documents
matching more query terms score higher through BM25-lite term accumulation (S0 does not implement
strict boolean AND). Ties break by `DocId` ascending for a deterministic, stable order.

## Tokenizer and normalization

One tokenizer serves both ingest and query so the same string always yields the same terms. The
pipeline, in order:

1. **Field split.** URL and title are tokenized separately; URL is first split on scheme/host/path
   punctuation (`: / . ? & = # _ - ~ @ %` and whitespace), title on Unicode whitespace and ASCII
   punctuation. The scheme (`https`) and a bare host are emitted as tokens; percent-encoded octets are
   decoded to bytes before splitting so `%2F` does not become a spurious token.
2. **Casefold.** ASCII uppercase → lowercase. Non-ASCII bytes pass through unchanged (S0 does **not**
   implement full Unicode case folding; that is future scope). Casefolding is byte-deterministic.
3. **Clamp.** Tokens shorter than 1 byte are dropped; tokens longer than **64 bytes** are truncated to
   64 bytes on a UTF-8 code-point boundary (never mid-continuation-byte).
4. **Dedup within a field is NOT performed** — term frequency is meaningful; the codec stores it.

The tokenizer is a pure function `std::vector<Term> Tokenize(std::string_view, Field)` with no
allocation of shared state, making it trivially unit-testable and reused verbatim by both `Ingest`
and `Query`. There is exactly **one** tokenizer implementation; the query path never re-implements it.

## Posting codec

### LEB128

Unsigned LEB128 (7 data bits/byte, high bit = continuation). `EncodeLeb128(std::uint64_t, out)` and
`DecodeLeb128(span, &cursor) -> std::optional<std::uint64_t>`. Decode returns `std::nullopt` on
truncation or overflow (>10 bytes) rather than reading out of bounds — this is the primitive that
makes segment decoding total and corruption-safe.

### Delta + block layout

For each term's posting list:

- Postings are grouped into blocks of `kBlockPostings = 128` (the last block may be short).
- Each block is prefixed by a **block header**: the block's **first absolute `DocId`** (LEB128) and
  the **block byte length** (LEB128), forming a skip pointer.
- Within a block, the first posting stores `DocId` as delta from the block's first `DocId` (i.e. 0),
  and each subsequent posting stores `DocId` delta from the previous (always ≥ 1 since postings are
  strictly ascending), followed by `term_frequency` (LEB128).
- A term dictionary entry records: term bytes, total posting count, byte offset of the term's first
  block, and number of blocks.

Blocks are the unit of caching and of skip: a query for a term reads the dictionary entry, then walks
block headers using the first-`DocId` skip pointers, decoding a block's body only when it may contain
a candidate. Decoded block bodies are what the [block cache](#block-cache) holds.

Rationale: delta+varint keeps ascending `DocId` lists compact (typical delta fits 1–2 bytes);
128-posting blocks bound decode cost and give a natural, fixed cache granule; block skip pointers keep
multi-term intersection cheap without a separate skip list structure. No general-purpose compression
library is introduced (that would be a vendored dependency).

## Storage: MemTable + immutable segment

### MemTable

The mutable, in-RAM index. It holds:

- A **document store**: `DocId -> StoredDocument` (contiguous, since ids are dense and monotonic).
- An **inverted index**: `Term -> std::vector<Posting>` kept in ascending-`DocId` order (ingest
  appends in id order, so the invariant is maintained by construction, not by re-sorting).
- Aggregate stats needed by the ranker: document count, total token count, per-doc length (token
  count), and running average document length.

`Ingest` mints a `DocId`, tokenizes url+title, and updates the store, the inverted index, and the
stats. The MemTable is the **only** writable structure; the segment is never mutated in place.

### Segment (immutable, memory-mapped)

`Flush` serializes the current MemTable into a single on-disk **segment file** and memory-maps it
read-only. A segment has this on-disk layout (all multi-byte integers little-endian; all sections
byte-aligned; all offsets from file start):

```text
[ SegmentHeader           fixed size, versioned ]
[ Document store block    StoredDocument records, LEB128-framed ]
[ Term dictionary block   sorted terms + posting metadata ]
[ Posting block region    delta+block posting bodies ]
[ Trailer                 CRC32C of everything above + repeated magic ]
```

#### SegmentHeader (versioned)

```text
magic            uint32  "ISS0" (0x30535349)   // Island Search Segment v0
format_version   uint16  = 1                    // bumped on any layout change
header_bytes     uint16  size of this header
flags            uint32  reserved, must be 0
doc_count        uint64
next_doc_id      uint64  DocId high-water mark (monotonic across reload)
doc_store_off    uint64  / doc_store_len    uint64
term_dict_off    uint64  / term_dict_len    uint64
posting_off      uint64  / posting_len      uint64
avg_doc_len_q16  uint64  average document length, fixed-point Q16
header_crc32c    uint32  CRC32C of all header bytes above this field
```

CRC32C is computed with a standard table (no external library; ~20 LOC). The trailer stores a CRC32C
over the doc-store + term-dict + posting regions so a truncated or bit-flipped body is detected before
any query touches it.

#### Versioning rule

A reader **rejects** any segment whose `magic` is wrong or whose `format_version` it does not
recognize, and treats it as [corrupt/quarantined](#corruption-quarantine) — never as an empty or
partially-readable index. `format_version` is bumped for **any** on-disk layout change; there is no
best-effort forward/backward compatibility in S0 (single-version reader). This is the migration
strategy: version-gate, quarantine on mismatch, rebuild from the MemTable/source, never silently
reinterpret bytes.

### Corruption quarantine

On open, `Segment::Open(path)` performs, in order: (1) map the file; (2) validate size ≥ header size;
(3) validate `magic` and `format_version`; (4) validate `header_crc32c`; (5) validate that all
section offsets/lengths lie within the mapped file and do not overlap; (6) validate the trailer
CRC32C over the body. **Any** failure causes `Open` to:

- return `SegmentError` (not throw across the facade),
- move the offending file to `<name>.corrupt-<timestamp>` **beside** it (quarantine, never delete —
  so the bad file can be inspected post-hoc), and
- leave `SearchIndex` running against the MemTable alone (queries still work, just without the
  persisted segment) so a corrupt segment degrades gracefully instead of taking down search.

Every decode path that reads segment bytes (LEB128 decode, block walk, dictionary lookup) is **total**:
it uses bounds-checked spans and returns `std::nullopt`/error on any inconsistency rather than reading
out of the mapped range. A corrupt segment can therefore never cause OOB reads, crashes, or wrong
results — the worst case is an empty result plus a quarantined file.

## Block cache (byte-bounded LRU)

`BlockCache` holds **decoded** posting blocks keyed by `(term_dict_index, block_index)`, evicting in
strict LRU order to keep the sum of held block byte-sizes ≤ a configured **byte budget**
(`kBlockCacheBytes`, default sized within the overall memory budget — see below). The budget is a hard
byte ceiling, **not** an entry count, so cache memory is predictable regardless of block size variance.
A block larger than the whole budget is decoded transiently and never inserted (so one pathological
block cannot exceed the ceiling). The cache is a pure accelerator: a cold miss re-decodes from the
mmap'd segment; correctness never depends on cache contents.

## Ranking: BM25-lite + recency

For query term `t` and document `d`:

```text
idf(t)      = ln( 1 + (N - df(t) + 0.5) / (df(t) + 0.5) )
tf_norm     = f(t,d) * (k1 + 1)
              -----------------------------------------
              f(t,d) + k1 * (1 - b + b * len(d) / avgdl)
bm25(d)     = Σ_t  idf(t) * tf_norm                  // sum over matched query terms
recency(d)  = exp( -ln(2) * age_days(d) / HALFLIFE_DAYS )   // in (0, 1]
score(d)    = bm25(d) * (1 + RECENCY_WEIGHT * recency(d))
```

Constants (documented, fixed for S0, unit-tested): `k1 = 1.2`, `b = 0.75`, `HALFLIFE_DAYS = 14`,
`RECENCY_WEIGHT = 0.5`. `N` is `doc_count`, `df(t)` is the term's posting count, `avgdl` is the
segment/MemTable average document length, `len(d)` is the document's token count, `age_days(d)` is
`(query_now_ms - visited_at_ms) / 86_400_000`. `query_now_ms` is passed into `Query` by the caller
(again, no hidden clock read), so ranking is deterministic and testable. Recency multiplies rather
than adds so a fresh page is boosted proportionally to its relevance instead of dominating it. Ties
(equal score) break by `DocId` ascending. This is "BM25-lite" because it scores single terms
independently with no phrase/proximity/field-boost weighting — those are future scope.

## Facade: Ingest / Query / Flush

```cpp
class SearchIndex {
 public:
    struct Options {
        std::filesystem::path segment_path;   // where Flush writes / Open reads
        std::size_t block_cache_bytes = kBlockCacheBytes;
    };

    static std::expected<SearchIndex, SearchError> Open(Options options);

    // Mints a DocId, tokenizes, updates the MemTable. Returns the new id.
    std::expected<DocId, SearchError> Ingest(const DocumentInput& input);

    // Queries MemTable + segment, merges, ranks, returns top-k.
    // query_now_ms feeds the recency term (no hidden clock).
    SearchResult Query(const Query& query, std::uint64_t query_now_ms) const;

    // Serializes the current MemTable to a versioned segment (atomic rename),
    // then re-opens it read-only as the active segment. MemTable is retained
    // (S0 does not clear it on flush; segment + MemTable are queried together,
    // MemTable postings shadow-append after the segment's DocId range).
    std::expected<void, SearchError> Flush();
};
```

- **Error behavior.** No exceptions cross the facade; all fallible operations return
  `std::expected<T, SearchError>`. `SearchError` is an enum-tagged struct
  (`{ SearchErrorKind kind; std::string detail; }`) with kinds
  `kIoError, kCorruptSegment, kVersionMismatch, kInvalidInput, kRefusedForPrivacy, kBudgetError`.
  `Query` is total and never fails (a broken segment yields MemTable-only results), so it returns a
  plain `SearchResult`.
- **`Flush` atomicity.** `SegmentWriter` writes to `segment_path + ".tmp"`, `fsync`s, then
  `std::filesystem::rename`s over `segment_path` (atomic on the same filesystem). A crash mid-write
  leaves the old segment (or none) intact; the partial `.tmp` is ignored on next open.
- **MemTable + segment query.** Because `DocId` is monotonic and the segment covers a contiguous
  id prefix, `Query` scans the segment for ids `< next_doc_id_at_flush` and the MemTable for ids
  minted since; there is no id overlap, so merge is a concatenation of two disjoint posting streams
  per term followed by a single top-k selection.

## Concurrency and lifecycle

- `SearchIndex` is **single-writer**: `Ingest` and `Flush` must be called from one thread (or under
  external synchronization). This matches the intended future call site (a single history-ingest path)
  and avoids introducing locks or a background thread in S0.
- `Query` is **const** and safe to call concurrently with other `Query` calls **only** when no
  `Ingest`/`Flush` is in flight; S0 does not promise reader/writer concurrency and documents this as a
  hard precondition. The block cache is therefore not internally synchronized (no mutex, no atomics)
  in S0 — adding a concurrent-reader guarantee is future scope with its own design.
- **Lifecycle.** `Open` maps any existing segment (quarantining a bad one) and initializes an empty
  MemTable. Destruction unmaps the segment and frees the MemTable; no global state, no singletons, no
  static init. The mmap is held for the object's lifetime; `Flush` swaps the active mapping under the
  single-writer precondition.

## Ingestion and privacy

S0 enforces hard, testable refusals at the `Ingest` boundary as defense-in-depth, independent of
whatever policy the future caller applies:

- **No `data:` URLs.** Any `url` whose scheme is `data:` is refused (`kRefusedForPrivacy`) — such URLs
  can embed arbitrary page content and must never enter a persisted index.
- **No credentialed URLs.** Any `url` containing userinfo (`user:pass@host`) is refused. S0 reuses the
  existing browser judgment about credentialed URLs by accepting the caller's already-validated
  address: when the caller has an `island::ValidatedAddress`, S0 reads its `url` field (read-only,
  by value) and additionally re-checks the `data:`/credential refusals locally so the library is safe
  even if called without pre-validation. `ValidatedAddress` is the **only** shared seam; S0 does not
  call into `address_policy.cc` or link `src/main`.
- **No secrets in memory beyond the indexed bytes.** S0 stores only url+title+timestamp; it never
  stores cookies, form data, page bodies, or headers. The `partition_tag` is opaque and caller-chosen;
  S0 never derives it from credentials.
- **On-device only.** There is no code path that opens a socket or writes anywhere except the
  configured `segment_path` (and its `.tmp`/`.corrupt-*` siblings). This is enforced structurally by
  the library linking no networking facility.

## Memory budget and measurement

### Budget

- **Target: ≤ 32 MB process RSS attributable to search at 100,000 documents** (url+title), measured as
  the delta between a baseline RSS (process with `SearchIndex` constructed but empty) and RSS after
  ingesting the 100k-doc corpus and issuing a representative query mix. The segment on disk is separate
  and not counted against the RSS ceiling; the mmap'd segment's *resident* pages are counted (that is
  why the budget targets RSS, not `malloc` bytes).
- **Sub-budgets** (guidance, not separately gated): document store ≤ ~14 MB, inverted structures
  (dictionary + resident posting pages) ≤ ~12 MB, block cache ≤ `kBlockCacheBytes` (default **4 MB**),
  ranker/scratch ≤ ~2 MB. These sum under 32 MB with headroom; the CI gate asserts the **total**, not
  the parts.

### Measurement methodology (`search_membench`)

`search_membench` is a standalone executable (no CEF, no GoogleTest) that:

1. Generates a deterministic synthetic corpus of 100k documents from a fixed PRNG seed: realistic
   url+title token distributions (host/path segments + title words, Zipf-ish term frequencies), so runs
   are reproducible and comparable across machines. The generator is in-repo, deterministic, and
   documented; no corpus file is vendored.
2. Samples **process RSS per OS** at defined checkpoints (baseline, post-ingest, post-flush,
   post-query-mix) via:
   - macOS: `task_info(mach_task_self(), MACH_TASK_BASIC_INFO, ...)` → `resident_size`.
   - Linux: read `/proc/self/statm` field 2 (resident pages) × page size.
   - Windows: `GetProcessMemoryInfo` → `WorkingSetSize`.
   A thin `SampleResidentBytes()` abstraction picks the right implementation at compile time; no
   third-party memory library is introduced.
3. Prints a machine-readable line (`membench: docs=100000 rss_bytes=<n> ceiling=33554432 ...`) and
   **exits non-zero if `rss_bytes` exceeds the ceiling** (default `32 * 1024 * 1024`, overridable via
   `--ceiling-bytes` for local exploration but pinned in CI). This exit-code assertion is what CI
   consumes.
4. Reports the peak across checkpoints, not just the final sample, so a transient spike during flush is
   caught.

CI runs `search_membench` on each native runner in the existing matrix (guarded by
`ISLAND_ENABLE_SEARCH=ON` for the search-specific job) and fails the build if the ceiling is exceeded.
Because RSS includes allocator slack and resident mmap pages, the ceiling is set with the sub-budget
headroom above; if a platform's allocator inflates RSS past 32 MB despite the structures fitting, that
is a real defect to fix (arena/reserve tuning), not a reason to raise the ceiling without a recorded
decision.

## Build integration

- New static library target `island_search` (`src/search/CMakeLists.txt`), C++20, target-scoped
  includes, **no** CEF, **no** networking libs.
- New executable `search_membench` (`src/search/bench/CMakeLists.txt`), links `island_search` only.
- New test target `island_search_tests` reusing the repo's already-fetched GoogleTest via
  `gtest_discover_tests`; it links `island_search` and needs **no** CEF, so it builds in a
  CEF-less checkout.
- **Root CMake is guarded**: add `option(ISLAND_ENABLE_SEARCH "Build the on-device search kernel" OFF)`
  and, only when `ON`, `add_subdirectory(src/search)`. When `OFF` (the default), nothing about the
  existing configure/build/test/package flow changes — this keeps S0 non-disruptive and lets the
  numbered-phase CI stay green while search lands. Whether S0 should flip the default to `ON` or leave
  wiring to a later phase is an [open item](#open-items).

## Test surfaces

- **Tokenizer/Normalizer** (`tokenizer_test.cpp`): idempotence (tokenizing a token yields itself),
  URL splitting incl. percent-decoding, casefold, 64-byte UTF-8-boundary clamp, empty/degenerate
  inputs, ingest/query symmetry (same string → same terms).
- **LEB128** (`leb128_test.cpp`): round-trip across `0`, `1`, `2^7-1`, boundary widths, `UINT64_MAX`;
  truncation and overflow return `nullopt`; property test: `decode(encode(x)) == x` for random `x`.
- **PostingCodec** (`posting_codec_test.cpp`): delta+block round-trip for ascending id lists, short
  final block, block skip-pointer correctness, corrupt/truncated block body decodes to error not OOB.
- **MemTable** (`memtable_test.cpp`): ingest updates store/index/stats; posting lists stay
  id-ascending; average-doc-length math; `DocId` monotonic and never `0`.
- **Segment / SegmentWriter** (`segment_test.cpp`): MemTable→segment→reopen round-trip equals the
  in-memory query results; header CRC and trailer CRC validated; **corruption quarantine**: flipped
  magic, unknown version, truncated file, bit-flipped body, and overlapping-section offsets each
  quarantine the file to `.corrupt-*` and leave queries answerable from the MemTable.
- **BlockCache** (`block_cache_test.cpp`): byte-ceiling never exceeded under adversarial insert order,
  strict LRU eviction order, oversized-block bypass, cold-miss re-decode equals cached result.
- **Ranker** (`ranker_test.cpp`): BM25-lite constants produce documented scores on a hand-checked
  fixture; recency half-life behavior; multiplicative combination; deterministic tie-break by `DocId`.
- **Facade** (`search_index_test.cpp`): Ingest→Query top-k correctness; Flush then Query equals
  pre-flush results; MemTable+segment merge with disjoint id ranges; `std::expected` error kinds for
  IO/version/corrupt; **privacy refusals** for `data:` and credentialed URLs (`kRefusedForPrivacy`).
- **Memory gate** (`search_membench`): the CI ceiling assertion above; also runnable locally.

Tests follow the repo's Given/When/Then discipline, fakes-over-mocks (a `data:`-URL fixture, a
hand-built corrupt segment on a `t.TempDir`-equivalent temp path), and per-run temp-path isolation so
two checkouts can run the suite concurrently.

## Acceptance criteria

S0 is complete when, from a clean checkout **with `ISLAND_ENABLE_SEARCH=ON`**:

1. `island_search` and `island_search_tests` build with no CEF distribution present.
2. `ctest -R Search` (all search unit/property tests) passes on macOS arm64, with the same suite green
   on the other native CI runners the S0 CI job covers.
3. A MemTable→segment→reopen round-trip yields **identical** ranked results to the pre-flush in-memory
   query for a fixture corpus.
4. Each corruption case (bad magic, unknown version, truncation, body bit-flip, overlapping offsets)
   **quarantines** the file to `.corrupt-*` and leaves `Query` answering from the MemTable — no crash,
   no OOB, no wrong result (verified under a sanitizer build where available).
5. `data:` URLs and credentialed URLs are refused at `Ingest` with `kRefusedForPrivacy`.
6. `search_membench` reports **RSS ≤ 32 MB at 100k docs** on each native CI runner and **exits
   non-zero** if the ceiling is exceeded; the CI job asserts the exit code.
7. With `ISLAND_ENABLE_SEARCH=OFF` (default), the existing Phase 0–3 configure/build/test/package flow
   is byte-for-byte unchanged (no new default targets, no new default CI cost).
8. The library links no CEF and opens no socket (verified by inspecting the link line and by the
   absence of any networking include).

## Migration and compatibility strategy

There is no persisted S0 data in existing installs, so there is no data migration. The forward
strategy is encoded in the format itself: **version-gated segments**. Any change to the on-disk layout
bumps `format_version`; a reader that sees an unrecognized version quarantines the file and rebuilds
from source rather than reinterpreting bytes. The `partition_tag` reserved field and `flags` header
field give room to add per-space partitioning or new capabilities without an immediate format break.
No existing consumer depends on `island_search` yet (it is `OFF` by default), so the facade can evolve
in a later phase without breaking anyone.

## Risks and decisions for the techlead to confirm

1. **RSS ceiling realism.** 32 MB at 100k docs assumes the sub-budgets and allocator behavior above.
   If a platform's allocator inflates RSS, the fix is arena/reserve tuning, not raising the ceiling.
   Confirm the ceiling and the "fix, don't raise" posture.
2. **`std::expected` availability.** S0 uses `std::expected` (C++23-in-practice; libc++/libstdc++/MSVC
   support varies under `-std=c++20`). Decision to confirm: rely on the toolchain's `std::expected`,
   or ship a tiny in-repo `Expected<T,E>` shim (no vendored dep) if a target's stdlib lacks it. The
   design is written against the `std::expected` shape either way.
3. **Segment covers a contiguous DocId prefix.** The MemTable+segment merge relies on `Flush` never
   interleaving ids. Confirm the single-writer precondition is acceptable for the future call site.
4. **Guard default `OFF`.** Confirm S0 ships with `ISLAND_ENABLE_SEARCH OFF` and leaves the flip-to-ON
   to a later wiring phase (keeps numbered-phase CI untouched).

## Open items

These are recorded, not decided, in S0:

1. **One index per space vs one tagged index.** S0 stays neutral: it stores an opaque `partition_tag`
   per document and reserves a `partition_tag` query filter, but implements neither multi-index
   management nor tag filtering. The decision (and its cost model — N small indices vs one index with
   a tag predicate) is deferred to the wiring phase. Whichever is chosen must not require a segment
   `format_version` bump if it only uses the reserved field.
2. **`add_subdirectory` guard default.** S0 defaults `ISLAND_ENABLE_SEARCH OFF`. Whether a later phase
   flips it to `ON` in root CMake, or search stays behind the flag until fully wired, is open and
   owned by the techlead.
3. **Future network egress — UNRESOLVED USER DECISION.** Architecture A (on-device) is chosen for S0,
   but the long-term direction among **(a) on-device only**, **(b) federated/hybrid** (query a remote
   index alongside the local one), and **(c) crawler** (fetch and index remote content) is an
   **unresolved user decision**. S0 does not foreclose any of them, but also does not build toward any
   of them: no socket, no remote schema, no crawler hooks. Revisiting this requires its own spec and an
   explicit user decision, because it changes the privacy and egress posture fundamentally.
