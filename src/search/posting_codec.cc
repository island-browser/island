#include "search/posting_codec.h"

#include <cstdint>

namespace island {
namespace search {
namespace {

// Number of payload bits carried by one LEB128 byte.
inline constexpr unsigned kLeb128Bits = 7;

// Appends `value` as an unsigned LEB128 varint to `out`. `value` fits in 64
// bits, so it never exceeds 10 bytes. Never throws.
void AppendVarint(std::uint64_t value, std::vector<std::uint8_t>& out) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7Fu);
        value >>= kLeb128Bits;
        if (value != 0) {
            byte |= 0x80u;
        }
        out.push_back(byte);
    } while (value != 0);
}

}  // namespace

bool PostingCodecEncode(std::span<const std::uint64_t> ids, std::vector<std::uint8_t>& out) {
    // Validate strict ascending order up front so a contract violation leaves
    // `out` untouched.
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] <= ids[i - 1]) {
            return false;
        }
    }

    // Chunk into fixed-size blocks; each carries up to kPostingBlockSize ids.
    // Deltas are global across blocks; only the count header is per-block.
    std::size_t base = 0;
    std::uint64_t previous = 0;
    bool have_previous = false;
    while (base < ids.size()) {
        const std::size_t take =
            (ids.size() - base < kPostingBlockSize) ? (ids.size() - base) : kPostingBlockSize;
        // Block header: number of ids in this block (1..kPostingBlockSize).
        out.push_back(static_cast<std::uint8_t>(take));
        for (std::size_t i = 0; i < take; ++i) {
            const std::uint64_t id = ids[base + i];
            const std::uint64_t delta = have_previous ? id - previous : id;
            AppendVarint(delta, out);
            previous = id;
            have_previous = true;
        }
        base += take;
    }
    return true;
}

PostingDecodeResult PostingCodecDecode(std::span<const std::uint8_t> in) {
    PostingDecodeResult result;
    std::size_t pos = 0;
    std::uint64_t previous = 0;
    bool have_previous = false;

    // An empty posting list is encoded as an empty stream. Non-empty input is
    // a sequence of self-describing blocks; the stream ends exactly on a block
    // boundary, so exhausted input is success, not truncation.
    while (pos < in.size()) {
        // Block header: read the count byte. 0 is reserved as corrupt.
        const std::uint8_t count = in[pos++];
        if (count == 0) {
            result.ids.clear();
            result.consumed = 0;
            result.error = PostingCodecError::kCorrupt;
            return result;
        }

        for (unsigned i = 0; i < count; ++i) {
            // Read one LEB128 varint, bounds-checked.
            std::uint64_t delta = 0;
            unsigned shift = 0;
            while (true) {
                if (pos >= in.size()) {
                    result.ids.clear();
                    result.consumed = 0;
                    result.error = PostingCodecError::kTruncated;
                    return result;
                }
                const std::uint8_t byte = in[pos++];
                const std::uint64_t payload = byte & 0x7Fu;
                // A 10th byte (shift 63) may only carry the single final bit.
                if (shift >= 64 || (shift == 63 && payload > 1)) {
                    result.ids.clear();
                    result.consumed = 0;
                    result.error = PostingCodecError::kCorrupt;
                    return result;
                }
                delta |= payload << shift;
                if ((byte & 0x80u) == 0) {
                    break;
                }
                shift += kLeb128Bits;
            }

            // A zero delta is only corrupt when it would repeat a prior id;
            // the first id may legitimately be 0.
            if (delta == 0 && have_previous) {
                result.ids.clear();
                result.consumed = 0;
                result.error = PostingCodecError::kCorrupt;
                return result;
            }

            const std::uint64_t id = have_previous ? previous + delta : delta;
            if (have_previous && id < previous) {
                result.ids.clear();
                result.consumed = 0;
                result.error = PostingCodecError::kCorrupt;
                return result;
            }
            result.ids.push_back(id);
            previous = id;
            have_previous = true;
        }
    }

    result.error = PostingCodecError::kOk;
    result.consumed = pos;
    return result;
}

}  // namespace search
}  // namespace island