<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-09-01 | Updated: 2026-09-01 -->

# search/store

## Purpose

The on-disk half of the Phase S0 search kernel: the versioned segment format, the writer that
serializes a `MemTable` into one immutable segment, and the memory-mapped reader that validates a
segment before any query is allowed to touch its bytes. CEF-free, like the rest of `src/search`.

## Status

**Unit W4 of the Phase S0 plan.** Built only behind `ISLAND_ENABLE_SEARCH`, which is `OFF` by
default; see `../AGENTS.md` for what that means for a plain `cmake -B build -S .`.

Read `docs/superpowers/specs/2026-08-10-island-search-phase-s0-design.md` (sections "Segment layout",
"Versioning rule", "Corruption quarantine") before changing anything here. The on-disk layout is a
compatibility surface: any change to it requires a `format_version` bump.

## Key Files

| File | Description |
|------|-------------|
| `segment_header.{h,cc}` | The fixed 88-byte versioned header, its little-endian codec, and `header_crc32c` |
| `segment_writer.{h,cc}` | `EncodeSegment` (pure bytes, filesystem-free) and `WriteSegment` (tmp → fsync → atomic rename) |
| `segment.{h,cc}` | `Segment::Open` validation ladder, the per-OS read-only mmap seam, and `QuarantineSegment` |

## For AI Agents

### Working In This Directory

- **Never read a mapped field through a reinterpreted pointer.** A segment is read straight out of a
  memory mapping, which carries no alignment guarantee, and an unaligned load faults on arm64. Every
  multi-byte field goes through `std::memcpy` into a local. All six targets are little-endian, so no
  byte swap is needed — but the codec is written with explicit shifts so that stays true by
  construction rather than by luck.
- **`format_version` is bumped for ANY layout change.** There is no best-effort forward or backward
  compatibility in S0: a reader that does not recognize the version quarantines the file. Changing a
  field's width, order, or meaning without a bump is a silent data-corruption bug.
- **Release the mapping before quarantining.** Windows refuses to rename a file that still has a live
  view, so `Segment::Open`'s failure path calls `Mapping::Reset()` before moving the file. This is
  invisible on POSIX and broken on Windows — the same class of defect as the earlier
  "Close session file readers before removing them in tests" fix.
- **Quarantine moves, never deletes.** A rejected segment goes to `<name>.corrupt-<unix_millis>`
  beside itself so it stays available for post-hoc inspection.
- **Every byte-reading path is total.** Decoders return `std::nullopt` on truncation or inconsistency
  rather than reading past the mapped range. A corrupt segment must never cause an out-of-bounds
  read, a crash, or a wrong result — the worst permitted outcome is an empty result plus a
  quarantined file.
- Platform branching follows the shape of `../mem_sampler.cc`: compile-time `#if` on
  `_WIN32`/`__APPLE__`/`__linux__`, no third-party library.

### Testing Requirements

`tests/search/segment_test.cpp` covers round-trip fidelity plus the corruption ladder from the plan:
flipped magic, unknown version, truncation, bit-flipped body, overlapping sections, and a section
running past the file. Each must quarantine and leave the index answerable from the MemTable alone.

```bash
cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
cmake --build build-search
ctest --test-dir build-search -R Segment --output-on-failure
```

A corruption test must fail when its guard is removed. Two tests here were originally **vacuous** —
they passed with the guard deleted because an unrelated check rejected the file first. When adding
one, verify both directions: it passes with the guard, and fails without it.
