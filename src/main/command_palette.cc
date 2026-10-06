#include "command_palette.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

namespace island {
namespace {

// ASCII-only case folding so ranking is deterministic regardless of locale.
std::string FoldAscii(std::string_view text) {
    std::string folded;
    folded.reserve(text.size());
    for (const char character : text) {
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return folded;
}

std::string_view TrimAsciiWhitespace(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// A word boundary is any position whose preceding character is not ASCII
// alphanumeric, so spaces and URL punctuation ('/', '.', ':', '-', '_', '?',
// '#', '&', '=') all start words in titles and URLs.
bool PrecededByWordBoundary(const std::string& folded_candidate, std::size_t position) {
    if (position == 0) {
        // Position 0 is a prefix match, which outranks word-boundary matches.
        return false;
    }
    return std::isalnum(static_cast<unsigned char>(folded_candidate[position - 1])) == 0;
}

bool IsSubsequence(const std::string& folded_candidate, const std::string& folded_query) {
    std::size_t cursor = 0;
    for (const char character : folded_query) {
        const std::size_t found = folded_candidate.find(character, cursor);
        if (found == std::string::npos) {
            return false;
        }
        cursor = found + 1;
    }
    return true;
}

PaletteMatchRank RankTabEntry(const PaletteTabEntry& tab, std::string_view query) {
    // The better of the title and URL classifications; the enum's underlying
    // order is exactly the quality order.
    return std::min(ClassifyPaletteMatch(tab.title, query), ClassifyPaletteMatch(tab.url, query));
}

PaletteMatchRank RankBookmarkEntry(const PaletteBookmarkEntry& bookmark, std::string_view query) {
    // Same better-of-title-and-URL rule as tabs; an untitled bookmark ranks by
    // its URL alone.
    return std::min(ClassifyPaletteMatch(bookmark.title, query),
                    ClassifyPaletteMatch(bookmark.url, query));
}

}  // namespace

PaletteMatchRank ClassifyPaletteMatch(std::string_view candidate, std::string_view query) {
    const std::string_view trimmed_query = TrimAsciiWhitespace(query);
    if (trimmed_query.empty()) {
        // Callers list everything in canonical order for an empty query rather
        // than routing it through the ranker.
        return PaletteMatchRank::kNone;
    }

    const std::string folded_candidate = FoldAscii(candidate);
    const std::string folded_query = FoldAscii(trimmed_query);

    if (folded_candidate == folded_query) {
        return PaletteMatchRank::kExact;
    }
    if (folded_candidate.starts_with(folded_query)) {
        return PaletteMatchRank::kPrefix;
    }
    for (std::size_t at = folded_candidate.find(folded_query); at != std::string::npos;
         at = folded_candidate.find(folded_query, at + 1)) {
        if (PrecededByWordBoundary(folded_candidate, at)) {
            return PaletteMatchRank::kWordBoundary;
        }
    }
    if (IsSubsequence(folded_candidate, folded_query)) {
        return PaletteMatchRank::kSubsequence;
    }
    return PaletteMatchRank::kNone;
}

std::vector<PaletteEntry> ComposePaletteResults(std::string_view query,
                                                const std::vector<PaletteTabEntry>& tabs,
                                                const std::vector<PaletteSpaceEntry>& spaces) {
    return ComposePaletteResults(query, tabs, spaces, {});
}

std::vector<PaletteEntry> ComposePaletteResults(
    std::string_view query, const std::vector<PaletteTabEntry>& tabs,
    const std::vector<PaletteSpaceEntry>& spaces,
    const std::vector<PaletteBookmarkEntry>& bookmarks) {
    std::vector<PaletteEntry> canonical;
    canonical.reserve(tabs.size() + spaces.size() + bookmarks.size() + 1U);
    for (const PaletteTabEntry& tab : tabs) {
        canonical.push_back({.kind = PaletteEntryKind::kTab, .tab = tab});
    }
    for (const PaletteSpaceEntry& space : spaces) {
        canonical.push_back({.kind = PaletteEntryKind::kSpace, .space = space});
    }
    for (const PaletteBookmarkEntry& bookmark : bookmarks) {
        canonical.push_back({.kind = PaletteEntryKind::kBookmark, .bookmark = bookmark});
    }

    if (TrimAsciiWhitespace(query).empty()) {
        // Canonical order: every tab, then every space, and no URL row — there
        // is nothing typed to go to.
        return canonical;
    }

    struct ScoredEntry {
        PaletteEntry entry;
        std::size_t input_index;
        PaletteMatchRank rank;
    };
    std::vector<ScoredEntry> scored;
    scored.reserve(canonical.size());
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        const PaletteEntry& entry = canonical[index];
        PaletteMatchRank rank = PaletteMatchRank::kNone;
        switch (entry.kind) {
            case PaletteEntryKind::kTab:
                rank = RankTabEntry(entry.tab, query);
                break;
            case PaletteEntryKind::kBookmark:
                rank = RankBookmarkEntry(entry.bookmark, query);
                break;
            case PaletteEntryKind::kSpace:
            case PaletteEntryKind::kUrl:
                rank = ClassifyPaletteMatch(entry.space.name, query);
                break;
        }
        if (rank != PaletteMatchRank::kNone) {
            scored.push_back({.entry = entry, .input_index = index, .rank = rank});
        }
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const ScoredEntry& lhs, const ScoredEntry& rhs) {
                         if (lhs.rank != rhs.rank) {
                             return lhs.rank < rhs.rank;
                         }
                         return lhs.input_index < rhs.input_index;
                     });

