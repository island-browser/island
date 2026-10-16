#include "plan_graph.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "plan_fixture.h"

namespace island::agent {
namespace {

const PlanNode& Find(const PlanGraph& g, const std::string& id) {
    for (const PlanNode& node : g.nodes) {
        if (node.id == id) return node;
    }
    ADD_FAILURE() << "no node " << id;
    static const PlanNode kNone;
    return kNone;
}

TEST(PlanGraphTest, FlatPlanIsAChainAndNotBranching) {
    const PlanGraph g = BuildPlanGraph({{"a", "pending"}, {"b", "pending"}}, {});
    ASSERT_EQ(g.nodes.size(), 2U);
    EXPECT_FALSE(g.branching);
    EXPECT_EQ(g.nodes[1].after, std::vector<std::string>{g.nodes[0].id});
}

TEST(PlanGraphTest, SpecExampleLaysOutThreeBranchesOneJoin) {
    const PlanGraph g = BuildPlanGraph(SpecPlan(), {"c"});
    EXPECT_TRUE(g.branching);
    EXPECT_EQ(Find(g, "m").kind, PlanNodeKind::kJoin);
    EXPECT_EQ(Find(g, "c").state, PlanNodeState::kSkipped);
    EXPECT_EQ(Find(g, "a1").column, 3);
    EXPECT_EQ(Find(g, "m").column, 4);
    EXPECT_EQ(Find(g, "r").column, 5);
    EXPECT_GT(Find(g, "b").row, Find(g, "a").row);
    EXPECT_EQ(Find(g, "s").row, 1);
    EXPECT_EQ(Find(g, "m").row, 1);
}

TEST(PlanGraphTest, CyclesAreBrokenAndReported) {
    const PlanGraph g = BuildPlanGraph({E("a", {"b"}, ""), E("b", {"a"}, "")}, {});
    EXPECT_EQ(g.nodes.size(), 2U);
    EXPECT_FALSE(g.dropped.empty());
}

TEST(PlanGraphTest, UnknownAfterIsDropped) {
    const PlanGraph g = BuildPlanGraph({E("a", {"nope"}, "")}, {});
    ASSERT_EQ(g.nodes.size(), 1U);
    EXPECT_TRUE(g.nodes[0].after.empty());
    EXPECT_EQ(g.dropped.size(), 1U);
}

TEST(PlanGraphTest, DuplicateIdsKeepTheFirst) {
    const PlanGraph g = BuildPlanGraph({E("a", {}, ""), E("a", {}, "")}, {});
    EXPECT_EQ(g.nodes.size(), 1U);
}

TEST(PlanGraphTest, DepthBeyondThreeBecomesNeedsYou) {
    const PlanGraph g = BuildPlanGraph(
        {E("s", {}, ""), E("a", {"s"}, "A"), E("x", {"s"}, "B"), E("a1", {"a"}, "A.1"),
         E("a2", {"a"}, "A.2"), E("a11", {"a1"}, "A.1.1"), E("a12", {"a1"}, "A.1.2"),
         E("a111", {"a11"}, "A.1.1.1"), E("a112", {"a11"}, "A.1.1.2")},
        {});
    EXPECT_EQ(Find(g, "a11").kind, PlanNodeKind::kStep);
    EXPECT_EQ(Find(g, "a111").kind, PlanNodeKind::kNeedsYou);
    EXPECT_EQ(Find(g, "a111").needs_you, "depth");
}

TEST(PlanGraphTest, FiveHundredEntriesStayFast) {
    std::vector<AcpPlanEntry> many;
    for (int i = 0; i < 500; ++i) {
        many.push_back(E(
            "n" + std::to_string(i),
            i ? std::vector<std::string>{"n" + std::to_string(i - 1)} : std::vector<std::string>{},
            ""));
    }
    const auto start = std::chrono::steady_clock::now();
    const PlanGraph g = BuildPlanGraph(many, {});
    EXPECT_EQ(g.nodes.size(), 500U);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
}

TEST(PlanGraphTest, StatusMapsToState) {
    AcpPlanEntry run = E("a", {}, "");
    run.status = "in_progress";
    AcpPlanEntry done = E("b", {"a"}, "");
    done.status = "completed";
    AcpPlanEntry bad = E("c", {"b"}, "");
    bad.status = "failed";
    const PlanGraph g = BuildPlanGraph({run, done, bad}, {});
    EXPECT_EQ(Find(g, "a").state, PlanNodeState::kRunning);
    EXPECT_EQ(Find(g, "b").state, PlanNodeState::kDone);
    EXPECT_EQ(Find(g, "c").state, PlanNodeState::kFailed);
}

TEST(PlanGraphTest, MetaNeedsYouMakesANeedsYouNode) {
    AcpPlanEntry e = E("a", {}, "");
    e.island->needs_you = "sign-in";
    const PlanGraph g = BuildPlanGraph({e}, {});
    ASSERT_EQ(g.nodes.size(), 1U);
    EXPECT_EQ(g.nodes[0].kind, PlanNodeKind::kNeedsYou);
    EXPECT_EQ(g.nodes[0].needs_you, "sign-in");
}

TEST(PlanGraphTest, JsonCarriesTheLayout) {
    const PlanGraph g = BuildPlanGraph(SpecPlan(), {"c"});
    const auto parsed = json::Parse(PlanGraphJson(g));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->FindMember("branching")->bool_val);
    const json::Value* nodes = parsed->FindMember("nodes");
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->array_val.size(), 10U);
    bool found = false;
    for (const json::Value& n : nodes->array_val) {
        if (n.StringOr("id", "") != "c") continue;
        found = true;
        EXPECT_EQ(n.StringOr("state", ""), "skipped");
        EXPECT_EQ(n.StringOr("kind", ""), "step");
        EXPECT_EQ(n.IntOr("column", -1), 2);
    }
    EXPECT_TRUE(found);
}

}  // namespace
}  // namespace island::agent
