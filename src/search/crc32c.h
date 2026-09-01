// CRC32C (Castagnoli) checksum, table-driven, no external library.
//
// Used by the segment header and trailer so a truncated or bit-flipped segment
// is detected before any query touches it.

#ifndef ISLAND_SEARCH_CRC32C_H_
#define ISLAND_SEARCH_CRC32C_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace island {
namespace search {

// One-shot CRC32C over `data`. Deterministic for identical inputs.
std::uint32_t Crc32c(std::string_view data);
std::uint32_t Crc32c(const void* data, std::size_t size);

// Incremental form: feeds `data` into a running CRC32C started from a prior
// result (or 0 for the first block of a stream).
std::uint32_t Crc32cContinue(std::uint32_t crc, std::string_view data);
std::uint32_t Crc32cContinue(std::uint32_t crc, const void* data, std::size_t size);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_CRC32C_H_
