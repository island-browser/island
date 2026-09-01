#include "search/store/segment_header.h"

#include <cstring>

#include "search/crc32c.h"

namespace island {
namespace search {
namespace {

// Little-endian appends. Writing byte by byte keeps the encoder independent of
// the host's own integer layout, so the file is identical on every target.
void AppendU16(std::uint16_t value, std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendU32(std::uint32_t value, std::vector<std::uint8_t>& out) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void AppendU64(std::uint64_t value, std::vector<std::uint8_t>& out) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

// Reads through memcpy into a local, then assembles little-endian by shifts, so
// the load is alignment-safe and endianness-explicit.
std::uint16_t ReadU16(const std::uint8_t* p) {
    std::uint8_t b[2];
    std::memcpy(b, p, sizeof(b));
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    std::uint8_t b[4];
    std::memcpy(b, p, sizeof(b));
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
        value = (value << 8) | b[i];
    }
    return value;
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    std::uint8_t b[8];
    std::memcpy(b, p, sizeof(b));
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | b[i];
    }
    return value;
}

}  // namespace

void AppendSegmentHeader(const SegmentHeader& header, std::vector<std::uint8_t>& out) {
    const std::size_t start = out.size();

    AppendU32(kSegmentMagic, out);
    AppendU16(kSegmentFormatVersion, out);
    AppendU16(static_cast<std::uint16_t>(kSegmentHeaderBytes), out);
    AppendU32(0, out);  // flags: reserved, must be zero.
    AppendU64(header.doc_count, out);
    AppendU64(header.next_doc_id, out);
    AppendU64(header.doc_store_off, out);
    AppendU64(header.doc_store_len, out);
    AppendU64(header.term_dict_off, out);
    AppendU64(header.term_dict_len, out);
    AppendU64(header.posting_off, out);
    AppendU64(header.posting_len, out);
    AppendU64(header.avg_doc_len_q16, out);

    // The CRC covers every header byte written above it, and nothing else.
    const std::uint32_t crc = Crc32c(out.data() + start, out.size() - start);
    AppendU32(crc, out);
}

std::optional<SegmentHeader> ParseSegmentHeader(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kSegmentHeaderBytes) {
        return std::nullopt;
    }
    const std::uint8_t* p = bytes.data();

    if (ReadU32(p) != kSegmentMagic) {
        return std::nullopt;
    }
    if (ReadU16(p + 4) != kSegmentFormatVersion) {
        return std::nullopt;
    }
    if (ReadU16(p + 6) != static_cast<std::uint16_t>(kSegmentHeaderBytes)) {
        return std::nullopt;
    }
    if (ReadU32(p + 8) != 0) {
        return std::nullopt;
    }

    const std::uint32_t stored_crc = ReadU32(p + kSegmentHeaderBytes - 4);
    if (Crc32c(p, kSegmentHeaderBytes - 4) != stored_crc) {
        return std::nullopt;
    }

    SegmentHeader header;
    header.doc_count = ReadU64(p + 12);
    header.next_doc_id = ReadU64(p + 20);
    header.doc_store_off = ReadU64(p + 28);
    header.doc_store_len = ReadU64(p + 36);
    header.term_dict_off = ReadU64(p + 44);
    header.term_dict_len = ReadU64(p + 52);
    header.posting_off = ReadU64(p + 60);
    header.posting_len = ReadU64(p + 68);
    header.avg_doc_len_q16 = ReadU64(p + 76);
    return header;
}

}  // namespace search
}  // namespace island
