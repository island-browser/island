#include "search/store/segment.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "search/crc32c.h"
#include "search/posting_codec.h"
#include "search/varint.h"

namespace island {
namespace search {
namespace {

SearchError MakeError(SearchErrorKind kind, std::string detail) {
    SearchError error;
    error.kind = kind;
    error.detail = std::move(detail);
    return error;
}

// Reads one LEB128-framed byte string: a varint length followed by that many
// bytes. Total: any truncation or overflow yields std::nullopt rather than a
// read past the mapped range.
std::optional<std::string_view> DecodeFramedBytes(std::span<const std::uint8_t> bytes,
                                                  std::size_t* cursor) {
    const std::optional<std::uint64_t> length = DecodeVarint(bytes, cursor);
    if (!length.has_value()) {
        return std::nullopt;
    }
    if (*length > bytes.size() - *cursor) {
        return std::nullopt;
    }
    const auto* start = reinterpret_cast<const char*>(bytes.data() + *cursor);
    *cursor += static_cast<std::size_t>(*length);
    return std::string_view(start, static_cast<std::size_t>(*length));
}

}  // namespace

// ---------------------------------------------------------------------------
// Read-only file mapping, one thin compile-time branch per OS. No third-party
// library; the shape mirrors src/search/mem_sampler.cc.
// ---------------------------------------------------------------------------
class Segment::Mapping {
  public:
    Mapping() = default;

    ~Mapping() { Reset(); }

    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;

    // Maps `path` read-only. Returns false when the file cannot be opened,
    // cannot be mapped, or is empty (an empty file can hold no header).
    bool Map(const std::filesystem::path& path) {
        Reset();
#if defined(_WIN32)
        file_ = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        LARGE_INTEGER file_size{};
        if (::GetFileSizeEx(file_, &file_size) == 0 || file_size.QuadPart <= 0) {
            Reset();
            return false;
        }
        mapping_ = ::CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            Reset();
            return false;
        }
        void* view = ::MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr) {
            Reset();
            return false;
        }
        data_ = static_cast<const std::uint8_t*>(view);
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        return true;
#else
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        struct stat info {};
        if (::fstat(fd, &info) != 0 || info.st_size <= 0) {
            ::close(fd);
            return false;
        }
        const auto size = static_cast<std::size_t>(info.st_size);
        void* view = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        // The mapping outlives the descriptor, so the descriptor is closed
        // immediately rather than held for the segment's lifetime.
        ::close(fd);
        if (view == MAP_FAILED) {
            return false;
        }
        data_ = static_cast<const std::uint8_t*>(view);
        size_ = size;
        return true;
#endif
    }

    // Releases the mapping. Called before quarantine because Windows refuses to
    // rename a file that still has a live view.
    void Reset() {
#if defined(_WIN32)
        if (data_ != nullptr) {
            ::UnmapViewOfFile(data_);
        }
        if (mapping_ != nullptr) {
            ::CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_ != nullptr && size_ > 0) {
            ::munmap(const_cast<std::uint8_t*>(data_), size_);
        }
#endif
        data_ = nullptr;
        size_ = 0;
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return {data_, size_}; }

  private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
#if defined(_WIN32)
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#endif
};

Segment::Segment() : mapping_(std::make_unique<Mapping>()) {}

Segment::~Segment() = default;

double Segment::avg_doc_len() const noexcept {
    return static_cast<double>(header_.avg_doc_len_q16) / static_cast<double>(kAvgDocLenQ16One);
}

std::optional<std::filesystem::path> QuarantineSegment(const std::filesystem::path& path) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    std::filesystem::path target = path;
    target += ".corrupt-" + std::to_string(millis);

    std::error_code ec;
    // std::filesystem::rename replaces an existing target on both POSIX and
    // Win32, and the timestamped name makes a collision practically impossible
    // anyway. The file is moved, never removed: a quarantined segment stays
    // available for post-hoc inspection.
    std::filesystem::rename(path, target, ec);
    if (ec) {
        return std::nullopt;
    }
    return target;
}

