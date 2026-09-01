// Serializes a MemTable into one immutable on-disk segment.
//
// The write is atomic from a reader's point of view: bytes go to
// "<segment_path>.tmp", are flushed to stable storage, and only then replace
// segment_path via a rename. A crash mid-write therefore leaves the previous
// segment intact rather than a half-written file, and Segment::Open never sees
// a partially materialized index.

#ifndef ISLAND_SEARCH_STORE_SEGMENT_WRITER_H_
#define ISLAND_SEARCH_STORE_SEGMENT_WRITER_H_

#include <cstdint>
#include <filesystem>
#include <vector>

#include "search/expected.h"
#include "search/memtable.h"
#include "search/types.h"

namespace island {
namespace search {

// Encodes `table` in the segment layout. Exposed separately from the file write
// so the byte format can be tested without touching a filesystem.
[[nodiscard]] std::vector<std::uint8_t> EncodeSegment(const MemTable& table);

// Writes `table` to `segment_path` through the tmp-fsync-rename sequence above.
[[nodiscard]] Expected<void, SearchError> WriteSegment(const MemTable& table,
                                                       const std::filesystem::path& segment_path);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_STORE_SEGMENT_WRITER_H_
