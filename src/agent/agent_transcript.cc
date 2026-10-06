#include "agent_transcript.h"

#include <utility>

#include "json_util.h"

namespace island::agent {

std::string_view TranscriptKindName(TranscriptItem::Kind kind) {
    switch (kind) {
        case TranscriptItem::Kind::kUser:
            return "user";
        case TranscriptItem::Kind::kAgent:
            return "agent";
        case TranscriptItem::Kind::kThought:
            return "thought";
        case TranscriptItem::Kind::kTool:
            return "tool";
        case TranscriptItem::Kind::kPlan:
            return "plan";
        case TranscriptItem::Kind::kPermission:
            return "permission";
        case TranscriptItem::Kind::kError:
            return "error";
        case TranscriptItem::Kind::kNotice:
            return "notice";
    }
    return "notice";
}

void AgentTranscript::AddUserPrompt(std::string text) {
    Push({.kind = TranscriptItem::Kind::kUser, .text = std::move(text)});
}

void AgentTranscript::AddNotice(std::string text) {
    Push({.kind = TranscriptItem::Kind::kNotice, .text = std::move(text)});
}

void AgentTranscript::Apply(const AcpEvent& event) {
    using Kind = TranscriptItem::Kind;
    switch (event.type) {
        case AcpEvent::Type::kAgentMessageChunk:
        case AcpEvent::Type::kAgentThoughtChunk: {
            const Kind kind =
                event.type == AcpEvent::Type::kAgentMessageChunk ? Kind::kAgent : Kind::kThought;
            // Consecutive chunks of the same kind stream into one bubble.
            if (!items_.empty() && items_.back().kind == kind) {
                items_.back().text += event.text;
                ++revision_;
            } else {
                Push({.kind = kind, .text = event.text});
            }
            return;
        }
        case AcpEvent::Type::kToolCall: {
            if (TranscriptItem* existing = FindTool(event.tool_call_id)) {
                if (!event.title.empty()) existing->text = event.title;
                if (!event.status.empty()) existing->status = event.status;
                ++revision_;
                return;
            }
            Push({.kind = Kind::kTool,
                  .text = event.title.empty() ? std::string("Tool call") : event.title,
                  .tool_call_id = event.tool_call_id,
                  .tool_kind = event.kind,
                  .status = event.status.empty() ? std::string("pending") : event.status});
            return;
        }
        case AcpEvent::Type::kToolCallUpdate: {
            TranscriptItem* existing = FindTool(event.tool_call_id);
            if (existing == nullptr) {
                Push({.kind = Kind::kTool,
                      .text = event.title.empty() ? std::string("Tool call") : event.title,
                      .tool_call_id = event.tool_call_id,
                      .tool_kind = event.kind,
                      .status = event.status.empty() ? std::string("pending") : event.status});
                return;
            }
            if (!event.title.empty()) existing->text = event.title;
            if (!event.status.empty()) existing->status = event.status;
            if (!event.kind.empty()) existing->tool_kind = event.kind;
            ++revision_;
            return;
        }
        case AcpEvent::Type::kPlan: {
            // One live plan per turn: update it in place when it is the latest
            // plan since the last user prompt.
            for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
                if (it->kind == Kind::kUser) break;
                if (it->kind == Kind::kPlan) {
                    it->plan = event.plan;
                    ++revision_;
                    return;
                }
            }
            Push({.kind = Kind::kPlan, .plan = event.plan});
            return;
        }
        case AcpEvent::Type::kPermissionRequest:
            Push({.kind = Kind::kPermission,
                  .text = event.title.empty() ? std::string("The agent asks for permission")
                                              : event.title,
                  .tool_call_id = event.tool_call_id,
                  .tool_kind = event.kind,
                  .request_id = event.request_id,
                  .options = event.options});
            return;
        case AcpEvent::Type::kTurnEnded:
            CancelOpenPermissions();
            if (event.text == "cancelled") {
                AddNotice("Stopped.");
            } else if (event.text == "max_tokens" || event.text == "max_turn_requests") {
                AddNotice("The agent stopped early (" + event.text + ").");
            } else if (event.text == "refusal") {
                AddNotice("The agent declined to continue.");
            }
            return;
        case AcpEvent::Type::kError:
            // A failed turn (or a dead agent) will never answer open prompts.
            CancelOpenPermissions();
            Push({.kind = Kind::kError, .text = event.text});
            return;
        case AcpEvent::Type::kStateChanged:
            return;
    }
}

void AgentTranscript::ResolvePermission(std::int64_t request_id, std::string resolution) {
    for (TranscriptItem& item : items_) {
        if (item.kind == TranscriptItem::Kind::kPermission && item.request_id == request_id &&
            item.resolution.empty()) {
            item.resolution = std::move(resolution);
            ++revision_;
            return;
        }
    }
}

void AgentTranscript::CancelOpenPermissions() {
    for (TranscriptItem& item : items_) {
        if (item.kind == TranscriptItem::Kind::kPermission && item.resolution.empty()) {
            item.resolution = "Cancelled";
            ++revision_;
        }
    }
}

void AgentTranscript::Clear() {
    items_.clear();
    ++revision_;
}

TranscriptItem* AgentTranscript::FindTool(std::string_view tool_call_id) {
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        if (it->kind == TranscriptItem::Kind::kTool && it->tool_call_id == tool_call_id) {
            return &*it;
        }
    }
    return nullptr;
}

void AgentTranscript::Push(TranscriptItem item) {
    items_.push_back(std::move(item));
    if (items_.size() > kMaxItems) {
        items_.erase(items_.begin(),
                     items_.begin() + static_cast<std::ptrdiff_t>(items_.size() - kMaxItems));
    }
    ++revision_;
}

std::string AgentTranscript::ToJson() const {
    using json::Value;
    Value list = Value::MakeArray();
    for (const TranscriptItem& item : items_) {
        Value entry = Value::MakeObject()
                          .Set("kind", Value::String(std::string(TranscriptKindName(item.kind))))
                          .Set("text", Value::String(item.text));
        if (item.kind == TranscriptItem::Kind::kTool) {
            entry.Set("id", Value::String(item.tool_call_id))
                .Set("tool_kind", Value::String(item.tool_kind))
                .Set("status", Value::String(item.status));
        }
        if (item.kind == TranscriptItem::Kind::kPlan) {
            Value plan = Value::MakeArray();
            for (const AcpPlanEntry& step : item.plan) {
                plan.Push(Value::MakeObject()
                              .Set("content", Value::String(step.content))
                              .Set("status", Value::String(step.status)));
            }
            entry.Set("plan", std::move(plan));
        }
        if (item.kind == TranscriptItem::Kind::kPermission) {
            Value options = Value::MakeArray();
            for (const AcpPermissionOption& option : item.options) {
                options.Push(Value::MakeObject()
                                 .Set("id", Value::String(option.id))
                                 .Set("name", Value::String(option.name))
                                 .Set("kind", Value::String(option.kind)));
            }
            entry.Set("request_id", Value::Int(item.request_id))
                .Set("options", std::move(options))
                .Set("resolution", Value::String(item.resolution));
        }
        list.Push(std::move(entry));
    }
    return json::Serialize(list);
}

}  // namespace island::agent
