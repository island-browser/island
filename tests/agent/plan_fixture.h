#ifndef ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_
#define ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_

// Shared plan fixtures for the agent tests: the ten-entry example plan from the
// native-look spec (section 5), plus the E(...) helper that builds one entry.

#include <string>
#include <utility>
#include <vector>

#include "acp_client.h"

namespace island::agent {

// An entry that carries `_meta.island`.
inline AcpPlanEntry E(std::string id, std::vector<std::string> after, std::string branch,
                      std::string kind = "step") {
    AcpPlanEntry entry;
    entry.content = id;
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
    return {E("q", {}, "", "question"), E("s", {"q"}, ""),   E("a", {"s"}, "A"),
            E("b", {"s"}, "B"),         E("c", {"s"}, "C"),  E("a1", {"a"}, "A"),
            E("a2", {"a"}, "A"),        E("b1", {"b"}, "B"), E("m", {"a1", "a2", "b1"}, "", "join"),
            E("r", {"m"}, "", "result")};
}

}  // namespace island::agent

#endif  // ISLAND_TESTS_AGENT_PLAN_FIXTURE_H_
