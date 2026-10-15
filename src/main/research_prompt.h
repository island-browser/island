#ifndef ISLAND_RESEARCH_PROMPT_H_
#define ISLAND_RESEARCH_PROMPT_H_

// The CEF-free half of the start page's "Research" mode: the prompt that turns
// the configured ACP agent into a researcher, the name of the space it works
// in, and the extraction of the final Markdown report from the agent's turn.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent_transcript.h"

namespace island {

// The final report is wrapped between these marker lines so it can be told
// apart from the agent's running commentary.
inline constexpr std::string_view kReportBeginMarker = "<<<ISLAND_REPORT_BEGIN>>>";
inline constexpr std::string_view kReportEndMarker = "<<<ISLAND_REPORT_END>>>";

// Questions longer than this are clipped (on a UTF-8 boundary) in the prompt.
inline constexpr std::size_t kMaxResearchQuestionBytes = 2000;

// The research space's name: the question with whitespace collapsed, clipped to
// about 40 bytes on a UTF-8 boundary ("..." marks a clip). Never empty.
[[nodiscard]] std::string ResearchSpaceName(std::string_view question);

// The full instruction sent to the agent for `question`.
[[nodiscard]] std::string BuildResearchPrompt(std::string_view question);

// The Markdown report of the turn that just ended: the text between the
// markers of the newest agent message that has them, else the newest agent
// message of the turn. Only items after the last user prompt are considered.
// std::nullopt when the turn produced no agent text.
[[nodiscard]] std::optional<std::string> ExtractResearchReport(
    const std::vector<agent::TranscriptItem>& items);

// How a research turn ended, from the transcript of the session that ran it.
struct ResearchOutcome {
    enum class Kind : std::uint8_t { kReport, kCancelled, kFailed };
    Kind kind = Kind::kFailed;
    std::string markdown;  // kReport
    std::string message;   // kCancelled / kFailed: what to tell the user
};

// Classifies the turn that started when `turns_ended_before` turns had ended.
// A turn that never ended (the session was reset, the agent died while the
// prompt was queued) is a failure carrying `session_error` when there is one.
[[nodiscard]] ResearchOutcome ClassifyResearchTurn(const agent::AgentTranscript& transcript,
                                                   std::uint64_t turns_ended_before,
                                                   std::string_view session_error);

// A safe file name for saving a report: the title, sanitized, ending in ".md".
[[nodiscard]] std::string ReportFileName(std::string_view title);

// The first level-1 heading of `markdown`, else the question (clipped).
[[nodiscard]] std::string ReportTitle(std::string_view markdown, std::string_view question);

}  // namespace island

#endif  // ISLAND_RESEARCH_PROMPT_H_
