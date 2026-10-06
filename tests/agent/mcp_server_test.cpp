#include "mcp_server.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fake_browser_host.h"

namespace island::agent {
namespace {

using json::Value;

class McpServerTest : public ::testing::Test {
  protected:
    testing::FakeBrowserHost host_;
    BrowserToolbox toolbox_{host_};
    int dispatched_ = 0;
    McpServer server_{toolbox_, [this](std::function<void()> task) {
                          ++dispatched_;
                          task();
                      }};

    std::optional<Value> Send(std::string_view text, bool expect_reply = true) {
        std::optional<std::optional<std::string>> captured;
        server_.HandleMessage(text, [&](std::optional<std::string> reply) { captured = reply; });
        EXPECT_TRUE(captured.has_value());
        if (!captured || !captured->has_value()) {
            EXPECT_FALSE(expect_reply);
            return std::nullopt;
        }
        EXPECT_TRUE(expect_reply);
        return json::Parse(**captured);
    }
};

TEST_F(McpServerTest, InitializeNegotiatesVersionAndAdvertisesTools) {
    const auto reply = Send(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})");
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->IntOr("id", 0), 1);
    const Value* result = reply->FindMember("result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->StringOr("protocolVersion", ""), "2025-03-26");
    EXPECT_NE(result->FindMember("capabilities")->FindMember("tools"), nullptr);
    EXPECT_EQ(result->FindMember("serverInfo")->StringOr("name", ""), "island-browser");

    const auto unknown = Send(
        R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}})");
    EXPECT_EQ(unknown->FindMember("result")->StringOr("protocolVersion", ""),
              McpServer::kLatestProtocolVersion);
    EXPECT_EQ(unknown->StringOr("id", ""), "a");
}

TEST_F(McpServerTest, NotificationsGetNoReply) {
    EXPECT_FALSE(Send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", false));
}

TEST_F(McpServerTest, ToolsListCarriesSchemasAndAnnotations) {
    const auto reply = Send(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    const Value* tools = reply->FindMember("result")->FindMember("tools");
    ASSERT_NE(tools, nullptr);
    ASSERT_FALSE(tools->array_val.empty());
    for (const Value& tool : tools->array_val) {
        EXPECT_FALSE(tool.StringOr("name", "").empty());
        EXPECT_EQ(tool.FindMember("inputSchema")->StringOr("type", ""), "object");
        EXPECT_NE(tool.FindMember("annotations"), nullptr);
    }
}

TEST_F(McpServerTest, ToolsCallRunsThroughTheDispatcher) {
    const auto reply = Send(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"browser_open_tab","arguments":{"url":"https://x.test"}}})");
    EXPECT_EQ(dispatched_, 1);
    const Value* result = reply->FindMember("result");
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->BoolOr("isError", true));
    EXPECT_EQ(host_.calls, std::vector<std::string>{"open:https://x.test"});
}

TEST_F(McpServerTest, ToolErrorsAreResultsAndProtocolErrorsAreErrors) {
    const auto tool_error = Send(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"browser_navigate","arguments":{}}})");
    EXPECT_TRUE(tool_error->FindMember("result")->BoolOr("isError", false));

    const auto unknown_tool =
        Send(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"rm_rf"}})");
    EXPECT_EQ(unknown_tool->FindMember("error")->IntOr("code", 0), -32602);

    const auto unknown_method = Send(R"({"jsonrpc":"2.0","id":6,"method":"resources/list"})");
    EXPECT_EQ(unknown_method->FindMember("error")->IntOr("code", 0), -32601);

    const auto garbage = Send("{not json");
    EXPECT_EQ(garbage->FindMember("error")->IntOr("code", 0), -32600);
    EXPECT_EQ(dispatched_, 1);
}

TEST_F(McpServerTest, PingAnswersEmptyResult) {
    const auto reply = Send(R"({"jsonrpc":"2.0","id":7,"method":"ping"})");
    EXPECT_TRUE(reply->FindMember("result")->IsObject());
}

}  // namespace
}  // namespace island::agent
