#include "plan_edits.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "plan_fixture.h"
#include "plan_graph.h"

namespace island::agent {
namespace {

TEST(PlanEditsTest, RunMessageListsSkipsAndInstructions) {
    PlanEdits edits{.skipped = {"c"}, .instructions = {{"a2", "Only bug issues"}}};
    const std::string m = BuildPlanRunMessage(SpecPlan(), edits, std::nullopt);
    EXPECT_NE(m.find("Skip: c"), std::string::npos);
    EXPECT_NE(m.find("a2: Only bug issues"), std::string::npos);
    EXPECT_NE(m.find("Run the whole plan"), std::string::npos);
}

TEST(PlanEditsTest, RunBranchNamesTheBranch) {
    EXPECT_NE(BuildPlanRunMessage(SpecPlan(), {}, "A").find("Run only branch A"),
              std::string::npos);
}

TEST(PlanEditsTest, SkipsSurviveAPlanUpdateForIdsThatStillExist) {
    PlanEdits edits{.skipped = {"c", "gone"}};
    const PlanEdits next = CarryEditsOver(edits, SpecPlan());
    EXPECT_EQ(next.skipped, (std::set<std::string>{"c"}));
}

TEST(PlanEditsTest, ExpandAndBranchMessages) {
    EXPECT_EQ(BuildExpandMessage("a2"), "Expand step a2");
    EXPECT_EQ(BuildBranchFromMessage("a"), "Add a branch after step a");
}

TEST(PlanEditsTest, UnknownMetaKeysPassThroughUnchanged) {
    // The agent's own _meta keys, inside the island object and beside it, go back as sent.
    AcpPlanEntry entry = E("a2", {"a"}, "A");
    entry.raw_meta =
        R"({"island":{"id":"a2","after":["a"],"branch":"A","kind":"step","x-inner":2},"x-other":1})";
    const std::string m = BuildPlanRunMessage({entry}, {}, std::nullopt);
    EXPECT_NE(m.find(R"("x-other":1)"), std::string::npos) << m;
    EXPECT_NE(m.find(R"("x-inner":2)"), std::string::npos) << m;
}

TEST(PlanEditsTest, PlainPlanEditsUseTheGeneratedIds) {
    const std::vector<AcpPlanEntry> plan = PlainPlan();
    EXPECT_EQ(PlanEntryIds(plan), (std::vector<std::string>{"p0", "p1", "p2"}));
    PlanEdits edits{.skipped = {"p1"}, .instructions = {{"p0", "Use the forum"}}};
    const std::string m = BuildPlanRunMessage(plan, edits, std::nullopt);
    EXPECT_NE(m.find("Skip: p1"), std::string::npos) << m;
    EXPECT_NE(m.find("Instruction for p0: Use the forum"), std::string::npos) << m;
    // The skip survives a plan update that still has the same entries.
    EXPECT_EQ(CarryEditsOver(edits, plan).skipped, (std::set<std::string>{"p1"}));
}

}  // namespace
}  // namespace island::agent
