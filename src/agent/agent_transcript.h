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
    void AddUserPrompt(std::string text);
    void AddNotice(std::string text);
    void Apply(const AcpEvent& event);
    // Marks a permission prompt answered with the given label.
    void ResolvePermission(std::int64_t request_id, std::string resolution);
    // Marks every unanswered permission prompt as cancelled.
    void CancelOpenPermissions();
    void Clear();

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
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_TRANSCRIPT_H_
