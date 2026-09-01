// The fixed, versioned segment header and its byte-exact serialization.
//
// Every multi-byte field is little-endian and is read through std::memcpy
// rather than a reinterpreted pointer: a segment is read straight out of a
// memory mapping, where nothing guarantees the natural alignment a
// reinterpret_cast would require, and an unaligned load faults on arm64. All
// six supported targets are little-endian, so the memcpy needs no byte swap.

#ifndef ISLAND_SEARCH_STORE_SEGMENT_HEADER_H_
#define ISLAND_SEARCH_STORE_SEGMENT_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace island {
namespace search {

// "ISS0" — Island Search Segment v0 — read little-endian.
inline constexpr std::uint32_t kSegmentMagic = 0x30535349;

// Bumped on ANY on-disk layout change. A reader that does not recognize the
// version quarantines the file; it never reinterprets the bytes.
inline constexpr std::uint16_t kSegmentFormatVersion = 1;

// magic(4) + format_version(2) + header_bytes(2) + flags(4) + doc_count(8) +
// next_doc_id(8) + 3 * (offset(8) + length(8)) + avg_doc_len_q16(8) +
// header_crc32c(4).
inline constexpr std::size_t kSegmentHeaderBytes = 88;

// body_crc32c(4) + repeated magic(4).
inline constexpr std::size_t kSegmentTrailerBytes = 8;

// avg_doc_len is stored as fixed-point Q16 so the header holds no floating
// point and is bit-reproducible across targets.
inline constexpr std::uint64_t kAvgDocLenQ16One = 1ULL << 16;

struct SegmentHeader {
    std::uint64_t doc_count = 0;
    std::uint64_t next_doc_id = 1;
    std::uint64_t doc_store_off = 0;
    std::uint64_t doc_store_len = 0;
    std::uint64_t term_dict_off = 0;
    std::uint64_t term_dict_len = 0;
    std::uint64_t posting_off = 0;
    std::uint64_t posting_len = 0;
    std::uint64_t avg_doc_len_q16 = 0;
};

// Appends the header's kSegmentHeaderBytes bytes, with header_crc32c computed
// over the preceding bytes of this same header.
void AppendSegmentHeader(const SegmentHeader& header, std::vector<std::uint8_t>& out);

// Parses a header from the front of `bytes`. Returns std::nullopt when the
// span is too short, the magic or version is unrecognized, the recorded
// header_bytes disagrees with this build, flags are not zero, or the stored
// header CRC does not match. Never reads outside `bytes`.
[[nodiscard]] std::optional<SegmentHeader> ParseSegmentHeader(std::span<const std::uint8_t> bytes);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_STORE_SEGMENT_HEADER_H_
