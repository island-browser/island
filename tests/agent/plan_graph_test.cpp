#include "plan_graph.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
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

void ExpectNoSharedCells(const PlanGraph& g) {
    std::map<std::pair<int, int>, std::string> cells;
    for (const PlanNode& node : g.nodes) {
        const auto [it, fresh] = cells.emplace(std::make_pair(node.column, node.row), node.id);
        EXPECT_TRUE(fresh) << node.id << " shares (" << node.column << "," << node.row << ") with "
                           << it->second;
    }
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
    EXPECT_EQ(Find(g, "m").depth, 0);
    EXPECT_EQ(Find(g, "r").depth, 0);
    EXPECT_EQ(Find(g, "a1").depth, 2);
    ExpectNoSharedCells(g);
}

TEST(PlanGraphTest, SequentialFanOutFanInStaysShallow) {
    std::vector<AcpPlanEntry> plan{E("s0", {}, "")};
    std::string trunk = "s0";
    for (int k = 0; k < 4; ++k) {
        const std::string x = "x" + std::to_string(k);
        const std::string y = "y" + std::to_string(k);
        const std::string j = "j" + std::to_string(k);
        plan.push_back(E(x, {trunk}, "X"));
        plan.push_back(E(y, {trunk}, "Y"));
        plan.push_back(E(j, {x, y}, "", "join"));
        trunk = j;
    }
    const PlanGraph g = BuildPlanGraph(plan, {});
    for (int k = 0; k < 4; ++k) {
        const PlanNode& j = Find(g, "j" + std::to_string(k));
        EXPECT_EQ(j.depth, 0) << j.id;
        EXPECT_EQ(j.kind, PlanNodeKind::kJoin) << j.id;
        EXPECT_TRUE(j.needs_you.empty()) << j.id;
    }
    EXPECT_EQ(Find(g, "x3").depth, 1);
    EXPECT_EQ(Find(g, "x3").kind, PlanNodeKind::kStep);
    EXPECT_EQ(Find(g, "y3").depth, 1);
    EXPECT_EQ(Find(g, "y3").kind, PlanNodeKind::kStep);
    ExpectNoSharedCells(g);
}

TEST(PlanGraphTest, JoinInsideABranchKeepsTheBranchDepth) {
    const PlanGraph g =
        BuildPlanGraph({E("s", {}, ""), E("a", {"s"}, "A"), E("b", {"s"}, "B"), E("a1", {"a"}, "A"),
                        E("a2", {"a"}, "A"), E("m", {"a1", "a2"}, "A", "join")},
                       {});
    EXPECT_EQ(Find(g, "a1").depth, 2);
    EXPECT_EQ(Find(g, "m").depth, 1);
}

TEST(PlanGraphTest, JoinOverABranchPointAndItsChildKeepsItsLevel) {
    const PlanGraph g =
        BuildPlanGraph({E("x", {}, ""), E("y", {"x"}, ""), E("m", {"x", "y"}, "", "join")}, {});
    EXPECT_EQ(Find(g, "y").depth, 1);
    EXPECT_EQ(Find(g, "m").depth, 0);
}

TEST(PlanGraphTest, JoinTreeDoesNotCoverALongerLane) {
    const PlanGraph g = BuildPlanGraph(
        {E("s", {}, ""), E("a", {"s"}, "A"), E("b", {"s"}, "B"), E("c", {"s"}, "C"),
         E("a1", {"a"}, "A"), E("b1", {"b"}, "B"), E("b2", {"b1"}, "B"), E("b3", {"b2"}, "B"),
         E("c1", {"c"}, "C"), E("m", {"a1", "c1"}, "", "join"), E("r", {"m"}, "", "result")},
        {});
    ExpectNoSharedCells(g);
}

TEST(PlanGraphTest, LaterRootsStartBelowEverythingPlaced) {
    const PlanGraph g = BuildPlanGraph(
        {E("s", {}, ""), E("a", {"s"}, "A"), E("b", {"s"}, "B"), E("m", {"a", "b"}, "", "join"),
         E("m1", {"m"}, ""), E("m2", {"m"}, ""), E("m3", {"m"}, ""), E("m4", {"m"}, ""),
         E("z", {}, ""), E("z1", {"z"}, ""), E("z2", {"z1"}, ""), E("z3", {"z2"}, "")},
        {});
    ExpectNoSharedCells(g);
    int lowest_m = 0;
    for (const char* id : {"m1", "m2", "m3", "m4"}) lowest_m = std::max(lowest_m, Find(g, id).row);
    EXPECT_GT(Find(g, "z").row, lowest_m);
}

TEST(PlanGraphTest, CyclesAreBrokenAndReported) {
    const PlanGraph g = BuildPlanGraph({E("a", {"b"}, ""), E("b", {"a"}, "")}, {});
    EXPECT_EQ(g.nodes.size(), 2U);
    EXPECT_EQ(g.dropped, std::vector<std::string>{"b->a: cycle"});
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

TEST(PlanGraphTest, DeepNodeKeepsTheAgentsNeedsYouReason) {
    std::vector<AcpPlanEntry> plan{E("s", {}, ""),
                                   E("a", {"s"}, "A"),
                                   E("x", {"s"}, "B"),
                                   E("a1", {"a"}, "A.1"),
                                   E("a2", {"a"}, "A.2"),
                                   E("a11", {"a1"}, "A.1.1"),
                                   E("a12", {"a1"}, "A.1.2"),
                                   E("a111", {"a11"}, "A.1.1.1"),
                                   E("a112", {"a11"}, "A.1.1.2")};
    plan[7].island->needs_you = "sign-in";
    const PlanGraph g = BuildPlanGraph(plan, {});
    EXPECT_EQ(Find(g, "a111").depth, 4);
    EXPECT_EQ(Find(g, "a111").kind, PlanNodeKind::kNeedsYou);
    EXPECT_EQ(Find(g, "a111").needs_you, "sign-in");
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

TEST(PlanGraphTest, SpecPlanNodesCarryReadableTitles) {
    const PlanGraph graph = BuildPlanGraph(SpecPlan(), {});
    std::map<std::string, std::string> titles;
    for (const PlanNode& node : graph.nodes) {
        titles[node.id] = node.title;
        EXPECT_NE(node.title, node.id) << node.id;
    }
    EXPECT_EQ(titles["a2"], "Known problems in open issues");
    EXPECT_EQ(titles["m"], "Weigh reviews, prices and owner reports");
    EXPECT_EQ(titles["r"], "Write the recommendation");
}

TEST(PlanGraphTest, PlainPlanGetsGeneratedIdsAndAChain) {
    EXPECT_EQ(PlanEntryIds(PlainPlan()), (std::vector<std::string>{"p0", "p1", "p2"}));
    const PlanGraph graph = BuildPlanGraph(PlainPlan(), {});
    ASSERT_EQ(graph.nodes.size(), 3U);
    EXPECT_EQ(graph.nodes[2].after, (std::vector<std::string>{"p1"}));
    EXPECT_EQ(graph.nodes[0].title, "Find two sources");
}

}  // namespace
}  // namespace island::agent
