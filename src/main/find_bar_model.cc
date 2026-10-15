#include "find_bar_model.h"

#include <algorithm>
#include <string>

namespace island {

FindRequest FindBarModel::StartSearch() {
    ClearResults();
    if (query_.empty()) {
        return {.kind = FindRequest::Kind::kStop, .clear_selection = true};
    }
    searched_ = true;
    return {.kind = FindRequest::Kind::kFind,
            .text = query_,
            .forward = true,
            .match_case = match_case_,
            .find_next = false};
}

FindRequest FindBarModel::Open() {
    if (open_) {
        return {};
    }
    open_ = true;
    return query_.empty() ? FindRequest{} : StartSearch();
}

FindRequest FindBarModel::Close() {
    if (!open_) {
        return {};
    }
    open_ = false;
    const bool had_search = searched_;
    ClearResults();
    return had_search ? FindRequest{.kind = FindRequest::Kind::kStop, .clear_selection = false}
                      : FindRequest{};
}

FindRequest FindBarModel::SetQuery(std::string_view text) {
    if (!open_ || text == query_) {
        return {};
    }
    query_ = std::string(text);
    return StartSearch();
}

FindRequest FindBarModel::SetMatchCase(bool match_case) {
    if (match_case == match_case_) {
        return {};
    }
    match_case_ = match_case;
    return open_ && !query_.empty() ? StartSearch() : FindRequest{};
}

FindRequest FindBarModel::Step(bool forward) {
    if (!open_ || query_.empty()) {
        return {};
    }
    searched_ = true;
    return {.kind = FindRequest::Kind::kFind,
            .text = query_,
            .forward = forward,
            .match_case = match_case_,
            .find_next = true};
}

void FindBarModel::ClearResults() noexcept {
    searched_ = false;
    reported_ = false;
    total_ = 0;
    ordinal_ = 0;
}

void FindBarModel::OnResult(int count, int active_match_ordinal) noexcept {
    // A late reply after Close(), or for an emptied field, describes nothing on screen.
    if (!open_ || query_.empty()) {
        return;
    }
    searched_ = true;
    reported_ = true;
    if (count >= 0) {
        total_ = count;
    }
    if (active_match_ordinal > 0) {
        ordinal_ = active_match_ordinal;
    }
    if (total_ == 0) {
        ordinal_ = 0;
    }
}

std::string FindBarModel::CountLabel() const {
    if (query_.empty() || !reported_) {
        return {};
    }
    // CEF activates the first match of a new search without always reporting its ordinal.
    const int shown = total_ > 0 ? std::max(ordinal_, 1) : 0;
    return std::to_string(shown) + " of " + std::to_string(total_);
}

}  // namespace island
