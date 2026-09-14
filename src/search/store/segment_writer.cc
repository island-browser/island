#include "search/store/segment_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "search/crc32c.h"
#include "search/posting_codec.h"
#include "search/store/segment_header.h"
#include "search/varint.h"

namespace island {
namespace search {
namespace {

// A rename is only durable once the containing directory entry itself is
// flushed; without this the segment can survive a power loss under its old
// temporary name. POSIX only — Win32 has no directory-handle fsync equivalent.
void SyncDirectoryAfterRename(const std::filesystem::path& segment_path) {
#if !defined(_WIN32)
    const std::filesystem::path parent =
        segment_path.has_parent_path() ? segment_path.parent_path() : ".";
    const int dir_fd = ::open(parent.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#else
    static_cast<void>(segment_path);
#endif
}

SearchError MakeError(SearchErrorKind kind, std::string detail) {
    SearchError error;
    error.kind = kind;
    error.detail = std::move(detail);
    return error;
}

void AppendFramedBytes(std::string_view value, std::vector<std::uint8_t>& out) {
    AppendVarint(value.size(), out);
    out.insert(out.end(), value.begin(), value.end());
}

void AppendU32(std::uint32_t value, std::vector<std::uint8_t>& out) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

// Buffered sequential writer over the platform file API. Encoded pieces land
// in a fixed 1 MiB user-space buffer, so writing per-record scratch vectors
// never assembles a whole section -- let alone a whole segment image -- in
// memory. Every byte routed through Write() also feeds the running body CRC.
class FileOutputStream {
  public:
    FileOutputStream(const FileOutputStream&) = delete;
    FileOutputStream& operator=(const FileOutputStream&) = delete;

    explicit FileOutputStream(const std::filesystem::path& path) {
#if defined(_WIN32)
        handle_ = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    }

    ~FileOutputStream() { Close(); }

    [[nodiscard]] bool is_open() const {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }

    bool Write(const std::uint8_t* data, std::size_t size) {
        crc_ = Crc32cContinue(crc_, data, size);
        std::size_t offset = 0;
        while (offset < size) {
            const std::size_t take = std::min(buffer_.size() - fill_, size - offset);
            std::memcpy(buffer_.data() + fill_, data + offset, take);
            fill_ += take;
            offset += take;
            if (fill_ == buffer_.size() && !FlushBuffer()) {
                return false;
            }
        }
        return true;
    }

    bool Write(const std::vector<std::uint8_t>& bytes) { return Write(bytes.data(), bytes.size()); }

    // Header and trailer bytes are written through here: the trailer's CRC
    // covers only the body regions between them.
    bool WriteWithoutCrc(const std::uint8_t* data, std::size_t size) {
        std::size_t offset = 0;
        while (offset < size) {
            const std::size_t take = std::min(buffer_.size() - fill_, size - offset);
            std::memcpy(buffer_.data() + fill_, data + offset, take);
            fill_ += take;
            offset += take;
            if (fill_ == buffer_.size() && !FlushBuffer()) {
                return false;
            }
        }
        return true;
    }

    // Flushes the buffer, then fsyncs and closes the file. Only after this
    // does the caller rename the temporary over the segment path.
    bool Finish() {
        if (!FlushBuffer()) {
            return false;
        }
#if defined(_WIN32)
        const bool flushed = ::FlushFileBuffers(handle_) != 0;
        ::CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return flushed;
#else
        if (::fsync(fd_) != 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        ::close(fd_);
        fd_ = -1;
        return true;
#endif
    }

    void Close() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    [[nodiscard]] std::uint32_t crc() const { return crc_; }

  private:
    bool FlushBuffer() {
        if (fill_ == 0) {
            return true;
        }
#if defined(_WIN32)
        DWORD written = 0;
        if (::WriteFile(handle_, buffer_.data(), static_cast<DWORD>(fill_), &written, nullptr) ==
                0 ||
            written != fill_) {
            return false;
        }
#else
        std::size_t written_total = 0;
        while (written_total < fill_) {
            const ssize_t written =
                ::write(fd_, buffer_.data() + written_total, fill_ - written_total);
            if (written <= 0) {
                return false;
            }
            written_total += static_cast<std::size_t>(written);
        }
#endif
        fill_ = 0;
        return true;
    }

    std::vector<std::uint8_t> buffer_ = std::vector<std::uint8_t>(1u << 20);
    std::size_t fill_ = 0;
    std::uint32_t crc_ = 0;
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

// Encodes every document record into `scratch` and feeds `sink`. Reused by the
// size pass (with a counting sink) and the write pass so both walks produce
// byte-identical bodies. `total` accumulates the encoded size.
template <typename Sink>
bool StreamDocumentStore(const MemTable& table, std::vector<std::uint8_t>& scratch, Sink& sink,
                         std::uint64_t* total) {
    const std::uint64_t first_doc_id = table.next_doc_id() - table.doc_count();
    for (std::uint64_t id = first_doc_id; id < table.next_doc_id(); ++id) {
        const std::optional<MemTableRecord> record = table.RecordAt(id);
        if (!record.has_value()) {
            continue;
        }
        scratch.clear();
        AppendFramedBytes(record->document.url, scratch);
        AppendFramedBytes(record->document.title, scratch);
        AppendVarint(record->document.visited_at_ms, scratch);
        AppendVarint(record->title_token_count, scratch);
        AppendVarint(record->url_token_count, scratch);
        if (record->document.partition_tag.has_value()) {
            AppendVarint(1, scratch);
            AppendFramedBytes(*record->document.partition_tag, scratch);
        } else {
            AppendVarint(0, scratch);
        }
        if (!sink.Write(scratch)) {
            return false;
        }
        *total += scratch.size();
    }
    return true;
}

// A sink that counts instead of writing, for the size pass.
class CountingSink {
  public:
    bool Write(const std::vector<std::uint8_t>& bytes) {
        total_ += bytes.size();
        return true;
    }

    [[nodiscard]] std::uint64_t total() const { return total_; }

  private:
    std::uint64_t total_ = 0;
};

}  // namespace

std::vector<std::uint8_t> EncodeSegment(const MemTable& table) {
    const std::uint64_t doc_count = table.doc_count();
    const std::uint64_t next_doc_id = table.next_doc_id();
    const std::uint64_t first_doc_id = next_doc_id - doc_count;

    // Document store: one record per id, ascending, so ids stay implicit --
    // first_doc_id follows from next_doc_id minus doc_count on the read side.
    std::vector<std::uint8_t> doc_store;
    for (std::uint64_t id = first_doc_id; id < next_doc_id; ++id) {
        const std::optional<MemTableRecord> record = table.RecordAt(id);
        if (!record.has_value()) {
            continue;
        }
        AppendFramedBytes(record->document.url, doc_store);
        AppendFramedBytes(record->document.title, doc_store);
        AppendVarint(record->document.visited_at_ms, doc_store);
        AppendVarint(record->title_token_count, doc_store);
        AppendVarint(record->url_token_count, doc_store);
        if (record->document.partition_tag.has_value()) {
            AppendVarint(1, doc_store);
            AppendFramedBytes(*record->document.partition_tag, doc_store);
        } else {
            AppendVarint(0, doc_store);
        }
    }

    // Posting region and term dictionary are built together so each dictionary
    // entry can record its posting body's offset within the region.
    std::vector<std::uint8_t> postings;
    std::vector<std::uint8_t> term_dict;
    for (const MemTable::SortedTerm& term : table.SortedTerms()) {
        const std::uint64_t rel_off = postings.size();
        std::vector<std::uint8_t> encoded;
        if (!PostingCodecEncode(term.postings, encoded)) {
            // The MemTable only ever holds strictly ascending ids, so a refusal
            // here would mean an invariant broke upstream; skip the term rather
            // than emit a body the reader could not decode.
            continue;
        }
        postings.insert(postings.end(), encoded.begin(), encoded.end());
        AppendFramedBytes(std::string_view(term.term.data(), term.term.size()), term_dict);
        AppendVarint(rel_off, term_dict);
        AppendVarint(postings.size() - rel_off, term_dict);
    }

    SegmentHeader header;
    header.doc_count = doc_count;
    header.next_doc_id = next_doc_id;
    header.doc_store_off = kSegmentHeaderBytes;
    header.doc_store_len = doc_store.size();
    header.term_dict_off = header.doc_store_off + header.doc_store_len;
    header.term_dict_len = term_dict.size();
    header.posting_off = header.term_dict_off + header.term_dict_len;
    header.posting_len = postings.size();
    header.avg_doc_len_q16 =
        static_cast<std::uint64_t>(table.avg_doc_length() * static_cast<double>(kAvgDocLenQ16One));

    std::vector<std::uint8_t> out;
    out.reserve(kSegmentHeaderBytes + doc_store.size() + term_dict.size() + postings.size() +
                kSegmentTrailerBytes);
    AppendSegmentHeader(header, out);
    out.insert(out.end(), doc_store.begin(), doc_store.end());
    out.insert(out.end(), term_dict.begin(), term_dict.end());
    out.insert(out.end(), postings.begin(), postings.end());

    // Trailer: CRC32C over the body regions, then the magic repeated so a
    // truncation that lands on a plausible length is still rejected.
    const std::uint32_t body_crc =
        Crc32c(out.data() + kSegmentHeaderBytes, out.size() - kSegmentHeaderBytes);
    AppendU32(body_crc, out);
    AppendU32(kSegmentMagic, out);
    return out;
}

Expected<void, SearchError> WriteSegment(const MemTable& table,
                                         const std::filesystem::path& segment_path) {
    // The header carries absolute section offsets, so every body length must
    // be known before the first byte is written. The dictionary scales with
    // vocabulary, not corpus size, and is built once in memory; the document
    // store and the posting region -- the parts that scale with the corpus --
    // are walked twice (size pass, then write pass) into reused scratch
    // buffers, so no full segment image is ever materialized.
    std::vector<std::uint8_t> term_dict;
    std::vector<std::uint8_t> encoded;
    std::uint64_t postings_len = 0;
    for (const MemTable::SortedTerm& term : table.SortedTerms()) {
        encoded.clear();
        if (!PostingCodecEncode(term.postings, encoded)) {
            continue;
        }
        AppendFramedBytes(std::string_view(term.term.data(), term.term.size()), term_dict);
        AppendVarint(postings_len, term_dict);
        AppendVarint(encoded.size(), term_dict);
        postings_len += encoded.size();
    }

    CountingSink counter;
    std::uint64_t doc_store_len = 0;
    std::vector<std::uint8_t> scratch;
    if (!StreamDocumentStore(table, scratch, counter, &doc_store_len)) {
        return Expected<void, SearchError>::Error(
            MakeError(SearchErrorKind::kIoError, "segment size pass failed"));
    }

    SegmentHeader header;
    header.doc_count = table.doc_count();
    header.next_doc_id = table.next_doc_id();
    header.doc_store_off = kSegmentHeaderBytes;
    header.doc_store_len = doc_store_len;
    header.term_dict_off = header.doc_store_off + header.doc_store_len;
    header.term_dict_len = term_dict.size();
    header.posting_off = header.term_dict_off + header.term_dict_len;
    header.posting_len = postings_len;
    header.avg_doc_len_q16 =
        static_cast<std::uint64_t>(table.avg_doc_length() * static_cast<double>(kAvgDocLenQ16One));

    std::vector<std::uint8_t> header_bytes;
    AppendSegmentHeader(header, header_bytes);

    std::filesystem::path tmp_path = segment_path;
    tmp_path += ".tmp";
    FileOutputStream out(tmp_path);
    if (!out.is_open()) {
        return Expected<void, SearchError>::Error(
            MakeError(SearchErrorKind::kIoError, "segment temporary file could not be opened"));
    }

    bool ok = out.WriteWithoutCrc(header_bytes.data(), header_bytes.size());
    if (ok) {
        std::uint64_t written = 0;
        ok = StreamDocumentStore(table, scratch, out, &written);
    }
    if (ok) {
        ok = out.Write(term_dict);
    }
    if (ok) {
        for (const MemTable::SortedTerm& term : table.SortedTerms()) {
            encoded.clear();
            if (!PostingCodecEncode(term.postings, encoded)) {
                // Skip exactly the terms the dictionary skipped, so the
                // posting region matches the header's length.
                continue;
            }
            ok = out.Write(encoded);
            if (!ok) {
                break;
            }
        }
    }
    if (ok) {
        std::vector<std::uint8_t> trailer;
        AppendU32(out.crc(), trailer);
        AppendU32(kSegmentMagic, trailer);
        ok = out.WriteWithoutCrc(trailer.data(), trailer.size());
    }
    if (ok) {
        ok = out.Finish();
    }
    out.Close();

    if (!ok) {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return Expected<void, SearchError>::Error(
            MakeError(SearchErrorKind::kIoError, "segment temporary file could not be written"));
    }

    std::error_code ec;
    // Replaces an existing segment atomically on both POSIX and Win32.
    std::filesystem::rename(tmp_path, segment_path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return Expected<void, SearchError>::Error(
            MakeError(SearchErrorKind::kIoError, "segment rename failed: " + ec.message()));
    }
    // The rename is only durable once the directory entry is flushed too.
    SyncDirectoryAfterRename(segment_path);
    return Expected<void, SearchError>::Ok();
}

}  // namespace search
}  // namespace island