Expected<std::unique_ptr<Segment>, SearchError> Segment::Open(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        // No segment yet is not corruption: there is nothing to quarantine.
        return Expected<std::unique_ptr<Segment>, SearchError>::Error(
            MakeError(SearchErrorKind::kIoError, "segment file does not exist"));
    }

    auto segment = std::unique_ptr<Segment>(new Segment());

    // Any failure below must release the mapping before moving the file.
    const auto reject = [&segment, &path](SearchErrorKind kind, std::string detail) {
        segment->mapping_->Reset();
        const std::optional<std::filesystem::path> quarantined = QuarantineSegment(path);
        if (quarantined.has_value()) {
            detail += " (quarantined to " + quarantined->filename().string() + ")";
        } else {
            detail += " (quarantine failed)";
        }
        return Expected<std::unique_ptr<Segment>, SearchError>::Error(
            MakeError(kind, std::move(detail)));
    };

    // (1) map the file.
    if (!segment->mapping_->Map(path)) {
        return reject(SearchErrorKind::kIoError, "segment could not be mapped");
    }
    const std::span<const std::uint8_t> bytes = segment->mapping_->bytes();

    // (2) size must hold at least a header and a trailer.
    if (bytes.size() < kSegmentHeaderBytes + kSegmentTrailerBytes) {
        return reject(SearchErrorKind::kCorruptSegment, "segment shorter than header plus trailer");
    }

    // (3) + (4) magic, version and header CRC, all inside ParseSegmentHeader.
    const std::optional<SegmentHeader> header = ParseSegmentHeader(bytes);
    if (!header.has_value()) {
        return reject(SearchErrorKind::kVersionMismatch,
                      "segment magic, version, or header checksum rejected");
    }
    segment->header_ = *header;

    // (5) every section must lie inside the body region and none may overlap.
    const std::uint64_t body_begin = kSegmentHeaderBytes;
    const std::uint64_t body_end = bytes.size() - kSegmentTrailerBytes;
    struct Section {
        std::uint64_t off;
        std::uint64_t len;
    };
    const Section sections[] = {
        {header->doc_store_off, header->doc_store_len},
        {header->term_dict_off, header->term_dict_len},
        {header->posting_off, header->posting_len},
    };
    for (const Section& section : sections) {
        // The addition cannot wrap: len is bounded by body_end below, and the
        // check is written so an overflowing len fails the first comparison.
        if (section.off < body_begin || section.off > body_end ||
            section.len > body_end - section.off) {
            return reject(SearchErrorKind::kCorruptSegment, "segment section out of bounds");
        }
    }
    for (std::size_t i = 0; i < std::size(sections); ++i) {
        for (std::size_t j = i + 1; j < std::size(sections); ++j) {
            const Section& a = sections[i];
            const Section& b = sections[j];
            if (a.len > 0 && b.len > 0 && a.off < b.off + b.len && b.off < a.off + a.len) {
                return reject(SearchErrorKind::kCorruptSegment, "segment sections overlap");
            }
        }
    }

    // (6) trailer: body CRC and the repeated magic.
    const std::uint8_t* trailer = bytes.data() + body_end;
    std::uint8_t trailer_bytes[kSegmentTrailerBytes];
    std::memcpy(trailer_bytes, trailer, kSegmentTrailerBytes);
    std::uint32_t stored_body_crc = 0;
    std::uint32_t repeated_magic = 0;
    for (int i = 3; i >= 0; --i) {
        stored_body_crc = (stored_body_crc << 8) | trailer_bytes[i];
        repeated_magic = (repeated_magic << 8) | trailer_bytes[4 + i];
    }
    if (repeated_magic != kSegmentMagic) {
        return reject(SearchErrorKind::kCorruptSegment, "segment trailer magic rejected");
    }
    const std::uint32_t body_crc =
        Crc32c(bytes.data() + body_begin, static_cast<std::size_t>(body_end - body_begin));
    if (body_crc != stored_body_crc) {
        return reject(SearchErrorKind::kCorruptSegment, "segment body checksum rejected");
    }

    // The id range is dense, so the first id follows from the header alone.
    if (header->next_doc_id < header->doc_count + 1) {
        return reject(SearchErrorKind::kCorruptSegment, "segment doc id range inconsistent");
    }
    segment->first_doc_id_ = header->next_doc_id - header->doc_count;

    // Index the term dictionary.
    const std::span<const std::uint8_t> dict =
        bytes.subspan(static_cast<std::size_t>(header->term_dict_off),
                      static_cast<std::size_t>(header->term_dict_len));
    std::size_t cursor = 0;
    std::string_view previous_term;
    while (cursor < dict.size()) {
        const std::optional<std::string_view> term = DecodeFramedBytes(dict, &cursor);
        if (!term.has_value()) {
            return reject(SearchErrorKind::kCorruptSegment, "segment term dictionary truncated");
        }
        const std::optional<std::uint64_t> rel_off = DecodeVarint(dict, &cursor);
        const std::optional<std::uint64_t> len = DecodeVarint(dict, &cursor);
        if (!rel_off.has_value() || !len.has_value()) {
            return reject(SearchErrorKind::kCorruptSegment, "segment term metadata truncated");
        }
        if (*rel_off > header->posting_len || *len > header->posting_len - *rel_off) {
            return reject(SearchErrorKind::kCorruptSegment, "segment posting range out of bounds");
        }
        if (!segment->terms_.empty() && !(previous_term < *term)) {
            return reject(SearchErrorKind::kCorruptSegment, "segment terms not sorted");
        }
        previous_term = *term;
        segment->terms_.push_back(TermEntry{*term, header->posting_off + *rel_off, *len});
    }

    // Index the document store: one offset per record, in id order.
    const std::span<const std::uint8_t> store =
        bytes.subspan(static_cast<std::size_t>(header->doc_store_off),
                      static_cast<std::size_t>(header->doc_store_len));
    segment->doc_off_.reserve(static_cast<std::size_t>(header->doc_count));
    cursor = 0;
    for (std::uint64_t i = 0; i < header->doc_count; ++i) {
        if (cursor >= store.size()) {
            return reject(SearchErrorKind::kCorruptSegment, "segment document store truncated");
        }
        segment->doc_off_.push_back(cursor);
        // Skip the record: url, title, visited_at_ms, both token counts, tag.
        if (!DecodeFramedBytes(store, &cursor).has_value() ||
            !DecodeFramedBytes(store, &cursor).has_value() ||
            !DecodeVarint(store, &cursor).has_value() ||
            !DecodeVarint(store, &cursor).has_value() ||
            !DecodeVarint(store, &cursor).has_value()) {
            return reject(SearchErrorKind::kCorruptSegment, "segment document record truncated");
        }
        const std::optional<std::uint64_t> has_tag = DecodeVarint(store, &cursor);
        if (!has_tag.has_value() || *has_tag > 1) {
            return reject(SearchErrorKind::kCorruptSegment, "segment document tag flag invalid");
        }
        if (*has_tag == 1 && !DecodeFramedBytes(store, &cursor).has_value()) {
            return reject(SearchErrorKind::kCorruptSegment, "segment document tag truncated");
        }
    }

    return Expected<std::unique_ptr<Segment>, SearchError>(std::move(segment));
}

