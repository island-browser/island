#include "plan_edits.h"

#include <algorithm>

#include "json_util.h"
#include "plan_graph.h"

namespace island::agent {
namespace {

// The entry as a plain ACP plan entry, in the shape the agent sent it: the edited content and
// status, and `_meta` taken from the entry's raw metadata with only Island's fields replaced.
// Keys the agent added stay as they were, so the plan goes back unchanged apart from the edits.
json::Value EntryJson(const AcpPlanEntry& entry, const std::string& id, const PlanEdits& edits) {
    const auto edited = edits.instructions.find(id);
    json::Value value = json::Value::MakeObject().Set(
        "content",
        json::Value::String(edited != edits.instructions.end() ? edited->second : entry.content));
    value.Set("status", json::Value::String(entry.status.empty() ? "pending" : entry.status));

    std::optional<json::Value> parsed = json::Parse(entry.raw_meta);
    json::Value meta =
        parsed.has_value() && parsed->IsObject() ? std::move(*parsed) : json::Value::MakeObject();
    if (entry.island.has_value()) {
        const IslandPlanMeta& island_meta = *entry.island;
        json::Value after = json::Value::MakeArray();
        for (const std::string& dependency : island_meta.after) {
            after.Push(json::Value::String(dependency));
        }
        const json::Value* raw_island = meta.FindMember("island");
        json::Value island = raw_island != nullptr && raw_island->IsObject()
                                 ? *raw_island
                                 : json::Value::MakeObject();
        island.Set("id", json::Value::String(island_meta.id))
            .Set("after", std::move(after))
            .Set("branch", json::Value::String(island_meta.branch))
            .Set("kind", json::Value::String(island_meta.kind))
            .Set("tool", json::Value::String(island_meta.tool))
            .Set("site", json::Value::String(island_meta.site))
            .Set("needs_you", json::Value::String(island_meta.needs_you));
        meta.Set("island", std::move(island));
    }
    if (!meta.object_val.empty()) {
        value.Set("_meta", std::move(meta));
    }
    return value;
}

}  // namespace

std::string BuildPlanRunMessage(const std::vector<AcpPlanEntry>& plan, const PlanEdits& edits,
                                std::optional<std::string> only_branch) {
    const std::vector<std::string> ids = PlanEntryIds(plan);
    // A branch run keeps the trunk and the chosen branch; the others stay unrun.
    const auto kept = [&](std::size_t index) {
        if (edits.skipped.contains(ids[index])) return false;
        const AcpPlanEntry& entry = plan[index];
        if (!only_branch.has_value() || !entry.island.has_value()) return true;
        return entry.island->branch.empty() || entry.island->branch == *only_branch;
    };
    std::string message = "The person approved the research plan. Run it with these edits.\n";
    if (!edits.skipped.empty()) {
        message += "Skip: ";
        bool first = true;
        for (const std::string& id : edits.skipped) {
            message += (first ? "" : ", ") + id;
            first = false;
        }
        message += "\n";
    }
    for (const auto& [id, text] : edits.instructions) {
        message += "Instruction for " + id + ": " + text + "\n";
    }
    message += only_branch.has_value()
                   ? "Run only branch " + *only_branch +
                         " and the trunk steps; leave the other branches unrun.\n"
                   : "Run the whole plan.\n";
    json::Value entries = json::Value::MakeArray();
    for (std::size_t index = 0; index < plan.size(); ++index) {
        if (kept(index)) entries.Push(EntryJson(plan[index], ids[index], edits));
    }
    message += "The plan, one entry per line:\n";
    for (const json::Value& entry : entries.array_val) {
        message += json::Serialize(entry) + "\n";
    }
    return message;
}

std::string BuildExpandMessage(std::string_view node_id) {
    return "Expand step " + std::string(node_id);
}

std::string BuildBranchFromMessage(std::string_view node_id) {
    return "Add a branch after step " + std::string(node_id);
}

PlanEdits CarryEditsOver(const PlanEdits& edits, const std::vector<AcpPlanEntry>& next_plan) {
    const std::vector<std::string> ids = PlanEntryIds(next_plan);
    const std::set<std::string> present(ids.begin(), ids.end());
    PlanEdits next;
    for (const std::string& id : edits.skipped) {
        if (present.contains(id)) next.skipped.insert(id);
    }
    for (const auto& [id, text] : edits.instructions) {
        if (present.contains(id)) next.instructions.emplace(id, text);
    }
    return next;
}

}  // namespace island::agent
