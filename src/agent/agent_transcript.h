#ifndef ISLAND_AGENT_AGENT_TRANSCRIPT_H_
#define ISLAND_AGENT_AGENT_TRANSCRIPT_H_

// Folds the ACP event stream into the conversation the agent panel shows:
// user turns, streamed agent text, thoughts, tool calls with live status,
// the current plan, permission prompts, and errors. Pure data; the panel
// renders ToJson().

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "acp_client.h"

namespace island::agent {

struct TranscriptItem {
    enum class Kind : std::uint8_t {
        kUser,
        kAgent,
        kThought,
        kTool,
        kPlan,
        kPermission,
        kError,
        kNotice,
    };
    Kind kind = Kind::kNotice;
    std::string text;
    // Context that accompanied this user turn. It is display-only metadata; the full context
    // remains in the ACP prompt and is never repeated in the bubble.
    std::string context_title;
    std::string context_url;
    std::string context_selection;
    std::string tool_call_id;
    std::string tool_kind;
    std::string status;
    std::vector<AcpPlanEntry> plan;
    std::int64_t request_id = 0;
    std::vector<AcpPermissionOption> options;
    // Permission prompts: the chosen option's name once answered.
    std::string resolution;
};

[[nodiscard]] std::string_view TranscriptKindName(TranscriptItem::Kind kind);

class AgentTranscript {
  public:
    void AddUserPrompt(std::string text, std::string context = {});
    void AddNotice(std::string text);
    void Apply(const AcpEvent& event);
    // Marks a permission prompt answered with the given label.
    void ResolvePermission(std::int64_t request_id, std::string resolution);
    // Marks every unanswered permission prompt as cancelled.
    void CancelOpenPermissions();
    void Clear();
    // Records that a turn is over and why (an ACP stop reason such as
    // "end_turn" or "cancelled", or "error"). Apply() calls this for turn-ended
    // and error events; the session calls it when it drops a queued prompt.
    void NoteTurnEnded(std::string reason);

    // Turns that have ended since the transcript was created. Unlike the items
    // this never resets, so a caller can snapshot it before sending a prompt and
    // tell later whether that prompt's turn finished.
    [[nodiscard]] std::uint64_t turns_ended() const noexcept { return turns_ended_; }
    [[nodiscard]] const std::string& last_turn_end() const noexcept { return last_turn_end_; }
    [[nodiscard]] const std::vector<TranscriptItem>& items() const noexcept { return items_; }
    // Bumped on every change so the view can skip redundant re-renders.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::string ToJson() const;

    // Oldest items are dropped past this many to bound memory.
    static constexpr std::size_t kMaxItems = 500;

  private:
    TranscriptItem* FindTool(std::string_view tool_call_id);
    void Push(TranscriptItem item);

    std::vector<TranscriptItem> items_;
    std::uint64_t revision_ = 0;
    std::uint64_t turns_ended_ = 0;
    std::string last_turn_end_;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_TRANSCRIPT_H_
