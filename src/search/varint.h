// Unsigned LEB128 varint used to frame segment sections.
//
// Encode appends; decode is total: it returns std::nullopt on truncation or
// overflow (>10 payload bytes) instead of reading out of bounds, which is the
// primitive that keeps every segment-byte read path bounds-checked.

#ifndef ISLAND_SEARCH_VARINT_H_
#define ISLAND_SEARCH_VARINT_H_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace island {
namespace search {

// Appends `value` as an unsigned LEB128 varint (1..10 bytes). Never throws.
void AppendVarint(std::uint64_t value, std::vector<std::uint8_t>& out);

// Decodes one varint from `bytes` starting at (and advancing) `*cursor`.
// Returns std::nullopt when the stream truncates mid-value or the value
// overflows 64 bits; `*cursor` is left unchanged on failure. Never reads out
// of bounds; never throws.
std::optional<std::uint64_t> DecodeVarint(std::span<const std::uint8_t> bytes,
                                          std::size_t* cursor);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_VARINT_H_
