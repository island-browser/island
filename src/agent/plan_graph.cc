#include "plan_graph.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>

#include "json_util.h"

namespace island::agent {
namespace {

using json::Value;

struct Draft {
    PlanNode node;
    std::vector<int> parents;  // draft indices, in `after` order
};

PlanNodeKind KindFromMeta(const std::string& kind) {
    if (kind == "question") return PlanNodeKind::kQuestion;
    if (kind == "join") return PlanNodeKind::kJoin;
    if (kind == "result") return PlanNodeKind::kResult;
    return PlanNodeKind::kStep;
}

PlanNodeState StateFromStatus(const std::string& status) {
    if (status == "in_progress") return PlanNodeState::kRunning;
    if (status == "completed") return PlanNodeState::kDone;
    if (status == "failed") return PlanNodeState::kFailed;
    return PlanNodeState::kQueued;
}

const char* KindName(PlanNodeKind kind) {
    switch (kind) {
        case PlanNodeKind::kQuestion:
            return "question";
        case PlanNodeKind::kStep:
            return "step";
        case PlanNodeKind::kNeedsYou:
            return "needs_you";
        case PlanNodeKind::kJoin:
            return "join";
        case PlanNodeKind::kResult:
            return "result";
    }
    return "step";
}

const char* StateName(PlanNodeState state) {
    switch (state) {
        case PlanNodeState::kQueued:
            return "queued";
        case PlanNodeState::kRunning:
            return "running";
        case PlanNodeState::kDone:
            return "done";
        case PlanNodeState::kFailed:
            return "failed";
        case PlanNodeState::kSkipped:
            return "skipped";
    }
    return "queued";
}

}  // namespace

std::vector<std::string> PlanEntryIds(const std::vector<AcpPlanEntry>& entries) {
    std::vector<std::string> ids;
    ids.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const AcpPlanEntry& entry = entries[i];
        const bool has_id = entry.island.has_value() && !entry.island->id.empty();
        ids.push_back(has_id ? entry.island->id : "p" + std::to_string(i));
    }
    return ids;
}

