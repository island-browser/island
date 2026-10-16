#include "plan_edits.h"

#include <algorithm>

#include "json_util.h"

namespace island::agent {
namespace {

std::string EntryId(const AcpPlanEntry& entry) {
    return entry.island.has_value() ? entry.island->id : std::string();
}

// The entry as a plain ACP plan entry with Island's metadata, the same shape the
// agent sent it in, so the edited plan can be sent back verbatim.
json::Value EntryJson(const AcpPlanEntry& entry, const PlanEdits& edits) {
    const std::string id = EntryId(entry);
    const auto edited = edits.instructions.find(id);
    json::Value value = json::Value::MakeObject().Set(
        "content", json::Value::String(edited != edits.instructions.end() && !id.empty()
                                           ? edited->second
                                           : entry.content));
    value.Set("status", json::Value::String(entry.status.empty() ? "pending" : entry.status));
    if (entry.island.has_value()) {
        const IslandPlanMeta& meta = *entry.island;
        json::Value after = json::Value::MakeArray();
        for (const std::string& dependency : meta.after) {
            after.Push(json::Value::String(dependency));
        }
        json::Value island = json::Value::MakeObject()
                                 .Set("id", json::Value::String(meta.id))
                                 .Set("after", std::move(after))
                                 .Set("branch", json::Value::String(meta.branch))
                                 .Set("kind", json::Value::String(meta.kind))
                                 .Set("tool", json::Value::String(meta.tool))
                                 .Set("site", json::Value::String(meta.site))
                                 .Set("needs_you", json::Value::String(meta.needs_you));
        value.Set("_meta", json::Value::MakeObject().Set("island", std::move(island)));
    }
    return value;
}

}  // namespace

std::string BuildPlanRunMessage(const std::vector<AcpPlanEntry>& plan, const PlanEdits& edits,
                                std::optional<std::string> only_branch) {
    // A branch run keeps the trunk and the chosen branch; the others stay unrun.
    const auto kept = [&](const AcpPlanEntry& entry) {
        if (edits.skipped.contains(EntryId(entry))) return false;
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
    for (const AcpPlanEntry& entry : plan) {
        if (kept(entry)) entries.Push(EntryJson(entry, edits));
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
    std::set<std::string> ids;
    for (const AcpPlanEntry& entry : next_plan) {
        ids.insert(EntryId(entry));
    }
    PlanEdits next;
    for (const std::string& id : edits.skipped) {
        if (ids.contains(id)) next.skipped.insert(id);
    }
    for (const auto& [id, text] : edits.instructions) {
        if (ids.contains(id)) next.instructions.emplace(id, text);
    }
    return next;
}

}  // namespace island::agent