std::optional<std::vector<std::uint64_t>> Segment::PostingsFor(std::string_view term) const {
    const auto it = std::lower_bound(
        terms_.begin(), terms_.end(), term,
        [](const TermEntry& entry, std::string_view needle) { return entry.term < needle; });
    if (it == terms_.end() || it->term != term) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> bytes = mapping_->bytes();
    const PostingDecodeResult decoded =
        PostingCodecDecode(bytes.subspan(static_cast<std::size_t>(it->posting_off),
                                         static_cast<std::size_t>(it->posting_len)));
    if (decoded.error != PostingCodecError::kOk) {
        return std::nullopt;
    }
    return decoded.ids;
}

std::optional<SegmentDocument> Segment::DocumentAt(DocId id) const {
    if (id < first_doc_id_ || id >= header_.next_doc_id) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> store =
        mapping_->bytes().subspan(static_cast<std::size_t>(header_.doc_store_off),
                                  static_cast<std::size_t>(header_.doc_store_len));
    std::size_t cursor = doc_off_[static_cast<std::size_t>(id - first_doc_id_)];

    const std::optional<std::string_view> url = DecodeFramedBytes(store, &cursor);
    const std::optional<std::string_view> title = DecodeFramedBytes(store, &cursor);
    const std::optional<std::uint64_t> visited = DecodeVarint(store, &cursor);
    const std::optional<std::uint64_t> title_tokens = DecodeVarint(store, &cursor);
    const std::optional<std::uint64_t> url_tokens = DecodeVarint(store, &cursor);
    const std::optional<std::uint64_t> has_tag = DecodeVarint(store, &cursor);
    if (!url.has_value() || !title.has_value() || !visited.has_value() ||
        !title_tokens.has_value() || !url_tokens.has_value() || !has_tag.has_value()) {
        return std::nullopt;
    }

    SegmentDocument record;
    record.document.id = id;
    record.document.url = std::string(*url);
    record.document.title = std::string(*title);
    record.document.visited_at_ms = *visited;
    record.title_token_count = static_cast<std::uint32_t>(*title_tokens);
    record.url_token_count = static_cast<std::uint32_t>(*url_tokens);
    if (*has_tag == 1) {
        const std::optional<std::string_view> tag = DecodeFramedBytes(store, &cursor);
        if (!tag.has_value()) {
            return std::nullopt;
        }
        record.document.partition_tag = std::string(*tag);
    }
    return record;
}

std::vector<std::string_view> Segment::Terms() const {
    std::vector<std::string_view> out;
    out.reserve(terms_.size());
    for (const TermEntry& entry : terms_) {
        out.push_back(entry.term);
    }
    return out;
}

}  // namespace search
}  // namespace island
