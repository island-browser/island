#ifndef ISLAND_SEARCH_POSTING_CODEC_H_
#define ISLAND_SEARCH_POSTING_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace island {
namespace search {

// Maximum number of document ids in one encoded block. A block header is a
// single count byte, so this must fit in 7 bits (0 is reserved as corrupt).
inline constexpr std::size_t kPostingBlockSize = 128;

// Result of a decode attempt. The hot path never throws; all failure modes are
// reported through this enum.
enum class PostingCodecError : std::uint8_t {
    kOk,
    // Input ended before a complete block header or varint was available.
    kTruncated,
    // Input was structurally invalid: reserved count byte, a varint that
    // overflows uint64, a zero/negative delta, or accumulated overflow.
    kCorrupt,
};

// Output of PostingCodecDecode. On success `error` is kOk, `ids` holds the
// strictly ascending decoded ids, and `consumed` equals the number of input
// bytes read (always `in.size()` on success). On failure `ids` is empty and
// `consumed` is unspecified.
struct PostingDecodeResult {
    std::vector<std::uint64_t> ids;
    PostingCodecError error = PostingCodecError::kOk;
    std::size_t consumed = 0;
};

// Appends the LEB128 delta-encoded, block-grouped form of the strictly
// ascending posting list `ids` to `out`. Multiple lists may be appended to the
// same `out` sequentially, producing a concatenation of their encodings.
//
// Returns false (and leaves `out` unchanged) when `ids` is not strictly
// ascending, which is a caller contract violation. Never throws.
bool PostingCodecEncode(std::span<const std::uint64_t> ids, std::vector<std::uint8_t>& out);

// Decodes one posting list from the front of `in`, reading blocks until `in`
// is exhausted. Returns kOk with `ids` populated and `consumed` set on
// success; returns kTruncated or kCorrupt on malformed input. Bounds-checked:
// never reads past `in`, never throws.
//
// The encoding is not self-delimiting at the list level: to decode the n-th of
// several concatenated lists the caller passes the exact subspan of `in` that
// holds it, not the full buffer.
PostingDecodeResult PostingCodecDecode(std::span<const std::uint8_t> in);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_POSTING_CODEC_H_