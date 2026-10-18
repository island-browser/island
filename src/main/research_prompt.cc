#include "research_prompt.h"

#include <cctype>

#include "download_path.h"
#include "plan_graph.h"

namespace island {
namespace {

constexpr std::size_t kSpaceNameBytes = 40;
constexpr std::size_t kTitleBytes = 80;

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsSpace(text[begin])) ++begin;
    while (end > begin && IsSpace(text[end - 1])) --end;
    return std::string(text.substr(begin, end - begin));
}

// Runs of whitespace (including newlines) become one space.
std::string CollapseWhitespace(std::string_view text) {
    std::string out;
    bool pending_space = false;
    for (const char c : text) {
        if (IsSpace(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) out.push_back(' ');
        pending_space = false;
        out.push_back(c);
    }
    return out;
}

// Clips to at most `limit` bytes without splitting a UTF-8 sequence; `clipped`
// reports whether anything was cut.
std::string ClipUtf8(std::string text, std::size_t limit, bool* clipped = nullptr) {
    if (clipped != nullptr) *clipped = text.size() > limit;
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
    text.resize(end);
    return text;
}

// One outer ```fence around the whole body is dropped.
std::string StripOuterFence(std::string body) {
    if (body.rfind("```", 0) != 0 || body.size() < 7) return body;
    const std::size_t first_newline = body.find('\n');
    if (first_newline == std::string::npos) return body;
    std::string inner = Trim(std::string_view(body).substr(first_newline + 1));
    if (inner.size() < 3 || inner.compare(inner.size() - 3, 3, "```") != 0) return body;
    inner.resize(inner.size() - 3);
    // Only when the fence encloses everything (no other fence inside).
    if (inner.find("```") != std::string::npos) return body;
    return Trim(inner);
}

// Whether the turn that ended last (the items after the last user prompt) carried a plan.
bool TurnHasPlan(const std::vector<agent::TranscriptItem>& items) {
    using Kind = agent::TranscriptItem::Kind;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->kind == Kind::kUser) break;
        if (it->kind == Kind::kPlan) return true;
    }
    return false;
}

std::optional<std::string> ExtractReport(const std::vector<agent::TranscriptItem>& items,
                                         bool allow_unmarked) {
    using Kind = agent::TranscriptItem::Kind;
    std::optional<std::string> fallback;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->kind == Kind::kUser) break;
        if (it->kind != Kind::kAgent) continue;
        const std::string& text = it->text;
        const std::size_t begin = text.rfind(kReportBeginMarker);
        if (begin != std::string::npos) {
            const std::size_t body_begin = begin + kReportBeginMarker.size();
            const std::size_t end = text.find(kReportEndMarker, body_begin);
            const std::string_view body = std::string_view(text).substr(
                body_begin, end == std::string::npos ? std::string::npos : end - body_begin);
            std::string report = StripOuterFence(Trim(body));
            if (!report.empty()) return report;
        }
        if (allow_unmarked && !fallback.has_value()) {
            std::string trimmed = Trim(text);
            if (!trimmed.empty()) fallback = std::move(trimmed);
        }
    }
    return allow_unmarked ? fallback : std::nullopt;
}

}  // namespace

std::string ResearchSpaceName(std::string_view question) {
    bool clipped = false;
    std::string name = ClipUtf8(CollapseWhitespace(question), kSpaceNameBytes, &clipped);
    name = Trim(name);
    if (name.empty()) return "Research";
    if (clipped) name += "...";
    return name;
}

