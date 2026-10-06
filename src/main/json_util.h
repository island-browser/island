#ifndef ISLAND_JSON_UTIL_H_
#define ISLAND_JSON_UTIL_H_

// CEF-free minimal JSON support shared by the local stores (session, prefs,
// bookmarks) and the bookmark importer. Extracted verbatim from
// session_store.cc's private json namespace; the recursive-descent parser
// never throws because island_browser_core builds with -fno-exceptions --
// failures surface as an empty Parse() result.

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace island {
namespace json {

// kDouble is appended (never reordered): it carries numbers with a fraction or
// exponent, which the agent protocols (MCP, ACP) may send.
enum class Type : std::uint8_t { kNull, kBool, kInt, kString, kArray, kObject, kDouble };

struct Value;

using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

struct Value {
    Type type = Type::kNull;
    bool bool_val = false;
    std::int64_t int_val = 0;
    double double_val = 0.0;
    std::string string_val;
    Array array_val;
    Object object_val;

    bool IsObject() const noexcept { return type == Type::kObject; }
    bool IsArray() const noexcept { return type == Type::kArray; }
    bool IsString() const noexcept { return type == Type::kString; }
    bool IsInt() const noexcept { return type == Type::kInt; }
    bool IsBool() const noexcept { return type == Type::kBool; }
    bool IsNull() const noexcept { return type == Type::kNull; }
    bool IsNumber() const noexcept { return type == Type::kInt || type == Type::kDouble; }
    double AsDouble() const noexcept {
        return type == Type::kDouble ? double_val : static_cast<double>(int_val);
    }

    static Value Null() { return Value{}; }
    static Value Bool(bool b) {
        Value v;
        v.type = Type::kBool;
        v.bool_val = b;
        return v;
    }
    static Value Int(std::int64_t i) {
        Value v;
        v.type = Type::kInt;
        v.int_val = i;
        return v;
    }
    static Value Double(double d) {
        Value v;
        v.type = Type::kDouble;
        v.double_val = d;
        return v;
    }
    static Value String(std::string s) {
        Value v;
        v.type = Type::kString;
        v.string_val = std::move(s);
        return v;
    }
    static Value MakeArray(Array items = {}) {
        Value v;
        v.type = Type::kArray;
        v.array_val = std::move(items);
        return v;
    }
    static Value MakeObject(Object members = {}) {
        Value v;
        v.type = Type::kObject;
        v.object_val = std::move(members);
        return v;
    }

    // Appends (or replaces) an object member and returns *this for chaining.
    Value& Set(std::string key, Value value) {
        type = Type::kObject;
        for (auto& [k, v] : object_val) {
            if (k == key) {
                v = std::move(value);
                return *this;
            }
        }
        object_val.emplace_back(std::move(key), std::move(value));
        return *this;
    }
    Value& Push(Value value) {
        type = Type::kArray;
        array_val.push_back(std::move(value));
        return *this;
    }

    // Typed member lookups that fall back when the member is missing or has
    // the wrong type.
    std::string_view StringOr(std::string_view key, std::string_view fallback) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsString() ? std::string_view(v->string_val) : fallback;
    }
    std::int64_t IntOr(std::string_view key, std::int64_t fallback) const noexcept {
        const Value* v = FindMember(key);
        if (v == nullptr) return fallback;
        if (v->IsInt()) return v->int_val;
        if (v->type == Type::kDouble && std::isfinite(v->double_val) &&
            v->double_val == std::floor(v->double_val) && std::fabs(v->double_val) < 9.0e15) {
            return static_cast<std::int64_t>(v->double_val);
        }
        return fallback;
    }
    bool BoolOr(std::string_view key, bool fallback) const noexcept {
        const Value* v = FindMember(key);
        return v != nullptr && v->IsBool() ? v->bool_val : fallback;
    }

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
    // Non-finite doubles have no JSON spelling and are written as null.
    void DoubleValue(double v) {
        if (!std::isfinite(v)) {
            EmitRaw("null");
            return;
        }
        // to_chars(double) needs macOS 13.3, above the 12.0 deployment target, so
        // format through a classic-locale stream instead.
        // Prefer the short 15-digit spelling and fall back to 17 digits only
        // when the short one does not round-trip.
        std::string text;
        for (const int precision : {15, 17}) {
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.precision(precision);
            stream << v;
            text = stream.str();
            std::istringstream back(text);
            back.imbue(std::locale::classic());
            double reparsed = 0.0;
            back >> reparsed;
            if (!back.fail() && reparsed == v) break;
        }
        EmitRaw(text);
        // Keep the value a JSON number with a fraction so it round-trips as kDouble.
        if (text.find_first_of(".eEn") == std::string_view::npos) EmitRaw(".0");
    }

    // Writes an arbitrary parsed/built value tree.
    void WriteValue(const Value& value) {
        switch (value.type) {
            case Type::kNull:
                NullValue();
                return;
            case Type::kBool:
                BoolValue(value.bool_val);
                return;
            case Type::kInt:
                IntValue(value.int_val);
                return;
            case Type::kDouble:
                DoubleValue(value.double_val);
                return;
            case Type::kString:
                StringValue(value.string_val);
                return;
            case Type::kArray:
                StartArray();
                for (const Value& item : value.array_val) {
                    ArrayElement();
                    WriteValue(item);
                }
                EndArray();
                return;
            case Type::kObject:
                StartObject();
                for (const auto& [k, v] : value.object_val) {
                    Key(k);
                    WriteValue(v);
                }
                EndObject();
                return;
        }
    }

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
            } else if (static_cast<unsigned char>(c) < 0x20) {
                // Remaining control characters must be \u-escaped to stay valid JSON.
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                EmitRaw(escaped);
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
// result instead. Nesting is capped at kMaxDepth: the input can come from web
// pages (agent page scripts), agents, and imported files, and unbounded
// recursion would let any of them overflow the stack.
class Parser {
  public:
    static constexpr int kMaxDepth = 128;

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
            case '[': {
                if (depth_ >= kMaxDepth) return Fail();
                ++depth_;
                Value nested = input_[pos_] == '{' ? ParseObject() : ParseArray();
                --depth_;
                return nested;
            }
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
                    case 'b':
                        result += '\b';
                        break;
                    case 'f':
                        result += '\f';
                        break;
                    case 'u': {
                        std::uint32_t code_point = 0;
                        if (!ParseHex4(code_point)) return Fail();
                        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                            // A high surrogate must be followed by an escaped low surrogate.
                            std::uint32_t low = 0;
                            if (pos_ + 2 >= input_.size() || input_[pos_ + 1] != '\\' ||
                                input_[pos_ + 2] != 'u') {
                                return Fail();
                            }
                            pos_ += 2;
                            if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF) return Fail();
                            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                        } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                            return Fail();
                        }
                        AppendUtf8(result, code_point);
                        break;
                    }
                    default:
                        return Fail();
                }
                ++pos_;
            } else if (static_cast<unsigned char>(input_[pos_]) < 0x20) {
                // Raw control characters are not allowed inside JSON strings.
                return Fail();
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

    // Reads the four hex digits after "\u"; on entry pos_ points at the 'u'
    // and on success it points at the last hex digit (the caller advances).
    bool ParseHex4(std::uint32_t& out) {
        if (pos_ + 4 >= input_.size()) return false;
        std::uint32_t value = 0;
        for (std::size_t i = 1; i <= 4; ++i) {
            const char c = input_[pos_ + i];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        pos_ += 4;
        out = value;
        return true;
    }

    static void AppendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ConsumeDigits() {
        const std::size_t start = pos_;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])))
            ++pos_;
        return pos_ != start;
    }

    Value ParseNumber() {
        std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            return Fail();
        }
        ConsumeDigits();
        bool is_double = false;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            if (!ConsumeDigits()) return Fail();
            is_double = true;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            if (!ConsumeDigits()) return Fail();
            is_double = true;
        }

        std::string_view num_str = input_.substr(start, pos_ - start);
        if (is_double) {
            // strtod is locale-sensitive and from_chars(double) is missing on older
            // libc++, so parse through a classic-locale stream.
            std::istringstream stream{std::string(num_str)};
            stream.imbue(std::locale::classic());
            double parsed = 0.0;
            stream >> parsed;
            if (stream.fail()) return Fail();
            return Value::Double(parsed);
        }
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
    int depth_ = 0;
    bool failed_ = false;
};

// Serializes a value tree to compact JSON text.
inline std::string Serialize(const Value& value) {
    std::ostringstream out;
    Writer writer(out);
    writer.WriteValue(value);
    return out.str();
}

// Parses JSON text; std::nullopt on any malformed input.
inline std::optional<Value> Parse(std::string_view text) { return Parser(text).Parse(); }

}  // namespace json
}  // namespace island

#endif  // ISLAND_JSON_UTIL_H_
