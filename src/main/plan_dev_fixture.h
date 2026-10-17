#ifndef ISLAND_MAIN_PLAN_DEV_FIXTURE_H_
#define ISLAND_MAIN_PLAN_DEV_FIXTURE_H_

// The developer capture's sample research plan (`--island-dev-actions=plan_fixture`). It is
// the ten-entry plan of the native-look spec, section 5, and a unit test keeps it equal to
// the test fixture SpecPlan() in tests/agent/plan_fixture.h.

#include <string>
#include <utility>
#include <vector>

#include "acp_client.h"

namespace island {

inline agent::AcpPlanEntry DevPlanEntry(std::string id, std::vector<std::string> after,
                                        std::string branch, std::string kind, std::string content) {
    agent::AcpPlanEntry entry;
    entry.content = std::move(content);
    entry.status = "pending";
    agent::IslandPlanMeta meta;
    meta.id = std::move(id);
    meta.after = std::move(after);
    meta.branch = std::move(branch);
    meta.kind = std::move(kind);
    entry.island = std::move(meta);
    return entry;
}

// The same titles as SpecPlan() in tests/agent/plan_fixture.h.
inline std::vector<agent::AcpPlanEntry> DevPlanFixture() {
    return {DevPlanEntry("q", {}, "", "question", "Which laptop fits my budget and work?"),
            DevPlanEntry("s", {"q"}, "", "step", "Shortlist current laptops for development"),
            DevPlanEntry("a", {"s"}, "A", "step", "Read professional reviews"),
            DevPlanEntry("b", {"s"}, "B", "step", "Compare prices at retailers"),
            DevPlanEntry("c", {"s"}, "C", "step", "Read owner forums"),
            DevPlanEntry("a1", {"a"}, "A", "step", "Collect battery and keyboard tests"),
            DevPlanEntry("a2", {"a"}, "A", "step", "Known problems in open issues"),
            DevPlanEntry("b1", {"b"}, "B", "step", "Check stock and return policies"),
            DevPlanEntry("m", {"a1", "a2", "b1"}, "", "join",
                         "Weigh reviews, prices and owner reports"),
            DevPlanEntry("r", {"m"}, "", "result", "Write the recommendation")};
}

}  // namespace island

#endif  // ISLAND_MAIN_PLAN_DEV_FIXTURE_H_
