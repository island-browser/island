#ifndef ISLAND_FIND_BAR_MODEL_H_
#define ISLAND_FIND_BAR_MODEL_H_

// CEF-free state of the find-in-page bar: the query, the match-case toggle, the
// "n of m" counts reported by CefFindHandler::OnFindResult, and the decision of
// which CefBrowserHost call each user gesture needs. BrowserWindow executes the
// returned FindRequest; tests drive the model without a CEF runtime.

#include <string>
#include <string_view>

namespace island {

// What the window must ask CEF to do after a model event.
struct FindRequest {
    enum class Kind { kNone, kFind, kStop };
    Kind kind = Kind::kNone;
    // kFind: CefBrowserHost::Find(text, forward, match_case, find_next).
    std::string text;
    bool forward = true;
    bool match_case = false;
    bool find_next = false;
    // kStop: CefBrowserHost::StopFinding(clear_selection).
    bool clear_selection = false;

    [[nodiscard]] bool operator==(const FindRequest&) const = default;
};

class FindBarModel {
  public:
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] bool match_case() const noexcept { return match_case_; }
    // Total matches (m) and the 1-based active match (n); both 0 until a result arrives.
    [[nodiscard]] int total() const noexcept { return total_; }
    [[nodiscard]] int ordinal() const noexcept { return ordinal_; }
    // True once a Find for the current query was issued.
    [[nodiscard]] bool searched() const noexcept { return searched_; }
    // True once CEF reported a result for the current search, so "0 of 0" is meaningful.
    [[nodiscard]] bool reported() const noexcept { return reported_; }

    // Shows the bar. Reopening with a remembered query re-runs it so the page highlights are
    // back; opening again while open only asks the view to select the field text.
    [[nodiscard]] FindRequest Open();
    // Hides the bar. The active match stays selected on the page (like Chrome), highlights go.
    [[nodiscard]] FindRequest Close();
    // The field text changed: a new search from the top, or a stop when the text is empty.
    [[nodiscard]] FindRequest SetQuery(std::string_view text);
    [[nodiscard]] FindRequest SetMatchCase(bool match_case);
    // Enter / Shift+Enter / Cmd+G / Cmd+Shift+G.
    [[nodiscard]] FindRequest Step(bool forward);
    // The page navigated or the tab changed: counts no longer describe the document.
    void ClearResults() noexcept;
    // CefFindHandler::OnFindResult. Negative count / non-positive ordinal mean "unchanged".
    void OnResult(int count, int active_match_ordinal) noexcept;

    // "3 of 12", "0 of 0", or "" until a result arrives (typing never flashes "0 of 0").
    [[nodiscard]] std::string CountLabel() const;

  private:
    [[nodiscard]] FindRequest StartSearch();

    bool open_ = false;
    std::string query_;
    bool match_case_ = false;
    bool searched_ = false;
    bool reported_ = false;
    int total_ = 0;
    int ordinal_ = 0;
};

}  // namespace island

#endif  // ISLAND_FIND_BAR_MODEL_H_
