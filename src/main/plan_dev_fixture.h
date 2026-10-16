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
                                        std::string branch, std::string kind = "step") {
    agent::AcpPlanEntry entry;
    entry.content = id;
    entry.status = "pending";
    agent::IslandPlanMeta meta;
    meta.id = std::move(id);
    meta.after = std::move(after);
    meta.branch = std::move(branch);
    meta.kind = std::move(kind);
    entry.island = std::move(meta);
    return entry;
}

inline std::vector<agent::AcpPlanEntry> DevPlanFixture() {
    using agent::AcpPlanEntry;
    return {DevPlanEntry("q", {}, "", "question"), DevPlanEntry("s", {"q"}, ""),
            DevPlanEntry("a", {"s"}, "A"),         DevPlanEntry("b", {"s"}, "B"),
            DevPlanEntry("c", {"s"}, "C"),         DevPlanEntry("a1", {"a"}, "A"),
            DevPlanEntry("a2", {"a"}, "A"),        DevPlanEntry("b1", {"b"}, "B"),
            DevPlanEntry("m", {"a1", "a2", "b1"}, "", "join"),
            DevPlanEntry("r", {"m"}, "", "result")};
}

}  // namespace island

#endif  // ISLAND_MAIN_PLAN_DEV_FIXTURE_H_
