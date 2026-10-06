#include "jsonrpc.h"

#include <utility>

namespace island::agent::jsonrpc {

namespace {

json::Value Envelope() {
    return json::Value::MakeObject().Set("jsonrpc", json::Value::String("2.0"));
}

bool IsValidId(const json::Value& id) { return id.IsString() || id.IsNumber() || id.IsNull(); }

}  // namespace

Message Classify(std::string_view text) {
    Message message;
    std::optional<json::Value> parsed = json::Parse(text);
    if (!parsed || !parsed->IsObject()) return message;
    const json::Value& root = *parsed;
    if (root.StringOr("jsonrpc", "") != "2.0") return message;

    const json::Value* id = root.FindMember("id");
    const json::Value* method = root.FindMember("method");
    const json::Value* result = root.FindMember("result");
    const json::Value* error = root.FindMember("error");
    if (id != nullptr && !IsValidId(*id)) return message;

    if (method != nullptr) {
        if (!method->IsString()) return message;
        message.method = method->string_val;
        if (const json::Value* params = root.FindMember("params")) message.params = *params;
        if (id != nullptr) {
            message.kind = MessageKind::kRequest;
            message.id = *id;
        } else {
            message.kind = MessageKind::kNotification;
        }
        return message;
    }
    if (id == nullptr) return message;
    message.id = *id;
    if (result != nullptr) {
        message.kind = MessageKind::kResponse;
        message.result = *result;
        return message;
    }
    if (error != nullptr && error->IsObject()) {
        message.kind = MessageKind::kErrorResponse;
        message.error_code = error->IntOr("code", kInternalError);
        message.error_message = std::string(error->StringOr("message", ""));
        return message;
    }
    return message;
}

std::string MakeRequest(const json::Value& id, std::string_view method, json::Value params) {
    json::Value message = Envelope();
    message.Set("id", id);
    message.Set("method", json::Value::String(std::string(method)));
    message.Set("params", std::move(params));
    return json::Serialize(message);
}

std::string MakeNotification(std::string_view method, json::Value params) {
    json::Value message = Envelope();
    message.Set("method", json::Value::String(std::string(method)));
    message.Set("params", std::move(params));
    return json::Serialize(message);
}

std::string MakeResult(const json::Value& id, json::Value result) {
    json::Value message = Envelope();
    message.Set("id", id);
    message.Set("result", std::move(result));
    return json::Serialize(message);
}

std::string MakeError(const json::Value& id, std::int64_t code, std::string_view text) {
    json::Value error = json::Value::MakeObject();
    error.Set("code", json::Value::Int(code));
    error.Set("message", json::Value::String(std::string(text)));
    json::Value message = Envelope();
    message.Set("id", id);
    message.Set("error", std::move(error));
    return json::Serialize(message);
}

}  // namespace island::agent::jsonrpc
