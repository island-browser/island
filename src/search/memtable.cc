#include "search/memtable.h"

#include <algorithm>
#include <utility>

namespace island {
namespace search {
namespace {

// Accumulates one field's tokens into `entries`, keyed by term index. The
// caller resolves term indices first so both fields share one lookup path.
std::uint16_t SaturatingIncrement(std::uint16_t value) {
    return value == UINT16_MAX ? value : static_cast<std::uint16_t>(value + 1);
}

}  // namespace

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

DocId MemTable::AddDocument(StoredDocument document, std::span<const std::string> title_tokens,
                            std::span<const std::string> url_tokens) {
    const DocId assigned = next_doc_id_;
    document.id = assigned;

    StoredRecord record;
    record.document = std::move(document);
    record.title_token_count = static_cast<std::uint32_t>(title_tokens.size());
    record.url_token_count = static_cast<std::uint32_t>(url_tokens.size());

    // One entry per distinct term in this document, carrying both field
    // frequencies. Linear scans stay cheap because a single document holds few
    // distinct terms.
    const auto entry_for = [&record](std::uint32_t term_index) -> TermFrequencyEntry& {
        for (TermFrequencyEntry& existing : record.term_frequencies) {
            if (existing.term_index == term_index) {
                return existing;
            }
        }
        TermFrequencyEntry fresh;
        fresh.term_index = term_index;
        record.term_frequencies.push_back(fresh);
        return record.term_frequencies.back();
    };

    for (const std::string& token : title_tokens) {
        TermFrequencyEntry& entry = entry_for(TermIndexFor(token));
        entry.title_tf = SaturatingIncrement(entry.title_tf);
    }
    for (const std::string& token : url_tokens) {
        TermFrequencyEntry& entry = entry_for(TermIndexFor(token));
        entry.url_tf = SaturatingIncrement(entry.url_tf);
    }

    // Documents arrive in id order and each distinct term is appended at most
    // once per document, so every posting list stays ascending by construction.
    for (const TermFrequencyEntry& entry : record.term_frequencies) {
        postings_by_term_index_[entry.term_index].push_back(assigned);
    }

    total_tokens_ += title_tokens.size() + url_tokens.size();
    records_.push_back(std::move(record));
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
    view.document = &stored.document;
    view.title_token_count = stored.title_token_count;
    view.url_token_count = stored.url_token_count;
    view.term_frequencies = stored.term_frequencies;
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
        term.term = std::span<const char>(term_by_index_[index].data(), term_by_index_[index].size());
        term.postings = postings_by_term_index_[index];
        sorted.push_back(term);
    }
    return sorted;
}

}  // namespace search
}  // namespace island
