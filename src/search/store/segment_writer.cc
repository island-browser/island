#include "search/store/segment_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

// Writes `bytes` to `path` and flushes it, plus the containing directory on
// POSIX, so the later rename is durable rather than merely visible.
bool WriteAndSync(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
#if defined(_WIN32)
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::size_t written_total = 0;
    while (written_total < bytes.size()) {
        const DWORD chunk =
            static_cast<DWORD>(std::min<std::size_t>(bytes.size() - written_total, 1u << 20));
        DWORD written = 0;
        if (::WriteFile(file, bytes.data() + written_total, chunk, &written, nullptr) == 0) {
            ::CloseHandle(file);
            return false;
        }
        written_total += written;
    }
    const bool flushed = ::FlushFileBuffers(file) != 0;
    ::CloseHandle(file);
    return flushed;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    std::size_t written_total = 0;
    while (written_total < bytes.size()) {
        const ssize_t written =
            ::write(fd, bytes.data() + written_total, bytes.size() - written_total);
        if (written <= 0) {
            ::close(fd);
            return false;
        }
        written_total += static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        return false;
    }
    ::close(fd);

    // A rename is only durable once the directory entry itself is flushed;
    // without this the file can survive a power loss under its old name.
    const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : ".";
    const int dir_fd = ::open(parent.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
    return true;
#endif
}

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
        AppendFramedBytes(record->document->url, doc_store);
        AppendFramedBytes(record->document->title, doc_store);
        AppendVarint(record->document->visited_at_ms, doc_store);
        AppendVarint(record->title_token_count, doc_store);
        AppendVarint(record->url_token_count, doc_store);
        if (record->document->partition_tag.has_value()) {
            AppendVarint(1, doc_store);
            AppendFramedBytes(*record->document->partition_tag, doc_store);
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
    const std::vector<std::uint8_t> bytes = EncodeSegment(table);

    std::filesystem::path tmp_path = segment_path;
    tmp_path += ".tmp";

    if (!WriteAndSync(tmp_path, bytes)) {
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
    return Expected<void, SearchError>::Ok();
}

}  // namespace search
}  // namespace island
