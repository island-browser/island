#ifndef ISLAND_JSON_UTIL_H_
#define ISLAND_JSON_UTIL_H_

// CEF-free minimal JSON support shared by the local stores (session, prefs,
// bookmarks) and the bookmark importer. Extracted verbatim from
// session_store.cc's private json namespace; the recursive-descent parser
// never throws because island_browser_core builds with -fno-exceptions --
// failures surface as an empty Parse() result.

#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace island {
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
    bool HasRequiredBool(std::string_view key) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->type == Type::kBool;
    }
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

// Recursive-descent parser that never throws.  `island_browser_core` links
// against CEF, which builds this translation unit with -fno-exceptions, so
// every failure is recorded in `failed_` and surfaced as an empty Parse()
// result instead.
class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input), pos_(0) {}

    // Returns std::nullopt when the input is not a single well-formed JSON
    // value, including when trailing content follows the root value.
    std::optional<Value> Parse() {
        Value v = ParseValue();
        if (failed_) return std::nullopt;
        SkipWhitespace();
        if (pos_ != input_.size()) return std::nullopt;
        return v;
    }

  private:
    // Marks the parse as failed and yields a placeholder null value so callers
    // can `return Fail();` from any value-producing helper.
    Value Fail() {
        failed_ = true;
        return Value{};
    }

    void SkipWhitespace() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                                        input_[pos_] == '\n' || input_[pos_] == '\r'))
            ++pos_;
    }

    // Returns '\0' and marks failure when the input is exhausted.
    char Peek() {
        SkipWhitespace();
        if (pos_ >= input_.size()) {
            Fail();
            return '\0';
        }
        return input_[pos_];
    }

    bool Expect(char expected) {
        SkipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != expected) {
            Fail();
            return false;
        }
        ++pos_;
        return true;
    }

    Value ParseValue() {
        if (failed_) return Value{};
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
                if (failed_) return Value{};
                return ParseNumber();
        }
    }

    Value ParseLiteral(std::string_view expected, Type t, bool bool_val) {
        for (char c : expected) {
            if (pos_ >= input_.size() || input_[pos_] != c) return Fail();
            ++pos_;
        }
        Value v;
        v.type = t;
        v.bool_val = bool_val;
        return v;
    }

    Value ParseString() {
        if (!Expect('"')) return Value{};
        std::string result;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\') {
                ++pos_;
                if (pos_ >= input_.size()) return Fail();
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
                        return Fail();
                }
                ++pos_;
            } else {
                result += input_[pos_++];
            }
        }
        if (!Expect('"')) return Value{};
        Value v;
        v.type = Type::kString;
        v.string_val = std::move(result);
        return v;
    }

    Value ParseNumber() {
        std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            return Fail();
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])))
            ++pos_;

        std::string_view num_str = input_.substr(start, pos_ - start);
        std::int64_t val = 0;
        auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
        if (ec != std::errc{}) return Fail();

        Value v;
        v.type = Type::kInt;
        v.int_val = val;
        return v;
    }

    Value ParseObject() {
        if (!Expect('{')) return Value{};
        Value v;
        v.type = Type::kObject;

        SkipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;
            return v;
        }

        for (;;) {
            SkipWhitespace();
            // A member key must be a JSON string.
            Value key = ParseString();
            if (failed_) return Value{};
            if (!Expect(':')) return Value{};
            Value val = ParseValue();
            if (failed_) return Value{};
            v.object_val.emplace_back(std::move(key.string_val), std::move(val));

            SkipWhitespace();
            if (pos_ >= input_.size()) return Fail();
            if (input_[pos_] == '}') {
                ++pos_;
                return v;
            }
            if (input_[pos_] != ',') return Fail();
            ++pos_;  // consume comma
        }
    }

    Value ParseArray() {
        if (!Expect('[')) return Value{};
        Value v;
        v.type = Type::kArray;

        SkipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;
            return v;
        }

        for (;;) {
            v.array_val.push_back(ParseValue());
            if (failed_) return Value{};
            SkipWhitespace();
            if (pos_ >= input_.size()) return Fail();
            if (input_[pos_] == ']') {
                ++pos_;
                return v;
            }
            if (input_[pos_] != ',') return Fail();
            ++pos_;
        }
    }

    std::string_view input_;
    std::size_t pos_;
    bool failed_ = false;
};

}  // namespace json
}  // namespace island

#endif  // ISLAND_JSON_UTIL_H_
