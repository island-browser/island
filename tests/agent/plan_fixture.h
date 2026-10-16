#ifndef ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_
#define ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_

// Shared plan fixtures for the agent tests: the ten-entry example plan from the
// native-look spec (section 5), a plain ACP plan without `_meta.island`, and the E(...)
// helper that builds one entry. Step titles are authored for these fixtures: section 5
// only names "a2" ("Known problems in open issues"), the rest are chosen to match it.

#include <string>
#include <utility>
#include <vector>

#include "acp_client.h"

namespace island::agent {

// An entry that carries `_meta.island`. An empty `content` uses the id as the title.
inline AcpPlanEntry E(std::string id, std::vector<std::string> after, std::string branch,
                      std::string kind = "step", std::string content = {}) {
    AcpPlanEntry entry;
    entry.content = content.empty() ? id : std::move(content);
    entry.status = "pending";
    IslandPlanMeta meta;
    meta.id = std::move(id);
    meta.after = std::move(after);
    meta.branch = std::move(branch);
    meta.kind = std::move(kind);
    entry.island = std::move(meta);
    return entry;
}

// Question -> step -> three branches (A with two sub-steps, B with one, C), a join over
// a1/a2/b1, then the result.
inline std::vector<AcpPlanEntry> SpecPlan() {
    return {E("q", {}, "", "question", "Which laptop fits my budget and work?"),
            E("s", {"q"}, "", "step", "Shortlist current laptops for development"),
            E("a", {"s"}, "A", "step", "Read professional reviews"),
            E("b", {"s"}, "B", "step", "Compare prices at retailers"),
            E("c", {"s"}, "C", "step", "Read owner forums"),
            E("a1", {"a"}, "A", "step", "Collect battery and keyboard tests"),
            E("a2", {"a"}, "A", "step", "Known problems in open issues"),
            E("b1", {"b"}, "B", "step", "Check stock and return policies"),
            E("m", {"a1", "a2", "b1"}, "", "join", "Weigh reviews, prices and owner reports"),
            E("r", {"m"}, "", "result", "Write the recommendation")};
}

// A plain ACP plan: no entry carries `_meta.island`, so the ids are the generated "p<index>".
inline std::vector<AcpPlanEntry> PlainPlan() {
    std::vector<AcpPlanEntry> plan;
    for (const char* content : {"Find two sources", "Read them", "Summarise the answer"}) {
        AcpPlanEntry entry;
        entry.content = content;
        entry.status = "pending";
        plan.push_back(std::move(entry));
    }
    return plan;
}

}  // namespace island::agent

#endif  // ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_