std::string BuildResearchPrompt(std::string_view question) {
    const std::string asked = ClipUtf8(Trim(question), kMaxResearchQuestionBytes);
    const std::string space = ResearchSpaceName(question);
    std::string prompt;
    prompt += "You are doing a research task for the user inside the Island browser. ";
    prompt += "Use the island-browser tools (browser_*, page_read, page_snapshot, page_click, ";
    prompt += "page_type) to do it in the real browser.\n\nThe question:\n\"\"\"\n";
    prompt += asked;
    prompt += "\n\"\"\"\n\nPlan first. Before you open any page, send the research plan as a plan ";
    prompt +=
        "update with one entry per step. Put Island's branching in each entry's _meta.island, ";
    prompt += "for example:\n";
    prompt += R"({"content": "Known problems in open issues", "status": "pending",)";
    prompt += R"( "priority": "medium", "_meta": {"island": {"id": "a2", "after": ["a"],)";
    prompt += R"( "branch": "A", "kind": "step", "tool": "browser_open_tab",)";
    prompt += R"( "site": "github.com", "needs_you": "sign-in"}}})"
              "\n";
    prompt += "Each id is unique. after lists the ids a step waits for, and several ids make a ";
    prompt +=
        "join. kind is question, step, join or result. branch names a branch (A, B, ...) and ";
    prompt +=
        "is left out for trunk steps. Branch only where the work really splits, with at most " +
        std::to_string(agent::kMaxBranchDepth) + " levels of branching; a step that needs a ";
    prompt +=
        "deeper split, or needs the person to sign in or approve, gets needs_you set and asks ";
    prompt += "first. After you send the plan, wait for approval before running it. The person ";
    prompt += "approves in Island; waiting for that is not asking a question.\n\nHow to work:\n";
    prompt += "1. Island already opened a space named \"" + space + "\" for this question and ";
    prompt += "switched to it, so everything for this question stays together. Work in it: do ";
    prompt += "not create, rename or switch spaces, and do not touch the user's other spaces.\n";
    prompt += "2. Search the web for the question, then open at least four relevant and diverse ";
    prompt += "sources (prefer primary sources, official documentation and reputable outlets) ";
    prompt += "with browser_open_tab, one tab each. Keep the tabs open: the user will review ";
    prompt += "them. Do not open pages unrelated to the question.\n";
    prompt += "3. Read every source with page_read (use page_snapshot and page_click when the ";
    prompt += "content is behind a control) and note the concrete claims it makes.\n";
    prompt +=
        "If a page tool reports that a page has no live page, the person is probably viewing ";
    prompt += "another space: do not activate that page; leave the source unread, say so in the ";
    prompt += "report, and do not cite it.\n";
    prompt += "4. Compare the sources: where they agree, where they disagree, which is newer or ";
    prompt += "more authoritative. Do not rest an important claim on a single source.\n";
    prompt += "5. Do not ask the user questions. If the question is ambiguous, pick the most ";
    prompt += "likely reading and state that assumption in the report.\n\n";
    prompt += "Finish with ONE final message that contains only the report, between these two ";
    prompt += "marker lines (each on its own line, nothing else on it):\n";
    prompt += std::string(kReportBeginMarker) + "\n";
    prompt += "# <a short title for the report>\n";
    prompt += "<the report in Markdown>\n";
    prompt += std::string(kReportEndMarker) + "\n\n";
    prompt += "Report rules:\n";
    prompt += "- Open with a short summary that answers the question, then the findings in ";
    prompt += "sections, then a \"Sources\" list.\n";
    prompt += "- Cite every claim with a Markdown link to the page it came from, written ";
    prompt += "[name](https://...). Link only pages you actually opened and read. A claim ";
    prompt += "without a link does not belong in the report; say plainly what you could not ";
    prompt += "verify.\n";
    prompt += "- Note disagreements between sources instead of smoothing them over.\n";
    prompt += "- Write in the language of the question.\n";
    prompt += "- Put nothing outside the markers in the final message.\n";
    return prompt;
}

std::optional<std::string> ExtractResearchReport(
    const std::vector<agent::TranscriptItem>& items) {
    return ExtractReport(items, /*allow_unmarked=*/true);
}

ResearchOutcome ClassifyResearchTurn(const agent::AgentTranscript& transcript,
                                     std::uint64_t turns_ended_before,
                                     std::string_view session_error, bool plan_approved) {
    ResearchOutcome outcome;
    if (transcript.turns_ended() <= turns_ended_before) {
        outcome.message = session_error.empty()
                             ? "The agent stopped before it finished the research."
                             : std::string(session_error);
        return outcome;
    }
    const std::string& reason = transcript.last_turn_end();
    if (reason == "cancelled") {
        outcome.kind = ResearchOutcome::Kind::kCancelled;
        outcome.message = "Research cancelled.";
        return outcome;
    }
    if (reason != "end_turn") {
        outcome.message = reason == "error" && !session_error.empty()
                              ? std::string(session_error)
                              : "The agent stopped early (" + reason + ").";
        return outcome;
    }
    // A plan that is not approved yet means the agent is waiting: only a marked report can
    // end the research then. Once approved, the run's text is the report even without markers.
    const bool awaiting_candidate = TurnHasPlan(transcript.items()) && !plan_approved;
    std::optional<std::string> report =
        ExtractReport(transcript.items(), /*allow_unmarked=*/!awaiting_candidate);
    if (!report.has_value()) {
        if (awaiting_candidate) {
            outcome.kind = ResearchOutcome::Kind::kAwaitingApproval;
            outcome.message = "Waiting for approval";
            return outcome;
        }
        outcome.message = "The agent finished without writing a report.";
        return outcome;
    }
    outcome.kind = ResearchOutcome::Kind::kReport;
    outcome.markdown = std::move(*report);
    return outcome;
}

std::string ReportFileName(std::string_view title) {
    std::string name = SanitizeDownloadFileName(CollapseWhitespace(title));
    if (name == "download") name = "report";
    return name + ".md";
}

std::string ReportTitle(std::string_view markdown, std::string_view question) {
    std::size_t pos = 0;
    bool in_fence = false;
    while (pos <= markdown.size()) {
        std::size_t eol = markdown.find('\n', pos);
        if (eol == std::string_view::npos) eol = markdown.size();
        const std::string line = Trim(markdown.substr(pos, eol - pos));
        if (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0) {
            in_fence = !in_fence;
        } else if (!in_fence && line.size() > 2 && line[0] == '#' && line[1] == ' ') {
            std::string title = Trim(std::string_view(line).substr(2));
            while (!title.empty() && title.back() == '#') title.pop_back();
            title = CollapseWhitespace(Trim(title));
            if (!title.empty()) return ClipUtf8(std::move(title), kTitleBytes * 2);
        }
        pos = eol + 1;
    }
    const std::string fallback = CollapseWhitespace(question);
    return fallback.empty() ? "Research report" : ClipUtf8(fallback, kTitleBytes);
}

}  // namespace island