PlanGraph BuildPlanGraph(const std::vector<AcpPlanEntry>& entries,
                         const std::set<std::string>& skipped_ids) {
    PlanGraph graph;

    // 1. Entries to drafts: synthesized ids and chain edges for plain ACP entries, the
    //    first entry wins for a repeated id.
    const std::vector<std::string> ids = PlanEntryIds(entries);
    std::vector<Draft> drafts;
    std::vector<std::vector<std::string>> wanted;  // raw `after` ids per draft
    std::unordered_map<std::string, int> index_of;
    std::string previous_id;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const AcpPlanEntry& entry = entries[i];
        Draft draft;
        std::vector<std::string> after;
        draft.node.id = ids[i];
        if (entry.island.has_value()) {
            const IslandPlanMeta& meta = *entry.island;
            draft.node.kind = KindFromMeta(meta.kind);
            draft.node.branch = meta.branch;
            draft.node.tool = meta.tool;
            draft.node.site = meta.site;
            draft.node.needs_you = meta.needs_you;
            after = meta.after;
        } else if (i > 0) {
            after.push_back(previous_id);
        }
        previous_id = draft.node.id;
        if (index_of.count(draft.node.id) != 0) {
            graph.dropped.push_back(draft.node.id + ": duplicate id");
            continue;
        }
        draft.node.title = entry.content;
        draft.node.state = StateFromStatus(entry.status);
        if (skipped_ids.count(draft.node.id) != 0) draft.node.state = PlanNodeState::kSkipped;
        index_of.emplace(draft.node.id, static_cast<int>(drafts.size()));
        drafts.push_back(std::move(draft));
        wanted.push_back(std::move(after));
    }

    // 2. Resolve edges: unknown ids and repeats are dropped.
    const int n = static_cast<int>(drafts.size());
    for (int i = 0; i < n; ++i) {
        for (const std::string& id : wanted[i]) {
            const auto it = index_of.find(id);
            if (it == index_of.end()) {
                graph.dropped.push_back(drafts[i].node.id + ": unknown after " + id);
                continue;
            }
            if (std::find(drafts[i].parents.begin(), drafts[i].parents.end(), it->second) ==
                drafts[i].parents.end()) {
                drafts[i].parents.push_back(it->second);
            }
        }
    }

    // 3. Kahn. Whatever is left sits on or behind a cycle: it loses its back edges (edges
    //    from a later entry), which makes the remainder acyclic, and the sort runs again.
    auto topo = [&](std::vector<int>& order) {
        std::vector<int> indegree(n, 0);
        std::vector<std::vector<int>> children(n);
        for (int i = 0; i < n; ++i) {
            indegree[i] = static_cast<int>(drafts[i].parents.size());
            for (int p : drafts[i].parents) children[p].push_back(i);
        }
        std::priority_queue<int, std::vector<int>, std::greater<>> ready;
        for (int i = 0; i < n; ++i) {
            if (indegree[i] == 0) ready.push(i);
        }
        order.clear();
        while (!ready.empty()) {
            const int cur = ready.top();
            ready.pop();
            order.push_back(cur);
            for (int c : children[cur]) {
                if (--indegree[c] == 0) ready.push(c);
            }
        }
        return static_cast<int>(order.size()) == n;
    };
    std::vector<int> order;
    if (!topo(order)) {
        std::vector<bool> placed(n, false);
        for (int i : order) placed[i] = true;
        for (int i = 0; i < n; ++i) {
            if (placed[i]) continue;
            std::vector<int> kept;
            for (int p : drafts[i].parents) {
                if (!placed[p] && p >= i) {
                    graph.dropped.push_back(drafts[p].node.id + "->" + drafts[i].node.id +
                                            ": cycle");
                } else {
                    kept.push_back(p);
                }
            }
            drafts[i].parents = std::move(kept);
        }
        topo(order);
    }

    // 4. Children, branching, depth and column along the topological order. Depth is
    //    branch nesting: a node sits inside the branch points its parent hands down (the
    //    parent's own, plus the parent when it branches). A node with several inputs closes
    //    the branch point they diverged from, the last one they share.
    std::vector<std::vector<int>> children(n);
    for (int i = 0; i < n; ++i) {
        for (int p : drafts[i].parents) children[p].push_back(i);
    }
    std::vector<std::vector<int>> open(n);  // enclosing branch points, outermost first
    auto handed_down = [&](int p) {
        std::vector<int> stack = open[p];
        if (children[p].size() >= 2) stack.push_back(p);
        return stack;
    };
    std::vector<int> column(n, 0);
    for (int i : order) {
        Draft& d = drafts[i];
        for (int p : d.parents) column[i] = std::max(column[i], column[p] + 1);
        if (d.parents.size() == 1) {
            open[i] = handed_down(d.parents[0]);
        } else if (d.parents.size() >= 2) {
            std::vector<int> shared = handed_down(d.parents[0]);
            for (std::size_t k = 1; k < d.parents.size(); ++k) {
                const std::vector<int> other = handed_down(d.parents[k]);
                const auto end =
                    std::mismatch(shared.begin(), shared.end(), other.begin(), other.end()).first;
                shared.erase(end, shared.end());
            }
            if (!shared.empty()) shared.pop_back();
            open[i] = std::move(shared);
        }
        d.node.depth = static_cast<int>(open[i].size());
        d.node.column = column[i];
        if (children[i].size() >= 2) graph.branching = true;
        if (d.node.depth > kMaxBranchDepth && d.node.needs_you.empty()) d.node.needs_you = "depth";
        if (!d.node.needs_you.empty()) d.node.kind = PlanNodeKind::kNeedsYou;
        for (int p : d.parents) d.node.after.push_back(drafts[p].node.id);
    }

    // 5. Rows. The tree parent is the sole parent; roots and joins start their own tree.
    //    A tree's leaves take the next free rows and a parent sits in the middle of its
    //    tree children. A join sits in the middle of its inputs, and its own tree shifts
    //    to follow it, further down while any of its cells is taken. A root starts below
    //    everything placed so far.
    std::vector<std::vector<int>> tree_children(n);
    for (int i = 0; i < n; ++i) {
        if (drafts[i].parents.size() == 1) tree_children[drafts[i].parents[0]].push_back(i);
    }
    std::vector<int> row(n, 0);
    std::vector<bool> laid(n, false);
    std::set<std::pair<int, int>> taken;  // (column, row)
    bool any_laid = false;
    int bottom = 0;  // lowest row placed so far
    for (int root : order) {
        if (laid[root] || drafts[root].parents.size() == 1) continue;
        std::vector<int> preorder;
        std::vector<int> stack{root};
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            preorder.push_back(cur);
            for (auto it = tree_children[cur].rbegin(); it != tree_children[cur].rend(); ++it) {
                stack.push_back(*it);
            }
        }
        int leaf = 0;
        for (int cur : preorder) {
            if (tree_children[cur].empty()) row[cur] = leaf++;
        }
        for (auto it = preorder.rbegin(); it != preorder.rend(); ++it) {
            if (tree_children[*it].empty()) continue;
            int low = row[tree_children[*it].front()];
            int high = low;
            for (int c : tree_children[*it]) {
                low = std::min(low, row[c]);
                high = std::max(high, row[c]);
            }
            row[*it] = (low + high) / 2;
        }
        int shift = any_laid ? bottom + 1 : 0;
        if (!drafts[root].parents.empty()) {
            int low = row[drafts[root].parents.front()];
            int high = low;
            for (int p : drafts[root].parents) {
                low = std::min(low, row[p]);
                high = std::max(high, row[p]);
            }
            shift = (low + high) / 2 - row[root];
        }
        auto collides = [&] {
            for (int cur : preorder) {
                if (taken.count({column[cur], row[cur] + shift}) != 0) return true;
            }
            return false;
        };
        while (collides()) ++shift;
        for (int cur : preorder) {
            row[cur] += shift;
            laid[cur] = true;
            taken.emplace(column[cur], row[cur]);
            bottom = any_laid ? std::max(bottom, row[cur]) : row[cur];
            any_laid = true;
        }
    }
    int lowest = 0;
    for (int i = 0; i < n; ++i) lowest = std::min(lowest, row[i]);
    for (int i : order) {
        drafts[i].node.row = row[i] - lowest;
        graph.nodes.push_back(std::move(drafts[i].node));
    }
    return graph;
}

std::string PlanGraphJson(const PlanGraph& graph) {
    Value nodes = Value::MakeArray();
    for (const PlanNode& node : graph.nodes) {
        Value after = Value::MakeArray();
        for (const std::string& id : node.after) after.Push(Value::String(id));
        Value item = Value::MakeObject();
        item.Set("id", Value::String(node.id));
        item.Set("title", Value::String(node.title));
        item.Set("kind", Value::String(KindName(node.kind)));
        item.Set("state", Value::String(StateName(node.state)));
        item.Set("branch", Value::String(node.branch));
        item.Set("tool", Value::String(node.tool));
        item.Set("site", Value::String(node.site));
        item.Set("needs_you", Value::String(node.needs_you));
        item.Set("after", std::move(after));
        item.Set("column", Value::Int(node.column));
        item.Set("row", Value::Int(node.row));
        item.Set("depth", Value::Int(node.depth));
        nodes.Push(std::move(item));
    }
    Value root = Value::MakeObject();
    root.Set("branching", Value::Bool(graph.branching));
    root.Set("nodes", std::move(nodes));
    return json::Serialize(root);
}

}  // namespace island::agent
