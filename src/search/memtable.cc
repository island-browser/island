#include "search/memtable.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace island {
namespace search {

std::optional<std::uint64_t> DocumentArena::Append(std::string_view bytes) {
    // A string larger than one chunk can never be stored without straddling,
    // which the arena layout forbids; callers pre-validate so this is only a
    // defensive guard, never a partial write.
    if (bytes.size() > kChunkBytes) {
        return std::nullopt;
    }
    if (chunks_.empty() || bytes.size() > kChunkBytes - chunk_fill_) {
        chunks_.push_back(std::make_unique_for_overwrite<char[]>(kChunkBytes));
        chunk_fill_ = 0;
    }
    const std::uint64_t offset =
        (static_cast<std::uint64_t>(chunks_.size() - 1) << kChunkBits) | chunk_fill_;
    std::memcpy(chunks_.back().get() + chunk_fill_, bytes.data(), bytes.size());
    chunk_fill_ += bytes.size();
    return offset;
}

std::string_view DocumentArena::View(std::uint64_t offset, std::uint32_t size) const {
    const std::size_t chunk = static_cast<std::size_t>(offset >> kChunkBits);
    const std::size_t within = static_cast<std::size_t>(offset & (kChunkBytes - 1));
    return std::string_view(chunks_[chunk].get() + within, size);
}

MemTable::MemTable(std::uint64_t first_doc_id)
    : first_doc_id_(first_doc_id < 1 ? 1 : first_doc_id), next_doc_id_(first_doc_id_) {}

std::uint32_t MemTable::TermIndexFor(std::string_view term) {
    const auto it = term_index_.find(std::string(term));
    if (it != term_index_.end()) {
        return it->second;
    }
    const auto index = static_cast<std::uint32_t>(term_by_index_.size());
    term_by_index_.emplace_back(term);
    term_index_.emplace(term_by_index_.back(), index);
    postings_by_term_index_.emplace_back();
    return index;
}

std::optional<DocId> MemTable::AddDocument(StoredDocument document,
                                           std::span<const std::string> title_tokens,
                                           std::span<const std::string> url_tokens) {
    // Reject unstorable fields before any model state changes so the table
    // stays exactly as it was: no id consumed, no posting touched.
    if (document.url.size() > DocumentArena::kChunkBytes ||
        document.title.size() > DocumentArena::kChunkBytes ||
        (document.partition_tag.has_value() &&
         document.partition_tag->size() > DocumentArena::kChunkBytes)) {
        return std::nullopt;
    }

    const DocId assigned = next_doc_id_;
    document.id = assigned;

    // One entry per distinct term in this document, carrying both field
    // frequencies. Linear scans stay cheap because a single document holds few
    // distinct terms. The entries are transient ingest state: the ranker
    // re-derives frequencies from the stored text at query time, so nothing
    // per-document is retained after this function returns.
    frequency_scratch_.clear();
    const auto entry_for = [this](std::uint32_t term_index) -> FrequencyScratchEntry& {
        for (FrequencyScratchEntry& existing : frequency_scratch_) {
            if (existing.term_index == term_index) {
                return existing;
            }
        }
        FrequencyScratchEntry fresh;
        fresh.term_index = term_index;
        fresh.title_tf = 0;
        fresh.url_tf = 0;
        frequency_scratch_.push_back(fresh);
        return frequency_scratch_.back();
    };

    const auto saturating_increment = [](std::uint16_t value) {
        return value == UINT16_MAX ? value : static_cast<std::uint16_t>(value + 1);
    };
    for (const std::string& token : title_tokens) {
        FrequencyScratchEntry& entry = entry_for(TermIndexFor(token));
        entry.title_tf = saturating_increment(entry.title_tf);
    }
    for (const std::string& token : url_tokens) {
        FrequencyScratchEntry& entry = entry_for(TermIndexFor(token));
        entry.url_tf = saturating_increment(entry.url_tf);
    }

    // Documents arrive in id order and each distinct term is appended at most
    // once per document, so every posting list stays ascending by construction.
    for (const FrequencyScratchEntry& entry : frequency_scratch_) {
        postings_by_term_index_[entry.term_index].push_back(assigned);
    }

    StoredRecord record;
    // Pre-validated above: every append succeeds.
    record.url_offset = *arena_.Append(document.url);
    record.url_size = static_cast<std::uint32_t>(document.url.size());
    record.title_offset = *arena_.Append(document.title);
    record.title_size = static_cast<std::uint32_t>(document.title.size());
    record.visited_at_ms = document.visited_at_ms;
    record.title_token_count = static_cast<std::uint32_t>(title_tokens.size());
    record.url_token_count = static_cast<std::uint32_t>(url_tokens.size());
    if (document.partition_tag.has_value()) {
        record.tag_offset = *arena_.Append(*document.partition_tag);
        record.tag_size = static_cast<std::uint32_t>(document.partition_tag->size());
    }

    total_tokens_ += title_tokens.size() + url_tokens.size();
    records_.push_back(record);
    ++next_doc_id_;
    return assigned;
}

std::span<const std::uint64_t> MemTable::PostingsFor(std::string_view term) const {
    const auto it = term_index_.find(std::string(term));
    if (it == term_index_.end()) {
        return {};
    }
    return postings_by_term_index_[it->second];
}

std::optional<MemTableRecord> MemTable::RecordAt(DocId id) const {
    if (id < first_doc_id_ || id >= next_doc_id_) {
        return std::nullopt;
    }
    const StoredRecord& stored = records_[static_cast<std::size_t>(id - first_doc_id_)];
    MemTableRecord view;
    view.document.url = arena_.View(stored.url_offset, stored.url_size);
    view.document.title = arena_.View(stored.title_offset, stored.title_size);
    view.document.visited_at_ms = stored.visited_at_ms;
    if (stored.tag_size > 0) {
        view.document.partition_tag = arena_.View(stored.tag_offset, stored.tag_size);
    }
    view.title_token_count = stored.title_token_count;
    view.url_token_count = stored.url_token_count;
    return view;
}

std::vector<MemTable::SortedTerm> MemTable::SortedTerms() const {
    std::vector<std::uint32_t> order(term_by_index_.size());
    for (std::uint32_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [this](std::uint32_t lhs, std::uint32_t rhs) {
        return term_by_index_[lhs] < term_by_index_[rhs];
    });

    std::vector<SortedTerm> sorted;
    sorted.reserve(order.size());
    for (const std::uint32_t index : order) {
        SortedTerm term;
        term.term =
            std::span<const char>(term_by_index_[index].data(), term_by_index_[index].size());
        term.postings = postings_by_term_index_[index];
        sorted.push_back(term);
    }
    return sorted;
}

}  // namespace search
}  // namespace island
