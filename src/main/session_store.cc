#include "session_store.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace island {

// =========================================================================
// Minimal JSON value tree (read-only, built during parsing)
// =========================================================================

namespace json {

enum class Type : std::uint8_t { kNull, kBool, kInt, kString, kArray, kObject };

struct Value;

using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

struct Value {
    Type type = Type::kNull;
    bool bool_val = false;
    std::int64_t int_val = 0;
    std::string string_val;
    Array array_val;
    Object object_val;

    bool IsObject() const noexcept { return type == Type::kObject; }
    bool IsArray() const noexcept { return type == Type::kArray; }
    bool IsString() const noexcept { return type == Type::kString; }
    bool IsInt() const noexcept { return type == Type::kInt; }

    const Value* FindMember(std::string_view key) const noexcept {
        if (!IsObject()) return nullptr;
        for (const auto& [k, v] : object_val) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    // Quick schema helpers
    bool HasRequiredInt(std::string_view key) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsInt();
    }
    bool HasRequiredString(std::string_view key) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsString();
    }
    bool HasRequiredArray(std::string_view key) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsArray();
    }
    bool HasRequiredObject(std::string_view key) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsObject();
    }
};

// =========================================================================
// JSON writer
// =========================================================================

class Writer {
  public:
    explicit Writer(std::ostream& out) : out_(out) {}

    void StartObject() {
        Emit('{');
        comma_stack_.push_back(false);
    }
    void EndObject() {
        Emit('}');
        comma_stack_.pop_back();
    }
    void StartArray() {
        Emit('[');
        comma_stack_.push_back(false);
    }
    void EndArray() {
        Emit(']');
        comma_stack_.pop_back();
    }

    void Key(std::string_view k) {
        Comma();
        WriteString(k);
        Emit(':');
    }

    // Call before emitting each array element (except the first).
    void ArrayElement() { Comma(); }

    void NullValue() { EmitRaw("null"); }
    void IntValue(std::int64_t v) { EmitRaw(std::to_string(v)); }
    void UintValue(std::uint32_t v) { EmitRaw(std::to_string(v)); }
    void Uint64Value(std::uint64_t v) { EmitRaw(std::to_string(v)); }
    void StringValue(std::string_view v) { WriteString(v); }
    void BoolValue(bool v) { EmitRaw(v ? "true" : "false"); }

  private:
    void Emit(char c) { out_ << c; }
    void EmitRaw(std::string_view sv) { out_ << sv; }

    void WriteString(std::string_view sv) {
        Emit('"');
        for (char c : sv) {
            if (c == '"') {
                EmitRaw("\\\"");
            } else if (c == '\\') {
                EmitRaw("\\\\");
            } else if (c == '\n') {
                EmitRaw("\\n");
            } else if (c == '\r') {
                EmitRaw("\\r");
            } else if (c == '\t') {
                EmitRaw("\\t");
            } else {
                Emit(c);
            }
        }
        Emit('"');
    }

    void Comma() {
        if (comma_stack_.back()) Emit(',');
        comma_stack_.back() = true;
    }

    std::ostream& out_;
    std::vector<char> comma_stack_;
};

// =========================================================================
// JSON parser
// =========================================================================