    std::vector<PaletteEntry> results;
    results.reserve(scored.size() + 1U);
    for (ScoredEntry& scored_entry : scored) {
        results.push_back(std::move(scored_entry.entry));
    }
    // The URL affordance is offered for every non-empty query and always last;
    // validity is enforced by the validator at Submit, not by hiding the row.
    results.push_back({.kind = PaletteEntryKind::kUrl, .url_query = std::string(query)});
    return results;
}

CommandPaletteModel::CommandPaletteModel(PaletteUrlValidator validator)
    : validator_(std::move(validator)) {}

void CommandPaletteModel::Open() {
    visible_ = true;
    query_.clear();
    highlighted_index_ = 0;
    last_rejection_.reset();
}

bool CommandPaletteModel::Close() {
    const bool was_visible = visible_;
    visible_ = false;
    // Cancel purity: a surfaced rejection is dropped with the draft, and no
    // selection is ever produced by closing.
    last_rejection_.reset();
    return was_visible;
}

void CommandPaletteModel::SetTabs(std::vector<PaletteTabEntry> tabs) {
    tabs_ = std::move(tabs);
    highlighted_index_ = 0;
}

void CommandPaletteModel::SetSpaces(std::vector<PaletteSpaceEntry> spaces) {
    spaces_ = std::move(spaces);
    highlighted_index_ = 0;
}

void CommandPaletteModel::SetQuery(std::string query) {
    query_ = std::move(query);
    highlighted_index_ = 0;
    last_rejection_.reset();
}

void CommandPaletteModel::MoveHighlight(int delta) {
    const auto count = static_cast<int>(Results().size());
    if (count == 0) {
        return;
    }
    const int current = static_cast<int>(highlighted_index_);
    int next = (current + delta) % count;
    if (next < 0) {
        next += count;
    }
    highlighted_index_ = static_cast<std::size_t>(next);
}

void CommandPaletteModel::SetHighlightedIndex(std::size_t index) {
    if (index < Results().size()) {
        highlighted_index_ = index;
    }
}

void CommandPaletteModel::SetBookmarks(std::vector<PaletteBookmarkEntry> bookmarks) {
    bookmarks_ = std::move(bookmarks);
    highlighted_index_ = 0;
}

std::vector<PaletteEntry> CommandPaletteModel::Results() const {
    return ComposePaletteResults(query_, tabs_, spaces_, bookmarks_);
}

std::optional<PaletteSelection> CommandPaletteModel::Submit() {
    if (!visible_) {
        // A hidden or cancelled palette produces no action, ever.
        return std::nullopt;
    }
    const std::vector<PaletteEntry> results = Results();
    if (results.empty() || highlighted_index_ >= results.size()) {
        return std::nullopt;
    }

    const PaletteEntry entry = results[highlighted_index_];
    PaletteSelection selection;
    selection.kind = entry.kind;
    if (entry.kind == PaletteEntryKind::kUrl) {
        if (!validator_) {
            // No validator bound: the model never validates URLs itself, so a
            // kUrl row cannot be submitted.
            return std::nullopt;
        }
        selection.address = validator_(entry.url_query);
        if (!selection.address.is_valid()) {
            // Surface the rejection, keep the palette open, and consume
            // nothing — the draft stays editable, like the address bar's
            // kInvalid mode.
            last_rejection_ = selection.address.error;
            return std::nullopt;
        }
    } else if (entry.kind == PaletteEntryKind::kTab) {
        selection.tab = entry.tab;
    } else if (entry.kind == PaletteEntryKind::kBookmark) {
        selection.bookmark = entry.bookmark;
    } else {
        selection.space = entry.space;
    }

    // Exactly-once: closing on acceptance means a repeat Submit() finds the
    // palette hidden and returns no action, mirroring AddressBarModel::Submit.
    visible_ = false;
    last_rejection_.reset();
    return selection;
}

}  // namespace island
