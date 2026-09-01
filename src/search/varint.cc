#include "search/varint.h"

#include <cstddef>

namespace island {
namespace search {

namespace {

// Payload bits carried by one LEB128 byte.
inline constexpr unsigned kLeb128Bits = 7;

}  // namespace

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

std::optional<std::uint64_t> DecodeVarint(std::span<const std::uint8_t> bytes,
                                          std::size_t* cursor) {
    std::size_t pos = *cursor;
    std::uint64_t value = 0;
    unsigned shift = 0;

    while (true) {
        if (pos >= bytes.size()) {
            return std::nullopt;
        }
        const std::uint8_t byte = bytes[pos];
        const std::uint64_t payload = byte & 0x7Fu;
        // A 10th byte (shift 63) may only carry the single final bit.
        if (shift >= 64 || (shift == 63 && payload > 1)) {
            return std::nullopt;
        }
        ++pos;
        value |= payload << shift;
        if ((byte & 0x80u) == 0) {
            *cursor = pos;
            return value;
        }
        shift += kLeb128Bits;
    }
}

}  // namespace search
}  // namespace island
