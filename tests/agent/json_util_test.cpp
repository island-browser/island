#include "json_util.h"

#include <gtest/gtest.h>

#include <string>

namespace island::json {
namespace {

TEST(JsonUtilTest, DecodesUnicodeEscapesToUtf8) {
    const auto value = Parse(R"("caf\u00e9 \u2603 \ud83d\ude00")");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->string_val, "caf\xC3\xA9 \xE2\x98\x83 \xF0\x9F\x98\x80");
}

TEST(JsonUtilTest, RejectsLoneSurrogatesAndBadHex) {
    EXPECT_FALSE(Parse(R"("\ud83d")").has_value());
    EXPECT_FALSE(Parse(R"("\ude00")").has_value());
    EXPECT_FALSE(Parse(R"("\u12g4")").has_value());
}

TEST(JsonUtilTest, RejectsRawControlCharactersInStrings) {
    EXPECT_FALSE(Parse(std::string("\"a\x01b\"")).has_value());
}

TEST(JsonUtilTest, ParsesFractionsAndExponentsAsDoubles) {
    const auto value = Parse(R"({"a": 1.5, "b": -2e3, "c": 7})");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->FindMember("a")->type, Type::kDouble);
    EXPECT_DOUBLE_EQ(value->FindMember("a")->double_val, 1.5);
    EXPECT_DOUBLE_EQ(value->FindMember("b")->AsDouble(), -2000.0);
    EXPECT_EQ(value->FindMember("c")->type, Type::kInt);
    EXPECT_EQ(value->IntOr("b", 0), -2000);
    EXPECT_FALSE(Parse("1.").has_value());
    EXPECT_FALSE(Parse("1e").has_value());
}

TEST(JsonUtilTest, SerializesControlCharactersAsEscapes) {
    const std::string text = Serialize(Value::String(std::string("a\x01\b\"\\\n", 6)));
    // Kept out of the EXPECT_EQ arguments: MSVC's preprocessor mis-tokenizes a
    // raw string literal containing quotes inside a macro argument (C2146).
    const std::string expected = "\"a\\u0001\\u0008\\\"\\\\\\n\"";
    EXPECT_EQ(text, expected);
    const auto back = Parse(text);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->string_val, std::string("a\x01\b\"\\\n", 6));
}

TEST(JsonUtilTest, DoublesRoundTripAndStayDoubles) {
    for (const double d : {0.1, 1.0, -3.25, 1e21, 123456.789}) {
        const auto back = Parse(Serialize(Value::Double(d)));
        ASSERT_TRUE(back.has_value()) << d;
        EXPECT_EQ(back->type, Type::kDouble);
        EXPECT_DOUBLE_EQ(back->double_val, d);
    }
    EXPECT_EQ(Serialize(Value::Double(0.1)), "0.1");
}

TEST(JsonUtilTest, CapsNestingDepthInsteadOfOverflowingTheStack) {
    const int max = Parser::kMaxDepth;
    EXPECT_TRUE(Parse(std::string(max, '[') + std::string(max, ']')).has_value());
    EXPECT_FALSE(Parse(std::string(max + 1, '[') + std::string(max + 1, ']')).has_value());
    // A hostile page or agent can send arbitrarily deep input.
    EXPECT_FALSE(Parse(std::string(200000, '[')).has_value());
    std::string objects;
    for (int i = 0; i < 100000; ++i) objects += R"({"a":)";
    EXPECT_FALSE(Parse(objects).has_value());
}

TEST(JsonUtilTest, BuildersProduceCompactJson) {
    Value object = Value::MakeObject()
                       .Set("name", Value::String("island"))
                       .Set("tabs", Value::MakeArray().Push(Value::Int(1)).Push(Value::Bool(true)))
                       .Set("none", Value::Null());
    object.Set("name", Value::String("replaced"));
    EXPECT_EQ(Serialize(object), R"({"name":"replaced","tabs":[1,true],"none":null})");
    EXPECT_EQ(object.StringOr("name", ""), "replaced");
    EXPECT_EQ(object.StringOr("missing", "fallback"), "fallback");
    EXPECT_TRUE(object.BoolOr("missing", true));
}

}  // namespace
}  // namespace island::json
