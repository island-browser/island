#include "plan_edits.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "plan_fixture.h"

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

}  // namespace
}  // namespace island::agent