class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input), pos_(0) {}

    Value Parse() {
        Value v = ParseValue();
        SkipWhitespace();
        if (pos_ != input_.size()) {
            throw ParseError("trailing content after root value");
        }
        return v;
    }

  private:
    void SkipWhitespace() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                                        input_[pos_] == '\n' || input_[pos_] == '\r'))
            ++pos_;
    }

    char Peek() {
        SkipWhitespace();
        if (pos_ >= input_.size()) throw ParseError("unexpected end of input");
        return input_[pos_];
    }

    char Next() {
        SkipWhitespace();
        if (pos_ >= input_.size()) throw ParseError("unexpected end of input");
        return input_[pos_++];
    }

    void Expect(char expected) {
        SkipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != expected) {
            throw ParseError("expected '" + std::string(1, expected) + "'");
        }
        ++pos_;
    }

    Value ParseValue() {
        switch (Peek()) {
            case '{':
                return ParseObject();
            case '[':
                return ParseArray();
            case '"':
                return ParseString();
            case 't':
                return ParseLiteral("true", Type::kBool, true);
            case 'f':
                return ParseLiteral("false", Type::kBool, false);
            case 'n':
                return ParseLiteral("null", Type::kNull, false);
            default:
                return ParseNumber();
        }
    }

    Value ParseLiteral(std::string_view expected, Type t, bool bool_val) {
        for (char c : expected) {
            if (pos_ >= input_.size() || input_[pos_] != c) {
                throw ParseError("expected literal '" + std::string(expected) + "'");
            }
            ++pos_;
        }
        Value v;
        v.type = t;
        v.bool_val = bool_val;
        return v;
    }

    Value ParseString() {
        Expect('"');
        std::string result;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\') {
                ++pos_;
                if (pos_ >= input_.size()) throw ParseError("unterminated string escape");
                switch (input_[pos_]) {
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    case '/':
                        result += '/';
                        break;
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    default:
                        throw ParseError("unknown escape character");
                }
                ++pos_;
            } else {
                result += input_[pos_++];
            }
        }
        Expect('"');
        Value v;
        v.type = Type::kString;
        v.string_val = std::move(result);
        return v;
    }

    Value ParseNumber() {
        std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            throw ParseError("expected digit");
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])))
            ++pos_;

        std::string_view num_str = input_.substr(start, pos_ - start);
        std::int64_t val = 0;
        auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
        if (ec != std::errc{}) {
            throw ParseError("invalid number");
        }

        Value v;
        v.type = Type::kInt;
        v.int_val = val;
        return v;
    }

    Value ParseObject() {
        Expect('{');
        Value v;
        v.type = Type::kObject;

        SkipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;
            return v;
        }

        for (;;) {
            SkipWhitespace();
            // key must be a string
            Value key = ParseString();
            if (!key.IsString()) throw ParseError("object key must be a string");
            Expect(':');
            Value val = ParseValue();
            v.object_val.emplace_back(std::move(key.string_val), std::move(val));

            SkipWhitespace();
            if (pos_ >= input_.size()) throw ParseError("unterminated object");
            if (input_[pos_] == '}') {
                ++pos_;
                return v;
            }
            if (input_[pos_] != ',') throw ParseError("expected ',' or '}' in object");
            ++pos_;  // consume comma
        }
    }

    Value ParseArray() {
        Expect('[');
        Value v;
        v.type = Type::kArray;

        SkipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;
            return v;
        }

        for (;;) {
            v.array_val.push_back(ParseValue());
            SkipWhitespace();
            if (pos_ >= input_.size()) throw ParseError("unterminated array");
            if (input_[pos_] == ']') {
                ++pos_;
                return v;
            }
            if (input_[pos_] != ',') throw ParseError("expected ',' or ']' in array");
            ++pos_;
        }
    }

    std::string_view input_;
    std::size_t pos_;
};

}  // namespace json

// =========================================================================
// SessionState
// =========================================================================

SessionState SessionState::Default() noexcept {
    SessionState state;
    state.version = kCurrentSchemaVersion;
    return state;
}

// =========================================================================
// Serialize
// =========================================================================

namespace {

void WriteTabState(json::Writer& w, const TabState& tab) {
    w.StartObject();
    w.Key("id");
    w.Uint64Value(tab.id.value);
    w.Key("url");
    w.StringValue(tab.url);
    w.EndObject();
}

void WriteSplitState(json::Writer& w, const SplitState& split) {
    w.StartObject();
    w.Key("first_tab_id");
    w.Uint64Value(split.first_tab_id.value);
    w.Key("second_tab_id");
    w.Uint64Value(split.second_tab_id.value);
    w.EndObject();
}

void WriteSpaceState(json::Writer& w, const SpaceState& space) {
    w.StartObject();
    w.Key("id");
    w.Uint64Value(space.id.value);
    w.Key("name");
    w.StringValue(space.name);
    w.Key("color_argb");
    w.UintValue(space.color_argb);

    w.Key("tabs");
    w.StartArray();
    for (const auto& tab : space.tabs) {
        w.ArrayElement();
        WriteTabState(w, tab);
    }
    w.EndArray();

    if (space.active_tab_id.has_value()) {
        w.Key("active_tab_id");
        w.Uint64Value(space.active_tab_id->value);
    }

    if (space.split.has_value()) {
        w.Key("split");
        WriteSplitState(w, *space.split);
    }

    w.EndObject();
}

std::string SerializeToJson(const SessionState& state) {
    std::ostringstream oss;
    json::Writer w(oss);

    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(state.version));

    if (state.active_space_id.has_value()) {
        w.Key("active_space_id");
        w.Uint64Value(state.active_space_id->value);
    }

    w.Key("spaces");
    w.StartArray();
    for (const auto& space : state.spaces) {
        w.ArrayElement();
        WriteSpaceState(w, space);
    }
    w.EndArray();
    w.EndObject();

    // Trailing newline for human-readability; also helps with the
    // bookmark bracket test that asserts a trailing newline.
    oss << '\n';
    return oss.str();
}

}  // namespace

// =========================================================================
// Deserialize
// =========================================================================

namespace {

LoadResult ParseSessionState(const json::Value& root) {
    // Schema check: root must be an object
    if (!root.IsObject()) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }

