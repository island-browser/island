#include "search/crc32c.h"

#include <array>

namespace island {
namespace search {
namespace {

// Castagnoli polynomial, reflected bit order as used by iSCSI/EXT4/SSE4.2.
inline constexpr std::uint32_t kCrc32cPolynomial = 0x82F63B78u;

// Reflected lookup table, generated at program start from the polynomial.
// A 256-entry table keeps the hot loop branch-free with no vendored library.
const std::array<std::uint32_t, 256>& Table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ kCrc32cPolynomial : crc >> 1;
            }
            table[i] = crc;
        }
        return table;
    }();
    return table;
}

}  // namespace

std::uint32_t Crc32cContinue(std::uint32_t crc, const void* data, std::size_t size) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    const std::array<std::uint32_t, 256>& table = Table();
    crc ^= 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Crc32cContinue(std::uint32_t crc, std::string_view data) {
    return Crc32cContinue(crc, data.data(), data.size());
}

std::uint32_t Crc32c(const void* data, std::size_t size) {
    return Crc32cContinue(0, data, size);
}

std::uint32_t Crc32c(std::string_view data) {
    return Crc32c(data.data(), data.size());
}

}  // namespace search
}  // namespace island
