#ifndef ISLAND_AGENT_PLAN_GRAPH_H_
#define ISLAND_AGENT_PLAN_GRAPH_H_

// The research plan as a graph: validated, topologically ordered and laid out on a grid.
// Built from ACP plan entries (acp_client.h); CEF-free. plan.html draws the PlanGraphJson
// payload.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "acp_client.h"

namespace island::agent {

enum class PlanNodeKind : std::uint8_t { kQuestion, kStep, kNeedsYou, kJoin, kResult };
enum class PlanNodeState : std::uint8_t { kQueued, kRunning, kDone, kFailed, kSkipped };

struct PlanNode {
    std::string id, title, branch, tool, site, needs_you;
    PlanNodeKind kind = PlanNodeKind::kStep;
    PlanNodeState state = PlanNodeState::kQueued;
    std::vector<std::string> after;  // only edges that resolve
    int depth = 0;                   // branch points this node is nested in, 0 = trunk
    int column = 0, row = 0;         // layout grid
};

struct PlanGraph {
    std::vector<PlanNode> nodes;       // topological order
    bool branching = false;            // false -> open in List view
    std::vector<std::string> dropped;  // diagnostics: "a->b: cycle", "x: unknown after"
};

// Branch nesting allowed; a node nested deeper becomes a Needs-you node asking first.
inline constexpr int kMaxBranchDepth = 3;

// The id each entry has in the graph, in plan order: `_meta.island.id` when the entry carries
// one, else the generated "p<index>" that plain ACP entries get. Edits key on these ids.
[[nodiscard]] std::vector<std::string> PlanEntryIds(const std::vector<AcpPlanEntry>& entries);

[[nodiscard]] PlanGraph BuildPlanGraph(const std::vector<AcpPlanEntry>& entries,
                                       const std::set<std::string>& skipped_ids);

// Payload for plan.html.
[[nodiscard]] std::string PlanGraphJson(const PlanGraph& graph);

}  // namespace island::agent

#endif  // ISLAND_AGENT_PLAN_GRAPH_H_