    // version must be present and an integer
    if (!root.HasRequiredInt("version")) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }
    std::int64_t version_val = root.FindMember("version")->int_val;
    if (version_val != SessionState::kCurrentSchemaVersion) {
        return {SessionState::Default(), SessionError::kVersionMismatch};
    }

    // spaces must be present and an array
    if (!root.HasRequiredArray("spaces")) {
        return {SessionState::Default(), SessionError::kSchemaError};
    }

    SessionState state;
    state.version = SessionState::kCurrentSchemaVersion;

    // Optional active_space_id
    const json::Value* active_id = root.FindMember("active_space_id");
    if (active_id != nullptr) {
        if (active_id->IsInt() && active_id->int_val > 0) {
            state.active_space_id = SpaceId{static_cast<std::uint64_t>(active_id->int_val)};
        }
        // Non-int or non-positive → ignore, don't fail
    }

    const json::Array& spaces_arr = root.FindMember("spaces")->array_val;
    for (const auto& space_val : spaces_arr) {
        if (!space_val.IsObject()) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }

        // Required fields per space: id, name, color_argb, tabs (array)
        if (!space_val.HasRequiredInt("id") || !space_val.HasRequiredString("name") ||
            !space_val.HasRequiredInt("color_argb") || !space_val.HasRequiredArray("tabs")) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }

        SpaceState space_state;
        space_state.id = SpaceId{static_cast<std::uint64_t>(space_val.FindMember("id")->int_val)};
        space_state.name = space_val.FindMember("name")->string_val;
        std::int64_t color = space_val.FindMember("color_argb")->int_val;
        if (color < 0 || color > 0xFFFFFFFFLL) {
            return {SessionState::Default(), SessionError::kSchemaError};
        }
        space_state.color_argb = static_cast<std::uint32_t>(color);

        // Parse tabs
        const json::Array& tabs_arr = space_val.FindMember("tabs")->array_val;
        for (const auto& tab_val : tabs_arr) {
            if (!tab_val.IsObject() || !tab_val.HasRequiredInt("id") ||
                !tab_val.HasRequiredString("url")) {
                return {SessionState::Default(), SessionError::kSchemaError};
            }
            TabState tab_state;
            tab_state.id = TabId{static_cast<std::uint64_t>(tab_val.FindMember("id")->int_val)};
            tab_state.url = tab_val.FindMember("url")->string_val;
            space_state.tabs.push_back(std::move(tab_state));
        }

        // Optional active_tab_id
        const json::Value* atid = space_val.FindMember("active_tab_id");
        if (atid != nullptr) {
            if (atid->IsInt() && atid->int_val > 0) {
                space_state.active_tab_id = TabId{static_cast<std::uint64_t>(atid->int_val)};
            }
        }

        // Optional split
        const json::Value* split_val = space_val.FindMember("split");
        if (split_val != nullptr) {
            if (!split_val->IsObject() || !split_val->HasRequiredInt("first_tab_id") ||
                !split_val->HasRequiredInt("second_tab_id")) {
                return {SessionState::Default(), SessionError::kSchemaError};
            }
            SplitState split;
            split.first_tab_id =
                TabId{static_cast<std::uint64_t>(split_val->FindMember("first_tab_id")->int_val)};
            split.second_tab_id =
                TabId{static_cast<std::uint64_t>(split_val->FindMember("second_tab_id")->int_val)};
            space_state.split = std::move(split);
        }

        state.spaces.push_back(std::move(space_state));
    }

    return {std::move(state), SessionError::kNone};
}

}  // namespace

// =========================================================================
// SessionStore
// =========================================================================

LoadResult SessionStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {SessionState::Default(), SessionError::kFileReadError};
    }

    // Read the entire file into a string
    std::string content;
    try {
        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } catch (...) {
        return {SessionState::Default(), SessionError::kFileReadError};
    }

    // Parse JSON
    json::Value root;
    try {
        root = json::Parser(content).Parse();
    } catch (const json::ParseError&) {
        return {SessionState::Default(), SessionError::kParseError};
    }

    // Validate and convert
    return ParseSessionState(root);
}

SessionError SessionStore::Save(const std::filesystem::path& path, const SessionState& state) {
    std::string json_text;
    try {
        json_text = SerializeToJson(state);
    } catch (...) {
        return SessionError::kFileWriteError;
    }

    // Atomic write: write to a temp file in the same directory, then rename.
    // This guarantees the target file is either intact or absent — never
    // truncated.
    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return SessionError::kFileWriteError;

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return SessionError::kFileWriteError;
        out << json_text;
        if (!out) return SessionError::kFileWriteError;
        out.close();
        if (!out) return SessionError::kFileWriteError;
    }

    // Rename atomically
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        return SessionError::kFileWriteError;
    }

    return SessionError::kNone;
}

}  // namespace island
