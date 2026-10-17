#ifndef ISLAND_AGENT_PLAN_EDITS_H_
#define ISLAND_AGENT_PLAN_EDITS_H_

// The person's edits to a research plan (skips, edited instructions) and the
// messages that carry them back to the agent. CEF-free.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "acp_client.h"

namespace island::agent {

struct PlanEdits {
    std::set<std::string> skipped;
    std::map<std::string, std::string> instructions;  // id -> edited instruction
};

// One user message to the agent describing the person's edits and the run choice.
[[nodiscard]] std::string BuildPlanRunMessage(const std::vector<AcpPlanEntry>& plan,
                                              const PlanEdits& edits,
                                              std::optional<std::string> only_branch);
[[nodiscard]] std::string BuildExpandMessage(std::string_view node_id);
[[nodiscard]] std::string BuildBranchFromMessage(std::string_view node_id);
[[nodiscard]] std::string BuildRetryMessage(std::string_view node_id);
// Keeps the person's skips across a fresh plan update: drops ids no longer present.
[[nodiscard]] PlanEdits CarryEditsOver(const PlanEdits& edits,
                                       const std::vector<AcpPlanEntry>& next_plan);

}  // namespace island::agent

#endif  // ISLAND_AGENT_PLAN_EDITS_H_
